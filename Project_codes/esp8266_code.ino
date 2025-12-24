#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

// --- USER CONFIGURATION ---
const char* ssid = "Aryan";
const char* password = "12376543";
const char* server_ip = "10.232.248.253"; // YOUR LAPTOP IP
const int server_port = 5000;

// --- RFID PINS (ESP8266 NodeMCU) ---
// SDA(SS)=D4, SCK=D5, MOSI=D7, MISO=D6, RST=D3
#define SS_PIN  D4 
#define RST_PIN D3

// --- LCD CONFIGURATION ---
LiquidCrystal_I2C lcd(0x27, 16, 2); 

MFRC522 rfid(SS_PIN, RST_PIN);
float currentBill = 0.0;
float lastBill = -1.0; 

// Timer variables for non-blocking delay
unsigned long lastServerCheck = 0;
const unsigned long serverInterval = 2000; // Check server every 2 seconds

void setup() {
  Serial.begin(115200);
  
  // 1. Init LCD
  Wire.begin(D2, D1); // SDA=D2, SCL=D1
  lcd.init();
  lcd.backlight();
  
  lcd.setCursor(0, 0);
  lcd.print("System Starting");

  // 2. Init RFID
  SPI.begin();      
  rfid.PCD_Init();  
  
  // --- DEBUG: CHECK RFID CONNECTION ---
  Serial.print("\nChecking RFID Reader...");
  rfid.PCD_DumpVersionToSerial(); // This prints the firmware version
  // If it says 0x00 or 0xFF, check your wiring!

  WiFi.begin(ssid, password);
  Serial.println("\nConnecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  
  lcd.clear();
  lcd.print("WiFi Connected");
  delay(1000);
}

void loop() {
  // --- PART 1: FAST RFID CHECK (Run every loop) ---
  // We check this constantly so we don't miss a tap
  if (currentBill > 0) {
    // If a new card is present AND we can read it
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      Serial.println("\n>>> CARD DETECTED! Processing...");
      processPayment();
      
      // Stop reading this card (prevents double payment)
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }
  }

  // --- PART 2: SLOW SERVER CHECK (Run every 2 seconds) ---
  // We use millis() instead of delay() so we don't block the RFID reader
  if (millis() - lastServerCheck >= serverInterval) {
    lastServerCheck = millis();
    checkServerForBill();
  }
}

void checkServerForBill() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    
    String url = "http://" + String(server_ip) + ":" + String(server_port) + "/get-bill";
    http.begin(client, url);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);
      currentBill = doc["total_bill"];
    }
    http.end();
  }

  // Only update screen if bill changed
  if (currentBill != lastBill) {
    lcd.clear();
    if (currentBill > 0) {
      // Print to Serial
      Serial.println("\n------------------------------");
      Serial.print(" Pending Bill: ");
      Serial.print(currentBill);
      Serial.println(" Rs");
      Serial.println(" [SCAN CARD TO PAY]");
      Serial.println("------------------------------");
      
      // Print to LCD
      lcd.setCursor(0, 0);
      lcd.print("Bill: ");
      lcd.print(currentBill);
      lcd.print(" Rs");
      lcd.setCursor(0, 1);
      lcd.print("Tap Card to Pay");
    } else {
      Serial.println("\n[System Idle] No Pending Bill.");
      
      lcd.setCursor(0, 0);
      lcd.print("  Payment Desk");
      lcd.setCursor(0, 1);
      lcd.print("  System Idle");
    }
    lastBill = currentBill;
  }
}

void processPayment() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Processing...");
  lcd.setCursor(0, 1); lcd.print("Please Wait");

  // Get Card ID
  String tagID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    tagID += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    tagID += String(rfid.uid.uidByte[i], HEX);
  }
  tagID.toUpperCase();
  Serial.print("Card ID: "); Serial.println(tagID);

  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    
    String url = "http://" + String(server_ip) + ":" + String(server_port) + "/process-payment";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    JsonDocument doc;
    doc["amount_paid"] = currentBill;
    doc["rfid_tag"] = tagID;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpCode = http.POST(jsonString);
    
    if (httpCode == 200) {
      Serial.println("PAYMENT SUCCESSFUL!");
      
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Payment Success!");
      lcd.setCursor(0, 1); lcd.print("Paid: "); lcd.print(currentBill);
      
      currentBill = 0.0; 
      lastBill = -1.0; // Force update next cycle
      delay(3000); 
      
    } else {
      Serial.print("Failed: "); Serial.println(httpCode);
      
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Payment Failed");
      lcd.setCursor(0, 1); lcd.print("Try Again");
      
      delay(2000);
      lastBill = -1.0; 
    }
    http.end();
  }
}