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

String displayLang = "en"; // Global display language
bool useFahrenheit = false; // Global temperature unit setting

struct OLED_Dict {
  const char* connecting;
  const char* setup_req;
  const char* weather;
  const char* location;
  const char* rain;
  const char* syncing;
  const char* loading_w;
  const char* finding_l;
  const char* loading_r;
  const char* borrando;
};

OLED_Dict getOLEDDict() {
  if (displayLang == "es") return {"CONECTANDO", "SETUP REQUERIDO", "CLIMA", "UBICACION", "PROB. LLUVIA (5d)", "Sincronizando...", "Cargando Clima...", "Buscando Ubicacion...", "Cargando Lluvia...", "BORRANDO..."};
  if (displayLang == "zh") return {"连接中", "需要设置", "天气", "位置", "降雨概率 (5d)", "同步中...", "正在加载天气...", "正在寻找位置...", "正在加载降雨...", "正在擦除..."};
  if (displayLang == "pt") return {"CONECTANDO", "CONFIG NECESSÁRIA", "CLIMA", "LOCALIZAÇÃO", "PROB. CHUVA (5d)", "Sincronizando...", "Carregando Clima...", "Buscando Local...", "Carregando Chuva...", "APAGANDO..."};
  if (displayLang == "fr") return {"CONNEXION", "SORTIE D'USINE", "METEO", "LOCALISATION", "PROB. PLUE (5d)", "Synchronisation...", "Chargement météo...", "Recherche position...", "Chargement pluie...", "EFFACEMENT..."};
  return {"CONNECTING", "SETUP REQUIRED", "WEATHER", "LOCATION", "RAIN PROB. (5d)", "Syncing...", "Loading Weather...", "Finding Location...", "Loading Rain...", "ERASING..."};
}

// Function to draw a dynamic WiFi connecting animation
void drawConnectingAnimation(int frame) {
  display.clearDisplay();
  
  // --- YELLOW ZONE (Top 16px) ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Centered text
  display.setCursor(34, 4);
  display.print(getOLEDDict().connecting);
  
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

  // --- ZONA AMARILLA ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 0); 
  display.print(getOLEDDict().setup_req);

  display.setCursor(0, 10);
  display.print(F("RED: Weather-Config")); // SSID en zona amarilla

  // --- GRAFICOS (Zona Azul) ---
  // Icono Telefono
  display.drawRoundRect(10, 24, 18, 22, 2, SSD1306_WHITE);
  display.fillRect(12, 27, 14, 12, SSD1306_BLACK);
  display.drawLine(17, 42, 21, 42, SSD1306_WHITE);

  // Icono ESP32
  display.drawRect(90, 32, 24, 14, SSD1306_WHITE);
  display.drawLine(102, 32, 102, 24, SSD1306_WHITE);
  display.fillCircle(102, 23, 1, SSD1306_WHITE);
  
  // Animacion
  int step = frame % 4;
  for(int i=0; i<3; i++) {
     int x = 35 + (i*15) + (step * 3);
     if(x < 80) {
        display.drawLine(x, 34, x+4, 38, SSD1306_WHITE);
        display.drawLine(x+4, 38, x, 42, SSD1306_WHITE);
     }
  }

  // Info abajo
  display.setCursor(0, 56);
  display.print(F("weatherstation.local"));

  display.display();
}

String currentLat = "";
String currentLon = "";
int rainProb[5] = {0,0,0,0,0};


// Forward declaration
void handleNotFound();
void startAP(); 

