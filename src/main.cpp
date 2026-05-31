#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h> 

#define modePin 4 

//Web connection details - Name of the wifi and the password 
const char* ssid = "Gallery";
const char* password = ""; 
const byte DNS_PORT = 53; //DNS port used by captive devices 
IPAddress apIP(192, 168, 4, 1); //IP adress that the webpage is hosted on 

//Number of Slots for file storage on the device and the current slot to upload to 
const int numSlots = 10;
int currentSlot = 0;


DNSServer dnsServer; //Handle DNS requests 
AsyncWebServer server(80); //Web server running on the ESP32 , 80 is the default port for HTTP traffic 
Preferences prefs; //persistant key value store used to store the currentSlot data 

//Helper function to create IMG strings for finding / uploading images to the ESP storage 
String slotPath(int slot){
  return "/img"+String(slot)+".jpg"; 
}

void setupRoutes();

void savePointer(){
  prefs.begin("gallery", false); //Opens the named prefernace namespace 
  prefs.putInt("currentSlot", currentSlot); //Adds the INIT to the namespace with the provided name for lookup
  prefs.end(); //close the prefs 
}

void loadPointer(){
  prefs.begin("gallery", true);
  currentSlot = prefs.getInt("currentSlot", 0);
  prefs.end();
}

void setup() {
  Serial.begin(115200);//Start the clock at 115200 Hz 
  pinMode(modePin, INPUT_PULLUP); //Sets the behaviour for a pin 

  if(!LittleFS.begin()){ //Attemp to open the littleFS filesystem 
    Serial.println("An error has occurred while mounting LittleFS");
    return;
  }
  
  loadPointer(); // Look for current slot value 
  Serial.println("Current slot loaded: " + String(currentSlot)); // Output to terminal the currrent slot position

  //Configuring the WIFI protocol 
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0)); // configure : local IP , Gateway IP , subnet mask 
  WiFi.softAP(ssid, password); // Starts the wifi network 
  Serial.println("Access Point Started: " + String(ssid)); 

  dnsServer.start(DNS_PORT, "*", apIP); // Starts the DNS server with the port and the IP defined 
  setupRoutes();//Set up the handling , in this case we want to route all incoming traffic to the ip adress the gallery is on
  server.begin(); // Start the server 
  Serial.println("Web Server started");
}

void setupRoutes(){ // Serving static files , Redirecting , imageslot system and uploads 

  // Serve gallery (default page)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request){ //onHomepage request 
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html", "text/html"); //Start the response by getting the HTML FIle 
    response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0"); //Dont cache this html File ever
    response->addHeader("Pragma", "no-cache"); // For older browsers 
    response->addHeader("Expires", "0"); //Expires instantly , this pages is already out of date 
    request->send(response); // send the response back to the request 
  });

  // Serve upload page, same as previous function just for the upload page 
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
  //On these requests send them the redirect request 
  server.on("/hotspot-detect.html", HTTP_GET, redirect);
  server.on("/generate_204", HTTP_GET, redirect);
  server.on("/connecttest.txt", HTTP_GET, redirect);
  server.on("/ncsi.txt", HTTP_GET, redirect);

  // Serve images by slot number, on a request for an image 
  server.on("/img", HTTP_GET, [](AsyncWebServerRequest* request){
    
    if(!request->hasParam("slot")){ // If it doesnt have a number 
      request->send(400, "text/plain", "Bad Request: Missing slot parameter");
      return;
    }

    int slot = request->getParam("slot")->value().toInt(); // get the value of the parameter 
    if(slot < 0 || slot >= numSlots){
      request->send(400, "text/plain", "Bad Request: Invalid slot number"); //Check the slot is actually a reasonable number 
      return;
    }

    String path = slotPath(slot); // build a string path to the file using the slot 
    Serial.println("Image request for slot: " + String(slot) + " path: " + path + " exists: " + String(LittleFS.exists(path)));
    if(!LittleFS.exists(path)){
      request->send(404, "text/plain", "Not Found: No image in this slot"); // Check there is an image if not send an eror 
      return;
    }

    request->send(LittleFS, path, "image/jpeg"); // if passes through all the checks then send the image , final fucntion tells whayt the file type is 
  });

  // Handle image uploads
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest* request){ //HTTP_POST is when the server gets a request 
    request->send(200, "text/plain", "Upload received"); // tell the device the file has been recieved 
  }, [](AsyncWebServerRequest* request, String filename,
           size_t index, uint8_t* data, size_t len, bool final){
    static File uploadFile; // file 

    if(index == 0){ // open the file only on teh first chunk of upload 
      String path = slotPath(currentSlot);
      Serial.println("Starting upload for slot " + String(currentSlot) + " to path: " + path);
      uploadFile = LittleFS.open(path, "w"); // write mode to current slot path 
    }

    if(uploadFile) uploadFile.write(data, len); // if upload file is valid then write the data to the path with length len 

    if(final){ // if the final chunk of data 
      uploadFile.close(); // close the file 
      currentSlot = (currentSlot + 1) % numSlots; //update the position in the list 
      savePointer(); //save the pointer to non voiltile storage 
      Serial.println("Upload complete. Current slot is now: " + String(currentSlot));
    }
  }); 

  server.onNotFound([](AsyncWebServerRequest* request){ // Any request that the server cannot find (in this case basically anything as we only direct to one IP)
    request->redirect("http://192.168.4.1/"); // then direct to the one IP adress the ESP serves
  });
}

void loop() {
  dnsServer.processNextRequest(); // Continously process server requests 
}