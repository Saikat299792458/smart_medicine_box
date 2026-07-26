#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HTTPClient.h>

// Pin Assignments
#define OLED_SDA 4
#define OLED_SCL 5
#define BUTTON_PIN 6

// OLED Display Settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
TwoWire I2C_OLED = TwoWire(0);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2C_OLED, OLED_RESET);

// Button Settings
#define DEBOUNCE_DELAY 200

// API Settings - Match your device_id from JSON (12345)
const char* apiUrl = "https://bivob.lovable.app/api/public/device-medicines?device_id=12345";

// Data Structures
struct Alarm {
  String alarm_time;
  String repeat_type;
  bool enabled;
};

struct Medicine {
  String id;
  String medicine_name;
  String generic_name;
  String dosage;
  bool morning;
  bool noon;
  bool evening;
  bool night;
  String meal_type;
  String notes;
  int num_days;
  String effective_till;
  Alarm alarms[10];
  int alarmCount;
};

struct Patient {
  String id;
  String patient_name;
  int age;
  String gender;
  String mobile;
  String relationship;
  String address;
  Medicine medicines[20];
  int medicineCount;
};

// Global variables
Patient patients[10];
int patientCount = 0;
int currentPatientIndex = 0;
int currentMedicineIndex = 0;
unsigned long lastButtonPress = 0;
bool dataLoaded = false;

// Function prototypes
void fetchDataFromAPI();
void parseJSONData(String jsonString);
void displayMedicineInfo();
void displayPatientInfo();
void nextMedicine();
void setupDisplay();
void setupButton();
void showMessage(String line1, String line2, String line3, int delayMs);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  setupDisplay();
  setupButton();
  
  showMessage("Starting...", "Bivob MediBox", "", 1000);
  showMessage("WiFi Setup", "Connect to AP", "Bivob Medicine Box", 2000);
  
  // WiFi Manager
  WiFiManager wm;
  wm.setDebugOutput(true);
  
  bool connected = wm.autoConnect("Bivob Medicine Box", "12345678");
  
  if (!connected) {
    Serial.println("Failed to connect. Restarting...");
    showMessage("WiFi Failed!", "Restarting...", "", 3000);
    ESP.restart();
  }
  
  Serial.println("Connected to WiFi!");
  Serial.println(WiFi.localIP());
  
  showMessage("WiFi Connected!", WiFi.localIP().toString(), "Fetching Data...", 2000);
  
  // Fetch data from API
  fetchDataFromAPI();
  
  if (dataLoaded && patientCount > 0) {
    showMessage("Data Loaded!", String(patientCount) + " Patients Found", "", 1500);
    displayPatientInfo();
    delay(1500);
    displayMedicineInfo();
  } else {
    showMessage("No Data Found!", "Check Connection", "Will Retry in 10s", 10000);
    ESP.restart(); // Restart to try again
  }
}

void loop() {
  if (dataLoaded && patientCount > 0) {
    if (digitalRead(BUTTON_PIN) == LOW && (millis() - lastButtonPress) > DEBOUNCE_DELAY) {
      lastButtonPress = millis();
      nextMedicine();
    }
  }
  delay(50);
}

void setupDisplay() {
  I2C_OLED.begin(OLED_SDA, OLED_SCL, 400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  display.display();
}

void setupButton() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
}

void showMessage(String line1, String line2, String line3, int delayMs) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println(line1);
  display.setCursor(0, 25);
  display.println(line2);
  display.setCursor(0, 40);
  display.println(line3);
  display.display();
  delay(delayMs);
}

void fetchDataFromAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    showMessage("WiFi Error", "Not Connected", "", 2000);
    return;
  }
  
  Serial.println("Fetching from API: ");
  Serial.println(apiUrl);
  
  showMessage("Fetching Data", "Please wait...", "", 500);
  
  HTTPClient http;
  http.begin(apiUrl);
  http.setTimeout(10000); // 10 second timeout
  
  int httpCode = http.GET();
  Serial.printf("HTTP Response Code: %d\n", httpCode);
  
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("API Response (first 500 chars):");
      Serial.println(payload.substring(0, 500));
      
      // Check if response is valid JSON
      if (payload.length() > 0) {
        parseJSONData(payload);
      } else {
        Serial.println("Empty response from API");
        showMessage("API Error", "Empty Response", "", 2000);
      }
    } else {
      Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
      showMessage("API Error", "HTTP: " + String(httpCode), "", 2000);
    }
  } else {
    Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
    showMessage("Connection Error", http.errorToString(httpCode), "", 2000);
  }
  
  http.end();
}

void parseJSONData(String jsonString) {
  Serial.println("Parsing JSON...");
  
  // First, check if it's a valid JSON
  if (jsonString.length() == 0) {
    Serial.println("Empty JSON string");
    dataLoaded = false;
    return;
  }
  
  // Try to find if the response has a 'patients' key
  if (jsonString.indexOf("\"patients\"") == -1) {
    Serial.println("No 'patients' key found in response");
    Serial.println("Full response:");
    Serial.println(jsonString);
    dataLoaded = false;
    return;
  }
  
  DynamicJsonDocument doc(32768); // Increased buffer size to 32KB
  DeserializationError error = deserializeJson(doc, jsonString);
  
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    dataLoaded = false;
    return;
  }
  
  // Check if 'patients' array exists
  if (!doc.containsKey("patients")) {
    Serial.println("JSON does not contain 'patients' array");
    dataLoaded = false;
    return;
  }
  
  JsonArray patientsArray = doc["patients"];
  patientCount = patientsArray.size();
  
  Serial.printf("Found %d patients\n", patientCount);
  
  if (patientCount == 0) {
    Serial.println("No patients in array");
    dataLoaded = false;
    return;
  }
  
  if (patientCount > 10) patientCount = 10;
  
  for (int i = 0; i < patientCount; i++) {
    JsonObject patientJson = patientsArray[i];
    
    // Extract patient info with null checks
    patients[i].id = patientJson["id"] | "";
    patients[i].patient_name = patientJson["patient_name"] | "Unknown";
    patients[i].age = patientJson["age"] | 0;
    patients[i].gender = patientJson["gender"] | "";
    patients[i].mobile = patientJson["mobile"] | "";
    patients[i].relationship = patientJson["relationship"] | "";
    patients[i].address = patientJson["address"] | "";
    
    Serial.printf("Patient %d: %s (Age: %d)\n", i+1, patients[i].patient_name.c_str(), patients[i].age);
    
    // Parse medicines array
    JsonArray medicinesArray = patientJson["medicines"];
    patients[i].medicineCount = medicinesArray.size();
    
    Serial.printf("  Has %d medicines\n", patients[i].medicineCount);
    
    if (patients[i].medicineCount > 20) patients[i].medicineCount = 20;
    
    for (int j = 0; j < patients[i].medicineCount; j++) {
      JsonObject medicineJson = medicinesArray[j];
      
      patients[i].medicines[j].id = medicineJson["id"] | "";
      patients[i].medicines[j].medicine_name = medicineJson["medicine_name"] | "Unknown Medicine";
      patients[i].medicines[j].generic_name = medicineJson["generic_name"] | "";
      patients[i].medicines[j].dosage = medicineJson["dosage"] | "";
      patients[i].medicines[j].morning = medicineJson["morning"] | false;
      patients[i].medicines[j].noon = medicineJson["noon"] | false;
      patients[i].medicines[j].evening = medicineJson["evening"] | false;
      patients[i].medicines[j].night = medicineJson["night"] | false;
      patients[i].medicines[j].meal_type = medicineJson["meal_type"] | "";
      patients[i].medicines[j].notes = medicineJson["notes"] | "";
      patients[i].medicines[j].num_days = medicineJson["num_days"] | 0;
      patients[i].medicines[j].effective_till = medicineJson["effective_till"] | "";
      
      Serial.printf("    Medicine %d: %s (%s)\n", j+1, 
                    patients[i].medicines[j].medicine_name.c_str(),
                    patients[i].medicines[j].dosage.c_str());
      
      // Parse alarms
      JsonArray alarmsArray = medicineJson["alarms"];
      patients[i].medicines[j].alarmCount = alarmsArray.size();
      
      if (patients[i].medicines[j].alarmCount > 10) 
        patients[i].medicines[j].alarmCount = 10;
      
      for (int k = 0; k < patients[i].medicines[j].alarmCount; k++) {
        JsonObject alarmJson = alarmsArray[k];
        patients[i].medicines[j].alarms[k].alarm_time = alarmJson["alarm_time"] | "";
        patients[i].medicines[j].alarms[k].repeat_type = alarmJson["repeat_type"] | "";
        patients[i].medicines[j].alarms[k].enabled = alarmJson["enabled"] | false;
        Serial.printf("      Alarm %d: %s\n", k+1, patients[i].medicines[j].alarms[k].alarm_time.c_str());
      }
    }
  }
  
  dataLoaded = true;
  Serial.println("JSON parsing completed successfully!");
}