// Helper to wrap content in a professional HTML structure
String getHTML(String title, String content, String script = "", bool showLang = true) {
  String html = "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>" + title + "</title>";
  html += "<style>";
  html += ":root { --primary: #eab308; --bg: #0f172a; --card: #1e293b; --text: #fef08a; }";
  html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background: var(--bg); color: var(--text); display: flex; align-items: center; justify-content: center; min-height: 100vh; margin: 0; padding: 20px; box-sizing: border-box; }";
  html += ".card { background: var(--card); padding: 2rem; border-radius: 1rem; box-shadow: 0 10px 15px -3px rgba(0,0,0,0.1); width: 100%; max-width: 400px; text-align: center; }";
  html += "h1 { margin-top: 0; color: var(--primary); font-size: 1.5rem; margin-bottom: 1.5rem; }";
  html += "input[type=text], input[type=password], select { width: 100%; padding: 0.75rem; margin-top: 0.5rem; margin-bottom: 1rem; border: 1px solid #d1d5db; border-radius: 0.5rem; box-sizing: border-box; font-size: 1rem; transition: all 0.2s; background: white; }";
  html += "input[type=checkbox], input[type=radio] { width: 1.25rem; height: 1.25rem; margin: 0; cursor: pointer; accent-color: var(--primary); }";
  html += "input:focus, select:focus { outline: none; border-color: var(--primary); box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.2); }";
  html += "label { font-weight: 600; font-size: 0.875rem; display: block; text-align: left; color: #4b5563; }";
  html += ".opt-label { font-weight: normal; display: flex; align-items: center; gap: 0.75rem; padding: 0.75rem 0; border-bottom: 1px solid #f3f4f6; cursor: pointer; text-align: left; }";
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
  html += "  \"en\": {\"t_cfg\": \"WiFi Config\", \"l_ssid\": \"Select Network (SSID)\", \"l_pass\": \"Password\", \"p_pass\": \"********\", \"b_save\": \"Save & Connect\", \"l_rescan\": \"Rescan (Restarts AP)\", \"t_saving\": \"Saving...\", \"msg_app\": \"Applying changes & restarting...\", \"t_saved\": \"Saved!\", \"msg_cred\": \"Credentials updated.\", \"msg_rest\": \"Device is restarting...\", \"t_err\": \"Error\", \"msg_miss\": \"Missing required fields.\", \"b_retry\": \"Try Again\", \"opt_no\": \"No networks found\", \"opt_sel\": \"Select a network...\", \"t_scan\": \"Scanning...\", \"msg_scan\": \"Restarting to scan networks. Reconnect in 10s.\", \"l_lang\": \"Language\", \"t_dash\": \"Smart Control\", \"l_led\": \"Main LED\", \"st_on\": \"ON\", \"st_off\": \"OFF\"},\n";
  html += "  \"es\": {\"t_cfg\": \"Configuración WiFi\", \"l_ssid\": \"Seleccionar Red (SSID)\", \"l_pass\": \"Contraseña\", \"p_pass\": \"********\", \"b_save\": \"Guardar y Conectar\", \"l_rescan\": \"Escanear de nuevo (Reinicia AP)\", \"t_saving\": \"Guardando...\", \"msg_app\": \"Aplicando cambios y reiniciando...\", \"t_saved\": \"¡Guardado!\", \"msg_cred\": \"Credenciales actualizadas.\", \"msg_rest\": \"El dispositivo se está reiniciando...\", \"t_err\": \"Error\", \"msg_miss\": \"Faltan campos requeridos.\", \"b_retry\": \"Intentar de nuevo\", \"opt_no\": \"No se encontraron redes\", \"opt_sel\": \"Selecciona una red...\", \"t_scan\": \"Escaneando...\", \"msg_scan\": \"Reiniciando para escanear. Reconecte en 10s.\", \"l_lang\": \"Lenguaje\", \"t_dash\": \"Control Inteligente\", \"l_led\": \"LED Principal\", \"st_on\": \"ENCENDIDO\", \"st_off\": \"APAGADO\"},\n";
  html += "  \"zh\": {\"t_cfg\": \"WiFi 配置\", \"l_ssid\": \"选择网络 (SSID)\", \"l_pass\": \"密码\", \"p_pass\": \"********\", \"b_save\": \"保存并连接\", \"l_rescan\": \"重新扫描 (重启 AP)\", \"t_saving\": \"保存中...\", \"msg_app\": \"正在应用更改并重启...\", \"t_saved\": \"已保存！\", \"msg_cred\": \"凭据已更新。\", \"msg_rest\": \"设备正在重启...\", \"t_err\": \"错误\", \"msg_miss\": \"缺少必填字段。\", \"b_retry\": \"重试\", \"opt_no\": \"未找到网络\", \"opt_sel\": \"请选择网络...\", \"t_scan\": \"扫描中...\", \"msg_scan\": \"正在重启以扫描网络。请在10秒后重新连接。\", \"l_lang\": \"语言\", \"t_dash\": \"智能控制\", \"l_led\": \"主灯\", \"st_on\": \"开启\", \"st_off\": \"关闭\"},\n";
  html += "  \"pt\": {\"t_cfg\": \"Configuração WiFi\", \"l_ssid\": \"Selecionar Rede (SSID)\", \"l_pass\": \"Senha\", \"p_pass\": \"********\", \"b_save\": \"Salvar e Conectar\", \"l_rescan\": \"Escanear novamente (Reinicia AP)\", \"t_saving\": \"Salvando...\", \"msg_app\": \"Aplicando alterações e reiniciando...\", \"t_saved\": \"Salvo!\", \"msg_cred\": \"Credenciais atualizadas.\", \"msg_rest\": \"O dispositivo está reiniciando...\", \"t_err\": \"Erro\", \"msg_miss\": \"Campos obrigatórios ausentes.\", \"b_retry\": \"Tentar novamente\", \"opt_no\": \"Nenhuma rede encontrada\", \"opt_sel\": \"Selecione uma rede...\", \"t_scan\": \"Escaneando...\", \"msg_scan\": \"Reiniciando para escanear. Reconecte in 10s.\", \"l_lang\": \"Idioma\", \"t_dash\": \"Controle Inteligente\", \"l_led\": \"LED Principal\", \"st_on\": \"LIGADO\", \"st_off\": \"DESLIGADO\"},\n";
  html += "  \"fr\": {\"t_cfg\": \"Configuration WiFi\", \"l_ssid\": \"Sélectionner Réseau (SSID)\", \"l_pass\": \"Mot de passe\", \"p_pass\": \"********\", \"b_save\": \"Enregistrer et Connecter\", \"l_rescan\": \"Scanner à nouveau (Redémarre AP)\", \"t_saving\": \"Enregistrement...\", \"msg_app\": \"Application des modifications...\", \"t_saved\": \"Enregistré !\", \"msg_cred\": \"Identifiants mis à jour.\", \"msg_rest\": \"Redémarrage de l'appareil...\", \"t_err\": \"Error\", \"msg_miss\": \"Champs requis manquants.\", \"b_retry\": \"Réessayer\", \"opt_no\": \"Aucun réseau trouvé\", \"opt_sel\": \"Sélectionnez un réseau...\", \"t_scan\": \"Scan en cours...\", \"msg_scan\": \"Redémarrage pour scanner. Reconnexion dans 10s.\", \"l_lang\": \"Langue\", \"t_dash\": \"Contrôle Intelligent\", \"l_led\": \"LED Principal\", \"st_on\": \"ALLUMÉ\", \"st_off\": \"ÉTEINT\"}\n";
  html += "};\n";
  html += "function setLang(l){";
  html += " localStorage.setItem('lang',l);";
  html += " fetch('/set_lang?lang=' + l);"; // Update ESP32 display language
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
  content += "<input type='password' id='pass' name='password' required placeholder='********' data-i18n='p_pass'>";
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

