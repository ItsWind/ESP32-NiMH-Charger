#include "secrets.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "index.h"

#define _OUT_

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

int status = WL_IDLE_STATUS;
const char* ssid = SECRET_SSID;      // your network SSID (name)
const char* pass = SECRET_PASS;      // your network password (use for WPA, or use as key for WEP)

const long READ_VOLT_TIMER_MICROS = 500000;
const uint16_t READ_NO_BATT = 1475;
const uint16_t READ_FULL_CHARGE = 1455;
const uint16_t READ_FULL_CHARGE_SLOW = 1000;
const uint8_t DISPLAY_READ_FULL_MINUSOFFSET = 5;
const uint8_t HOLDER_MAX_READINGS = 30;

struct Holder {
  bool slowCharger;

  uint8_t ledPin;
  uint8_t gatePin;
  uint8_t readPin;

  uint8_t ledDutyCycle;
  uint8_t ledState;
  unsigned long ledStateTimer;
  long ledStateTimerCurrent;

  uint16_t avgReading;
  bool readGateOpen;

  uint16_t lastReadings[HOLDER_MAX_READINGS];
};
const uint8_t AMOUNT_OF_HOLDERS = 4;
Holder batteryHolders[] = {
  {false, 23, 14, 36, 0, 0, 0, 0, 0, false, {0}},
  {false, 17, 27, 39, 0, 0, 0, 0, 0, false, {0}},
  {false, 16, 26, 34, 0, 0, 0, 0, 0, false, {0}},
  {true, 4, 32, 35, 0, 0, 0, 0, 0, false, {0}}
};

void getHolderChargeLimits(Holder *holder, uint16_t minChargeBump, _OUT_ uint16_t *fullChargeToUse, _OUT_ uint16_t *minChargeToUse) {
  *fullChargeToUse = READ_FULL_CHARGE;
  *minChargeToUse = READ_FULL_CHARGE_SLOW + minChargeBump;
  if (holder->slowCharger) {
    *fullChargeToUse = READ_FULL_CHARGE_SLOW;
    *minChargeToUse = 0;
  }
}

float getHolderChargeLerpVal(Holder *holder) {
  uint16_t fullChargeToUse = 0;
  uint16_t minChargeToUse = 0;
  getHolderChargeLimits(holder, 200, &fullChargeToUse, &minChargeToUse);
  return ((float)holder->avgReading - (float)minChargeToUse) / ((float)fullChargeToUse - (float)minChargeToUse - (float)DISPLAY_READ_FULL_MINUSOFFSET);
}

float getHolderChargeLerp(Holder *holder, float a, float b) {
  /*uint16_t fullChargeToUse = READ_FULL_CHARGE;
  uint16_t minChargeToUse = READ_FULL_CHARGE_SLOW + 200;
  if (holder->slowCharger) {
    fullChargeToUse = READ_FULL_CHARGE_SLOW;
    minChargeToUse = 0;
  }*/

  float lerpVal = lerp(a, b, getHolderChargeLerpVal(holder));
  if (a < b) {
    if (lerpVal < a) {
      lerpVal = a;
    }
    else if (lerpVal > b) {
      lerpVal = b;
    }
  }
  return lerpVal;
}

void setHolderTypePin(Holder *holder, uint8_t pinNum, bool toggle) {
  bool setHigh = (bool)digitalRead(pinNum);
  if (toggle == setHigh) {
    return;
  }

  digitalWrite(pinNum, toggle ? HIGH : LOW);
}

uint8_t getHolderLEDDutyCycle(Holder *holder) {
  float maxDutyCycle = 128.0;
  float holderChargeLerpVal = getHolderChargeLerpVal(holder);
  return (uint8_t)pow(maxDutyCycle, holderChargeLerpVal);
}

void setHolderGate(Holder *holder, bool toggle) {
  setHolderTypePin(holder, holder->gatePin, toggle);
}

void setNewHolderLEDTimer(Holder *holder, unsigned long newTimer) {
  if (holder->ledState < 2) {
    holder->ledStateTimerCurrent = newTimer;
  }
  
  holder->ledStateTimer = newTimer;
  holder->ledState = 2;
}

