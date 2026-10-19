var ICONS = {
  memory: '<rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 6V3M15 6V3M9 21v-3M15 21v-3M6 9H3M6 15H3M21 9h-3M21 15h-3"/>',
  folder: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z"/>',
  folder_open: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v1"/><path d="M3 9h18l-2 10a2 2 0 0 1-2 1.6H7A2 2 0 0 1 5 19L3 9z"/>',
  folder_up: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z"/><path d="M12 16V10M9 13l3-3 3 3"/>',
  folder_data: '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z"/><path d="M8 13h8M8 16h5"/>',
  sync: '<path d="M20 11a8 8 0 0 0-14.9-4M4 4v5h5"/><path d="M4 13a8 8 0 0 0 14.9 4M20 20v-5h-5"/>',
  sync_alt: '<path d="M4 8h13l-3-3"/><path d="M20 16H7l3 3"/>',
  search: '<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>',
  download: '<path d="M12 3v12"/><path d="m7 10 5 5 5-5"/><path d="M5 21h14"/>',
  trash: '<path d="M3 6h18"/><path d="M8 6V4h8v2"/><path d="m6 6 1 14h10l1-14"/><path d="M10 11v6M14 11v6"/>',
  description: '<path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><path d="M14 2v6h6M8 13h8M8 17h8"/>',
  pie_chart: '<path d="M12 3a9 9 0 1 0 9 9h-9V3z"/><path d="M15 3.5A9 9 0 0 1 20.5 9H15V3.5z"/>',
  sd_card: '<path d="M7 3h7l5 5v13a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z"/><path d="M10 8v2M13 8v2M16 8v2"/>',
  update: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  schedule: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  warning: '<path d="M12 3 2 21h20L12 3z"/><path d="M12 9v5M12 18h.01"/>',
  public: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a15 15 0 0 1 0 18M12 3a15 15 0 0 0 0 18"/>',
  verified: '<circle cx="12" cy="12" r="9"/><path d="m8 12 3 3 5-6"/>',
  check: '<path d="m5 12 4 4L19 6"/>',
  check_circle: '<circle cx="12" cy="12" r="9"/><path d="m8 12 3 3 5-6"/>',
  tune: '<path d="M4 6h10M18 6h2M4 12h2M10 12h10M4 18h10M18 18h2"/><circle cx="16" cy="6" r="2"/><circle cx="8" cy="12" r="2"/><circle cx="16" cy="18" r="2"/>',
  list_alt: '<path d="M8 6h13M8 12h13M8 18h13"/><path d="M3 6h.01M3 12h.01M3 18h.01"/>',
  database: '<ellipse cx="12" cy="5" rx="8" ry="3"/><path d="M4 5v6c0 1.7 3.6 3 8 3s8-1.3 8-3V5"/><path d="M4 11v6c0 1.7 3.6 3 8 3s8-1.3 8-3v-6"/>',
  hard_drive: '<rect x="3" y="4" width="18" height="7" rx="2"/><rect x="3" y="13" width="18" height="7" rx="2"/><path d="M7 7.5h.01M7 16.5h.01"/>',
  gauge: '<path d="M12 14l4-4"/><path d="M20 16a8 8 0 1 0-16 0"/>',
  thermometer: '<path d="M14 14.8V5a2 2 0 0 0-4 0v9.8a4 4 0 1 0 4 0z"/><path d="M12 9v7"/>',
  battery: '<rect x="2" y="7" width="17" height="10" rx="2"/><path d="M22 11v2"/><path d="M5 10v4h7v-4z"/>',
  chevron_right: '<path d="m9 18 6-6-6-6"/>',
  arrow_up: '<path d="M12 19V5M5 12l7-7 7 7"/>',
  clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
  x: '<path d="M6 6l12 12M18 6 6 18"/>'
};

function icon(name, extraClass) {
  var cls = "icon" + (extraClass ? " " + extraClass : "");
  return '<svg class="' + cls + '" viewBox="0 0 24 24" aria-hidden="true">' + (ICONS[name] || "") + "</svg>";
}

function hydrateIcons(root) {
  var scope = root || document;
  var nodes = scope.querySelectorAll("[data-icon]");
  for (var i = 0; i < nodes.length; i++) {
    var name = nodes[i].getAttribute("data-icon");
    nodes[i].outerHTML = icon(name);
  }
}