// --- Global Display Config ---
int displayMode = 0; // 0=Cycle, 1=Static
int staticScreenIndex = 0; 
int enabledScreensMask = 31; // Default 11111 (All 5 screens enabled)

void handleSetDisplayConfig() {
  if (server.hasArg("mode") && server.hasArg("mask") && server.hasArg("static")) {
      displayMode = server.arg("mode").toInt();
      enabledScreensMask = server.arg("mask").toInt();
      staticScreenIndex = server.arg("static").toInt();
      
      Preferences d_prefs;
      d_prefs.begin("disp_cfg", false);
      d_prefs.putInt("mode", displayMode);
      d_prefs.putInt("mask", enabledScreensMask);
      d_prefs.putInt("static", staticScreenIndex);
      d_prefs.end();
      
      server.send(200, "text/plain", "OK");
  } else {
      server.send(400, "text/plain", "Missing args");
  }
}


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
        server.send(200, "text/plain", String(currentMode));
    } else {
        server.send(400, "text/plain", "Invalid Mode");
    }
  } else {
    server.send(400, "text/plain", "Missing Mode");
  }
}

void handleSetLang() {
  if (server.hasArg("lang")) {
    displayLang = server.arg("lang");
    Preferences l_prefs;
    l_prefs.begin("disp_cfg", false);
    l_prefs.putString("lang", displayLang);
    l_prefs.end();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing Lang");
  }
}

void handleSetTempUnit() {
  if (server.hasArg("unit")) {
    useFahrenheit = (server.arg("unit") == "F");
    Preferences d_prefs;
    d_prefs.begin("disp_cfg", false);
    d_prefs.putBool("isF", useFahrenheit);
    d_prefs.end();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing Unit");
  }
}

