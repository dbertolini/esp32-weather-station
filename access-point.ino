#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Function to draw a dynamic WiFi connecting animation
void drawConnectingAnimation(int frame) {
  display.clearDisplay();
  
  // --- YELLOW ZONE (Top 16px) ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Centered text
  display.setCursor(34, 4);
  display.print(F("CONECTANDO"));
  
  // --- BLUE ZONE (Bottom 48px) ---
  // Draw Antenna Base at bottom center
  int cx = 64;
  int cy = 55;
  
  // Antenna Pole/Base
  display.fillCircle(cx, cy, 2, SSD1306_WHITE);
  display.drawLine(cx, cy, cx, cy+8, SSD1306_WHITE); // Stick down
  
  // Signal Waves (Arcs)
  // We simulate arcs by drawing circles and blocking the bottom half
  
  // Wave 1
  if (frame >= 1) {
    display.drawCircle(cx, cy, 10, SSD1306_WHITE);
    display.drawCircle(cx, cy, 11, SSD1306_WHITE); // Thicker
  }
  
  // Wave 2
  if (frame >= 2) {
    display.drawCircle(cx, cy, 20, SSD1306_WHITE);
    display.drawCircle(cx, cy, 21, SSD1306_WHITE);
  }
  
  // Wave 3 (Reaches towards Yellow zone)
  if (frame >= 3) {
    display.drawCircle(cx, cy, 30, SSD1306_WHITE);
    display.drawCircle(cx, cy, 31, SSD1306_WHITE);
  }

  // Obscure the bottom half of the circles to create "Dish/Wave" look
  // Draw black rectangle below the center point
  display.fillRect(0, cy + 3, 128, 30, SSD1306_BLACK);
  
  // Redraw base to be sure it's on top if needed (though we cut below it)
  // Actually the cut (cy+3) might cut the base circle (radius 2). 
  // Base is at cy. cy+3 is fine.
  
  display.display();
}

const int LED_PIN = 2;
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

String ssid = "";
String password = "";

String networksHTML = ""; // Cache for scan results

// --- Global Variables for Time & Weather ---
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -10800; // GMT-3 (Argentina)
const int   daylightOffset_sec = 0; 

// Weather Data
float currentTemp = 0.0;
int currentHumidity = 0;
unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_INTERVAL = 900000; // 15 minutes
bool weatherAvailable = false;
String city = "Buenos Aires"; // Default, will update via IP

// Pantalla para modo Configuración (AP)
// Muestra icono de Telefono -> WiFi -> Chip
void drawConfigModeScreen(int frame) {
  display.clearDisplay();

  // --- ZONA AMARILLA (Titulo) ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 0); 
  display.print(F("SETUP REQUERIDO")); // o "MODO CONFIGURACION"

  // --- ZONA AZUL (Graficos) ---
  
  // 1. Icono Telefono (Izquierda)
  // Cuerpo
  display.drawRoundRect(10, 20, 18, 32, 2, SSD1306_WHITE);
  display.fillRect(12, 24, 14, 20, SSD1306_BLACK); // Pantalla negra
  display.drawLine(17, 48, 21, 48, SSD1306_WHITE); // Boton home
  // "App" en pantalla
  display.fillRect(14, 28, 10, 2, SSD1306_WHITE);
  display.fillRect(14, 32, 10, 2, SSD1306_WHITE);

  // 2. Icono ESP32 / AP (Derecha)
  display.drawRect(90, 28, 24, 16, SSD1306_WHITE);
  display.drawRect(92, 30, 4, 4, SSD1306_WHITE); // Chip visual
  // Antena
  display.drawLine(102, 28, 102, 20, SSD1306_WHITE);
  display.fillCircle(102, 19, 1, SSD1306_WHITE);
  
  // 3. Animacion de Flechas / Ondas
  // frame 0..3
  int step = frame % 4;
  int startX = 35;
  int endX = 80;
  
  // Flecha animada moviendose
  for(int i=0; i<3; i++) {
     int x = startX + (i*15) + (step * 3);
     if(x < endX) {
        // Dibujar Chevron >
        display.drawLine(x, 36, x+4, 36+4, SSD1306_WHITE);
        display.drawLine(x+4, 36+4, x, 36+8, SSD1306_WHITE);
     }
  }

  // Texto Info (Abajo del todo)
  display.setTextSize(1);
  display.setCursor(10, 56);
  display.print(F("RED: ESP32-Config"));

  display.display();
}
String currentLat = "";
String currentLon = "";
int rainProb[4] = {0,0,0,0};


// Forward declaration
void handleNotFound();
void startAP(); 

