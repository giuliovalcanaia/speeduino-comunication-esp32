/*
  Configuracao do RTC DS3231 para o Speeduino Logger

  Este sketch:
  1. Ajusta o RTC DS3231 com a hora de compilacao do codigo.
  2. Cria um ponto de acesso Wi-Fi aberto chamado "SPEEDUINO-RTC".
  3. Disponibiliza uma pagina web simples em http://192.168.4.1 para
     conferir a hora atual armazenada no RTC.

  IMPORTANTE sobre fuso horario:
  -----------------------------
  O horario de compilacao (__DATE__ e __TIME__) vem do relogio do computador.
  - Se o seu computador ja esta no horario de Sao Paulo (BRT, UTC-3),
    deixe HOURS_TO_SUBTRACT_FROM_UTC = 0.
  - Se o seu computador esta em UTC e voce quer gravar BRT (UTC-3) no RTC,
    use HOURS_TO_SUBTRACT_FROM_UTC = 3.

  Ajuste essa constante antes de compilar conforme a configuracao do seu PC.

  Ligação do DS3231 ao ESP32:
    VCC -> 3V3
    GND -> GND
    SDA -> GPIO21
    SCL -> GPIO22

  Compilacao: placa "ESP32 Dev Module".
*/

#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WebServer.h>

// ----------------------------- CONFIGURACAO ------------------------------
static const int  RTC_SDA_PIN = 21;
static const int  RTC_SCL_PIN = 22;

// Veja o comentario no inicio do arquivo sobre fuso horario.
static const int  HOURS_TO_SUBTRACT_FROM_UTC = 3;

static const char* WIFI_AP_SSID = "SPEEDUINO-RTC";
static const IPAddress LOCAL_IP(192, 168, 4, 1);
static const IPAddress GATEWAY(192, 168, 4, 1);
static const IPAddress SUBNET(255, 255, 255, 0);
// -------------------------------------------------------------------------

RTC_DS3231 rtc;
WebServer server(80);

static void formatDateTime(const DateTime& dt, char* out, size_t len) {
  snprintf(out, len, "%04d-%02d-%02dT%02d:%02d:%02d",
           dt.year(), dt.month(), dt.day(),
           dt.hour(), dt.minute(), dt.second());
}

static void handleRoot() {
  DateTime now = rtc.now();
  char iso[24];
  formatDateTime(now, iso, sizeof(iso));

  String html = "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "<meta charset=\"UTF-8\">"
                "<meta http-equiv=\"refresh\" content=\"1\">"
                "<title>RTC Speeduino</title>"
                "<style>"
                "body{font-family:sans-serif;text-align:center;margin-top:50px;background:#f5f5f5;}"
                "h1{font-size:2.5em;color:#222;}"
                "p{font-size:1.5em;color:#333;}"
                ".time{font-size:2em;color:#0066cc;margin-top:20px;}"
                "</style>"
                "</head>"
                "<body>"
                "<h1>RTC DS3231</h1>"
                "<p>Hora atual no RTC:</p>"
                "<p class=\"time\"><strong>" + String(iso) + "</strong></p>"
                "</body>"
                "</html>";

  server.send(200, "text/html", html);
}

static void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);

  if (!rtc.begin()) {
    Serial.println("Erro: DS3231 nao encontrado!");
    while (true) {
      delay(1000);
      Serial.println("Verifique a ligacao do RTC...");
    }
  }

  if (rtc.lostPower()) {
    Serial.println("RTC perdeu a alimentacao. Ajustando hora...");
  }

  DateTime compileTime(__DATE__, __TIME__);
  DateTime rtcTime = compileTime - TimeSpan(0, HOURS_TO_SUBTRACT_FROM_UTC, 0, 0);

  rtc.adjust(rtcTime);

  char iso[24];
  formatDateTime(rtcTime, iso, sizeof(iso));

  Serial.println("RTC ajustado.");
  Serial.print("Hora gravada no RTC: ");
  Serial.println(iso);

  if (HOURS_TO_SUBTRACT_FROM_UTC == 0) {
    Serial.println("(hora local do computador de compilacao)");
  } else {
    Serial.println("(UTC-3 - horario de Sao Paulo)");
  }

  WiFi.softAPConfig(LOCAL_IP, GATEWAY, SUBNET);
  WiFi.softAP(WIFI_AP_SSID, NULL);  // AP aberto, sem senha

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("AP Wi-Fi iniciado.");
  Serial.print("SSID: ");
  Serial.println(WIFI_AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

void loop() {
  server.handleClient();
}