// Handler for the Dashboard (STA mode)
void handleDashboard() {
  String content = "<h1 data-i18n='t_dash'>Smart Control</h1>";
  
  content += "<div style='display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid #eee; padding:10px 0;'>";
  content += "<div style='text-align:left;'><div><strong data-i18n='l_led'>Main LED</strong></div><div style='font-size:0.8rem; color:#666;' data-i18n='d_led'>Continuous</div></div>";
  String steadyState = (currentMode == MODE_STEADY) ? "checked" : "";
  content += "<label class='switch'><input type='checkbox' id='sw_steady' onchange='setMode(this.checked ? 1 : 0)' " + steadyState + "><span class='slider round'></span></label>";
  content += "</div>";

  // -- Temperature Unit --
  content += "<div style='display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid #eee; padding:10px 0;'>";
  content += "<div style='text-align:left;'><div><strong data-i18n='l_unit'>Temp Unit</strong></div><div style='font-size:0.8rem; color:#666;' data-i18n='d_unit'>Celsius / Fahrenheit</div></div>";
  content += "<div style='display:flex; background:#f1f5f9; border-radius:0.5rem; padding:2px;'>";
  content += "<button id='btn_c' onclick='setTempUnit(\"C\")' style='width:40px; margin:0; padding:4px; font-size:0.8rem; background:" + String(!useFahrenheit ? "var(--primary)" : "transparent") + "; color:" + String(!useFahrenheit ? "white" : "#64748b") + ";'>°C</button>";
  content += "<button id='btn_f' onclick='setTempUnit(\"F\")' style='width:40px; margin:0; padding:4px; font-size:0.8rem; background:" + String(useFahrenheit ? "var(--primary)" : "transparent") + "; color:" + String(useFahrenheit ? "white" : "#64748b") + ";'>°F</button>";
  content += "</div></div>";

  // -- Switch 2: Strobe Mode (Airplane) --
  content += "<div style='display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid #eee; padding:10px 0;'>";
  content += "<div style='text-align:left;'><div><strong data-i18n='l_strobe'>Airplane Mode</strong></div><div style='font-size:0.8rem; color:#666;' data-i18n='d_strobe'>Strobe Effect</div></div>";
  String strobeState = (currentMode == MODE_STROBE) ? "checked" : "";
  content += "<label class='switch'><input type='checkbox' id='sw_strobe' onchange='setMode(this.checked ? 2 : 0)' " + strobeState + "><span class='slider round'></span></label>";
  content += "</div>";

  // -- Display Config Section --
  content += "<h2 style='margin-top: 2rem; margin-bottom: 1rem; font-size: 1.25rem; color: var(--primary);' data-i18n='t_disp_cfg'>Display Settings</h2>"; 
  
  // Mode Selection (Radios)
  content += "<div style='margin-bottom: 1.5rem; display: flex; gap: 1.5rem; justify-content: center;'>";
  content += "  <label class='opt-label' style='border:none;'><input type='radio' name='d_mode' value='0' onchange='toggleDispMode()' " + String(displayMode == 0 ? "checked" : "") + "> <span data-i18n='l_cycle'>Cycle</span></label>";
  content += "  <label class='opt-label' style='border:none;'><input type='radio' name='d_mode' value='1' onchange='toggleDispMode()' " + String(displayMode == 1 ? "checked" : "") + "> <span data-i18n='l_static'>Static</span></label>";
  content += "</div>";

  // Cycle Options (Checkboxes)
  content += "<div id='cycle_opts' style='display: " + String(displayMode == 0 ? "block" : "none") + ";'>";
  content += "<p style='font-size:0.875rem; color:#6b7280; margin-bottom:0.75rem; text-align:left;' data-i18n='msg_cycle'>Select screens to cycle:</p>";
  String screens[5] = {"Clima", "Ubicacion", "Lluvia", "Fecha/Hora", "Estado"};
  for(int i=0; i<5; i++) {
     bool en = (enabledScreensMask >> i) & 1;
     content += "<label class='opt-label'><input type='checkbox' class='chk_scr' value='" + String(i) + "' " + (en ? "checked" : "") + "> <span data-i18n='s_" + String(i) + "'>" + screens[i] + "</span></label>";
  }
  content += "</div>";

  // Static Option (Select)
  content += "<div id='static_opts' style='display: " + String(displayMode == 1 ? "block" : "none") + ";'>";
  content += "<p style='font-size:0.875rem; color:#6b7280; margin-bottom:0.75rem; text-align:left;' data-i18n='msg_static'>Select screen to show:</p>";
  content += "<select id='sel_static' style='margin-top:0;'>";
  for(int i=0; i<5; i++) {
     content += "<option value='" + String(i) + "' " + (staticScreenIndex == i ? "selected" : "") + " data-i18n='s_" + String(i) + "'>" + screens[i] + "</option>";
  }
  content += "</select>";
  content += "</div>";
  
  content += "<button onclick='saveDispConfig()' data-i18n='b_save_disp' style='margin-top:1.5rem;'>Save Display Settings</button>";

  String extraDict = "Object.assign(dict.en, {'l_unit': 'Temp Unit', 'd_unit': 'Celsius / Fahrenheit', 'l_strobe': 'Airplane Mode', 't_disp_cfg': 'Display Settings', 'l_cycle': 'Cycle Mode', 'l_static': 'Static Mode', 'msg_cycle': 'Select screens to show:', 'msg_static': 'Select screen to lock:', 'b_save_disp': 'Update Display', 'd_led': 'Continuous', 'd_strobe': 'Strobe Effect', 's_0': 'Weather', 's_1': 'Location', 's_2': 'Rain', 's_3': 'Date/Time', 's_4': 'Status'});";
  extraDict += "Object.assign(dict.es, {'l_unit': 'Unidad Temp', 'd_unit': 'Celsius / Fahrenheit', 'l_strobe': 'Modo Avión', 't_disp_cfg': 'Configuración Pantalla', 'l_cycle': 'Modo Carrusel', 'l_static': 'Modo Fijo', 'msg_cycle': 'Pantallas visibles:', 'msg_static': 'Pantalla fija:', 'b_save_disp': 'Actualizar Pantalla', 'd_led': 'Continuo', 'd_strobe': 'Efecto Estroboscópico', 's_0': 'Clima', 's_1': 'Ubicación', 's_2': 'Lluvia', 's_3': 'Fecha/Hora', 's_4': 'Estado'});";
  extraDict += "Object.assign(dict.zh, {'l_unit': '温度单位', 'd_unit': '摄氏度 / 华氏度', 'l_strobe': '飞机模式', 't_disp_cfg': '显示设置', 'l_cycle': '循环模式', 'l_static': '静态模式', 'msg_cycle': '选择显示的屏幕:', 'msg_static': '选择锁定的屏幕:', 'b_save_disp': '更新显示', 'd_led': '连续', 'd_strobe': '闪烁效果', 's_0': '天气', 's_1': '位置', 's_2': '降雨', 's_3': '日期/时间', 's_4': '状态'});";
  extraDict += "Object.assign(dict.pt, {'l_unit': 'Unidade Temp', 'd_unit': 'Celsius / Fahrenheit', 'l_strobe': 'Modo Avião', 't_disp_cfg': 'Configurações de Tela', 'l_cycle': 'Modo Ciclo', 'l_static': 'Modo Estático', 'msg_cycle': 'Telas visíveis:', 'msg_static': 'Tela fija:', 'b_save_disp': 'Actualizar Tela', 'd_led': 'Contínuo', 'd_strobe': 'Efeito Estroboscópico', 's_0': 'Clima', 's_1': 'Localização', 's_2': 'Chuva', 's_3': 'Data/Hora', 's_4': 'Status'});";
  extraDict += "Object.assign(dict.fr, {'l_unit': 'Unité Temp', 'd_unit': 'Celsius / Fahrenheit', 'l_strobe': 'Mode Avion', 't_disp_cfg': 'Paramètres Écran', 'l_cycle': 'Mode Cycle', 'l_static': 'Mode Fixe', 'msg_cycle': 'Écrans visibles:', 'msg_static': 'Écran fixe:', 'b_save_disp': 'Mettre à jour', 'd_led': 'Continu', 'd_strobe': 'Effet Stroboscope', 's_0': 'Météo', 's_1': 'Localisation', 's_2': 'Pluie', 's_3': 'Date/Heure', 's_4': 'Statut'});";

  String script = extraDict + "function setMode(m) {";
  // Mutually exclusive UI logic
  script += " if(m==1) document.getElementById('sw_strobe').checked = false;";
  script += " if(m==2) document.getElementById('sw_steady').checked = false;";
  // API Call
  script += " fetch('/set_mode?mode=' + m).then(r => r.text()).then(res => { console.log('Mode set to ' + res); });";
  script += "}";

  script += "function setTempUnit(u) {";
  script += " const isF = (u=='F');";
  script += " document.getElementById('btn_f').style.background = isF ? 'var(--primary)' : 'transparent';";
  script += " document.getElementById('btn_f').style.color = isF ? 'white' : '#64748b';";
  script += " document.getElementById('btn_c').style.background = !isF ? 'var(--primary)' : 'transparent';";
  script += " document.getElementById('btn_c').style.color = !isF ? 'white' : '#64748b';";
  script += " fetch('/set_temp_unit?unit=' + u);";
  script += "}";
  
  script += "function toggleDispMode() {";
  script += " const m = document.querySelector('input[name=\"d_mode\"]:checked').value;";
  script += " document.getElementById('cycle_opts').style.display = (m=='0' ? 'block' : 'none');";
  script += " document.getElementById('static_opts').style.display = (m=='1' ? 'block' : 'none');";
  script += "}";

  script += "function saveDispConfig() {";
  script += " const m = document.querySelector('input[name=\"d_mode\"]:checked').value;";
  script += " const s = document.getElementById('sel_static').value;";
  script += " let mask = 0;";
  script += " document.querySelectorAll('.chk_scr:checked').forEach(c => { mask |= (1 << c.value); });";
  script += " const btn = document.querySelector('button[onclick=\"saveDispConfig()\"]'); const old = btn.innerHTML; btn.innerHTML='Saving...';";
  script += " fetch('/set_display?mode='+m+'&mask='+mask+'&static='+s).then(r=>{ btn.innerHTML='Saved!'; setTimeout(()=>{btn.innerHTML=old}, 2000); });";
  script += "}";
  
  server.send(200, "text/html", getHTML("Smart Dashboard", content, script));
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
    if (MDNS.begin("weatherstation")) {
      Serial.println("MDNS responder started. Access via http://weatherstation.local");
    }

    digitalWrite(LED_PIN, HIGH);
    
    // Start Web Server for Dashboard in STA mode
    server.on("/", handleDashboard);
    server.on("/set_mode", handleSetMode);
    server.on("/set_display", handleSetDisplayConfig);
    server.on("/set_lang", handleSetLang);
    server.on("/set_temp_unit", handleSetTempUnit);
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
                for(int i=0; i<5; i++) {
                  int comma = arrContent.indexOf(",", start);
                  if(comma == -1) comma = arrContent.length();
                  rainProb[i] = arrContent.substring(start, comma).toInt();
                  start = comma + 1;
                }
                 Serial.printf("Rain Prob: %d%% %d%% %d%% %d%% %d%%\n", rainProb[0], rainProb[1], rainProb[2], rainProb[3], rainProb[4]);
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
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
}

