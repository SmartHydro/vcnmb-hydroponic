#include <WiFiEsp.h>
#include <WiFiEspClient.h>
#include <WiFiEspServer.h>
#include <DHTesp.h>
#include <DFRobot_EC10.h>
#include <DFRobot_PH.h>

#define AP_SSID "SmartHydro"
#define AP_PASS "Hydro123!"
#define EC_DOWN_PUMP_PIN 8
#define EC_UP_PUMP_PIN 7
#define PH_DOWN_PUMP_PIN 6
#define PH_UP_PUMP_PIN 5
#define CIRCULATION_PUMP_PIN 9
#define EXTRACTOR_FAN_PIN 10
#define TENT_FAN_PIN 11
#define LIGHT_PIN 12
#define TEMP_HUMID_SENS_PIN 50
#define FLOW_SENS_PIN 52
#define AMB_LIGHT_SENS_PIN A2
#define PH_SENS_PIN A3
#define EC_SENS_PIN A4

WiFiEspServer WebServer(80);
DHTesp TempHumid;
DFRobot_EC10 EC10;
DFRobot_PH PH;
volatile int FlowSensorPulseCount = 0;
unsigned long CurrentTime, PreviousTime;

void setup() {
  // USB Serial Connection
  Serial.begin(115200);

  // DHT22 Temp/Humidity Sensor
  TempHumid.setup(TEMP_HUMID_SENS_PIN, 'AUTO_DETECT');

  // YF-B6 Flow Sensor
  pinMode(FLOW_SENS_PIN, INPUT);
  attachInterrupt(20, PulseCounterFlowSensor, FALLING);
  CurrentTime = millis();
  PreviousTime = 0;

  // Ambient Light Sensor
  pinMode(AMB_LIGHT_SENS_PIN, INPUT);

  // SEN0161 pH Sensor
  pinMode(PH_SENS_PIN, INPUT);
  PH.begin();

  // EC10 Current Sensor
  pinMode(EC_SENS_PIN, INPUT);

  // ESP module
  Serial1.begin(115200);
  WiFi.init(&Serial1);
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.print("no wifi shield, panicking!!!");
    while (true)
      ;
  }
  IPAddress SysIP(192, 168, 1, 10);
  WiFi.configAP(SysIP);
  WiFi.beginAP(AP_SSID, 13, AP_PASS, ENC_TYPE_WPA2_PSK, false);
  WebServer.begin();

  // setting all relay pins to output, and defaulting all off
  for (int i = 5; i <= 12; i++) {
    pinMode(i, OUTPUT);
    TogglePin(i);
  }
  // turning on equipment that should be on by default
  TogglePin(LIGHT_PIN);
  TogglePin(TENT_FAN_PIN);
  TogglePin(CIRCULATION_PUMP_PIN);
}

void loop() {
  WiFiEspClient WebClient = WebServer.available();
  if (!WebClient) { return; }
  String ClientRequest = WebClient.readStringUntil('\r');
  Serial.println("Client Request: " + ClientRequest);
  String Payload = "";

  while (WebClient.available()) {
    WebClient.read();
  }
  if (ClientRequest.indexOf("GET /hardware") >= 0) {
    Payload = HardwareToJson();
  }
  if (ClientRequest.indexOf("GET /sensor") >= 0) {
    Payload = SensorToJson();
  }
  if (ClientRequest.indexOf("GET /ph_in") >= 0) {
    TogglePin(PH_UP_PUMP_PIN);
  }
  if (ClientRequest.indexOf("GET /ph_out") >= 0) {
    TogglePin(PH_DOWN_PUMP_PIN);
  }
  if (ClientRequest.indexOf("GET /circ_pump") >= 0) {
    TogglePin(CIRCULATION_PUMP_PIN);
  }
  if (ClientRequest.indexOf("GET /ec_in") >= 0) {
    TogglePin(EC_UP_PUMP_PIN);
  }
  if (ClientRequest.indexOf("GET /ec_out") >= 0) {
    TogglePin(EC_DOWN_PUMP_PIN);
  }
  if (ClientRequest.indexOf("GET /fan_circ") >= 0) {
    TogglePin(TENT_FAN_PIN);
  }
  if (ClientRequest.indexOf("GET /fan_extractor") >= 0) {
    TogglePin(EXTRACTOR_FAN_PIN);
  }
  if (ClientRequest.indexOf("GET /light") >= 0) {
    TogglePin(LIGHT_PIN);
  }
  SendClientResponse(WebClient, Payload);
  WebClient.stop();
  Serial.println("Disconnecting from WiFi Client.");
}

