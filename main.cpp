#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h> 

#define modePin 4 

const char* ssid = "Sophias Gallery";
const char* password = ""; 
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

const int numSlots = 10;
int currentSlot = 0;

DNSServer dnsServer;
AsyncWebServer server(80);
Preferences prefs; 

String slotPath(int slot){
  return "/img"+String(slot)+".jpg"; 
}

void setupRoutes();

void savePointer(){
  prefs.begin("gallery", false);
  prefs.putInt("currentSlot", currentSlot);
  prefs.end();
}

void loadPointer(){
  prefs.begin("gallery", true);
  currentSlot = prefs.getInt("currentSlot", 0);
  prefs.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(modePin, INPUT_PULLUP);

  if(!LittleFS.begin()){
    Serial.println("An error has occurred while mounting LittleFS");
    return;
  }
  
  loadPointer();
  Serial.println("Current slot loaded: " + String(currentSlot));

  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(ssid, password);
  Serial.println("Access Point Started: " + String(ssid));

  dnsServer.start(DNS_PORT, "*", apIP);
  setupRoutes();
  server.begin();
  Serial.println("Web Server started");
}

void setupRoutes(){

  server.serveStatic("/Ladybug-removebg-preview.png", LittleFS, "/Ladybug-removebg-preview.png");
  server.serveStatic("/buttery-removebg-preview.png", LittleFS, "/buttery-removebg-preview.png");

  // Serve gallery (default page)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request){
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html");
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
  });

  // Serve upload page
  server.on("/upload.html", HTTP_GET, [](AsyncWebServerRequest* request){
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/upload.html", "text/html");
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
  });

  // Captive portal redirect hooks
  auto redirect = [](AsyncWebServerRequest* request){
    request->redirect("http://192.168.4.1/");
  };

  server.on("/hotspot-detect.html", HTTP_GET, redirect);
  server.on("/generate_204", HTTP_GET, redirect);
  server.on("/connecttest.txt", HTTP_GET, redirect);
  server.on("/ncsi.txt", HTTP_GET, redirect);

  // Serve images by slot number
  server.on("/img", HTTP_GET, [](AsyncWebServerRequest* request){
    
    if(!request->hasParam("slot")){
      request->send(400, "text/plain", "Bad Request: Missing slot parameter");
      return;
    }

    int slot = request->getParam("slot")->value().toInt();
    if(slot < 0 || slot >= numSlots){
      request->send(400, "text/plain", "Bad Request: Invalid slot number");
      return;
    }

    String path = slotPath(slot);
    Serial.println("Image request for slot: " + String(slot) + " path: " + path + " exists: " + String(LittleFS.exists(path)));
    if(!LittleFS.exists(path)){
      request->send(404, "text/plain", "Not Found: No image in this slot");
      return;
    }

    request->send(LittleFS, path, "image/jpeg");
  });

  // Handle image uploads
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest* request){
    request->send(200, "text/plain", "Upload received");
  }, [](AsyncWebServerRequest* request, String filename,
           size_t index, uint8_t* data, size_t len, bool final){
    static File uploadFile;

    if(index == 0){
      String path = slotPath(currentSlot);
      Serial.println("Starting upload for slot " + String(currentSlot) + " to path: " + path);
      uploadFile = LittleFS.open(path, "w");
    }

    if(uploadFile) uploadFile.write(data, len);

    if(final){
      uploadFile.close();
      currentSlot = (currentSlot + 1) % numSlots;
      savePointer();
      Serial.println("Upload complete. Current slot is now: " + String(currentSlot));
    }
  }); 

  server.onNotFound([](AsyncWebServerRequest* request){
    request->redirect("http://192.168.4.1/");
  });
}

void loop() {
  dnsServer.processNextRequest();
}