// Helper to wrap content in a professional HTML structure
String getHTML(String title, String content, String script = "", bool showLang = true) {
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>" + title + "</title>";
  html += "<style>";
  html += ":root { --primary: #2563eb; --bg: #f3f4f6; --card: #ffffff; --text: #1f2937; }";
  html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background: var(--bg); color: var(--text); display: flex; align-items: center; justify-content: center; min-height: 100vh; margin: 0; padding: 20px; box-sizing: border-box; }";
  html += ".card { background: var(--card); padding: 2rem; border-radius: 1rem; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.1); width: 100%; max-width: 400px; text-align: center; }";
  html += "h1 { margin-top: 0; color: var(--primary); font-size: 1.5rem; margin-bottom: 1.5rem; }";
  html += "input, select { width: 100%; padding: 0.75rem; margin-top: 0.5rem; margin-bottom: 1rem; border: 1px solid #d1d5db; border-radius: 0.5rem; box-sizing: border-box; font-size: 1rem; transition: border-color 0.2s; background: white; }";
  html += "input:focus, select:focus { outline: none; border-color: var(--primary); ring: 2px solid rgba(37, 99, 235, 0.2); }";
  html += "label { font-weight: 600; font-size: 0.875rem; display: block; text-align: left; color: #4b5563; }";
  html += "button { width: 100%; padding: 0.75rem; background: var(--primary); color: white; border: none; border-radius: 0.5rem; font-size: 1rem; font-weight: 600; cursor: pointer; transition: background 0.2s; margin-top: 1rem; }";
  html += "button:hover { background: #1d4ed8; }";
  html += ".hidden { display: none; }";
  html += ".status { margin-top: 1rem; color: #6b7280; font-size: 0.9rem; }";
  html += ".spinner { border: 3px solid #f3f3f3; border-top: 3px solid var(--primary); border-radius: 50%; width: 24px; height: 24px; animation: spin 1s linear infinite; margin: 1rem auto; }";
  html += "@keyframes spin { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }";
  html += ".lang-select { margin-bottom: 1rem; width: 100%; margin-top: 0.5rem; }";
  html += ".lang-container { text-align: left; margin-bottom: 1rem; display: " + String(showLang ? "block" : "none") + "; }";
  html += ".password-container { position: relative; width: 100%; margin-top: 0.5rem; margin-bottom: 1rem; }";
  html += ".password-container input { margin: 0 !important; }";
  html += ".toggle-pass { position: absolute; right: 12px; top: 50%; transform: translateY(-50%); cursor: pointer; font-size: 1.2rem; user-select: none; line-height: 1; }";
  html += ".switch { position: relative; display: inline-block; width: 50px; height: 26px; }";
  html += ".switch input { opacity: 0; width: 0; height: 0; }";
  html += ".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }";
  html += ".slider:before { position: absolute; content: ''; height: 18px; width: 18px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }";
  html += "input:checked + .slider { background-color: var(--primary); }";
  html += "input:checked + .slider:before { transform: translateX(24px); }";
  html += "</style></head><body>";
  html += "<div class='card'>";
  html += "<div class='lang-container'>";
  html += "<label for='lang' data-i18n='l_lang' style='font-size: 0.75rem; color: #6b7280;'>Language</label>";
  html += "<select id='lang' class='lang-select' onchange='setLang(this.value)'><option value='en'>🇺🇸 English</option><option value='es'>🇪🇸 Español</option><option value='zh'>🇨🇳 中文 (Mandarin)</option><option value='pt'>🇧🇷 Português</option><option value='fr'>🇫🇷 Français</option></select>";
  html += "</div>";
  html += content + "</div>";
  
  html += "<script>\n";
  html += "const dict = {\n";
  html += "  \"en\": {\"t_cfg\": \"WiFi Config\", \"l_ssid\": \"Select Network (SSID)\", \"l_pass\": \"Password\", \"b_save\": \"Save & Connect\", \"l_rescan\": \"Rescan (Restarts AP)\", \"t_saving\": \"Saving...\", \"msg_app\": \"Applying changes & restarting...\", \"t_saved\": \"Saved!\", \"msg_cred\": \"Credentials updated.\", \"msg_rest\": \"Device is restarting...\", \"t_err\": \"Error\", \"msg_miss\": \"Missing required fields.\", \"b_retry\": \"Try Again\", \"opt_no\": \"No networks found\", \"opt_sel\": \"Select a network...\", \"t_scan\": \"Scanning...\", \"msg_scan\": \"Restarting to scan networks. Reconnect in 10s.\", \"l_lang\": \"Language\", \"t_dash\": \"Smart Control\", \"l_led\": \"Main LED\", \"st_on\": \"ON\", \"st_off\": \"OFF\"},\n";
  html += "  \"es\": {\"t_cfg\": \"Configuración WiFi\", \"l_ssid\": \"Seleccionar Red (SSID)\", \"l_pass\": \"Contraseña\", \"b_save\": \"Guardar y Conectar\", \"l_rescan\": \"Escanear de nuevo (Reinicia AP)\", \"t_saving\": \"Guardando...\", \"msg_app\": \"Aplicando cambios y reiniciando...\", \"t_saved\": \"¡Guardado!\", \"msg_cred\": \"Credenciales actualizadas.\", \"msg_rest\": \"El dispositivo se está reiniciando...\", \"t_err\": \"Error\", \"msg_miss\": \"Faltan campos requeridos.\", \"b_retry\": \"Intentar de nuevo\", \"opt_no\": \"No se encontraron redes\", \"opt_sel\": \"Selecciona una red...\", \"t_scan\": \"Escaneando...\", \"msg_scan\": \"Reiniciando para escanear. Reconecte en 10s.\", \"l_lang\": \"Lenguaje\", \"t_dash\": \"Control Inteligente\", \"l_led\": \"LED Principal\", \"st_on\": \"ENCENDIDO\", \"st_off\": \"APAGADO\"},\n";
  html += "  \"zh\": {\"t_cfg\": \"WiFi 配置\", \"l_ssid\": \"选择网络 (SSID)\", \"l_pass\": \"密码\", \"b_save\": \"保存并连接\", \"l_rescan\": \"重新扫描 (重启 AP)\", \"t_saving\": \"保存中...\", \"msg_app\": \"正在应用更改并重启...\", \"t_saved\": \"已保存！\", \"msg_cred\": \"凭据已更新。\", \"msg_rest\": \"设备正在重启...\", \"t_err\": \"错误\", \"msg_miss\": \"缺少必填字段。\", \"b_retry\": \"重试\", \"opt_no\": \"未找到网络\", \"opt_sel\": \"请选择网络...\", \"t_scan\": \"扫描中...\", \"msg_scan\": \"正在重启以扫描网络。请在10秒后重新连接。\", \"l_lang\": \"语言\", \"t_dash\": \"智能控制\", \"l_led\": \"主灯\", \"st_on\": \"开启\", \"st_off\": \"关闭\"},\n";
  html += "  \"pt\": {\"t_cfg\": \"Configuração WiFi\", \"l_ssid\": \"Selecionar Rede (SSID)\", \"l_pass\": \"Senha\", \"b_save\": \"Salvar e Conectar\", \"l_rescan\": \"Escanear novamente (Reinicia AP)\", \"t_saving\": \"Salvando...\", \"msg_app\": \"Aplicando alterações e reiniciando...\", \"t_saved\": \"Salvo!\", \"msg_cred\": \"Credenciais atualizadas.\", \"msg_rest\": \"O dispositivo está reiniciando...\", \"t_err\": \"Erro\", \"msg_miss\": \"Campos obrigatórios ausentes.\", \"b_retry\": \"Tentar novamente\", \"opt_no\": \"Nenhuma rede encontrada\", \"opt_sel\": \"Selecione uma rede...\", \"t_scan\": \"Escaneando...\", \"msg_scan\": \"Reiniciando para escanear. Reconecte in 10s.\", \"l_lang\": \"Idioma\", \"t_dash\": \"Controle Inteligente\", \"l_led\": \"LED Principal\", \"st_on\": \"LIGADO\", \"st_off\": \"DESLIGADO\"},\n";
  html += "  \"fr\": {\"t_cfg\": \"Configuration WiFi\", \"l_ssid\": \"Sélectionner Réseau (SSID)\", \"l_pass\": \"Mot de passe\", \"b_save\": \"Enregistrer et Connecter\", \"l_rescan\": \"Scanner à nouveau (Redémarre AP)\", \"t_saving\": \"Enregistrement...\", \"msg_app\": \"Application des modifications...\", \"t_saved\": \"Enregistré !\", \"msg_cred\": \"Identifiants mis à jour.\", \"msg_rest\": \"Redémarrage de l'appareil...\", \"t_err\": \"Error\", \"msg_miss\": \"Champs requis manquants.\", \"b_retry\": \"Réessayer\", \"opt_no\": \"Aucun réseau trouvé\", \"opt_sel\": \"Sélectionnez un réseau...\", \"t_scan\": \"Scan en cours...\", \"msg_scan\": \"Redémarrage pour scanner. Reconnexion dans 10s.\", \"l_lang\": \"Langue\", \"t_dash\": \"Contrôle Intelligent\", \"l_led\": \"LED Principal\", \"st_on\": \"ALLUMÉ\", \"st_off\": \"ÉTEINT\"}\n";
  html += "};\n";
  html += "function setLang(l){";
  html += " localStorage.setItem('lang',l);";
  html += " const t=dict[l]||dict['en'];";
  html += " document.querySelectorAll('[data-i18n]').forEach(e=>{";
  html += "  const k=e.getAttribute('data-i18n');";
  html += "  if(t[k]) {";
  html += "   if(e.tagName=='INPUT') e.placeholder=t[k];";
  html += "   else e.innerHTML=t[k];";
  html += "  }";
  html += " });";
  html += "}";
  html += "function togglePass(){";
  html += " const p=document.getElementById('pass');";
  html += " const s=document.getElementById('toggleIcon');";
  html += " if(p.type=='password'){ p.type='text'; s.innerHTML='🙈'; }";
  html += " else { p.type='password'; s.innerHTML='👁️'; }";
  html += "}";
  html += "window.addEventListener('DOMContentLoaded', ()=>{";
  html += " const l=localStorage.getItem('lang')||'en';";
  html += " document.getElementById('lang').value=l;";
  html += " setLang(l);";
  html += "});";
  html += script;
  html += "</script>";
  html += "</body></html>";
  return html;
}