void SendClientResponse(WiFiEspClient WebClient, String Payload) {
  // return a HTTP reponse to the client
  WebClient.print(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Connection: close\r\n");

  // check if there is a payload to send, if yes, calculate the content length
  if (Payload.length() > 0) {
    WebClient.print("Content-Length: " + String(Payload.length()) + "\r\n\r\n" + Payload);
  }
}

String HardwareToJson() {
  // return all current hardware data in json format
  return "{\n \"pH_Up_Pump\" : \"" + String(digitalRead(PH_UP_PUMP_PIN))
         + "\",\n \"pH_Down_Pump\" : \"" + String(digitalRead(PH_DOWN_PUMP_PIN))
         + "\",\n \"EC_Up_Pump\" : \"" + String(digitalRead(EC_UP_PUMP_PIN))
         + "\",\n \"EC_Down_Pump\" : \"" + String(digitalRead(EC_DOWN_PUMP_PIN))
         + "\",\n \"Circulation_Pump\" : \"" + String(digitalRead(CIRCULATION_PUMP_PIN))
         + "\",\n \"Extractor_Fan\" : \"" + String(digitalRead(EXTRACTOR_FAN_PIN))
         + "\",\n \"Tent_Fan\" : \"" + String(digitalRead(TENT_FAN_PIN))
         + "\",\n \"Grow_Light\" : \"" + String(digitalRead(LIGHT_PIN)) + "\"\n}";
}

String SensorToJson() {
  // get temperature and humidity values
  TempAndHumidity DHT_Measurement = TempHumid.getTempAndHumidity();
  // return data from all sensors in json format
  return "{\n \"Temperature\" : \"" + String(DHT_Measurement.temperature)
         + "\",\n \"Humidity\" : \"" + String(DHT_Measurement.humidity)
         + "\",\n \"LightLevel\" : \"" + String(GetLightReading())
         + "\",\n \"FlowRate\" : \"" + String(GetFlowRateReading())
         + "\",\n \"pH\" : \"" + String(GetPHReading(DHT_Measurement.temperature))
         + "\",\n \"EC\" : \"" + String(GetECReading(DHT_Measurement.temperature)) + "\"\n}";
}

int GetLightReading() {
  // return ambient light sensor reading
  return (int)analogRead(AMB_LIGHT_SENS_PIN);
}

float GetECReading(float Temperature) {
  // calculate current voltage and return EC reading
  float EC_Voltage = (float)analogRead(EC_SENS_PIN) / 1024.0 * 5000.0;
  return EC10.readEC(EC_Voltage, Temperature);
}

float GetPHReading(float Temperature) {
  // calculate current voltage and return PH reading
  float PH_Voltage = (float)analogRead(PH_SENS_PIN) / 1024.0 * 5000.0;
  return PH.readPH(PH_Voltage, Temperature);
}

float GetFlowRateReading() {
  // return number of L/s flowing through the sensor
  float FlowCalibrationFactor = 6.6;
  if ((millis()) - PreviousTime > 1000) {
    detachInterrupt(20);
    float WaterFlowRate = ((1000.0 / (millis() - PreviousTime)) * FlowSensorPulseCount) / FlowCalibrationFactor;
    PreviousTime = millis();
    FlowSensorPulseCount = 0;
    return WaterFlowRate;
  }
}

void PulseCounterFlowSensor() {
  // used to keep track of pulses from the flow rate sensor
  FlowSensorPulseCount++;
}

void TogglePin(int PinToToggle) {
  // used to toggle a pins state to the opposite of what it currently is
  digitalWrite(PinToToggle, !digitalRead(PinToToggle));
}                              