void drawScreenWeather(float temp, int hum) {
  // Conversión a Fahrenheit si es necesario
  float displayTemp = temp;
  const char* unitStr = "C";
  if (useFahrenheit) {
    displayTemp = (temp * 9.0 / 5.0) + 32.0;
    unitStr = "F";
  }

  // Title
  display.setTextSize(1);
  display.setCursor(48, 16);
  display.print(getOLEDDict().weather);

  // Thermometer position
  int tx = 13;
  int ty = 48;

  // Data Temperatura
  display.setTextSize(2);
  display.setCursor(30, 32);
  display.printf("%.1f%s", displayTemp, unitStr);

  // Data Humedad
  display.setTextSize(1);
  display.setCursor(30, 52);
  display.printf("Hum: %d%%", hum);

  // Icono Termómetro detallado
  display.fillCircle(tx, ty, 4, SSD1306_WHITE);
  
  // Stem (rounded top)
  display.drawRoundRect(tx - 2, ty - 25, 5, 23, 2, SSD1306_WHITE);
  
  // Connect stem and bulb visually
  display.fillRect(tx - 1, ty - 5, 3, 3, SSD1306_WHITE);
  
  // Mercury level inside stem (Mapping 0-50 C to 0-18 pixels height)
  int level = map(constrain((int)temp, 0, 50), 0, 50, 0, 18);
  display.fillRect(tx - 1, ty - level - 4, 3, level + 1, SSD1306_WHITE);
  
  // Temperature Ticks on the stem
  for(int i=0; i<3; i++) {
    int yTick = ty - 12 - (i * 6);
    display.drawLine(tx + 4, yTick, tx + 6, yTick, SSD1306_WHITE);
  }
}