// Perform scan securely properly before AP starts
void performScan() {
  Serial.println("Scanning networks...");
  // Temporarily set STA mode to scan robustly
  WiFi.mode(WIFI_STA); 
  WiFi.disconnect();
  delay(100);
  
  int n = WiFi.scanNetworks();
  Serial.println("Scan done");
  
  if (n == 0) {
    networksHTML = "<option value='' disabled data-i18n='opt_no'>No se encontraron redes</option>";
  } else {
    networksHTML = "<option value='' disabled selected data-i18n='opt_sel'>Selecciona una red...</option>";
    for (int i = 0; i < n; ++i) {
      networksHTML += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + " (" + WiFi.RSSI(i) + " dBm)</option>";
    }
  }
}

// Function to serve the HTML configuration page
void handleRoot() {
  // Use cached networksHTML instead of scanning here logic to avoid connection drops
  String content = "<div id='mainForm'>";
  content += "<h1 data-i18n='t_cfg'>Configurar WiFi</h1>";
  content += "<form action='/save' method='POST' onsubmit='showLoading()'>";
  content += "<label data-i18n='l_ssid'>Seleccionar Red (SSID)</label>";
  content += "<select name='ssid' required>" + networksHTML + "</select>";
  content += "<label data-i18n='l_pass'>Contraseña</label>";
  content += "<div class='password-container'>";
  content += "<input type='password' id='pass' name='password' required placeholder='********'>";
  content += "<span class='toggle-pass' id='toggleIcon' onclick='togglePass()'>👁️</span>";
  content += "</div>";
  content += "<button type='submit' data-i18n='b_save'>Guardar y Conectar</button>";
  content += "</form>";
  content += "<p style='margin-top:10px;'><a href='/rescan' style='color:#6b7280;text-decoration:underline;font-size:0.9rem;' data-i18n='l_rescan'>Escanear de nuevo (Reinicia AP)</a></p>";
  content += "</div>";
  
  content += "<div id='loading' class='hidden'>";
  content += "<h1 data-i18n='t_saving'>Guardando...</h1>";
  content += "<div class='spinner'></div>";
  content += "<p class='status' data-i18n='msg_app'>Aplicando cambios y reiniciando...</p>";
  content += "</div>";

  String script = "function showLoading() { document.getElementById('mainForm').classList.add('hidden'); document.getElementById('loading').classList.remove('hidden'); }";
  
  server.send(200, "text/html", getHTML("Configuración WiFi", content, script));
}