void updateHolderLED(Holder *holder, unsigned long dt) {
  if (holder->ledState == 0) {
    holder->ledDutyCycle = 0;
  }
  else if (holder->ledState == 1) {
    holder->ledDutyCycle = 255;
  }
  else if (holder->ledState >= 2) {
    holder->ledStateTimerCurrent -= (long)dt;
    if (holder->ledStateTimerCurrent <= 0) {
      holder->ledStateTimerCurrent = (long)holder->ledStateTimer;
      // Normal cycle
      if (holder->ledState == 2) {
        if (holder->ledDutyCycle > 0) {
          holder->ledDutyCycle = 0;
        }
        else {
          holder->ledDutyCycle = getHolderLEDDutyCycle(holder);
        }
      }
      // BAD BLINK
      else {
        // NOT GOOD LEDS
        holder->ledDutyCycle = holder->ledDutyCycle == 16 ? 4 : 16;
      }
    }
  }

  analogWrite(holder->ledPin, holder->ledDutyCycle);
}

void readHolder(Holder *holder) {
  // Holder gates should all be set to closed at this point and properly waited to equalize.
  // Open this one for reading and wait for equalization
  setHolderGate(holder, true);
  delayMicroseconds(50);

  //Serial.println("--------------------");

  uint16_t readValue = analogRead(holder->readPin);
  // If readValue is 45 away from last avg, reset readings (likely a physical state change, so we want immediate effect)
  if (abs(readValue - holder->avgReading) >= 45) {
    holder->lastReadings[0] = readValue;
    // Replace rest of array with 0 to reset
    for (uint8_t i = 1; i < HOLDER_MAX_READINGS; i++) {
      holder->lastReadings[i] = 0;
    }

    holder->avgReading = readValue;
  }
  // Put readValue into last readings and calculate new avg
  else {
    uint8_t numOfValidReads = 1;
    uint32_t readTotal = (uint32_t)readValue;
    uint16_t newReadings[HOLDER_MAX_READINGS] = {0};
    newReadings[0] = readValue;
    
    // Read current array except last val
    for (uint8_t i = 0; i < HOLDER_MAX_READINGS-1; i++) {
      if (holder->lastReadings[i] == 0) {
        break;
      }

      newReadings[i+1] = holder->lastReadings[i];
      readTotal += holder->lastReadings[i];
      numOfValidReads++;
    }

    // Replace current array with new array
    for (uint8_t i = 0; i < HOLDER_MAX_READINGS; i++) {
      holder->lastReadings[i] = newReadings[i];
    }

    // Update avgReading
    holder->avgReading = (uint16_t)(readTotal / (uint32_t)numOfValidReads);
  }

  //holder->lastReading = readValue;
  //Serial.print("READING: ");
  //Serial.println(readValue);

  uint16_t fullChargeToUse = 0;
  uint16_t minChargeToUse = 0;
  getHolderChargeLimits(holder, 0, &fullChargeToUse, &minChargeToUse);

  // Assuming no battery found
  if (holder->avgReading >= READ_NO_BATT) {
    holder->readGateOpen = false;
    holder->ledState = 0;
    //Serial.println("STATUS: NO BATTERY DETECTED.");
  }
  else if (!holder->slowCharger && holder->avgReading <= READ_FULL_CHARGE_SLOW) {
    holder->readGateOpen = false;
    setNewHolderLEDTimer(holder, 100000);
    holder->ledState = 3;
    //Serial.println("STATUS: UNSAFE VOLTAGE DETECTED. GATE CLOSED FOR SAFETY.");
  }
  // Done charging, so turn gate off
  else if (holder->avgReading >= fullChargeToUse) {
    holder->readGateOpen = false;
    holder->ledState = 1;
    //Serial.println("STATUS: DONE.");
  }
  else {
    // Set LED state
    holder->readGateOpen = true;

    if (holder->avgReading < fullChargeToUse - DISPLAY_READ_FULL_MINUSOFFSET) {
      unsigned long timerToSet = (unsigned long)getHolderChargeLerp(holder, 2000000.0, 250000.0);
      setNewHolderLEDTimer(holder, timerToSet);
    }
    else {
      holder->ledState = 1;
    }

    //Serial.println("STATUS: CHARGING.");
  }

  // Close this one back up and wait for equalization for next one
  setHolderGate(holder, false);
  delayMicroseconds(50);
}

unsigned long oldTime = 0;
void setup() {
  setCpuFrequencyMhz(240);

  for (uint8_t i = 0; i < AMOUNT_OF_HOLDERS; i++) {
    Holder *holder = &batteryHolders[i];

    pinMode(holder->ledPin, OUTPUT);
    analogWrite(holder->ledPin, 0);

    pinMode(holder->gatePin, OUTPUT);
    digitalWrite(holder->gatePin, LOW);

    pinMode(holder->readPin, INPUT);
  }

  Serial.begin(115200);
  delay(1000);

  connectToWiFi();
  
  connectHTMLStuff();

  oldTime = micros();
}

