/*
 * scope-clock ESP-01 (ESP8266) WiFi co-processor.
 *
 * Role: a "dumb modem" for the STM32.  It joins WiFi, gets UTC from SNTP, and
 * serves a tiny web form for the message / mode / timezone.  Everything is
 * pushed to the STM32 over the UART (this sketch's Serial, 115200) as newline
 * commands the STM32 already parses (see firmware/stm32/src/esplink.c):
 *
 *   T=<utc_epoch>   TZ=<offset_sec>   M=<text>   MODE=<ANALOG|DIGITAL|MESSAGE|TEST>
 *
 * Board: "Generic ESP8266 Module" (ESP-01, 1 MB).  Flash it standalone; in the
 * clock, wire ESP TX(GPIO1) -> STM32 PA3(RX), ESP RX(GPIO3) -> STM32 PA2(TX),
 * common ground, solid 3V3.  The web form lives at http://scopeclock.local/.
 *
 *   >>> Set WIFI_SSID / WIFI_PASS below before flashing. <<<
 */
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <time.h>

static const char *WIFI_SSID = "YOUR_SSID";
static const char *WIFI_PASS = "YOUR_PASSWORD";

ESP8266WebServer server(80);

static long   tzOffset = 0;                 // seconds; default UTC
static String message  = "HELLO SCOPE";
static String mode     = "DIGITAL";
static uint32_t lastPush = 0;

static void pushLine(const String &s) { Serial.print(s); Serial.print('\n'); }

static void pushAll() {
  time_t now = time(nullptr);
  if (now > 100000) pushLine("T=" + String((uint32_t)now));
  pushLine("TZ=" + String(tzOffset));
  pushLine("MODE=" + mode);
  pushLine("M=" + message);
}

static String page() {
  String h = F("<!doctype html><meta name=viewport content='width=device-width,"
               "initial-scale=1'><title>Scope Clock</title>"
               "<style>body{font-family:system-ui;background:#061;color:#cfe;"
               "max-width:32rem;margin:2rem auto;padding:1rem}"
               "input,select{width:100%;padding:.5rem;margin:.3rem 0 1rem;"
               "font-size:1rem}button{padding:.6rem 1rem;font-size:1rem}"
               "h1{font-weight:600}</style><h1>Scope Clock</h1><form method=post "
               "action=/set>");
  h += F("<label>Message</label><input name=msg maxlength=63 value='");
  h += message; h += F("'>");
  h += F("<label>Mode</label><select name=mode>");
  const char *modes[] = {"ANALOG", "DIGITAL", "MESSAGE", "TEST"};
  for (auto mch : modes) {
    h += "<option"; if (mode == mch) h += " selected";
    h += ">"; h += mch; h += "</option>";
  }
  h += F("</select><label>UTC offset (seconds, e.g. -14400 = EDT)</label>"
         "<input name=tz type=number value='");
  h += String(tzOffset); h += F("'><button>Apply</button></form>");
  time_t now = time(nullptr);
  h += F("<p style='opacity:.7'>UTC epoch: ");
  h += String((uint32_t)now); h += F("</p>");
  return h;
}

static void handleRoot() { server.send(200, "text/html", page()); }

static void handleSet() {
  if (server.hasArg("msg"))  message  = server.arg("msg");
  if (server.hasArg("mode")) mode     = server.arg("mode");
  if (server.hasArg("tz"))   tzOffset = server.arg("tz").toInt();
  pushAll();
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(200);

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");   // SNTP, keep UTC
  if (MDNS.begin("scopeclock")) MDNS.addService("http", "tcp", 80);

  server.on("/", handleRoot);
  server.on("/set", HTTP_POST, handleSet);
  server.begin();
}

void loop() {
  MDNS.update();
  server.handleClient();

  // Re-discipline the STM32 clock every 10 s once SNTP has a fix.
  if (millis() - lastPush > 10000) {
    lastPush = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.begin(WIFI_SSID, WIFI_PASS);
    pushAll();
  }
}