// Handler to force a rescan (restarts AP to be safe)
void handleRescan() {
  String content = "<h1 data-i18n='t_scan'>Escaneando...</h1><p data-i18n='msg_scan'>El dispositivo se reiniciará para escanear redes. Vuelva a conectarse en 10 segundos.</p>";
  String script = "setTimeout(function(){window.location.href='/';}, 5000);";
  server.send(200, "text/html", getHTML("Escaneando", content, script));
  delay(1000);
  // Restarting is the easiest way to reset radios/scan cleanly without logic complexity
  ESP.restart(); 
}

// Function to handle form submission
void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String new_ssid = server.arg("ssid");
    String new_password = server.arg("password");

    preferences.begin("wifi", false);
    preferences.putString("ssid", new_ssid);
    preferences.putString("password", new_password);
    preferences.end();
    
    String content = "<h1 style='color: #10b981;' data-i18n='t_saved'>¡Guardado!</h1>";
    content += "<p class='status' data-i18n='msg_cred'>Credenciales actualizadas correctamente.</p>";
    content += "<p class='status' data-i18n='msg_rest'>El dispositivo se está reiniciando...</p>";
    
    server.send(200, "text/html", getHTML("Guardado", content, "", false));
    
    delay(2000);
    ESP.restart();
  } else {
    String content = "<h1 style='color: #ef4444;' data-i18n='t_err'>Error</h1>";
    content += "<p class='status' data-i18n='msg_miss'>Faltan campos requeridos.</p>";
    content += "<button onclick='history.back()' data-i18n='b_retry'>Intentar de nuevo</button>";
    server.send(400, "text/html", getHTML("Error", content, "", false));
  }
}

const int CONTROL_PIN = 5; 
enum LedMode { MODE_OFF = 0, MODE_STEADY = 1, MODE_STROBE = 2 };
int currentMode = MODE_OFF;

// Variables for Strobe effect
unsigned long lastStrobeTime = 0;
int strobeStep = 0; // 0: off, 1: first flash, 2: pause, 3: second flash, 4: long pause


// Handler for setting LED mode
void handleSetMode() {
  if (server.hasArg("mode")) {
    int newMode = server.arg("mode").toInt();
    if (newMode >= 0 && newMode <= 2) {
        currentMode = newMode;
        
        // Persist new mode
        Preferences io_prefs;
        io_prefs.begin("io_config", false);
        io_prefs.putInt("led_mode", currentMode);
        io_prefs.end();

        // Immediate reaction for Steady/Off to feel responsive
        if (currentMode == MODE_OFF) digitalWrite(CONTROL_PIN, LOW);
        else if (currentMode == MODE_STEADY) digitalWrite(CONTROL_PIN, HIGH);
        // Strobe is handled in loop()

        server.send(200, "text/plain", String(currentMode));
    } else {
        server.send(400, "text/plain", "Invalid Mode");
    }
  } else {
    server.send(400, "text/plain", "Missing Mode");
  }
}

