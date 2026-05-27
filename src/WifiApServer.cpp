#include "WifiApServer.h"
#include "utilities.h"
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <Update.h>

/* ── shared CSS injected once per page ───────────────────────── */
/* ── Dark Colorful theme ── */
static const char CSS[] PROGMEM = R"(
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:Arial,sans-serif;background:#0d1117;min-height:100vh;color:#e6edf3;font-size:14px}
.topbar{background:linear-gradient(90deg,#7c3aed,#2563eb,#0891b2);color:#fff;padding:6px 12px;display:flex;justify-content:space-between;align-items:center;font-size:13px}
.topbar .ver{color:#bfdbfe}
nav{background:#010409;display:flex;flex-wrap:wrap;gap:2px;padding:4px 6px;align-items:center}
nav a{padding:7px 14px;border-radius:4px;color:#fff;text-decoration:none;font-size:13px;font-weight:600}
nav a.live{background:#059669}nav a.live.on{background:#047857}
nav a.files{background:#0891b2}nav a.files.on{background:#0e7490}
nav a.log{background:#d97706}nav a.log.on{background:#b45309}
nav a.settings{background:#7c3aed}nav a.settings.on{background:#6d28d9}
nav a.about{background:#d97706}nav a.about.on{background:#b45309}
nav a.reboot{background:#ea580c;margin-left:auto}nav a.reboot:hover{background:#c2410c}
nav a.logout{background:#dc2626}nav a.logout:hover{background:#b91c1c}
.wrap{max-width:860px;margin:16px auto;padding:0 10px}
.card{background:#161b22;border-radius:10px;box-shadow:0 2px 14px rgba(0,0,0,.5);padding:16px;margin-bottom:14px;border:1px solid #30363d;border-top:3px solid #7c3aed}
.card h3{margin-bottom:10px;color:#c084fc;border-bottom:2px solid #3d2a6e;padding-bottom:6px}
.row{display:flex;flex-wrap:wrap;gap:8px;margin-bottom:6px}
.tag{background:#1f2937;color:#a78bfa;padding:3px 8px;border-radius:4px;font-size:12px;font-weight:600;border:1px solid #374151}
.tag.g{background:#052e16;color:#4ade80}
.tag.r{background:#450a0a;color:#f87171}
.tag.y{background:#451a03;color:#fbbf24}
label{display:block;margin-bottom:3px;font-weight:600;color:#a78bfa;font-size:13px}
input[type=text],input[type=password],input[type=number],input[type=url],input[type=email]{width:100%;padding:8px;border:1px solid #30363d;border-radius:5px;font-size:13px;margin-bottom:10px;background:#0d1117;color:#e6edf3}
input[type=range]{width:100%;margin-bottom:10px;accent-color:#7c3aed}
select{width:100%;padding:8px;border:1px solid #30363d;border-radius:5px;font-size:13px;margin-bottom:10px;background:#0d1117;color:#e6edf3}
.btn{display:inline-block;padding:8px 18px;border:none;border-radius:5px;cursor:pointer;font-size:13px;font-weight:600;text-decoration:none;color:#fff}
.btn-b{background:#2563eb}.btn-g{background:#059669}.btn-r{background:#dc2626}.btn-y{background:#d97706}.btn-p{background:#7c3aed}
.btn:hover{opacity:.85}
table{width:100%;border-collapse:collapse;font-size:13px}
th{background:linear-gradient(90deg,#7c3aed,#2563eb);color:#fff;padding:7px 8px;text-align:left}
td{padding:6px 8px;border-bottom:1px solid #21262d;color:#e6edf3}
tr:nth-child(even)td{background:#0d1117}
.login-wrap{min-height:100vh;display:flex;align-items:center;justify-content:center;background:#0d1117}
.login-box{background:#161b22;border-radius:14px;padding:36px;width:100%;max-width:360px;box-shadow:0 8px 28px rgba(124,58,237,.35);border:1px solid #30363d;border-top:4px solid #7c3aed}
.login-box h2{text-align:center;color:#c084fc;margin-bottom:4px}
.login-box .sub{text-align:center;color:#a78bfa;font-size:12px;margin-bottom:20px}
.alert{padding:8px 12px;border-radius:5px;margin-bottom:10px;font-size:13px}
.alert-r{background:#450a0a;color:#f87171}.alert-g{background:#052e16;color:#4ade80}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:10px}
@media(max-width:540px){.grid2{grid-template-columns:1fr}nav a{padding:5px 9px;font-size:12px}}
)";

/* ── helpers ─────────────────────────────────────────────────── */
WifiApServer::WifiApServer()
    : _server(nullptr), _st(nullptr), _startTime(0),
      _apTimeoutMs(300000UL), _noClientSince(0),
      _active(false), _tokenTime(0), _loggedIn(false) {
    _token[0] = '\0';
}

WifiApServer::~WifiApServer() { stop(); }

static uint32_t simpleRand(uint32_t seed) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}

void WifiApServer::startSession() {
    uint32_t r = simpleRand(millis() ^ (uint32_t)esp_random());
    snprintf(_token, sizeof(_token), "%08lX%08lX",
             (unsigned long)r, (unsigned long)simpleRand(r));
    _tokenTime = millis();
    _loggedIn  = true;
}

void WifiApServer::clearSession() { _loggedIn = false; _token[0] = '\0'; }

bool WifiApServer::checkAuth() {
    if (!_loggedIn) { redirectTo("/login"); return false; }
    if (millis() - _tokenTime > WEB_SESSION_TIMEOUT_MS) {
        clearSession(); redirectTo("/login"); return false;
    }
    String cookie = _server->header("Cookie");
    if (cookie.indexOf(_token) < 0) { redirectTo("/login"); return false; }
    _tokenTime = millis(); /* refresh */
    return true;
}

void WifiApServer::redirectTo(const char* url) {
    _server->sendHeader("Location", url);
    _server->send(302, "text/plain", "");
}

String WifiApServer::fmtBytes(uint32_t b) {
    if (b < 1024) return String(b) + " B";
    if (b < 1024 * 1024) return String(b / 1024) + " KB";
    return String(b / 1048576) + " MB";
}

String WifiApServer::topBar() {
    String s = F("<div class='topbar'><span>All-in-One Weather Station</span>"
                 "<span class='ver'>v");
    s += FIRMWARE_VERSION;
    s += F("</span></div>");
    return s;
}

String WifiApServer::navBar(const char* active) {
    String s = F("<nav>");
    auto tab = [&](const char* cls, const char* href, const char* label) {
        s += "<a class='"; s += cls;
        if (strcmp(cls, active) == 0) s += " on";
        s += "' href='"; s += href; s += "'>"; s += label; s += "</a>";
    };
    tab("live",     "/live",     "Live");
    tab("settings", "/settings", "Settings");
    tab("files",    "/files",    "Files");
    tab("about",    "/about",    "About");
    s += F("<a class='reboot' href='/reboot' onclick=\"return confirm('Reboot device?')\">&#8635; Reboot</a>");
    s += F("<a class='logout' href='/logout'>Logout</a>");
    s += F("</nav>");
    return s;
}

void WifiApServer::sendPage(const char* title, const char* activeTab,
                             const String& body, bool withNav) {
    _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server->send(200, "text/html", "");
    _server->sendContent(F("<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>"));
    _server->sendContent(title);
    _server->sendContent(F(" - All-in-One-Weather-Station v" FIRMWARE_VERSION "</title><style>"));
    _server->sendContent(CSS);
    _server->sendContent(F("</style></head><body>"));
    _server->sendContent(topBar());
    if (withNav) _server->sendContent(navBar(activeTab));
    _server->sendContent(F("<div class='wrap'>"));
    _server->sendContent(body);
    _server->sendContent(F("</div></body></html>"));
}

/* ── event log ───────────────────────────────────────────────── */
String WifiApServer::eventLogPath() {
    if (!_st || !_st->time) return "/Event-00-00-0000.csv";
    char buf[28];
    snprintf(buf, sizeof(buf), "/Event-%02u-%02u-%04u.csv",
             _st->time->date, _st->time->month, _st->time->year);
    return String(buf);
}

void WifiApServer::appendEvent(const char* event) {
    if (!_st || !_st->time) return;
    esp_task_wdt_reset();
    String path = eventLogPath();
    bool newFile = !LittleFS.exists(path);
    File f = LittleFS.open(path, FILE_APPEND);
    if (!f) return;
    if (newFile) f.print("DateTime,Event\r\n");
    char line[160];
    snprintf(line, sizeof(line), "%02u/%02u/%04u %02u:%02u:%02u,%s\r\n",
             _st->time->date, _st->time->month, _st->time->year,
             _st->time->hour, _st->time->minute, _st->time->second,
             event);
    f.print(line);
    f.close();
}

bool WifiApServer::isDataFile(const String& name) {
    /* matches DD-MM-YYYY.csv  or  DATA*.csv */
    if (name == "DATA.csv") return true;
    if (name.length() == 14 && name.endsWith(".csv") &&
        name[2] == '-' && name[5] == '-') return true;
    return false;
}

/* ── begin / stop ────────────────────────────────────────────── */
bool WifiApServer::begin(SystemStatus* status) {
    if (_active) return true;
    _st = status;
    _prefs.begin("ws-cfg", false);

    Serial.printf("[WiFi] Starting AP: %s\n", WIFI_AP_SSID);
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL)) {
        Serial.println(F("[WiFi] softAP FAILED"));
        WiFi.mode(WIFI_OFF);
        return false;
    }
    delay(100);
    Serial.printf("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());

    _server = new WebServer(80);
    const char* hdrs[] = {"Cookie"};
    _server->collectHeaders(hdrs, 1);

    _server->on("/",          HTTP_GET,  [this]() { hRoot(); });
    _server->on("/login",     HTTP_GET,  [this]() { hLogin(); });
    _server->on("/login",     HTTP_POST, [this]() { hLoginPost(); });
    _server->on("/logout",    HTTP_GET,  [this]() { hLogout(); });
    _server->on("/live",         HTTP_GET, [this]() { hLive(); });
    _server->on("/api/live",     HTTP_GET, [this]() { hApiLive(); });
    _server->on("/ble/connect",   HTTP_GET, [this]() { hBleConnect(); });
    _server->on("/ble/disconnect",HTTP_GET, [this]() { hBleDisconnect(); });
    _server->on("/ble/scan",      HTTP_GET, [this]() { hBleScan(); });
    _server->on("/ble/forget",    HTTP_GET, [this]() { hBleForget(); });
    _server->on("/ble/refresh",   HTTP_GET, [this]() { hBleRefresh(); });
    _server->on("/api/ble",       HTTP_GET, [this]() { hApiBle(); });
    _server->on("/files",     HTTP_GET,  [this]() { hFiles(); });
    _server->on("/download",  HTTP_GET,  [this]() { hDownload(); });
    _server->on("/delete",    HTTP_POST, [this]() { hDelete(); });
    _server->on("/deleteall", HTTP_POST, [this]() { hDeleteAll(); });
    _server->on("/log",       HTTP_GET,  [this]() { hLog(); });
    _server->on("/logdl",     HTTP_GET,  [this]() { hLogDownload(); });
    _server->on("/settings",  HTTP_GET,  [this]() { hSettings(); });
    _server->on("/setpwd",    HTTP_POST, [this]() { hSetPassword(); });
    _server->on("/setap",     HTTP_POST, [this]() { hSetAp(); });
    _server->on("/setble",    HTTP_POST, [this]() { hSetBle(); });
    _server->on("/setsrc",    HTTP_POST, [this]() { hSetSources(); });
    _server->on("/setsoil",   HTTP_POST, [this]() { hSetSoil(); });
    _server->on("/setweath",  HTTP_POST, [this]() { hSetWeather(); });
    _server->on("/setfile",   HTTP_POST, [this]() { hSetFile(); });
    _server->on("/setinflux", HTTP_POST, [this]() { hSetInflux(); });
    _server->on("/setntp",    HTTP_POST, [this]() { hSetNtp(); });
    _server->on("/setmem",    HTTP_POST, [this]() { hSetMem(); });
    _server->on("/setmqtt",   HTTP_POST, [this]() { hSetMqtt(); });
    _server->on("/setemail",  HTTP_POST, [this]() { hSetEmail(); });
    _server->on("/setota",     HTTP_POST, [this]() { hSetOta(); });
    _server->on("/testinflux", HTTP_GET,  [this]() { hTestInflux(); });
    _server->on("/ntpsync",    HTTP_GET,  [this]() { hNtpSyncNow(); });
    _server->on("/testmqtt",   HTTP_GET,  [this]() { hTestMqtt(); });
    _server->on("/testemail",  HTTP_GET,  [this]() { hTestEmail(); });
    _server->on("/testota",    HTTP_GET,  [this]() { hTestOta(); });
    _server->on("/otaupdate",  HTTP_GET,  [this]() { hOtaUpdate(); });
    _server->on("/about",     HTTP_GET,  [this]() { hAbout(); });
    _server->on("/reboot",    HTTP_GET,  [this]() { hReboot(); });
    _server->onNotFound([this]() { hNotFound(); });

    /* ArduinoOTA over AP */
    ArduinoOTA.setPassword(_prefs.getString("otapass", OTA_DEFAULT_PASSWORD).c_str());
    ArduinoOTA.begin();

    /* dynamic AP timeout from Preferences (minutes, 0=never) */
    uint32_t apMin = _prefs.getUInt("apTimeout", 5);
    _apTimeoutMs  = (apMin == 0) ? 0UL : apMin * 60000UL;
    _noClientSince = 0;

    _server->begin();
    _startTime = millis();
    _active    = true;
    Serial.printf("[WiFi] Web server started (timeout: %u min)\n", apMin);
    return true;
}

void WifiApServer::handleClient() {
    if (!_active || !_server) return;
    esp_task_wdt_reset();
    _server->handleClient();
    ArduinoOTA.handle();
}

void WifiApServer::stop() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    if (_server) { _server->stop(); delete _server; _server = nullptr; }
    if (_active) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        _prefs.end();
        Serial.println(F("[WiFi] AP stopped"));
    }
    _active = false;
}

bool WifiApServer::isTimedOut() {
    if (!_active)           return true;
    if (_apTimeoutMs == 0)  return false;  /* never */

    uint8_t clients = WiFi.softAPgetStationNum();
    if (clients > 0) {
        _noClientSince = 0;  /* client present — pause / reset countdown */
        return false;
    }
    /* no client connected */
    if (_noClientSince == 0) _noClientSince = millis();  /* start counting */
    return (millis() - _noClientSince >= _apTimeoutMs);
}

bool WifiApServer::isActive() { return _active; }

/* ═══════════════════════════════════════════════════════════════
   PAGE HANDLERS
   ═══════════════════════════════════════════════════════════════ */

void WifiApServer::hRoot() { redirectTo(_loggedIn ? "/live" : "/login"); }
void WifiApServer::hNotFound() { redirectTo("/login"); }
void WifiApServer::hLogout() {
    appendEvent("User logout");
    clearSession();
    redirectTo("/login");
}

/* ── LOGIN ─────────────────────────────────────────────────── */
void WifiApServer::hLogin() {
    String b = F("<div class='login-wrap'><div class='login-box'>"
        "<h2>&#127782; Weather Station</h2>"
        "<div class='sub'>All-in-One &nbsp;|&nbsp; v" FIRMWARE_VERSION "</div>"
        "<form method='POST' action='/login'>"
        "<label>Username</label>"
        "<input type='text' name='u' value='admin' required>"
        "<label>Password</label>"
        "<input type='password' name='p' required>"
        "<button class='btn btn-b' style='width:100%;padding:10px' type='submit'>Login</button>"
        "</form>"
        "<div style='margin-top:10px;text-align:center'>"
        "<a href='/settings' style='color:#6b7280;font-size:12px'>Reset password</a></div>"
        "</div></div>");
    _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server->send(200, "text/html", "");
    _server->sendContent(F("<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Login - All-in-One-Weather-Station v" FIRMWARE_VERSION "</title><style>"));
    _server->sendContent(CSS);
    _server->sendContent(F("</style></head><body>"));
    _server->sendContent(topBar());
    _server->sendContent(b);
    _server->sendContent(F("</body></html>"));
}

void WifiApServer::hLoginPost() {
    String u = _server->arg("u");
    String p = _server->arg("p");
    String storedPass = _prefs.getString("webpass", WEB_DEFAULT_PASS);
    String storedUser = WEB_DEFAULT_USER;

    char evBuf[80];
    String clientIp = _server->client().remoteIP().toString();

    if (u == storedUser && p == storedPass) {
        startSession();
        snprintf(evBuf, sizeof(evBuf), "Login SUCCESS user=%s ip=%s", u.c_str(), clientIp.c_str());
        appendEvent(evBuf);
        _server->sendHeader("Set-Cookie", String("sid=") + _token + "; Path=/");
        redirectTo("/live");
    } else {
        snprintf(evBuf, sizeof(evBuf), "Login FAILED user=%s ip=%s", u.c_str(), clientIp.c_str());
        appendEvent(evBuf);

        /* Email alarm for failed login */
        if (_st && _st->gsm && *_st->gsmAvail) {
            String smtp = _prefs.getString("emailsmtp", "");
            String eu   = _prefs.getString("emailuser", "");
            String ep   = _prefs.getString("emailpass", "");
            String eto  = _prefs.getString("emailto",   "");
            bool   alarm = _prefs.getBool("emailalarm", false);
            if (alarm && smtp.length() > 0 && eto.length() > 0) {
                char subj[80], bodyBuf[160];
                snprintf(subj, sizeof(subj), "WeatherStation Login Alarm");
                snprintf(bodyBuf, sizeof(bodyBuf),
                    "FAILED login attempt\r\nUser: %s\r\nIP: %s\r\nTime: %02u-%02u-%04u %02u:%02u",
                    u.c_str(), clientIp.c_str(),
                    _st->time->date, _st->time->month, _st->time->year,
                    _st->time->hour, _st->time->minute);
                _st->gsm->sendEmail(smtp.c_str(),
                    (uint16_t)_prefs.getUInt("emailport", EMAIL_DEFAULT_PORT),
                    eu.c_str(), ep.c_str(), eto.c_str(), subj, bodyBuf);
                appendEvent("Email alarm sent");
            }
        }

        String b = F("<div class='login-wrap'><div class='login-box'>"
            "<h2>&#127782; Weather Station</h2>"
            "<div class='sub'>v" FIRMWARE_VERSION "</div>"
            "<div class='alert alert-r'>Invalid username or password</div>"
            "<form method='POST' action='/login'>"
            "<label>Username</label><input type='text' name='u' required>"
            "<label>Password</label><input type='password' name='p' required>"
            "<button class='btn btn-b' style='width:100%;padding:10px' type='submit'>Login</button>"
            "</form></div></div>");
        _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        _server->send(200, "text/html", "");
        _server->sendContent(F("<!DOCTYPE html><html><head>"
            "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>Login</title><style>"));
        _server->sendContent(CSS);
        _server->sendContent(F("</style></head><body>"));
        _server->sendContent(topBar());
        _server->sendContent(b);
        _server->sendContent(F("</body></html>"));
    }
}

/* ── LIVE ──────────────────────────────────────────────────── */
void WifiApServer::hLive() {
    if (!checkAuth()) return;
    String b;
    b.reserve(900);

    /* auto-refresh JS: sensor data every 30 s, BLE data every 5 s */
    b += F("<script>"
        "function rfSensor(){"
        "fetch('/api/live').then(r=>r.json()).then(d=>{"
        "for(var k in d){var e=document.getElementById(k);if(e)e.textContent=d[k];}"
        "})}"
        "function bleIco(n){"
        "if(n==='Timestamp')return'&#128336;';"
        "if(n.indexOf('Temp')>=0||n.indexOf('TMP')>=0)return'&#127777;';"
        "if(n.indexOf('Humid')>=0)return'&#128167;';"
        "if(n.indexOf('Delta')>=0)return'&#9651;';"
        "if(n.indexOf('Rain')>=0)return'&#127783;';"
        "if(n.indexOf('Leaf')>=0)return'&#127807;';"
        "if(n.indexOf('PAR')>=0)return'&#9728;';"
        "if(n.indexOf('Soil')>=0)return'&#127774;';"
        "return'&#128307;';}"
        "function reqNus(){"
        "fetch('/ble/refresh').catch(function(){});"
        "document.getElementById('nus-age').textContent='Requesting...';}"
        "function rfBle(){"
        "fetch('/api/ble').then(r=>r.json()).then(d=>{"
        "var div=document.getElementById('ble-live');"
        /* ── not connected ── */
        "if(!d.connected){"
        "div.innerHTML='<p style=\"color:#a78bfa;font-size:13px\">&#128268; Not connected &mdash; "
        "use Settings &#8594; BLE Client to scan and connect.</p>';return;}"
        /* ── connection header ── */
        "var h='<div class=\"row\" style=\"margin-bottom:8px;align-items:center\">';"
        "h+='<span class=\"tag g\">&#128994; Connected</span>';"
        "h+='<span class=\"tag\" style=\"font-family:monospace\">'+d.mac+'</span>';"
        "h+='<span class=\"tag\">'+d.name+'</span>';"
        "if(d.nus)h+='<span class=\"tag y\">&#128301; Sniffer NUS</span>';"
        "h+='<a class=\"btn btn-r\" style=\"padding:3px 10px;font-size:12px;margin-left:auto\" "
        "href=\"/ble/disconnect\">Disconnect</a></div>';"
        /* ── NUS path ── */
        "if(d.nus){"
        "if(!d.hasData){"
        /* waiting for first notification */
        "h+='<div style=\"padding:16px;text-align:center;color:#a78bfa\">';"
        "h+='&#9711; Connected to Sniffer Portal<br>"
        "<small>Waiting for first data notification (up to 10 s)&hellip;</small>';"
        "h+='<br><br><button class=\"btn btn-p\" style=\"padding:4px 12px;font-size:12px\" "
        "onclick=\"reqNus()\">&#8635; Request Now</button></div>';"
        "}else{"
        /* age bar */
        "var ageStr=d.age<5?'just now':d.age+'s ago';"
        "h+='<div style=\"display:flex;justify-content:space-between;align-items:center;"
        "font-size:11px;color:#a78bfa;margin-bottom:8px\">';"
        "h+='<span>Last update: <strong id=\"nus-age\">'+ageStr+'</strong></span>';"
        "h+='<button class=\"btn btn-p\" style=\"padding:2px 10px;font-size:11px\" "
        "onclick=\"reqNus()\">&#8635; Request Update</button></div>';"
        /* sensor grid */
        "h+='<div style=\"display:grid;grid-template-columns:repeat(3,1fr);gap:8px\">';"
        "d.chars.forEach(function(c){"
        "var ico=bleIco(c.uuid);"
        "var stale=d.age>30?'opacity:.55':'opacity:1';"
        "h+='<div style=\"background:#1a1629;border:1px solid #30363d;border-radius:6px;padding:10px;"
        "text-align:center;'+stale+'\">';"
        "h+='<div style=\"font-size:11px;color:#a78bfa;margin-bottom:3px\">';"
        "h+=ico+' '+c.uuid+'</div>';"
        "h+='<div style=\"font-size:17px;font-weight:700;color:#e6edf3\">'+c.ascii+'</div>';"
        "h+='</div>';});"
        "h+='</div>';}"
        /* ── GATT path ── */
        "}else{"
        "if(d.chars.length===0){"
        "h+='<p style=\"color:#a78bfa;font-size:13px\">Connected (GATT). No readable characteristics found.</p>';"
        "}else{"
        "h+='<table><tr><th>Characteristic UUID</th><th>Hex</th><th>ASCII</th></tr>';"
        "d.chars.forEach(function(c){"
        "h+='<tr><td style=\"font-family:monospace;font-size:11px\">'+c.uuid+'</td>';"
        "h+='<td style=\"font-family:monospace\">'+c.hex+'</td>';"
        "h+='<td>'+c.ascii+'</td></tr>';});"
        "h+='</table>';}}"
        "div.innerHTML=h;"
        "}).catch(function(){})}"
        "setInterval(rfSensor,30000);"
        "setInterval(rfBle,5000);"
        "rfBle();"
        "</script>");

    /* Status row */
    b += F("<div class='card'><h3>&#128268; System Status</h3><div class='row'>");
    bool ga = _st->gsmAvail && *_st->gsmAvail;
    b += F("<span class='tag "); b += ga ? "g" : "r";
    b += F("'>GSM: "); b += ga ? "Connected" : "Offline"; b += F("</span>");
    b += F("<span class='tag'>Signal: <span id='gsm_rssi'>");
    b += (_st->gsmRssi ? *_st->gsmRssi : 0);
    b += F("</span> dBm</span>");
    uint8_t apClients = WiFi.softAPgetStationNum();
    b += F("<span class='tag g'>WiFi: AP "); b += WIFI_AP_SSID;
    b += F(" ("); b += apClients; b += F(" client"); b += (apClients != 1 ? "s" : ""); b += F(")</span>");
    bool ble = _st->bleActive && *_st->bleActive;
    b += F("<span class='tag "); b += ble ? "g" : "y";
    b += F("'>BLE: "); b += ble ? "Active" : "Inactive"; b += F("</span>");
    b += F("</div></div>");

    /* Battery & Memory */
    b += F("<div class='grid2'>");
    b += F("<div class='card'><h3>&#128267; Battery</h3>");
    b += F("<span class='tag' id='batt'>");
    b += (_st->battMv ? *_st->battMv : 0);
    b += F(" mV</span></div>");

    b += F("<div class='card'><h3>&#128190; Memory</h3>");
    if (_st->memory) {
        uint64_t used  = LittleFS.usedBytes();
        uint64_t total = LittleFS.totalBytes();
        uint8_t  pct   = total ? (used * 100 / total) : 0;
        b += F("<span class='tag ");
        b += (pct > 80 ? "r" : pct > 60 ? "y" : "g");
        b += F("'>"); b += pct; b += F("% used</span> ");
        b += fmtBytes(used); b += F(" / "); b += fmtBytes(total);
    }
    b += F("</div></div>");

    /* Soil */
    b += F("<div class='card'><h3>&#127807; Soil Sensor</h3>"
           "<div style='display:grid;grid-template-columns:repeat(3,1fr);gap:8px'>");
    if (_st->sensor) {
        auto& s = *_st->sensor;
        auto cell = [&](const char* ico, const char* lbl, const char* id, String v, const char* u) {
            b += F("<div style='background:#1a1629;border:1px solid #30363d;border-radius:6px;"
                   "padding:10px;text-align:center'>"
                   "<div style='font-size:11px;color:#a78bfa;margin-bottom:3px'>"); b += ico; b += " "; b += lbl;
            b += F("</div><div style='font-size:17px;font-weight:700;color:#e6edf3'>"
                   "<span id='"); b += id; b += F("'>"); b += v; b += F("</span>");
            if (u[0]) { b += F("<span style='font-size:12px;color:#a78bfa;margin-left:3px'>"); b += u; b += F("</span>"); }
            b += F("</div></div>");
        };
        cell("&#128167;", "Moisture",    "sh",  String(s.soil_humi/10.0,1),  "%");
        cell("&#127777;", "Temperature", "st",  String(s.soil_temp/10.0,1),  "°C");
        cell("&#9889;",   "EC",          "se",  String(s.soil_ec),           "µS/cm");
        cell("&#9879;",   "pH",          "sp",  String(s.soil_ph/10.0,1),    "");
        cell("&#127807;", "Nitrogen",    "sn",  String(s.soil_N),            "mg/kg");
        cell("&#127807;", "Phosphorus",  "spv", String(s.soil_P),            "mg/kg");
        cell("&#127807;", "Potassium",   "sk",  String(s.soil_K),            "mg/kg");
    }
    b += F("</div></div>");

    /* Weather */
    b += F("<div class='card'><h3>&#9925; Weather Sensor</h3>"
           "<div style='display:grid;grid-template-columns:repeat(3,1fr);gap:8px'>");
    if (_st->sensor) {
        auto& s = *_st->sensor;
        auto cell = [&](const char* ico, const char* lbl, const char* id, String v, const char* u) {
            b += F("<div style='background:#1a1629;border:1px solid #30363d;border-radius:6px;"
                   "padding:10px;text-align:center'>"
                   "<div style='font-size:11px;color:#a78bfa;margin-bottom:3px'>"); b += ico; b += " "; b += lbl;
            b += F("</div><div style='font-size:17px;font-weight:700;color:#e6edf3'>"
                   "<span id='"); b += id; b += F("'>"); b += v; b += F("</span>");
            if (u[0]) { b += F("<span style='font-size:12px;color:#a78bfa;margin-left:3px'>"); b += u; b += F("</span>"); }
            b += F("</div></div>");
        };
        cell("&#128168;", "Wind Speed",  "ws",  String(s.windSpeed/10.0,1),          "m/s");
        cell("&#129517;", "Wind Dir",    "wd",  String(s.windDir_Deg),               "°");
        cell("&#128167;", "Humidity",    "ah",  String(s.air_humidity/10.0,1),       "%");
        cell("&#127777;", "Temp",        "at",  String(s.air_temperature/10.0,1),    "°C");
        cell("&#127783;", "CO&#8322;",   "co2", String(s.CO2),                       "ppm");
        cell("&#127760;", "Pressure",    "pr",  String(s.pressure/10.0,1),           "kPa");
        cell("&#9728;",   "Illuminance", "il",  String((unsigned long)s.illuminance),"lux");
        cell("&#127783;", "Rainfall",    "rf",  String(s.rainfall/10.0,1),           "mm");
        cell("&#128262;", "Solar",       "so",  String(s.solar),                     "W/m²");
    }
    b += F("</div></div>");

    /* BLE Sensor Data — live-refresh via JS every 5 s */
    b += F("<div class='card'><h3>&#128301; BLE Sensor Data"
           "<span style='float:right;font-size:11px;color:#a78bfa'>&#8635; auto</span></h3>"
           "<div id='ble-live'><p style='color:#1d4ed8;font-size:13px'>Loading&hellip;</p></div>"
           "</div>");

    /* OTA last update — bottom of page */
    b += F("<div class='card'><h3>&#128260; OTA Last Update</h3>");
    b += F("<span class='tag g'>"); b += _st->lastOtaStr; b += F("</span></div>");

    sendPage("Live", "live", b);
}

void WifiApServer::hApiLive() {
    if (!_st) { _server->send(200, "application/json", "{}"); return; }
    char json[320];
    auto& s = *_st->sensor;
    snprintf(json, sizeof(json),
        "{\"gsm_rssi\":%d,\"batt\":\"%u mV\","
        "\"sh\":\"%.1f\",\"st\":\"%.1f\",\"se\":\"%u\","
        "\"sp\":\"%.1f\",\"sn\":\"%u\",\"spv\":\"%u\",\"sk\":\"%u\","
        "\"ws\":\"%.1f\",\"wd\":\"%u\",\"ah\":\"%.1f\",\"at\":\"%.1f\","
        "\"co2\":\"%u\",\"pr\":\"%.1f\",\"il\":\"%lu\",\"rf\":\"%.1f\",\"so\":\"%u\"}",
        (_st->gsmRssi ? *_st->gsmRssi : 0),
        (_st->battMv  ? *_st->battMv  : 0),
        s.soil_humi/10.0, s.soil_temp/10.0, s.soil_ec,
        s.soil_ph/10.0,   s.soil_N, s.soil_P, s.soil_K,
        s.windSpeed/10.0, s.windDir_Deg,
        s.air_humidity/10.0, s.air_temperature/10.0,
        s.CO2, s.pressure/10.0, (unsigned long)s.illuminance,
        s.rainfall/10.0, s.solar);
    _server->send(200, "application/json", json);
}

/* ── FILES ─────────────────────────────────────────────────── */
void WifiApServer::hFiles() {
    if (!checkAuth()) return;
    String b;
    b.reserve(700);

    b += F("<div class='card'><h3>&#128196; Data Files</h3>"
           "<table><tr><th>File</th><th>Size</th><th>Actions</th></tr>");

    File root = LittleFS.open("/");
    File file = root.openNextFile();
    bool any = false;
    while (file) {
        String fname = String(file.name());
        if (!fname.startsWith("/")) fname = "/" + fname;
        String bare = fname.substring(1);  /* strip leading / */
        if (isDataFile(bare)) {
            any = true;
            b += F("<tr><td>"); b += bare; b += F("</td><td>");
            b += fmtBytes(file.size());
            b += F("</td><td>"
                "<a class='btn btn-b' href='/download?f=");
            b += bare;
            b += F("'>&#11123; DL</a> "
                "<form style='display:inline' method='POST' action='/delete'>"
                "<input type='hidden' name='f' value='"); b += bare;
            b += F("'><button class='btn btn-r' type='submit' "
                "onclick=\"return confirm('Delete "); b += bare; b += F("?')\""
                ">&#128465;</button></form></td></tr>");
        }
        file = root.openNextFile();
    }
    if (!any) b += F("<tr><td colspan='3' style='text-align:center;color:#9ca3af'>No files</td></tr>");
    b += F("</table></div>");

    b += F("<form method='POST' action='/deleteall' "
           "onsubmit=\"return confirm('Delete ALL data files?')\">"
           "<button class='btn btn-r' type='submit'>&#128465; Delete ALL Files</button>"
           "</form>");

    sendPage("Files", "files", b);
}

void WifiApServer::hDownload() {
    if (!checkAuth()) return;
    String fname = "/" + _server->arg("f");
    if (!LittleFS.exists(fname)) { _server->send(404, "text/plain", "Not found"); return; }
    File f = LittleFS.open(fname, "r");
    if (!f) { _server->send(500, "text/plain", "Open failed"); return; }
    String disp = "attachment; filename=\"" + _server->arg("f") + "\"";
    _server->sendHeader("Content-Disposition", disp);
    _server->sendHeader("Content-Length", String(f.size()));
    esp_task_wdt_reset();
    _server->streamFile(f, "text/csv");
    f.close();
}

void WifiApServer::hDelete() {
    if (!checkAuth()) return;
    String fname = "/" + _server->arg("f");
    char ev[80]; snprintf(ev, sizeof(ev), "Delete file %s", fname.c_str());
    if (LittleFS.remove(fname)) appendEvent(ev);
    redirectTo("/files");
}

void WifiApServer::hDeleteAll() {
    if (!checkAuth()) return;
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while (file) {
        String bare = String(file.name());
        file = root.openNextFile();
        if (isDataFile(bare)) LittleFS.remove("/" + bare);
    }
    appendEvent("Delete ALL data files");
    redirectTo("/files");
}

/* ── LOG ───────────────────────────────────────────────────── */
void WifiApServer::hLog() {
    if (!checkAuth()) return;
    String b;
    b.reserve(600);
    b += F("<div class='card'><h3>&#128203; Event Log Files</h3>"
           "<table><tr><th>File</th><th>Size</th><th>Actions</th></tr>");

    File root = LittleFS.open("/");
    File file = root.openNextFile();
    bool any = false;
    while (file) {
        String fname = String(file.name());
        if (fname.startsWith("Event-") && fname.endsWith(".csv")) {
            any = true;
            b += F("<tr><td>"); b += fname; b += F("</td><td>");
            b += fmtBytes(file.size());
            b += F("</td><td>"
                "<a class='btn btn-y' href='/logdl?f="); b += fname;
            b += F("'>&#11123; DL</a> "
                "<form style='display:inline' method='POST' action='/delete'>"
                "<input type='hidden' name='f' value='"); b += fname;
            b += F("'><button class='btn btn-r' type='submit' "
                "onclick=\"return confirm('Delete?')\">&#128465;</button></form></td></tr>");
        }
        file = root.openNextFile();
    }
    if (!any) b += F("<tr><td colspan='3' style='text-align:center;color:#9ca3af'>No log files yet</td></tr>");
    b += F("</table></div>");

    /* Show last 20 lines of today's log */
    String today = eventLogPath();
    if (LittleFS.exists(today)) {
        b += F("<div class='card'><h3>Today's Log</h3>"
               "<table><tr><th>DateTime</th><th>Event</th></tr>");
        File lf = LittleFS.open(today, "r");
        if (lf) {
            lf.readStringUntil('\n'); /* skip header */
            String lines[20]; int n = 0, idx = 0;
            while (lf.available()) {
                String line = lf.readStringUntil('\n');
                line.trim();
                if (line.length() > 0) { lines[idx % 20] = line; idx++; n = min(idx, 20); }
            }
            lf.close();
            int start = (idx > 20) ? (idx % 20) : 0;
            for (int i = 0; i < n; i++) {
                String& ln = lines[(start + i) % 20];
                int c = ln.indexOf(',');
                if (c > 0) {
                    b += F("<tr><td>"); b += ln.substring(0, c);
                    b += F("</td><td>"); b += ln.substring(c + 1); b += F("</td></tr>");
                }
            }
        }
        b += F("</table></div>");
    }

    sendPage("Log", "log", b);
}

void WifiApServer::hLogDownload() {
    if (!checkAuth()) return;
    String fname = "/" + _server->arg("f");
    if (!LittleFS.exists(fname)) { _server->send(404, "text/plain", "Not found"); return; }
    File f = LittleFS.open(fname, "r");
    if (!f) { _server->send(500, "text/plain", "Open failed"); return; }
    String disp = "attachment; filename=\"" + _server->arg("f") + "\"";
    _server->sendHeader("Content-Disposition", disp);
    _server->sendHeader("Content-Length", String(f.size()));
    esp_task_wdt_reset();
    _server->streamFile(f, "text/csv");
    f.close();
}

/* ── SETTINGS HELPERS ─────────────────────────────────────── */
void WifiApServer::startSettingsPage(const String& msg) {
    _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server->send(200, "text/html", "");
    _server->sendContent(F("<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Settings - v" FIRMWARE_VERSION "</title><style>"));
    _server->sendContent(CSS);
    _server->sendContent(F("</style></head><body>"));
    _server->sendContent(topBar());
    _server->sendContent(navBar("settings"));
    _server->sendContent(F("<div class='wrap'>"));
    if (msg.length()) {
        bool ok = msg.startsWith("ok");
        _server->sendContent(F("<div class='alert "));
        _server->sendContent(ok ? "alert-g" : "alert-r");
        _server->sendContent(F("'>"));
        _server->sendContent(msg.substring(3));
        _server->sendContent(F("</div>"));
    }
}
void WifiApServer::endSettingsPage() {
    _server->sendContent(F("</div></body></html>"));
}
void WifiApServer::sendSettingsChunk(const String& html) {
    _server->sendContent(html);
}

/* ── SETTINGS PAGE ─────────────────────────────────────────── */
void WifiApServer::hSettings() {
    if (!checkAuth()) return;
    startSettingsPage(_server->arg("msg"));

    /* ── test-button JS ── */
    sendSettingsChunk(F("<script>"
        "function testSrv(u,id){"
        "var el=document.getElementById(id);"
        "el.innerHTML='<span style=\"color:#a78bfa\">Testing&#8230;</span>';"
        "fetch(u).then(function(r){return r.json();}).then(function(j){"
        "el.innerHTML=j.ok"
        "?'<span class=\"tag g\">&#10003; '+j.msg+'</span>'"
        ":'<span class=\"tag r\">&#10007; '+j.msg+'</span>';}"
        ").catch(function(){"
        "el.innerHTML='<span class=\"tag r\">&#10007; Request error</span>';});}"
        "function testInflux(){testSrv('/testinflux','influx-res');}"
        "function testMqtt(){testSrv('/testmqtt','mqtt-res');}"
        "function testEmail(){if(!confirm('Send a test email via GSM now?'))return;"
        "testSrv('/testemail','email-res');}"
        "function testOta(){testSrv('/testota','ota-res');}"
        "</script>"));

    /* ── 1. OTA ── */
    {
        String otasrv  = _prefs.getString("otaserver",   OTA_DEFAULT_SERVER);
        String otaprj  = _prefs.getString("otaproject",  OTA_DEFAULT_PROJECT);
        String otadev  = _prefs.getString("otadevice",   OTA_DEFAULT_DEVICE);
        String otadlpw = _prefs.getString("otadlpass",   OTA_DEFAULT_DLPASS);
        String otapw   = _prefs.getString("otapass",     OTA_DEFAULT_PASSWORD);
        uint32_t otaiv = _prefs.getUInt  ("otainterval", OTA_DEFAULT_INTERVAL_H);
        bool otaboot   = _prefs.getBool  ("otaboot",     false);
        String c = F("<div class='card'><h3>&#128260; OTA Update (via GSM)</h3>"
            "<p style='color:#a78bfa;font-size:12px;margin-bottom:10px'>Last update: <strong>");
        c += _st->lastOtaStr;
        c += F("</strong></p>"
            "<form method='POST' action='/setota'>"
            "<label>OTA Server URL (base, e.g. http://host:8889)</label>"
            "<input type='url' name='srv' value='"); c += otasrv; c += F("'>"
            "<div class='grid2'>"
            "<div><label>Project name</label>"
            "<input type='text' name='prj' value='"); c += otaprj; c += F("'></div>"
            "<div><label>Device type</label>"
            "<input type='text' name='dev' value='"); c += otadev; c += F("'></div>"
            "</div>"
            "<label>Download password (OTA server)</label>"
            "<input type='password' name='dpw' value='"); c += otadlpw; c += F("'>"
            "<label>ArduinoOTA password (local)</label>"
            "<input type='password' name='pw' value='"); c += otapw; c += F("'>"
            "<label>Auto-check interval (hours, 0=off)</label>"
            "<input type='number' name='iv' min='0' value='"); c += otaiv; c += F("'>"
            "<label style='display:flex;align-items:center;gap:8px;font-weight:normal;margin-bottom:10px'>"
            "<input type='checkbox' name='otaboot' value='1'");
        if (otaboot) c += F(" checked");
        c += F("> Check for new firmware every time the board boots up</label>"
            "<div style='display:flex;gap:8px;flex-wrap:wrap'>"
            "<button class='btn btn-g' type='submit'>Save OTA</button>"
            "<button class='btn btn-y' type='button' onclick='testOta()'>&#128268; Test Server</button>"
            "<a class='btn btn-r' href='/otaupdate' "
            "onclick=\"return confirm('Trigger OTA check? Device will exit AP, connect GSM and reboot if update found.')\""
            ">&#9654; Update Now</a>"
            "</div>"
            "</form>"
            "<div id='ota-res' style='margin-top:8px'></div>"
            "</div>");
        sendSettingsChunk(c);
    }

    /* ── 2. WiFi AP ── */
    uint32_t apMin = _prefs.getUInt("apTimeout", 5);
    {
        String c = F("<div class='card'><h3>&#128246; WiFi AP</h3>"
            "<p style='color:#6b7280;font-size:12px;margin-bottom:10px'>"
            "AP closes after the selected idle period with <strong>no connected client</strong>. "
            "Resets automatically when a client connects.</p>"
            "<form method='POST' action='/setap'>"
            "<label>Close AP after idle (no clients)</label>"
            "<select name='apt'>");
        auto opt = [&](uint32_t v, const char* lbl) {
            c += F("<option value='"); c += v;
            if (v == apMin) c += F("' selected>");
            else c += F("'>");
            c += lbl; c += F("</option>");
        };
        opt(1,"1 minute"); opt(5,"5 minutes"); opt(10,"10 minutes");
        opt(15,"15 minutes"); opt(30,"30 minutes"); opt(60,"1 hour"); opt(0,"Never");
        c += F("</select>"
            "<button class='btn btn-b' type='submit'>Save</button>"
            "</form></div>");
        sendSettingsChunk(c);
    }

    /* ── 2. BLE Client ── */
    {
        bool   bleEn  = _prefs.getBool("bleEnable", false);
        String saved  = (_st->bleSavedMac && strlen(_st->bleSavedMac)) ? String(_st->bleSavedMac) : "";
        bool   isConn = _st->bleConnDev && _st->bleConnDev->connected;
        uint8_t cnt   = (_st->bleDeviceCount ? *_st->bleDeviceCount : 0);

        String c = F("<div class='card'><h3>&#128268; BLE Client</h3>"
            "<form method='POST' action='/setble' style='margin-bottom:12px'>"
            "<label style='display:flex;align-items:center;gap:8px;font-weight:normal;margin-bottom:10px'>"
            "<input type='checkbox' name='en' value='1'");
        if (bleEn) c += F(" checked");
        c += F("> Enable BLE Client &mdash; scan &amp; connect to BLE sensors</label>"
            "<button class='btn btn-b' type='submit'>Save</button>"
            "</form>"
            "<hr style='margin:10px 0;border-color:#ede9fe'>");

        /* saved device + forget */
        if (saved.length()) {
            c += F("<div class='row' style='margin-bottom:10px'>"
                   "<span class='tag' style='font-family:monospace'>Saved: "); c += saved;
            c += F("</span>"
                   "<a class='btn btn-r' style='padding:4px 10px;font-size:12px' "
                   "href='/ble/forget' onclick=\"return confirm('Forget saved device?')\">"
                   "&#10060; Forget</a></div>");
        }

        /* scan button */
        c += F("<a class='btn btn-p' href='/ble/scan'>&#128269; Scan Now</a>");

        /* scan results */
        if (!isConn && cnt > 0) {
            c += F("<table style='margin-top:10px'>"
                   "<tr><th>Name</th><th>MAC</th><th>RSSI</th><th></th></tr>");
            for (uint8_t i = 0; i < cnt; i++) {
                const BLEFoundDevice& dev = _st->bleDevices[i];
                c += F("<tr><td>"); c += dev.name;
                c += F("</td><td style='font-family:monospace;font-size:12px'>"); c += dev.mac;
                c += F("</td><td>"); c += dev.rssi; c += F(" dBm</td><td>"
                       "<a class='btn btn-p' style='padding:4px 10px;font-size:12px' "
                       "href='/ble/connect?mac="); c += dev.mac; c += F("'>Connect</a>"
                       "</td></tr>");
            }
            c += F("</table>");
        } else if (!isConn && cnt == 0) {
            c += F("<p style='color:#9ca3af;font-size:12px;margin-top:8px'>"
                   "No results yet &mdash; press Scan Now.</p>");
        }

        c += F("</div>");
        sendSettingsChunk(c);
    }

    /* ── 3. Data Sources ── */
    {
        bool srcMod = _prefs.getBool("srcModbus", true);
        bool srcBle = _prefs.getBool("srcBle",    false);
        String c = F("<div class='card'><h3>&#128202; Data Sources</h3>"
            "<form method='POST' action='/setsrc'>"
            "<label style='display:flex;align-items:center;gap:8px;font-weight:normal;margin-bottom:8px'>"
            "<input type='checkbox' name='mod' value='1'");
        if (srcMod) c += F(" checked");
        c += F("> Modbus RS485</label>"
            "<label style='display:flex;align-items:center;gap:8px;font-weight:normal;margin-bottom:10px'>"
            "<input type='checkbox' name='ble' value='1'");
        if (srcBle) c += F(" checked");
        c += F("> BLE Sensor</label>"
            "<button class='btn btn-b' type='submit'>Save</button>"
            "</form></div>");
        sendSettingsChunk(c);
    }

    /* ── 3b. Soil Sensor (RS485) ── */
    {
        uint8_t soilId = _prefs.getUChar("soilSlaveId", SOIL_SLAVE_ID);
        String c = F("<div class='card'><h3>&#127793; Soil Sensor (RS485)</h3>"
            "<form method='POST' action='/setsoil'>"
            "<label>Slave ID (1-247)</label>"
            "<input type='number' name='sid' min='1' max='247' value='"); c += soilId;
        c += F("'>"
            "<button class='btn btn-b' type='submit'>Save</button>"
            "</form></div>");
        sendSettingsChunk(c);
    }

    /* ── 3c. Weather Sensor (RS485) ── */
    {
        uint8_t weathId = _prefs.getUChar("weathSlaveId", WEATHER_SLAVE_ID);
        String c = F("<div class='card'><h3>&#9925; Weather Sensor (RS485)</h3>"
            "<form method='POST' action='/setweath'>"
            "<label>Slave ID (1-247)</label>"
            "<input type='number' name='wid' min='1' max='247' value='"); c += weathId;
        c += F("'>"
            "<button class='btn btn-b' type='submit'>Save</button>"
            "</form></div>");
        sendSettingsChunk(c);
    }

    /* ── 4. File Intervals ── */
    {
        uint32_t dInt = _prefs.getUInt("fileInterval", 10);
        uint8_t pubBatch = _prefs.getUChar("pubBatchSize", PUBLISH_BATCH_SIZE);
        String c = F("<div class='card'><h3>&#128196; File &amp; Publish</h3>"
            "<form method='POST' action='/setfile'>"
            "<label>New data file every (minutes)</label>"
            "<input type='number' name='di' min='1' max='1440' value='"); c += dInt;
        c += F("'>"
            "<label>MQTT publish batch size (records)</label>"
            "<input type='number' name='pbs' min='1' max='60' value='"); c += pubBatch;
        c += F("'>"
            "<button class='btn btn-b' type='submit'>Save</button>"
            "</form></div>");
        sendSettingsChunk(c);
    }

    /* ── 5. InfluxDB v2 ── */
    {
        bool   inEn   = _prefs.getBool  ("influxEn",     false);
        String inHost = _prefs.getString("influxHost",   "");
        uint32_t inPt = _prefs.getUInt  ("influxPort",   8086);
        String inTok  = _prefs.getString("influxToken",  "");
        String inOrg  = _prefs.getString("influxOrg",    "");
        String inBkt  = _prefs.getString("influxBucket", "");
        String inSync = _prefs.getString("influxLastSync","--");
        String c = F("<div class='card'><h3>&#128200; InfluxDB v2</h3>"
            "<p style='color:#a78bfa;font-size:12px;margin-bottom:10px'>"
            "Last sync: <strong>");
        c += inSync;
        c += F("</strong></p>"
            "<form method='POST' action='/setinflux'>"
            "<label style='display:flex;align-items:center;gap:8px;font-weight:normal;margin-bottom:10px'>"
            "<input type='checkbox' name='en' value='1'");
        if (inEn) c += F(" checked");
        c += F("> Enable InfluxDB</label>"
            "<div class='grid2'>"
            "<div><label>Host / IP</label>"
            "<input type='text' name='host' value='"); c += inHost; c += F("'></div>"
            "<div><label>Port</label>"
            "<input type='number' name='port' value='"); c += inPt; c += F("'></div>"
            "</div>"
            "<label>API Token</label>"
            "<input type='text' name='tok' value='"); c += inTok; c += F("' autocomplete='off'>"
            "<div class='grid2'>"
            "<div><label>Organization</label>"
            "<input type='text' name='org' value='"); c += inOrg; c += F("'></div>"
            "<div><label>Bucket</label>"
            "<input type='text' name='bkt' value='"); c += inBkt; c += F("'></div>"
            "</div>"
            "<div style='display:flex;gap:8px;flex-wrap:wrap'>"
            "<button class='btn btn-b' type='submit'>Save</button>"
            "<button class='btn btn-y' type='button' onclick='testInflux()'>&#128268; Test Server</button>"
            "</div>"
            "</form>"
            "<div id='influx-res' style='margin-top:8px'></div>"
            "</div>");
        sendSettingsChunk(c);
    }

    /* ── 6. NTP ── */
    {
        bool ntpEn = _prefs.getBool("ntpEnable", true);
        String c = F("<div class='card'><h3>&#128336; NTP Sync</h3>"
            "<form method='POST' action='/setntp'>"
            "<label style='display:flex;align-items:center;gap:8px;font-weight:normal;margin-bottom:12px'>"
            "<input type='checkbox' name='en' value='1'");
        if (ntpEn) c += F(" checked");
        c += F("> Sync time every time board connects via GSM</label>"
            "<button class='btn btn-b' type='submit'>Save</button>"
            "</form></div>");
        sendSettingsChunk(c);
    }

    /* ── 7. Internal Memory ── */
    {
        uint8_t  memPct  = (uint8_t)_prefs.getUInt("memRollover", 80);
        bool     memEn   = _prefs.getBool("memRolloverEn", true);
        uint64_t fsUsed  = LittleFS.usedBytes();
        uint64_t fsTotal = LittleFS.totalBytes();
        uint8_t  fsPct   = fsTotal ? (uint8_t)(fsUsed * 100 / fsTotal) : 0;
        const char* barCol = (fsPct > 80) ? "#dc2626" : (fsPct > 60) ? "#d97706" : "#059669";
        String c = F("<div class='card'><h3>&#128190; Internal Memory</h3>");
        c += F("<div style='margin-bottom:14px'>"
               "<div style='display:flex;justify-content:space-between;"
               "font-size:12px;color:#a78bfa;margin-bottom:4px'>"
               "<span>Used: "); c += fmtBytes((uint32_t)fsUsed);
        c += F("</span><span>Free: "); c += fmtBytes((uint32_t)(fsTotal - fsUsed));
        c += F("</span><span>Total: "); c += fmtBytes((uint32_t)fsTotal);
        c += F("</span></div>"
               "<div style='background:#2d1a5c;border-radius:6px;height:18px;overflow:hidden'>"
               "<div style='background:"); c += barCol;
        c += F(";height:100%;width:"); c += fsPct;
        c += F("%'></div></div>"
               "<div style='text-align:center;font-size:12px;color:#c4b5fd;margin-top:3px'>"
               "<strong>"); c += fsPct;
        c += F("% used</strong></div></div>");
        c += F("<form method='POST' action='/setmem'>"
            "<label>Rollover at (%) <span id='mpv'>");  c += memPct; c += F("</span>%</label>"
            "<input type='range' name='pct' min='10' max='90' value='"); c += memPct;
        c += F("' oninput=\"document.getElementById('mpv').textContent=this.value\""
            " style='width:100%;margin-bottom:10px'>"
            "<label style='display:flex;align-items:center;gap:8px;font-weight:normal;margin-bottom:10px'>"
            "<input type='checkbox' name='en' value='1'");
        if (memEn) c += F(" checked");
        c += F("> Enable rollover (keep last entry)</label>"
            "<button class='btn btn-b' type='submit'>Save</button>"
            "</form></div>");
        sendSettingsChunk(c);
    }

    /* ── 8. MQTT ── */
    {
        bool     mqEn   = _prefs.getBool  ("mqttEnable", true);
        String   mqHost = _prefs.getString("mqttHost",   MQTT_BROKER);
        uint32_t mqPort = _prefs.getUInt  ("mqttPort",   MQTT_PORT);
        String   mqUser = _prefs.getString("mqttUser",   MQTT_USER);
        String c = F("<div class='card'><h3>&#128255; MQTT</h3>"
            "<form method='POST' action='/setmqtt'>"
            "<label style='display:flex;align-items:center;gap:8px;font-weight:normal;margin-bottom:10px'>"
            "<input type='checkbox' name='en' value='1'");
        if (mqEn) c += F(" checked");
        c += F("> Enable MQTT</label>"
            "<div class='grid2'>"
            "<div><label>Host / IP</label>"
            "<input type='text' name='host' value='"); c += mqHost; c += F("'></div>"
            "<div><label>Port</label>"
            "<input type='number' name='port' value='"); c += mqPort; c += F("'></div>"
            "</div>"
            "<div class='grid2'>"
            "<div><label>Username</label>"
            "<input type='text' name='user' value='"); c += mqUser; c += F("'></div>"
            "<div><label>Password</label>"
            "<input type='password' name='pass' placeholder='(unchanged if blank)'></div>"
            "</div>"
            "<div style='display:flex;gap:8px;flex-wrap:wrap'>"
            "<button class='btn btn-b' type='submit'>Save</button>"
            "<button class='btn btn-y' type='button' onclick='testMqtt()'>&#128268; Test Server</button>"
            "</div>"
            "</form>"
            "<div id='mqtt-res' style='margin-top:8px'></div>"
            "</div>");
        sendSettingsChunk(c);
    }

    /* ── 9. Email / Gmail ── */
    {
        bool     emEn  = _prefs.getBool  ("emailEnable", false);
        String   gmail = _prefs.getString("emailuser",   "");
        String   eto   = _prefs.getString("emailto",     "");
        String   ltTo  = _prefs.getString("emaillightto","");
        uint32_t freq  = _prefs.getUInt  ("emailFreqH",  24);
        bool     alarm = _prefs.getBool  ("emailalarm",  false);
        String c = F("<div class='card'><h3>&#128140; Email / Gmail Alarm</h3>"
            "<form method='POST' action='/setemail'>"
            "<label style='display:flex;align-items:center;gap:8px;font-weight:normal;margin-bottom:10px'>"
            "<input type='checkbox' name='en' value='1'");
        if (emEn) c += F(" checked");
        c += F("> Enable email notifications</label>"
            "<label>Gmail Address (sender)</label>"
            "<input type='email' name='gmail' value='"); c += gmail; c += F("'>"
            "<label>Gmail App Password</label>"
            "<input type='password' name='gpass' placeholder='(unchanged if blank)'>"
            "<label>Primary Recipient</label>"
            "<input type='email' name='eto' value='"); c += eto; c += F("'>"
            "<label>Notify every (hours)</label>"
            "<input type='number' name='freq' min='1' max='168' value='"); c += freq; c += F("'>"
            "<label>Lightning Report Recipient</label>"
            "<input type='email' name='lightto' value='"); c += ltTo; c += F("'>"
            "<label style='display:flex;align-items:center;gap:8px;font-weight:normal;margin-bottom:4px'>"
            "<input type='checkbox' name='alarm' value='1'");
        if (alarm) c += F(" checked");
        c += F("> Login attempt alarm (sends Time/Date, Username)</label>"
            "<div style='display:flex;gap:8px;flex-wrap:wrap;margin-top:6px'>"
            "<button class='btn btn-y' type='submit'>Save Email</button>"
            "<button class='btn btn-p' type='button' onclick='testEmail()'>&#128140; Test Email</button>"
            "</div>"
            "</form>"
            "<div id='email-res' style='margin-top:8px'></div>"
            "</div>");
        sendSettingsChunk(c);
    }

    /* ── 11. Change Password ── */
    {
        String c = F("<div class='card'><h3>&#128272; Change Password</h3>"
            "<form method='POST' action='/setpwd'>"
            "<label>New Password</label>"
            "<input type='password' name='p1' placeholder='New password' required>"
            "<label>Confirm Password</label>"
            "<input type='password' name='p2' placeholder='Confirm' required>"
            "<button class='btn btn-p' type='submit'>Save Password</button>"
            "</form></div>");
        sendSettingsChunk(c);
    }

    endSettingsPage();
}

/* ── SETTINGS SAVE HANDLERS ──────────────────────────────── */
void WifiApServer::hSetPassword() {
    if (!checkAuth()) return;
    String p1 = _server->arg("p1"), p2 = _server->arg("p2");
    if (p1 != p2 || p1.length() < 4) redirectTo("/settings?msg=er:Passwords don't match or too short");
    else { _prefs.putString("webpass", p1); appendEvent("Password changed"); redirectTo("/settings?msg=ok:Password updated"); }
}

void WifiApServer::hSetAp() {
    if (!checkAuth()) return;
    _prefs.putUInt("apTimeout", (uint32_t)_server->arg("apt").toInt());
    appendEvent("AP timeout updated");
    redirectTo("/settings?msg=ok:AP timeout saved");
}

void WifiApServer::hSetBle() {
    if (!checkAuth()) return;
    _prefs.putBool("bleEnable", _server->arg("en") == "1");
    appendEvent("BLE Client settings updated");
    redirectTo("/settings?msg=ok:BLE settings saved — restart to apply");
}

void WifiApServer::hSetSources() {
    if (!checkAuth()) return;
    _prefs.putBool("srcModbus", _server->arg("mod") == "1");
    _prefs.putBool("srcBle",    _server->arg("ble") == "1");
    appendEvent("Data sources updated");
    redirectTo("/settings?msg=ok:Data sources saved");
}

void WifiApServer::hSetSoil() {
    if (!checkAuth()) return;
    int sid = _server->arg("sid").toInt();
    if (sid >= 1 && sid <= 247) _prefs.putUChar("soilSlaveId", (uint8_t)sid);
    appendEvent("Soil sensor updated");
    redirectTo("/settings?msg=ok:Soil sensor saved");
}

void WifiApServer::hSetWeather() {
    if (!checkAuth()) return;
    int wid = _server->arg("wid").toInt();
    if (wid >= 1 && wid <= 247) _prefs.putUChar("weathSlaveId", (uint8_t)wid);
    appendEvent("Weather sensor updated");
    redirectTo("/settings?msg=ok:Weather sensor saved");
}

void WifiApServer::hSetFile() {
    if (!checkAuth()) return;
    _prefs.putUInt("fileInterval", (uint32_t)_server->arg("di").toInt());
    int pbs = _server->arg("pbs").toInt();
    if (pbs >= 1 && pbs <= 60) _prefs.putUChar("pubBatchSize", (uint8_t)pbs);
    appendEvent("File & publish settings updated");
    redirectTo("/settings?msg=ok:Settings saved");
}

void WifiApServer::hSetInflux() {
    if (!checkAuth()) return;
    _prefs.putBool  ("influxEn",     _server->arg("en")   == "1");
    _prefs.putString("influxHost",   _server->arg("host"));
    _prefs.putUInt  ("influxPort",   (uint32_t)_server->arg("port").toInt());
    _prefs.putString("influxToken",  _server->arg("tok"));
    _prefs.putString("influxOrg",    _server->arg("org"));
    _prefs.putString("influxBucket", _server->arg("bkt"));
    appendEvent("InfluxDB settings updated");
    redirectTo("/settings?msg=ok:InfluxDB settings saved");
}

void WifiApServer::hNtpSyncNow() {
    if (!checkAuth()) return;
    if (!_st || !_st->bleIsNus || !*_st->bleIsNus ||
        !_st->bleConnDev || !_st->bleConnDev->connected ||
        _st->bleConnDev->charCount == 0) {
        redirectTo("/settings?msg=er:BLE NUS not connected");
        return;
    }
    if (!_st->time) {
        redirectTo("/settings?msg=er:No time struct");
        return;
    }
    /* chars[0] = ts field, format: "YYYY-MM-DD HH:MM:SS" */
    const char* ts = _st->bleConnDev->chars[0].ascii;
    int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
    if (sscanf(ts, "%d-%d-%d %d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc) != 6 ||
        yr < 2020 || mo < 1 || mo > 12 || dy < 1 || dy > 31) {
        redirectTo("/settings?msg=er:BLE timestamp invalid");
        return;
    }
    _st->time->year   = (uint16_t)yr;
    _st->time->month  = (uint8_t)mo;
    _st->time->date   = (uint8_t)dy;
    _st->time->hour   = (uint8_t)hr;
    _st->time->minute = (uint8_t)mn;
    _st->time->second = (uint8_t)sc;
    snprintf(_st->time->dateStr, sizeof(_st->time->dateStr),
             "%02u/%02u/%04u", dy, mo, yr);
    snprintf(_st->time->timeStr, sizeof(_st->time->timeStr),
             "%02u:%02u:%02u", hr, mn, sc);
    appendEvent("NTP synced from BLE");
    redirectTo("/settings?msg=ok:Time synced from BLE");
}

void WifiApServer::hSetNtp() {
    if (!checkAuth()) return;
    _prefs.putBool("ntpEnable", _server->arg("en") == "1");
    appendEvent("NTP settings updated");
    redirectTo("/settings?msg=ok:NTP settings saved");
}

void WifiApServer::hSetMem() {
    if (!checkAuth()) return;
    uint32_t pct = (uint32_t)_server->arg("pct").toInt();
    if (pct < 10) pct = 10;
    if (pct > 90) pct = 90;
    _prefs.putUInt("memRollover",   pct);
    _prefs.putBool("memRolloverEn", _server->arg("en") == "1");
    appendEvent("Memory settings updated");
    redirectTo("/settings?msg=ok:Memory settings saved");
}

void WifiApServer::hSetMqtt() {
    if (!checkAuth()) return;
    _prefs.putBool  ("mqttEnable", _server->arg("en")   == "1");
    _prefs.putString("mqttHost",   _server->arg("host"));
    _prefs.putUInt  ("mqttPort",   (uint32_t)_server->arg("port").toInt());
    _prefs.putString("mqttUser",   _server->arg("user"));
    if (_server->arg("pass").length()) _prefs.putString("mqttPass", _server->arg("pass"));
    if (_st && _st->gsm) {
        String pass = _prefs.getString("mqttPass", MQTT_PASSWORD);
        _st->gsm->setMqttConfig(
            _server->arg("host").c_str(),
            (uint16_t)_server->arg("port").toInt(),
            _server->arg("user").c_str(),
            pass.c_str(), MQTT_CLIENT_ID);
    }
    appendEvent("MQTT settings updated");
    redirectTo("/settings?msg=ok:MQTT settings saved");
}

void WifiApServer::hSetEmail() {
    if (!checkAuth()) return;
    _prefs.putBool  ("emailEnable", _server->arg("en")     == "1");
    _prefs.putString("emailuser",   _server->arg("gmail"));
    if (_server->arg("gpass").length()) {
        _prefs.putString("emailpass", _server->arg("gpass"));
        _prefs.putString("emailsmtp", "smtp.gmail.com");
        _prefs.putUInt  ("emailport", 587);
    }
    _prefs.putString("emailto",       _server->arg("eto"));
    _prefs.putUInt  ("emailFreqH",    (uint32_t)_server->arg("freq").toInt());
    _prefs.putString("emaillightto",  _server->arg("lightto"));
    _prefs.putBool  ("emailalarm",    _server->arg("alarm") == "1");
    appendEvent("Email settings updated");
    redirectTo("/settings?msg=ok:Email settings saved");
}

void WifiApServer::hSetOta() {
    if (!checkAuth()) return;
    _prefs.putString("otaserver",  _server->arg("srv"));
    _prefs.putString("otaproject", _server->arg("prj"));
    _prefs.putString("otadevice",  _server->arg("dev"));
    _prefs.putString("otadlpass",  _server->arg("dpw"));
    _prefs.putString("otapass",    _server->arg("pw"));
    _prefs.putUInt  ("otainterval",(uint32_t)_server->arg("iv").toInt());
    _prefs.putBool  ("otaboot",    _server->arg("otaboot") == "1");
    ArduinoOTA.setPassword(_server->arg("pw").c_str());
    appendEvent("OTA settings updated");
    redirectTo("/settings?msg=ok:OTA settings saved");
}

void WifiApServer::hOtaUpdate() {
    if (!checkAuth()) return;

    String srv = _prefs.getString("otaserver", "");
    if (srv.length() == 0) { redirectTo("/settings?msg=er:No OTA server URL set"); return; }

    /* Set flag — main loop will stop AP, init GSM, and call checkRemoteOTA().
       Doing GSM TCP inside the WiFi AP handler causes mux conflicts and
       interference between the two network stacks. */
    if (_st && _st->otaCheckNow) *_st->otaCheckNow = true;
    appendEvent("OTA update requested via web portal");

    String b = F(
        "<div class='card'>"
        "<h3>&#128260; OTA Update Starting</h3>"
        "<p style='color:#e6edf3;margin:10px 0'>"
        "WiFi AP is closing now. The device will connect via GSM, "
        "check for a firmware update and reboot automatically if one is found.</p>"
        "<div class='tag y' style='display:block;padding:10px;margin:12px 0'>"
        "&#9654; OTA in progress &mdash; this may take 3&ndash;5 minutes over GPRS</div>"
        "<p style='color:#a78bfa;font-size:12px;margin-top:8px'>"
        "Reconnect to <b>WeatherStation_AP</b> afterward to verify the firmware version. "
        "If the device does not reboot, no update was available.</p>"
        "</div>");
    sendPage("OTA Update", "settings", b);
}


/* ── BLE CONNECT / DISCONNECT ────────────────────────────── */
void WifiApServer::hBleConnect() {
    if (!checkAuth()) return;
    String mac = _server->arg("mac");
    if (mac.length() == 17 && _st->bleTargetMac && _st->bleConnPending) {
        strncpy(_st->bleTargetMac, mac.c_str(), 17);
        _st->bleTargetMac[17] = '\0';
        *_st->bleConnPending = true;
        char ev[48]; snprintf(ev, sizeof(ev), "BLE connect request: %s", mac.c_str());
        appendEvent(ev);
    }
    redirectTo("/live");
}

void WifiApServer::hBleDisconnect() {
    if (!checkAuth()) return;
    if (_st->bleTargetMac)  { _st->bleTargetMac[0] = '\0'; }
    if (_st->bleConnDev)    { _st->bleConnDev->connected = false; _st->bleConnDev->charCount = 0; }
    if (_st->bleConnPending){ *_st->bleConnPending = false; }
    appendEvent("BLE disconnected");
    redirectTo("/live");
}

void WifiApServer::hBleScan() {
    if (!checkAuth()) return;
    if (_st->bleScanRequest) *_st->bleScanRequest = true;
    /* Show "scanning" splash with auto-redirect */
    _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server->send(200, "text/html", "");
    _server->sendContent(F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='4;url=/settings'>"
        "<style>body{font-family:Arial;display:flex;align-items:center;justify-content:center;"
        "min-height:100vh;background:#f5f3ff;text-align:center;color:#4c1d95}</style>"
        "</head><body><div>"
        "<h2>&#128268; Scanning for BLE devices&hellip;</h2>"
        "<p style='color:#a78bfa;margin-top:10px'>Returning to Settings in 4 s&hellip;</p>"
        "</div></body></html>"));
}

void WifiApServer::hBleForget() {
    if (!checkAuth()) return;
    _prefs.remove("bleSavedMac");
    if (_st->bleSavedMac)    { _st->bleSavedMac[0]  = '\0'; }
    if (_st->bleTargetMac)   { _st->bleTargetMac[0] = '\0'; }
    if (_st->bleConnDev)     { _st->bleConnDev->connected = false; _st->bleConnDev->charCount = 0; }
    if (_st->bleConnPending) { *_st->bleConnPending = false; }
    appendEvent("BLE forget saved device");
    redirectTo("/live");
}

void WifiApServer::hApiBle() {
    if (!_st || !_st->bleConnDev) {
        _server->send(200, "application/json",
            F("{\"connected\":false,\"nus\":false,\"hasData\":false,\"age\":0}"));
        return;
    }
    BLEConnectedDevice& d = *_st->bleConnDev;
    bool nus      = _st->bleIsNus     && *_st->bleIsNus;
    bool hasData  = nus && d.charCount > 0;
    uint32_t age  = 0;
    if (hasData && _st->bleNusLastMs && *_st->bleNusLastMs > 0)
        age = (uint32_t)((millis() - *_st->bleNusLastMs) / 1000UL);

    String j = F("{\"connected\":");
    j += d.connected ? "true" : "false";
    j += F(",\"nus\":");     j += nus     ? "true" : "false";
    j += F(",\"hasData\":"); j += hasData ? "true" : "false";
    j += F(",\"age\":"); j += age;
    j += F(",\"mac\":\"");   j += d.mac;
    j += F("\",\"name\":\""); j += d.name;
    j += F("\",\"chars\":[");
    for (uint8_t i = 0; i < d.charCount; i++) {
        if (i > 0) j += ',';
        j += F("{\"uuid\":\"");    j += d.chars[i].uuid;
        j += F("\",\"hex\":\"");   j += d.chars[i].value;
        j += F("\",\"ascii\":\""); j += d.chars[i].ascii;
        j += F("\"}");
    }
    j += F("]}");
    _server->send(200, "application/json", j);
}

void WifiApServer::hBleRefresh() {
    if (!checkAuth()) return;
    if (_st->bleNusRefreshReq) *_st->bleNusRefreshReq = true;
    _server->send(200, "application/json", F("{\"ok\":true}"));
}

/* ── ABOUT ─────────────────────────────────────────────────── */
void WifiApServer::hAbout() {
    if (!checkAuth()) return;
    String b = F(
        "<div class='card' style='text-align:center;line-height:2'>"

        /* Institution */
        "<p style='font-size:13px;color:#6b7280;letter-spacing:.5px'>Created by</p>"
        "<p style='font-weight:700;font-size:16px;color:#4c1d95;margin-top:4px'>"
        "IDEA Laboratory @ KMUTT</p>"
        "<p style='color:#374151'>Electrical Engineering Department</p>"

        "<div style='margin:18px auto;width:60px;border-top:2px solid #3d2a6e'></div>"

        /* Project name */
        "<p style='font-size:20px;font-weight:700;color:#2563eb'>"
        "All-in-One Weather Station</p>"

        "<div style='margin:16px auto;width:40px;border-top:1px solid #e5e7eb'></div>"

        /* "By" */
        "<p style='color:#9ca3af;font-size:13px;letter-spacing:2px'>BY</p>"
        "<br>"

        /* Thai names */
        "<p style='font-size:15px'><strong>อาจารย์ จักรกฤช กันทอง</strong></p>"
        "<p style='font-size:15px'><strong>อาจารย์ ธิระศักดิ์ เสภากล่อม</strong></p>"
        "<p style='font-size:15px'><strong>นาย กฤตภาส แจ่มวิลัยพันธุ์</strong></p>"
        "<br>"

        /* English names */
        "<p>Dr. Jakkrit Kunthong</p>"
        "<p>Aj. Tirasak Sapaklom</p>"
        "<p>Mr. Krittapat Jamvilaiphan</p>"

        "<div style='margin:16px auto;width:40px;border-top:1px solid #e5e7eb'></div>"

        "<p style='color:#6b7280;font-size:13px'>16 May 2026</p>"
        "<br>"
        "<span class='tag g'>Firmware v" FIRMWARE_VERSION "</span>"
        "&nbsp;"
        "<span class='tag'>Build " BUILD_DATE "</span>"
        "</div>"

        /* Reboot card */
        "<div class='card' style='text-align:center'>"
        "<h3 style='color:#dc2626;margin-bottom:12px'>&#9889; System Control</h3>"
        "<a class='btn btn-r' href='/reboot' "
        "onclick=\"return confirm('Reboot the device now?')\">"
        "&#128260; Reboot Device</a>"
        "</div>");

    sendPage("About", "about", b);
}

void WifiApServer::hReboot() {
    if (!checkAuth()) return;
    appendEvent("Manual reboot via web UI");
    _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server->send(200, "text/html", "");
    _server->sendContent(F("<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='8;url=/login'>"
        "<style>body{font-family:Arial;display:flex;align-items:center;justify-content:center;"
        "min-height:100vh;background:#4c1d95;color:#fff;text-align:center}</style>"
        "</head><body>"
        "<div><h2>&#128260; Rebooting&hellip;</h2>"
        "<p style='color:#93c5fd;margin-top:12px'>Reconnect to WeatherStation_AP in ~10 seconds</p>"
        "</div></body></html>"));
    delay(500);
    ESP.restart();
}

/* ── TEST / SYNC HANDLERS ─────────────────────────────────────── */

void WifiApServer::hTestInflux() {
    if (!checkAuth()) return;
    String host = _prefs.getString("influxHost", "");
    uint16_t port = (uint16_t)_prefs.getUInt("influxPort", 8086);
    if (host.length() == 0) {
        _server->send(200, "application/json",
            F("{\"ok\":false,\"msg\":\"No host configured\"}"));
        return;
    }
    WiFiClient c;
    bool ok = (c.connect(host.c_str(), port) != 0);
    if (ok) c.stop();
    char json[64];
    snprintf(json, sizeof(json), "{\"ok\":%s,\"msg\":\"%s\"}",
        ok ? "true" : "false",
        ok ? "TCP connected OK" : "Connection failed");
    _server->send(200, "application/json", json);
}


void WifiApServer::hTestMqtt() {
    if (!checkAuth()) return;
    String host = _prefs.getString("mqttHost", "");
    uint16_t port = (uint16_t)_prefs.getUInt("mqttPort", MQTT_PORT);
    if (host.length() == 0) {
        _server->send(200, "application/json",
            F("{\"ok\":false,\"msg\":\"No host configured\"}"));
        return;
    }
    WiFiClient c;
    bool ok = (c.connect(host.c_str(), port) != 0);
    if (ok) c.stop();
    char json[64];
    snprintf(json, sizeof(json), "{\"ok\":%s,\"msg\":\"%s\"}",
        ok ? "true" : "false",
        ok ? "TCP connected OK" : "Connection failed");
    _server->send(200, "application/json", json);
}

void WifiApServer::hTestEmail() {
    if (!checkAuth()) return;
    if (!_st || !_st->gsm || !_st->gsmAvail || !*_st->gsmAvail) {
        _server->send(200, "application/json",
            F("{\"ok\":false,\"msg\":\"GSM not connected\"}"));
        return;
    }
    String smtp = _prefs.getString("emailsmtp", "smtp.gmail.com");
    uint16_t port = (uint16_t)_prefs.getUInt("emailport", EMAIL_DEFAULT_PORT);
    String eu  = _prefs.getString("emailuser", "");
    String ep  = _prefs.getString("emailpass", "");
    String eto = _prefs.getString("emailto",   "");
    if (eu.length() == 0 || eto.length() == 0 || ep.length() == 0) {
        _server->send(200, "application/json",
            F("{\"ok\":false,\"msg\":\"Email not configured\"}"));
        return;
    }
    char body[128] = "Test from All-in-One WeatherStation";
    if (_st->time) {
        snprintf(body, sizeof(body),
            "Test from All-in-One WeatherStation\r\nTime: %02u/%02u/%04u %02u:%02u",
            _st->time->date, _st->time->month, _st->time->year,
            _st->time->hour, _st->time->minute);
    }
    bool ok = _st->gsm->sendEmail(smtp.c_str(), port, eu.c_str(), ep.c_str(),
        eto.c_str(), "WeatherStation Test Email", body);
    char json[60];
    snprintf(json, sizeof(json), "{\"ok\":%s,\"msg\":\"%s\"}",
        ok ? "true" : "false",
        ok ? "Email sent OK" : "Send failed");
    _server->send(200, "application/json", json);
}

void WifiApServer::hTestOta() {
    if (!checkAuth()) return;
    String srv = _prefs.getString("otaserver", "");
    if (srv.length() == 0) {
        _server->send(200, "application/json", F("{\"ok\":false,\"msg\":\"No URL configured\"}"));
        return;
    }
    if (!_st || !_st->gsm) {
        _server->send(200, "application/json", F("{\"ok\":false,\"msg\":\"GSM handler unavailable\"}"));
        return;
    }
    /* init GSM if not yet done */
    TinyGsm* modem = _st->gsm->getModem();
    if (!modem) {
        esp_task_wdt_reset();
        if (!_st->gsm->init(GSM_SERIAL)) {
            _server->send(200, "application/json", F("{\"ok\":false,\"msg\":\"GSM init failed\"}"));
            return;
        }
        modem = _st->gsm->getModem();
    }
    if (!_st->gsm->isGprsConnected()) {
        esp_task_wdt_reset();
        if (!_st->gsm->connectNetwork()) {
            _server->send(200, "application/json", F("{\"ok\":false,\"msg\":\"GPRS connect failed\"}"));
            return;
        }
    }
    String prj  = _prefs.getString("otaproject", OTA_DEFAULT_PROJECT);
    String dev  = _prefs.getString("otadevice",  OTA_DEFAULT_DEVICE);
    String dlpw = _prefs.getString("otadlpass",  OTA_DEFAULT_DLPASS);
    const char* devStr = dev.length() ? dev.c_str() : OTA_DEFAULT_DEVICE;

    /* parse host:port */
    String host; uint16_t port = 80;
    {
        String s = srv;
        if      (s.startsWith("http://"))  s = s.substring(7);
        else if (s.startsWith("https://")) s = s.substring(8);
        int slash = s.indexOf('/');
        String hp = (slash >= 0) ? s.substring(0, slash) : s;
        int colon = hp.lastIndexOf(':');
        if (colon >= 0) { host = hp.substring(0, colon); port = (uint16_t)hp.substring(colon+1).toInt(); }
        else host = hp;
    }

    /* use mux 1 — mux 0 is reserved for GsmHandler MQTT client */
    modem->sendAT("+CIPCLOSE=1"); modem->waitResponse(3000); esp_task_wdt_reset();
    TinyGsmClient client(*modem, 1);
    if (!client.connect(host.c_str(), port)) {
        _server->send(200, "application/json", F("{\"ok\":false,\"msg\":\"TCP connect failed\"}"));
        return;
    }
    /* /update: 304=up-to-date, 200=new fw available, 401=auth fail */
    String req = String("GET /update HTTP/1.1\r\n")
               + "Host: " + host + "\r\n"
               + "Connection: close\r\n"
               + "x-ESP32-version: " FIRMWARE_VERSION "\r\n"
               + "x-ESP32-device: "  + devStr + "\r\n"
               + "x-ESP32-project: " + prj + "\r\n";
    if (dlpw.length()) req += "x-ESP32-password: " + dlpw + "\r\n";
    req += "\r\n";
    client.print(req);

    uint32_t t0 = millis();
    while (!client.available() && millis() - t0 < 15000) { esp_task_wdt_reset(); delay(300); }

    int code = -1;
    if (client.available()) {
        String sl = client.readStringUntil('\n');
        int sp = sl.indexOf(' ');
        if (sp >= 0) code = sl.substring(sp+1, sp+4).toInt();
    }
    client.stop();

    char json[80];
    if (code == 304) {
        snprintf(json, sizeof(json), "{\"ok\":true,\"msg\":\"Server OK — firmware up to date (v%s)\"}", FIRMWARE_VERSION);
    } else if (code == 200) {
        snprintf(json, sizeof(json), "{\"ok\":true,\"msg\":\"Server OK — new firmware available\"}");
    } else if (code == 401) {
        snprintf(json, sizeof(json), "{\"ok\":false,\"msg\":\"Auth failed — check download password\"}");
    } else if (code < 0) {
        snprintf(json, sizeof(json), "{\"ok\":false,\"msg\":\"No response from server\"}");
    } else {
        snprintf(json, sizeof(json), "{\"ok\":false,\"msg\":\"HTTP %d\"}", code);
    }
    _server->send(200, "application/json", json);
}
