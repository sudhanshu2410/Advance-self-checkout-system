#include "esp_camera.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include "HX711.h"

// --- USER CONFIGURATION ---
const char* ssid = "Aryan";
const char* password = "12376543";

// UPDATE THIS WITH YOUR LAPTOP IP (Check Server Terminal)
const char* server_ip = "10.232.248.253"; 
const int server_port = 5000;

// --- HARDWARE PINS ---
#define BUTTON_PIN 3
#define FLASH_LED_PIN 4 
#define I2C_SDA 15
#define I2C_SCL 14
#define LOADCELL_DOUT_PIN 13
#define LOADCELL_SCK_PIN 12

// --- OBJECTS ---
HX711 scale;
#define CALIBRATION_FACTOR 451.00f 
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// Global variable to hold the Total Bill (received from server)
float lastKnownTotal = 0.0; 

// --- CAMERA PINS (AI THINKER MODEL) ---
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void setup() {
  Serial.begin(115200);
  
  // 1. Init Pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW); 

  // 2. Init LCD
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init(); 
  lcd.backlight();
  
  // 3. Init Load Cell
  lcd.setCursor(0, 0); lcd.print("Init Scale...");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);
  scale.tare(); 

  // 4. Connect WiFi
  lcd.clear(); lcd.print("Connecting WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  
  // 5. Init Camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA; // 640x480
  config.jpeg_quality = 12;
  config.fb_count = 1;
  esp_camera_init(&config);
  
  // Ready State
  lcd.clear();
  lcd.print("System Ready");
  delay(1000);
  updateIdleScreen();
}

void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BUTTON_PIN) == HIGH) return; 

    // --- STEP 1: CAPTURE ---
    lcd.clear(); lcd.print("Capturing...");
    digitalWrite(FLASH_LED_PIN, HIGH); delay(250); 
    
    // Flush Buffer (Dummy read)
    camera_fb_t * fb = esp_camera_fb_get(); 
    esp_camera_fb_return(fb); 
    delay(50); 
    
    // Real Capture
    fb = esp_camera_fb_get();
    digitalWrite(FLASH_LED_PIN, LOW);

    if (!fb) { lcd.print("Cam Fail"); delay(2000); return; }
    
    // --- STEP 2: GET WEIGHT ---
    float weight = 0.0;
    if (scale.is_ready()) weight = scale.get_units(5); 
    if (weight < 1.0) weight = 0.0;

    // --- STEP 3: SEND TO SERVER (SINGLE CALL) ---
    lcd.setCursor(0, 1); lcd.print("Processing...");
    processTransaction(fb, weight);
    
    esp_camera_fb_return(fb);
    
    // Wait for button release
    while (digitalRead(BUTTON_PIN) == LOW) { delay(10); }
    
    // Show Result for 2 seconds then go to Idle
    delay(2000); 
    updateIdleScreen();
  }
}

void updateIdleScreen() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Total Bill:");
  lcd.setCursor(0, 1); lcd.print(lastKnownTotal, 2); lcd.print(" Rs");
}

void processTransaction(camera_fb_t * fb, float weight) {
  HTTPClient http;
  
  // We use the new merged endpoint
  String url = "http://" + String(server_ip) + ":" + String(server_port) + "/process-item";
  
  http.setTimeout(10000); 
  http.begin(url);
  
  // Send Image in Body, Weight in Header
  http.addHeader("Content-Type", "image/jpeg");
  http.addHeader("Weight", String(weight)); 
  
  int httpCode = http.POST(fb->buf, fb->len);

  if (httpCode == 200) {
    String payload = http.getString();
    
    // Parse Response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      String item = doc["item"];
      float itemCost = doc["item_cost"];
      lastKnownTotal = doc["grand_total"]; // Update local total from Server

      lcd.clear();
      
      if (item == "Unknown" || item == "None") {
        lcd.print("No Item Found");
      } else {
        lcd.print(item); // e.g. "onion"
        lcd.setCursor(0, 1); 
        lcd.print("Cost: "); lcd.print(itemCost, 1); lcd.print(" Rs");
      }
    } else {
      lcd.clear(); lcd.print("JSON Error");
    }
  } else {
    lcd.clear(); lcd.print("Server Error");
    Serial.println(httpCode);
  }
  http.end();
}