// Handler for the Dashboard (STA mode)
void handleDashboard() {
  String content = "<h1 data-i18n='t_dash'>Smart Control</h1>";
  
  // -- Switch 1: Steady Mode --
  content += "<div style='display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid #eee; padding:10px 0;'>";
  content += "<div style='text-align:left;'><div><strong data-i18n='l_led'>Main LED</strong></div><div style='font-size:0.8rem; color:#666;'>Continuous</div></div>";
  String steadyState = (currentMode == MODE_STEADY) ? "checked" : "";
  content += "<label class='switch'><input type='checkbox' id='sw_steady' onchange='setMode(this.checked ? 1 : 0)' " + steadyState + "><span class='slider round'></span></label>";
  content += "</div>";

  // -- Switch 2: Strobe Mode (Airplane) --
  content += "<div style='display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid #eee; padding:10px 0;'>";
  content += "<div style='text-align:left;'><div><strong data-i18n='l_strobe'>Airplane Mode</strong></div><div style='font-size:0.8rem; color:#666;'>Strobe Effect</div></div>";
  String strobeState = (currentMode == MODE_STROBE) ? "checked" : "";
  content += "<label class='switch'><input type='checkbox' id='sw_strobe' onchange='setMode(this.checked ? 2 : 0)' " + strobeState + "><span class='slider round'></span></label>";
  content += "</div>";

  String script = "function setMode(m) {";
  // Mutually exclusive UI logic
  script += " if(m==1) document.getElementById('sw_strobe').checked = false;";
  script += " if(m==2) document.getElementById('sw_steady').checked = false;";
  // API Call
  script += " fetch('/set_mode?mode=' + m).then(r => r.text()).then(res => { console.log('Mode set to ' + res); });";
  script += "}";
  
  // Update dictionary with new keys
  String extraDict = "<script>Object.assign(dict.en, {'l_strobe': 'Airplane Mode'});Object.assign(dict.es, {'l_strobe': 'Modo Avión'});Object.assign(dict.zh, {'l_strobe': '飞机模式'});Object.assign(dict.pt, {'l_strobe': 'Modo Avião'});Object.assign(dict.fr, {'l_strobe': 'Mode Avion'});</script>";
  
  server.send(200, "text/html", getHTML("Smart Dashboard", content + extraDict, script));
}

void handleNotFound() {
  server.sendHeader("Location", "/", true); 
  server.send(302, "text/plain", "");
}

void setupWiFi() {
  preferences.begin("wifi", true); // Read-only mode
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  preferences.end();

  if (ssid == "") {
    Serial.println("No SSID stored. Starting AP mode.");
    startAP();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Connecting to WiFi");
  
  // Wait for connection for up to 10 seconds
  // Wait for connection for up to 10 seconds
  unsigned long startAttemptTime = millis();
  int frame = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    drawConnectingAnimation(frame);
    frame = (frame + 1) % 4;
    delay(250);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Start mDNS
    if (MDNS.begin("control")) {
      Serial.println("MDNS responder started. Access via http://control.local");
    }

    digitalWrite(LED_PIN, HIGH);
    
    // Start Web Server for Dashboard in STA mode
    server.on("/", handleDashboard);
    server.on("/set_mode", handleSetMode);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("Dashboard Server Started");

    
    // Init Time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("Time configured");
  } else {
    Serial.println("\nFailed to connect. Starting AP mode.");
    startAP();
  }
}

// --- Helper Functions for Weather & Display ---