long checkReadTimer = 0;
void loop() {
  unsigned long thisTime = micros();
  unsigned long dt = 0;
  // Handle overflow
  if (thisTime < oldTime) {
    unsigned long maxNum = 0;
    maxNum--;

    dt = (maxNum - oldTime) + thisTime + 1;
  }
  else {
    dt = thisTime - oldTime;
  }
  oldTime = thisTime;

  checkReadTimer -= (long)dt;
  bool isReadTime = checkReadTimer <= 0;

  // Set every holder's gate to off and wait for MOSFET gate drain for voltage read stabilization
  if (isReadTime) {
    for (uint8_t i = 0; i < AMOUNT_OF_HOLDERS; i++) {
      Holder *holder = &batteryHolders[i];

      setHolderGate(holder, false);
    }
    delayMicroseconds(50);
    dt += 50;
  }

  // Perform reads and/or update leds
  for (uint8_t i = 0; i < AMOUNT_OF_HOLDERS; i++) {
    Holder *holder = &batteryHolders[i];

    if (isReadTime) {
      readHolder(holder);
      dt += 100;
    }

    updateHolderLED(holder, dt);
  }

  if (isReadTime) {
    // Set holder gates based on readGateOpen
    for (uint8_t i = 0; i < AMOUNT_OF_HOLDERS; i++) {
      Holder *holder = &batteryHolders[i];
      setHolderGate(holder, holder->readGateOpen);
    }

    checkReadTimer = READ_VOLT_TIMER_MICROS;

    unsigned long timeNeededForLoop = micros() - thisTime;
    Serial.println(timeNeededForLoop);
  }

  checkWiFi();

  checkHTMLStuff(dt);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  /*AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_BINARY) {
    switch(data[0]) {
      case 0:
        setColor(data[1], data[2], data[3]);
        break;
    }
  }*/
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

String processor(const String& var){
  /*Serial.println(var);
  if(var == "STATE"){
    if (ledState){
      return "ON";
    }
    else{
      return "OFF";
    }
  }*/
  return String();
}

unsigned long sendReadingsThroughWSTimer = 0;
void checkHTMLStuff(unsigned long dt) {
  // LOOP
  // If no clients currently, don't prepare/send anything
  if (!ws.getClients().isEmpty()) {
    // Timer for update every 1 second
    sendReadingsThroughWSTimer += dt;
    if (sendReadingsThroughWSTimer >= 1000000) {
      sendReadingsThroughWSTimer = 0;

      // Create data to send to web clients from readings
      uint8_t byteArrLength = 9;
      uint8_t byteArr[byteArrLength];
      byteArr[0] = 0;
      for (uint8_t i = 0; i < AMOUNT_OF_HOLDERS; i++) {
        Holder *holder = &batteryHolders[i];

        float holderReadingFloat = (float)holder->avgReading;
        float fullChargeFloat = holder->slowCharger ? (float)READ_FULL_CHARGE_SLOW : (float)READ_FULL_CHARGE;
        
        uint8_t indexBase = i + 1;

        if (holderReadingFloat >= (float)READ_NO_BATT) {
          byteArr[i+indexBase] = 0;
          byteArr[i+indexBase+1] = 0;
        }
        else {
          // Calculate battery %
          byteArr[i+indexBase] = (uint8_t)getHolderChargeLerp(holder, 1.0, 100.0);

          // Calculate battery voltage * 100
          int16_t voltReading = (int16_t)(holderReadingFloat / 10.0) - 10;
          if (voltReading < 0) {
            voltReading = 0;
          }
          byteArr[i+indexBase+1] = (uint8_t)voltReading;
        }
      }
      // Send data
      ws.binaryAll((uint8_t *)byteArr, byteArrLength);
    }
  }

  // Clean up
  ws.cleanupClients();
}

void connectHTMLStuff() {
  // SETUP
  initWebSocket();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", HTML_CONTENT, processor);
  });

  // Start server
  server.begin();
}

void checkWiFi() {
  if (!WiFi.isConnected()) {
    Serial.println("Retrying WiFi connection");
    connectToWiFi();
  }
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi failed");
    return;
  }

  Serial.println("WiFi connected");
}