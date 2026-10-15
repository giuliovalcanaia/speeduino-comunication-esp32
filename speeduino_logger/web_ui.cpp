#include "web_ui.h"
#include "config.h"

#if USE_WIFI

#include <WebServer.h>
#include <SD.h>
#include "log_store.h"

static WebServer webServer(80);

static String htmlEscape(const String& s) {
  String r;
  r.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': r += "&amp;";  break;
      case '<': r += "&lt;";   break;
      case '>': r += "&gt;";   break;
      case '"': r += "&quot;"; break;
      default:  r += c;        break;
    }
  }
  return r;
}

static String urlEncode(const String& s) {
  String r;
  r.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
      r += c;
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
      r += buf;
    }
  }
  return r;
}

static String urlDecode(const String& s) {
  String r;
  r.reserve(s.length());
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '%' && i + 2 < s.length()) {
      char hex[3] = {s[i + 1], s[i + 2], '\0'};
      r += (char)strtol(hex, NULL, 16);
      i += 2;
    } else if (c == '+') {
      r += ' ';
    } else {
      r += c;
    }
  }
  return r;
}

static bool isPathSafe(const String& path) {
  if (!path.startsWith("/")) return false;
  if (path.indexOf("..") >= 0) return false;
  return true;
}

static String fileNameFromPath(const String& path) {
  int idx = path.lastIndexOf('/');
  if (idx < 0) return path;
  return path.substring(idx + 1);
}

static void sendDirectoryListing() {
  String dir = urlDecode(webServer.arg("dir"));
  if (dir.length() == 0) dir = "/";
  if (!isPathSafe(dir)) {
    webServer.send(403, "text/plain", "Caminho invalido");
    return;
  }

  // Obtem o caminho do log atual ANTES de travar o SD (evita deadlock, ja
  // que logStoreCurrentPath() tambem pega o mutex).
  String currentPath = logStoreCurrentPath();

  logStoreLock();
  File folder = SD.open(dir.c_str());
  if (!folder || !folder.isDirectory()) {
    logStoreUnlock();
    webServer.send(404, "text/plain", "Pasta nao encontrada");
    return;
  }

  String html =
    "<!DOCTYPE html>"
    "<html><head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
    "<title>Speeduino Logs</title>"
    "<style>"
    "body{font-family:sans-serif;margin:20px;background:#f5f5f5;}"
    "h1{color:#333;}"
    "table{width:100%;max-width:900px;background:#fff;border-collapse:collapse;box-shadow:0 2px 4px rgba(0,0,0,0.1);}"
    "th,td{padding:12px;text-align:left;border-bottom:1px solid #ddd;}"
    "th{background:#0066cc;color:#fff;}"
    "a{color:#0066cc;text-decoration:none;}"
    "a:hover{text-decoration:underline;}"
    ".up{font-weight:bold;}"
    ".current{color:#999;font-style:italic;}"
    "</style></head><body>"
    "<h1>Logs do Speeduino</h1>"
    "<p>Pasta: <strong>" + htmlEscape(dir) + "</strong></p>"
    "<table><tr><th>Nome</th><th>Tamanho</th><th>Acao</th></tr>";

  if (dir != "/") {
    String parent = dir.substring(0, dir.lastIndexOf('/'));
    if (parent.length() == 0) parent = "/";
    html += "<tr><td class=\"up\" colspan=\"3\"><a href=\"/?dir=" + urlEncode(parent) + "\">[..]</a></td></tr>";
  }

  File entry = folder.openNextFile();
  while (entry) {
    String entryName = entry.name();
    // entry.name() pode retornar caminho absoluto; isola o nome relativo
    String displayName = fileNameFromPath(entryName);
    String fullPath;
    if (dir == "/") fullPath = "/" + displayName;
    else            fullPath = dir + "/" + displayName;

    html += "<tr>";
    if (entry.isDirectory()) {
      html += "<td>" + htmlEscape(displayName) + "/</td>";
      html += "<td>-</td>";
      html += "<td><a href=\"/?dir=" + urlEncode(fullPath) + "\">Abrir</a></td>";
    } else {
      html += "<td>" + htmlEscape(displayName) + "</td>";
      html += "<td>" + String(entry.size()) + " bytes</td>";
      if (fullPath == currentPath) {
        html += "<td class=\"current\">gravando agora</td>";
      } else {
        html += "<td><a href=\"/download?file=" + urlEncode(fullPath) + "\">Download</a></td>";
      }
    }
    html += "</tr>";
    entry.close();
    entry = folder.openNextFile();
  }
  folder.close();
  logStoreUnlock();

  html += "</table></body></html>";
  webServer.send(200, "text/html; charset=utf-8", html);
}

static void handleDownload() {
  String file = urlDecode(webServer.arg("file"));
  if (file.length() == 0) {
    webServer.send(400, "text/plain", "Parametro 'file' ausente");
    return;
  }
  if (!isPathSafe(file)) {
    webServer.send(403, "text/plain", "Caminho invalido");
    return;
  }

  // Nao permite baixar o arquivo que ainda esta aberto para escrita.
  String currentPath = logStoreCurrentPath();
  if (file == currentPath) {
    webServer.send(409, "text/plain", "Arquivo ainda sendo gravado. Aguarde a rotacao.");
    return;
  }

  logStoreLock();
  File f = SD.open(file.c_str(), FILE_READ);
  size_t fsize = f ? f.size() : 0;
  logStoreUnlock();
  if (!f) {
    webServer.send(404, "text/plain", "Arquivo nao encontrado");
    return;
  }

  String name = fileNameFromPath(file);
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
  webServer.setContentLength(fsize);
  webServer.send(200, "text/csv", "");

  // Stream em chunks, liberando o mutex do SD entre eles para nao travar
  // a gravacao durante downloads longos.
  uint8_t chunk[WEB_CHUNK_BYTES];
  size_t remaining = fsize;
  while (remaining > 0) {
    size_t want = min(remaining, (size_t)sizeof(chunk));
    logStoreLock();
    int n = f.read(chunk, want);
    logStoreUnlock();
    if (n <= 0) break;
    webServer.client().write(chunk, n);
    remaining -= n;
  }

  logStoreLock();
  f.close();
  logStoreUnlock();
}

void webUiInit() {
  webServer.on("/", HTTP_GET, sendDirectoryListing);
  webServer.on("/download", HTTP_GET, handleDownload);
  webServer.begin();
  Serial.println("Servidor web de logs em http://192.168.4.1/");
}

void webUiHandle() { webServer.handleClient(); }

#else   // !USE_WIFI

void webUiInit()   {}
void webUiHandle() {}

#endif