void updateWeather() {
  if(WiFi.status() == WL_CONNECTED) {
    // 1. Get Location (HTTP)
    // Use proper WiFiClient to prevent heap issues with implicit client reuse
    WiFiClient client;
    HTTPClient http;
    
    // Defaulting to Buenos Aires approx if fails
    // Defaulting to Buenos Aires approx if fails
    // String lat = "-34.6"; // Removed local var
    // String lon = "-58.4"; // Removed local var
    // using globals currentLat / currentLon instad
    
    // Serial debugging
    Serial.println("Fetching Location from IP-API...");
    
    if (http.begin(client, "http://ip-api.com/json/?fields=lat,lon")) {
       int httpCode = http.GET();
       if (httpCode > 0) {
         String payload = http.getString();
         Serial.println("Got Location Payload");
         
         int latIdx = payload.indexOf("\"lat\":");
         int lonIdx = payload.indexOf("\"lon\":");
         
         if(latIdx > 0 && lonIdx > 0) {
            int comma1 = payload.indexOf(",", latIdx);
            if (comma1 > 0) currentLat = payload.substring(latIdx + 6, comma1);
            
            int comma2 = payload.indexOf("}", lonIdx); 
            if(comma2 < 0) comma2 = payload.indexOf(",", lonIdx);
            if (comma2 > 0) currentLon = payload.substring(lonIdx + 6, comma2);
         }
       } else {
         Serial.printf("IP-API Error: %d\n", httpCode);
       }
       http.end();
    } else {
       Serial.println("Unable to connect to IP-API");
    }

    // 2. Get Weather (HTTPS) - ONLY if we have a location
    if (currentLat != "" && currentLon != "") {
      Serial.println("Fetching Weather from Open-Meteo...");
      WiFiClientSecure clientSecure;
      clientSecure.setInsecure(); // Skip certificate verification
      
      String weatherURL = "https://api.open-meteo.com/v1/forecast?latitude=" + currentLat + "&longitude=" + currentLon + "&current=temperature_2m,relative_humidity_2m&daily=precipitation_probability_max&timezone=auto";
    
    // Create new HTTPClient to be safe
    HTTPClient https;
    if (https.begin(clientSecure, weatherURL)) {
        int httpCode = https.GET();
        if (httpCode > 0) {
          String payload = https.getString();
          Serial.println("Got Weather Payload");
          
          int currentBlock = payload.indexOf("\"current\":{");
          if (currentBlock > 0) {
            int tempIdx = payload.indexOf("\"temperature_2m\":", currentBlock);
            int humIdx = payload.indexOf("\"relative_humidity_2m\":", currentBlock);
            
            if (tempIdx > 0 && humIdx > 0) {
               // Robust parsing by finding the colon
               int colonT = payload.indexOf(":", tempIdx);
               int commaT = payload.indexOf(",", colonT);
               if (colonT > 0 && commaT > 0) {
                   String tStr = payload.substring(colonT + 1, commaT);
                   currentTemp = tStr.toFloat();
               }
               
               int colonH = payload.indexOf(":", humIdx);
               int endBrace = payload.indexOf("}", colonH);
               if (colonH > 0 && endBrace > 0) {
                   String hStr = payload.substring(colonH + 1, endBrace);
                   currentHumidity = hStr.toInt();
                   
                   weatherAvailable = true;
                   lastWeatherUpdate = millis();
                   Serial.printf("Weather Updated: %.1fC %d%%\n", currentTemp, currentHumidity);
               }
            }
          }
          
          // Parse Daily Rain Probability
          int dailyBlock = payload.indexOf("\"daily\":");
          if (dailyBlock > 0) {
            int probIdx = payload.indexOf("\"precipitation_probability_max\":", dailyBlock);
            if (probIdx > 0) {
              int startArr = payload.indexOf("[", probIdx);
              int endArr = payload.indexOf("]", startArr);
              if (startArr > 0 && endArr > 0) {
                String arrContent = payload.substring(startArr+1, endArr);
                // Simple parser for "0,20,50,0..."
                int start = 0;
                for(int i=0; i<4; i++) {
                  int comma = arrContent.indexOf(",", start);
                  if(comma == -1) comma = arrContent.length();
                  rainProb[i] = arrContent.substring(start, comma).toInt();
                  start = comma + 1;
                }
                 Serial.printf("Rain Prob: %d%% %d%% %d%% %d%%\n", rainProb[0], rainProb[1], rainProb[2], rainProb[3]);
              }
            }
            }
        } else {
           Serial.printf("OpenMeteo Error: %d\n", httpCode);
        }
        https.end();
    } else {
        Serial.println("Unable to connect to OpenMeteo");
    }
  } else {
      Serial.println("No valid location yet, skipping weather.");
  }
  }
}

// --- New Helper Functions for OLED Screens ---

void drawHeader(struct tm timeinfo) {
  // Yellow Zone (Top section) - used for persistent info
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  
  // Date Left
  display.setCursor(0, 0);
  display.printf("%02d/%02d", timeinfo.tm_mday, timeinfo.tm_mon + 1);

  // Time Center
  display.setCursor(48, 0);
  display.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

  // Battery (Placeholder) - Right side
  int batLevel = 100;
  display.drawRect(92, 0, 12, 7, SSD1306_WHITE);
  display.fillRect(94, 2, (8*batLevel)/100, 3, SSD1306_WHITE);
  display.fillRect(104, 2, 2, 3, SSD1306_WHITE); // Battery Tip

  // Signal - Far Right
  long rssi = WiFi.RSSI();
  int bars = 0;
  if(rssi > -55) bars = 4;
  else if(rssi > -65) bars = 3;
  else if(rssi > -75) bars = 2;
  else if(rssi > -85) bars = 1;

  for(int b=0; b<4; b++) {
    int h = (b+1)*2; 
    // x pos: 110 start, 3px width
    if(b < bars) display.fillRect(110 + (b*3), 7 - h, 2, h, SSD1306_WHITE);
    else display.drawRect(110 + (b*3), 7 - h, 2, h, SSD1306_WHITE);
  }
  
  // Separator Line
  display.drawLine(0, 9, 128, 9, SSD1306_WHITE);
}