void drawScreenLocation(String lat, String lon) {
  // HUD Crosshair Style
  display.drawLine(64, 20, 64, 55, SSD1306_WHITE); // Vertical
  display.drawLine(30, 38, 98, 38, SSD1306_WHITE); // Horizontal
  display.drawCircle(64, 38, 8, SSD1306_WHITE);     // Target
  
  // Title (masked box)
  display.fillRect(35, 17, 58, 10, SSD1306_BLACK); 
  display.setCursor(40, 18);
  display.print(getOLEDDict().location);
  
  // Coords
  display.setCursor(2, 54);
  display.print(lat);
  
  display.setCursor(70, 54);
  display.print(lon);
}

void drawScreenRain(int probs[5]) {
  display.setCursor(15, 18);
  display.print(getOLEDDict().rain);
  
  // Bar Chart for 5 days
  for(int i=0; i<5; i++) {
    int val = probs[i];
    int h = map(val, 0, 100, 0, 35); // max height 35px
    int barWidth = 12; // Narrower for 5 bars
    int spacing = 8;
    int x = 18 + (i * (barWidth + spacing));
    int bottomY = 62;
    
    display.fillRect(x, bottomY - h, barWidth, h, SSD1306_WHITE);
    
    // Label (Only show >0 to avoid clutter)
    if(val > 0) {
      display.setCursor(x, bottomY - h - 10);
      display.print(val);
    }
  }
}