function pad(n) {
  return n < 10 ? "0" + n : String(n);
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"]/g, function (c) {
    return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c];
  });
}

function urlEncode(value) {
  var out = "";
  for (const c of String(value)) {
    if (/[A-Za-z0-9\-_.~\/]/.test(c)) {
      out += c;
    } else {
      var bytes = new TextEncoder().encode(c);
      for (var i = 0; i < bytes.length; i++) {
        out += "%" + bytes[i].toString(16).toUpperCase().padStart(2, "0");
      }
    }
  }
  return out;
}

function formatBytes(bytes) {
  var n = Number(bytes) || 0;
  if (n < 1024) return n.toLocaleString("pt-BR") + " B";
  if (n < 1024 * 1024) return (n / 1024).toLocaleString("pt-BR", { maximumFractionDigits: 1 }) + " KB";
  if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toLocaleString("pt-BR", { maximumFractionDigits: 2 }) + " MB";
  return (n / (1024 * 1024 * 1024)).toLocaleString("pt-BR", { maximumFractionDigits: 2 }) + " GB";
}

function formatBytesDetailed(bytes) {
  var n = Number(bytes) || 0;
  return formatBytes(n) + " (" + n.toLocaleString("pt-BR") + " bytes)";
}

function parseLogName(name) {
  var match = /^(\d{4})-(\d{2})-(\d{2})T(\d{2})-(\d{2})-(\d{2})\.csv$/.exec(name);
  if (!match) return null;
  return {
    date: match[3] + "/" + match[2] + "/" + match[1],
    time: match[4] + ":" + match[5] + ":" + match[6]
  };
}

function formatLocalDateTime(date) {
  return pad(date.getDate()) + "/" + pad(date.getMonth() + 1) + "/" + date.getFullYear() + " " +
    pad(date.getHours()) + ":" + pad(date.getMinutes()) + ":" + pad(date.getSeconds());
}

function formatLocalDate(date) {
  return date.getFullYear() + "-" + pad(date.getMonth() + 1) + "-" + pad(date.getDate());
}

function formatLocalTime(date) {
  return pad(date.getHours()) + ":" + pad(date.getMinutes()) + ":" + pad(date.getSeconds());
}

function formatLocalISO(date) {
  return formatLocalDate(date) + "T" + formatLocalTime(date);
}

function fakeCsvText() {
  var header = "datetime,time_s,src,secl,status1,engine,dwell_ms,MAP_kPa,IAT_C,CLT_C,batt_V,O2_AFR,egoCorr_pct,RPM,gammaE_pct,VE1_pct,AFR_target,PW1_ms,advance_deg,TPS_pct,loops_per_s,spark_bits,ethanol_pct,baro_kPa,tpsADC,errors,vss,gear,fuelPress,oilPress";
  var lines = [header];
  var base = new Date();
  base.setHours(12, 34, 56, 0);
  for (var i = 0; i < 300; i++) {
    var d = new Date(base.getTime() + i * 100);
    var row = formatLocalISO(d) + "," + (i / 10).toFixed(3) + ",poll";
    row += ",1,0,1,2.5,95,25,85,13.8,14.7,100,850,100,40,14.5,3.2,18,5.0,120,1,0,101,20,0,0,3,300,350";
    lines.push(row);
  }
  return lines.join("\n") + "\n";
}

function fakeCsvDownload(file) {
  var name = String(file).substring(String(file).lastIndexOf("/") + 1);
  var blob = new Blob([fakeCsvText()], { type: "text/csv;charset=utf-8" });
  var url = URL.createObjectURL(blob);
  var a = document.createElement("a");
  a.href = url;
  a.download = name;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
}

function showToast(message, ok, ack) {
  var toast = document.getElementById("toast");
  if (!toast) return;
  var text = document.getElementById("toast-message");
  var iconSlot = document.getElementById("toast-icon");
  var ackEl = document.getElementById("toast-ack");
  if (text) text.textContent = message;
  if (iconSlot) iconSlot.innerHTML = icon(ok ? "check_circle" : "warning");
  if (ackEl) ackEl.textContent = ack || (ok ? "ACK" : "ERRO");
  toast.classList.toggle("error", !ok);
  toast.classList.add("visible");
  clearTimeout(showToast.timer);
  showToast.timer = setTimeout(function () {
    toast.classList.remove("visible");
  }, 3200);
}

document.addEventListener("DOMContentLoaded", function () {
  hydrateIcons(document);
});