void drawScreenWeather(float temp, int hum) {
  // Futuristic Frame / Aesthetics
  // Simple corners to mimic HUD
  display.drawLine(0, 15, 10, 15, SSD1306_WHITE);
  display.drawLine(0, 15, 0, 25, SSD1306_WHITE);
  
  display.drawLine(118, 15, 128, 15, SSD1306_WHITE);
  display.drawLine(128, 15, 128, 25, SSD1306_WHITE);

  // Title
  display.setTextSize(1);
  display.setCursor(45, 15);
  display.print(F("CLIMA"));

  // Data
  display.setTextSize(2);
  display.setCursor(10, 32);
  display.printf("%.1fC", temp);

  display.setTextSize(1);
  display.setCursor(85, 40);
  display.printf("H:%d%%", hum);
}

void drawScreenLocation(String lat, String lon) {
  // HUD Crosshair Style
  display.drawLine(64, 20, 64, 55, SSD1306_WHITE); // Vertical
  display.drawLine(30, 38, 98, 38, SSD1306_WHITE); // Horizontal
  display.drawCircle(64, 38, 8, SSD1306_WHITE);     // Target
  
  // Title (masked box)
  display.fillRect(35, 13, 58, 10, SSD1306_BLACK); 
  display.setCursor(40, 14);
  display.print(F("UBICACION"));
  
  // Coords
  display.setCursor(2, 54);
  display.print(lat);
  
  display.setCursor(70, 54);
  display.print(lon);
}

void drawScreenRain(int probs[4]) {
  display.setCursor(15, 14);
  display.print(F("PROB. LLUVIA (4d)"));
  
  // Bar Chart for 4 days
  for(int i=0; i<4; i++) {
    int val = probs[i];
    int h = map(val, 0, 100, 0, 35); // max height 35px
    int barWidth = 15;
    int spacing = 10;
    int x = 20 + (i * (barWidth + spacing));
    int bottomY = 62;
    
    display.fillRect(x, bottomY - h, barWidth, h, SSD1306_WHITE);
    
    // Label
    display.setCursor(x, bottomY - h - 10);
    if(val > 0) display.print(val);
  }
}

void drawScreenSystemInfo(struct tm timeinfo) {
  // Full screen, NO HEADER
  
  // Large Time
  display.setTextSize(2);
  display.setCursor(16, 8);
  display.printf("%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  
  // Date
  display.setTextSize(1);
  display.setCursor(30, 28);
  display.printf("%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
  
  // WiFi Info
  display.setCursor(5, 45);
  display.print(F("WiFi: "));
  display.print(WiFi.SSID().substring(0, 12)); // Truncate if too long
  
  display.setCursor(5, 55);
  display.printf("Sig: %ld dBm  Bat: 100%%", WiFi.RSSI());
}

void drawFuturisticDashboard() {
  static int currentScreen = 0;
  static unsigned long lastScreenSwitch = 0;
  unsigned long now = millis();
  
  // Cycle Logic (5 seconds per screen)
  if (now - lastScreenSwitch > 5000) {
    currentScreen++;
    if(currentScreen > 3) currentScreen = 0;
    lastScreenSwitch = now;
  }

  display.clearDisplay();
  
  // Get Time
  struct tm timeinfo;
  bool timeSynced = getLocalTime(&timeinfo);
  if(!timeSynced){
    display.setCursor(20, 30);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.println(F("Sincronizando..."));
    display.display();
    return;
  }

  // Draw Header (except for Info Screen 3)
  if (currentScreen != 3) {
    drawHeader(timeinfo);
  }

  // Draw Main Content
  switch(currentScreen) {
    case 0: // Temperature
      if(weatherAvailable) drawScreenWeather(currentTemp, currentHumidity);
      else {
        display.setCursor(20, 30);
        display.print(F("Cargando Clima..."));
      }
      break;
    case 1: // Location
      if(currentLat != "") drawScreenLocation(currentLat, currentLon);
      else {
        display.setCursor(20, 30);
        display.print(F("Buscando Ubicacion..."));
      }
      break;
    case 2: // Rain
      if(weatherAvailable) drawScreenRain(rainProb);
      else {
        display.setCursor(20, 30);
        display.print(F("Cargando Lluvia..."));
      }
      break;
    case 3: // System Info (Full Screen)
      drawScreenSystemInfo(timeinfo);
      break;
  }

  display.display();
}

void startAP() {
  // Do the scan FIRST, while in STA mode (cleaner)
  performScan();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-Config", ""); // Open AP, no password
  Serial.println("AP Started: ESP32-Config");
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());

  // Setup DNS Server to redirect all domains to local IP
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  digitalWrite(LED_PIN, LOW); // Ensure LED is OFF in AP mode

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/rescan", handleRescan);
  // Important: Catch-all handler for captive portal redirection
  server.onNotFound(handleNotFound); 
  server.begin();
}

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

const int RESET_PIN = 4; // Reverted to Pin 4 for Factory Reset
unsigned long buttonPressStartTime = 0;
bool buttonPressed = false;

void setup() {
  // Disable Brownout Detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);

  // Inicializar OLED
  // SDA = 21, SCL = 22 (Default ESP32)
  Wire.begin(21, 22); 
  
  // Dirección 0x3C es la más común para estos módulos
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
  } else {
    Serial.println(F("OLED Init OK"));
    drawConnectingAnimation(0);
  }
  pinMode(LED_PIN, OUTPUT);
  
  pinMode(CONTROL_PIN, OUTPUT);
  // Restore state from preferences
  Preferences io_prefs;
  io_prefs.begin("io_config", true); // Read-only
  currentMode = io_prefs.getInt("led_mode", MODE_OFF); 
  io_prefs.end();
  
  // Apply initial state
  if (currentMode == MODE_STEADY) digitalWrite(CONTROL_PIN, HIGH);
  else digitalWrite(CONTROL_PIN, LOW);
  
  pinMode(RESET_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW); // Start with LED OFF

  // Explicitly reset WiFi logic to ensure clean state
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);

  setupWiFi();
}