void displayMedicineInfo() {
  if (!dataLoaded || patientCount == 0) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Display patient name (first line)
  display.setCursor(0, 0);
  display.print("Patient: ");
  display.println(patients[currentPatientIndex].patient_name);
  
  // Draw a line
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  // Display medicine name and dosage
  Medicine& med = patients[currentPatientIndex].medicines[currentMedicineIndex];
  
  display.setCursor(0, 16);
  display.print(med.medicine_name);
  
  display.setCursor(0, 28);
  display.print("Dosage: ");
  display.println(med.dosage);
  
  // Display meal type if available
  if (med.meal_type.length() > 0 && med.meal_type != "null") {
    display.setCursor(0, 40);
    display.print(med.meal_type);
  }
  
  // Display schedule (morning/noon/evening/night)
  display.setCursor(0, 52);
  display.print("Sch: ");
  if (med.morning) display.print("M ");
  if (med.noon) display.print("N ");
  if (med.evening) display.print("E ");
  if (med.night) display.print("Ngt");
  
  // Show medicine number at bottom right
  char progress[10];
  sprintf(progress, "%d/%d", currentMedicineIndex + 1, patients[currentPatientIndex].medicineCount);
  display.setCursor(SCREEN_WIDTH - 30, SCREEN_HEIGHT - 8);
  display.print(progress);
  
  display.display();
  
  // Serial output for debugging
  Serial.println("=== Medicine Display ===");
  Serial.print("Patient: ");
  Serial.println(patients[currentPatientIndex].patient_name);
  Serial.print("Medicine: ");
  Serial.print(med.medicine_name);
  Serial.print(" - ");
  Serial.println(med.dosage);
}

void displayPatientInfo() {
  if (!dataLoaded || patientCount == 0) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.println("=== PATIENT INFO ===");
  
  display.setCursor(0, 12);
  display.print("Name: ");
  display.println(patients[currentPatientIndex].patient_name);
  
  display.setCursor(0, 24);
  display.print("Age: ");
  display.print(String(patients[currentPatientIndex].age));
  display.print("  ");
  if (patients[currentPatientIndex].gender.length() > 0) {
    display.println(patients[currentPatientIndex].gender);
  } else {
    display.println("");
  }
  
  display.setCursor(0, 38);
  display.print("Medicines: ");
  display.println(String(patients[currentPatientIndex].medicineCount));
  
  display.setCursor(0, 52);
  display.print("Press button");
  
  display.display();
}

void nextMedicine() {
  currentMedicineIndex++;
  
  // Check if we've gone through all medicines for current patient
  if (currentMedicineIndex >= patients[currentPatientIndex].medicineCount) {
    currentMedicineIndex = 0;
    currentPatientIndex++;
    
    // Check if we've gone through all patients
    if (currentPatientIndex >= patientCount) {
      currentPatientIndex = 0; // Loop back to first patient
    }
    
    // Show patient info when switching to new patient
    displayPatientInfo();
    delay(1500);
  }
  
  // Display the next medicine
  displayMedicineInfo();
}