void drawScreenTime(struct tm timeinfo) {
  // NO HEADER - Full Screen control
  // Date in Yellow Zone (Top 16px)
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(34, 4); // Centered
  display.printf("%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);

  // --- Analog Clock in Blue Zone (y > 16) ---
  int cx = 32;
  int cy = 42; 
  int r = 18;

  // Clock Frame
  display.drawCircle(cx, cy, r, SSD1306_WHITE);
  display.fillCircle(cx, cy, 2, SSD1306_WHITE); // Center hub

  // Ticks (12, 3, 6, 9)
  display.drawPixel(cx, cy-r, SSD1306_WHITE); 
  display.drawPixel(cx+r, cy, SSD1306_WHITE); 
  display.drawPixel(cx, cy+r, SSD1306_WHITE); 
  display.drawPixel(cx-r, cy, SSD1306_WHITE); 

  // Calculate Angles (Adjusting so 0 is 12 o'clock)
  // 30 degrees per hour, 6 degrees per minute
  float hAngle = ((timeinfo.tm_hour % 12) + timeinfo.tm_min / 60.0) * 30.0; 
  hAngle = (hAngle - 90) * 0.0174532925; // Convert degrees to radians

  float mAngle = (timeinfo.tm_min) * 6.0; 
  mAngle = (mAngle - 90) * 0.0174532925;

  // Draw Hands
  // Hour hand (shorter)
  display.drawLine(cx, cy, cx + cos(hAngle) * (r * 0.6), cy + sin(hAngle) * (r * 0.6), SSD1306_WHITE);
  
  // Minute hand (longer)
  display.drawLine(cx, cy, cx + cos(mAngle) * (r * 0.9), cy + sin(mAngle) * (r * 0.9), SSD1306_WHITE);

  // Digital Time on the Right
  display.setTextSize(2);
  display.setCursor(64, 34); 
  display.printf("%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
}

void drawScreenStatus() {
  // WITH HEADER - Start content at y=17
  
  // WiFi Info
  display.setTextSize(1);
  display.setCursor(0, 18);
  display.print(F("RED: "));
  String s = WiFi.SSID();
  if(s.length() > 10) s = s.substring(0, 10) + "..";
  display.print(s);

  // Signal Section
  display.setCursor(0, 35);
  display.print(F("WIFI:"));
  
  long rssi = WiFi.RSSI();
  int bars = 0;
  if(rssi > -60) bars = 5;
  else if(rssi > -70) bars = 4;
  else if(rssi > -80) bars = 3;
  else if(rssi > -90) bars = 2;
  else bars = 1;

  for(int i=0; i<5; i++) {
     int h = 4 + (i*2); 
     int x = 35 + (i*5);
     int y = 45; // Bottom aligned-ish
     if(i < bars) display.fillRect(x, y - h, 3, h, SSD1306_WHITE);
     else display.drawRect(x, y - h, 3, h, SSD1306_WHITE);
  }
  
  // Battery Section
  display.setCursor(70, 35);
  display.print(F("BAT:"));
  // Battery Icon
  display.drawRect(95, 34, 20, 10, SSD1306_WHITE);
  display.fillRect(97, 36, 16, 6, SSD1306_WHITE); // 100% fixed
  display.fillRect(115, 36, 2, 6, SSD1306_WHITE); // Tip

  // URL de conexion al portal
  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print(F("weatherstation.local"));
}

void drawFuturisticDashboard() {
  static int currentScreen = 0;
  static unsigned long lastScreenSwitch = 0;
  unsigned long now = millis();
  
  if (displayMode == 1) { // Static Mode
     currentScreen = staticScreenIndex;
  } else { // Cycle Mode
      // Cycle Logic (5 seconds per screen)
      if (now - lastScreenSwitch > 5000) {
        int nextScreen = currentScreen;
        for(int i=0; i<5; i++) { // Try up to 5 times (all screens)
            nextScreen++;
            if(nextScreen > 4) nextScreen = 0;
            
            // Check bitmask: (mask >> screen_idx) & 1
            if ((enabledScreensMask >> nextScreen) & 1) {
                currentScreen = nextScreen;
                break;
            }
        }
        lastScreenSwitch = now;
      }
  }

  display.clearDisplay();
  
  // Get Time
  struct tm timeinfo;
  bool timeSynced = getLocalTime(&timeinfo);
  if(!timeSynced){
    display.setCursor(20, 30);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.println(getOLEDDict().syncing);
    display.display();
    return;
  }

  // Draw Header (For all except Time screen which is #3)
  if (currentScreen != 3) {
    drawHeader(timeinfo);
  }

  // Draw Main Content
  switch(currentScreen) {
    case 0: // Temperature
      if(weatherAvailable) drawScreenWeather(currentTemp, currentHumidity);
      else {
        display.setCursor(20, 30);
        display.print(getOLEDDict().loading_w);
      }
      break;
    case 1: // Location
      if(currentLat != "") drawScreenLocation(currentLat, currentLon);
      else {
        display.setCursor(20, 30);
        display.print(getOLEDDict().finding_l);
      }
      break;
    case 2: // Rain
      if(weatherAvailable) drawScreenRain(rainProb);
      else {
        display.setCursor(20, 30);
        display.print(getOLEDDict().loading_r);
      }
      break;
    case 3: // Time & Date (Custom Layout)
      drawScreenTime(timeinfo);
      break;
    case 4: // Status (Battery/Signal)
      drawScreenStatus();
      break;
  }

  display.display();
}

void startAP() {
  // Do the scan FIRST, while in STA mode (cleaner)
  performScan();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Weather-Station-Config", ""); // Open AP, no password
  Serial.println("AP Started: Weather-Station-Config");
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
  
  if(MDNS.begin("weatherstation")) {
    Serial.println("MDNS responder started");
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Error setting up MDNS responder!");
  }

  // Setup DNS Server to redirect all domains to local IP
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  digitalWrite(LED_PIN, LOW); // Ensure LED is OFF in AP mode

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/rescan", handleRescan);
  server.on("/set_lang", handleSetLang);
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

  // Load Display Config
  Preferences d_prefs;
  d_prefs.begin("disp_cfg", true);
  displayMode = d_prefs.getInt("mode", 0);
  enabledScreensMask = d_prefs.getInt("mask", 31);
  staticScreenIndex = d_prefs.getInt("static", 0);
  displayLang = d_prefs.getString("lang", "en");
  useFahrenheit = d_prefs.getBool("isF", false);
  d_prefs.end();
  
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

// Function to draw reset countdown
// Function to draw reset countdown
void drawResetCountdown(int secondsLeft, unsigned long progress) {
  display.clearDisplay();
  
  // Background stays black (default after clearDisplay)
  display.setTextColor(SSD1306_WHITE);
  
  // Borde interno
  display.drawRoundRect(2, 2, 124, 60, 4, SSD1306_WHITE);
  
  // Título
  display.setTextSize(2); 
  display.setCursor(34, 8); 
  display.print(F("RESET"));

  // Número de cuenta regresiva
  display.setTextSize(2);
  display.setCursor(40, 28);
  display.printf("en %ds", secondsLeft);

  // Barra de progreso
  int barWidth = 100;
  int barHeight = 8;
  int barX = 14;
  int barY = 48;
  
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  
  int fillW = map(progress, 0, 5000, 0, barWidth-4);
  if(fillW < 0) fillW = 0;
  if(fillW > barWidth-4) fillW = barWidth-4;
  
  display.fillRect(barX+2, barY+2, fillW, barHeight-4, SSD1306_WHITE);

  display.display();
}

void handleResetButton() {
  const unsigned long RESET_TIME_MS = 5000;

  // Check for Factory Reset (Pin 4 to GND)
  if (digitalRead(RESET_PIN) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      buttonPressStartTime = millis();
      Serial.println("Boton de reset detectado...");
    }
    
    unsigned long elapsed = millis() - buttonPressStartTime;
    
    // Feedback visual inmediato: Apagar LED mientras se presiona
    digitalWrite(LED_PIN, LOW); 
    
    // Show countdown on screen
    int secondsLeft = (RESET_TIME_MS - elapsed + 999) / 1000;
    if(secondsLeft < 0) secondsLeft = 0;
    
    // Only draw if we haven't triggered yet
    if (elapsed <= RESET_TIME_MS) {
       drawResetCountdown(secondsLeft, elapsed);
    }

    // If held for 5 seconds, reset WiFi credentials
    if (elapsed > RESET_TIME_MS) {
      Serial.println("\nResetting WiFi Credentials...");
      
      // Update screen to say "RESETTING..."
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(2);
      display.setCursor(10, 25);
      display.print(getOLEDDict().borrando);
      display.display();
      
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
    /* If button is released before 5 seconds, reset state */
    // Also clear the display to remove the "RESET en X" message immediately
    if (buttonPressed) {
      Serial.println("Reset cancelado (boton soltado).");      
      buttonPressed = false; 
      // Force screen clear so we don't see the countdown stuck
      display.clearDisplay();
      display.display();
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