void handleResetButton() {
  // Check for Factory Reset (Pin 4 to GND)
  if (digitalRead(RESET_PIN) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressStartTime = millis();
      Serial.println("Boton de reset detectado...");
    }
    
    // Feedback visual inmediato: Apagar LED mientras se presiona
    digitalWrite(LED_PIN, LOW); 

    // If held for 3 seconds, reset WiFi credentials
    if (millis() - buttonPressStartTime > 3000) {
      Serial.println("\nResetting WiFi Credentials...");
      
      // Blink LED rapidly to indicate reset action
      for (int i = 0; i < 10; i++) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(50);
      }
      digitalWrite(LED_PIN, LOW);

      preferences.begin("wifi", false);
      preferences.clear();
      preferences.end();
      
      // Also clear IO state
      Preferences io_prefs;
      io_prefs.begin("io_config", false);
      io_prefs.clear();
      io_prefs.end();
      
      Serial.println("Credentials cleared. Restarting...");
      delay(1000);
      ESP.restart();
    }
  } else {
    // If button is released before 3 seconds, reset state
    if (buttonPressed) {
      Serial.println("Reset cancelado (boton soltado).");
    }
    buttonPressed = false;
  }
}

void loop() {
  handleResetButton();

  // Handle web server requests if needed (AP mode or STA Dashboard)
  // In AP mode, dnsServer needs handling too
  if (WiFi.getMode() == WIFI_AP) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  
  // LED Logic: Only show status if NOT currently pressing the reset button
  if (!buttonPressed) {
    // Status LED (Pin 2 usually) tracks Connection
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(LED_PIN, HIGH);
    } else {
      digitalWrite(LED_PIN, LOW);
    }
    
    // --- New Dashboard Logic ---
    // Only update screen if in STA mode (connected)
    if (WiFi.status() == WL_CONNECTED) {
       // Weather update timer
       if (millis() - lastWeatherUpdate > WEATHER_INTERVAL || lastWeatherUpdate == 0) {
           updateWeather();
       }
       
       // Sreen Refresh Rate (every 200ms is enough for seconds to feel real)
       static unsigned long lastScreenUpdate = 0;
       if (millis() - lastScreenUpdate > 200) {
           drawFuturisticDashboard();
           lastScreenUpdate = millis();
       }
    } else if (WiFi.getMode() == WIFI_AP) {
       // Display AP Config Screen
       static unsigned long lastAPScreenUpdate = 0;
       if (millis() - lastAPScreenUpdate > 250) {
           int frame = millis() / 250; 
           drawConfigModeScreen(frame);
           lastAPScreenUpdate = millis();
       }
    }


    // -- Strobe Logic for Pin 5 --
    if (currentMode == MODE_STROBE) {
      unsigned long now = millis();
      // Double flash sequence: Flash(50) - Gap(100) - Flash(50) - LongGap(1500)
      switch (strobeStep) {
        case 0: // Idle -> First Flash
           digitalWrite(CONTROL_PIN, HIGH);
           lastStrobeTime = now;
           strobeStep = 1;
           break;
        case 1: // End First Flash
           if (now - lastStrobeTime > 50) {
             digitalWrite(CONTROL_PIN, LOW);
             lastStrobeTime = now;
             strobeStep = 2;
           }
           break;
        case 2: // End Short Gap
           if (now - lastStrobeTime > 100) {
             digitalWrite(CONTROL_PIN, HIGH);
             lastStrobeTime = now;
             strobeStep = 3;
           }
           break;
        case 3: // End Second Flash
           if (now - lastStrobeTime > 50) {
             digitalWrite(CONTROL_PIN, LOW);
             lastStrobeTime = now;
             strobeStep = 4;
           }
           break;
        case 4: // End Long Gap
           if (now - lastStrobeTime > 1500) {
             strobeStep = 0; // Restart
           }
           break;
      }
    } else if (currentMode == MODE_STEADY) {
       // Enforce HIGH just in case
       digitalWrite(CONTROL_PIN, HIGH); 
    } else {
       // MODE_OFF
       digitalWrite(CONTROL_PIN, LOW);
    }
  }
}
