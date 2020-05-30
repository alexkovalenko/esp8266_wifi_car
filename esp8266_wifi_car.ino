#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>
#include <Hash.h>

ESP8266WiFiMulti wifiMulti;
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);

const char* ssid = "ESP8266Wifi";
const char* pass = "wifipassword";

const char* mdnsName = "esp8266";

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/4.7.0/css/font-awesome.min.css'>
    <style>
        .btn {
            background-color: DodgerBlue;
            border: none;
            color: white;
            padding: 12px 16px;
            font-size: 16px;
            cursor: pointer;
            width: 100%;
            height: 100%;
            font-size: 100px;
        }
        .btn:hover {
            background-color: RoyalBlue;
        }
        .speed-range {
            width: 100%;
            height: 100px;
        }
    </style>
    <script>
        var connection = new WebSocket('ws://' + location.hostname + ':81/', ['arduino']);
        connection.onopen = function () {
            connection.send('Connect ' + new Date());
        };
        connection.onerror = function (error) {
            console.log('WebSocket Error ', error);
        };
        connection.onmessage = function (e) {
            console.log('Server: ', e.data);
        };
        function changeDirection(direction) {
            console.log('Changing direction: ' + direction);
            connection.send('#direction#' + direction);
        }
        var changeSpeedTimeoutId;
        function changeSpeed(speed) {
           clearTimeout(changeSpeedTimeoutId);
           changeSpeedTimeoutId = setTimeout(function() {
            console.log('Changing speed: ' + speed);
            connection.send('#speed#' + speed);
            }, 1000);
        }
    </script>
</head>
<body>
    <table>
        <tr>
            <td colspan="3">
                <input type="range" min="250" max="1000" value="400" class="speed-range" oninput="changeSpeed(this.value)"> 
            </td>
        </tr>
        <tr>
            <td>
            <td>
                <button class="btn" onclick="changeDirection('forward')"><i class="fa fa-arrow-up"></i></button>
            <td>
        <tr>
            <td>
                <button class="btn" onclick="changeDirection('left')"><i class="fa fa-arrow-left"></i></button>
            <td>
                <button class="btn" onclick="changeDirection('stop')"><i class="fa fa-stop"></i></button>
            <td>
                <button class="btn" onclick="changeDirection('right')"><i class="fa fa-arrow-right"></i></button>
        <tr>
            <td>
                <button class="btn" onclick="changeDirection('turn-left')"><i class="fa fa-undo"></i></button>
            <td>
                <button class="btn" onclick="changeDirection('backward')"><i class="fa fa-arrow-down"></i></button>
            <td>
                <button class="btn" onclick="changeDirection('turn-right')"><i class="fa fa-undo fa-flip-horizontal"></i></button>
    </table>
</body>
</html>
)rawliteral";

int in3 = 2;     /* GPIO2(D4) -> IN3   */

int in1 = 15;   /* GPIO15(D8) -> IN1  */

int in4 = 0;    /* GPIO0(D3) -> IN4   */

int in2 = 13;  /* GPIO13(D7) -> IN2  */

int ena = 14; /* GPIO14(D5) -> Motor-A Enable */

int enb = 12;  /* GPIO12(D6) -> Motor-B Enable */

char* direction = "none";

const int MIN_SPEED = 270;
int currentSpeed = 300;

void setup() {
  Serial.begin(9600);
  startWiFi();
  startWebSocket();
  startMDNS();
  startServer();
  startMotors();
}

void startWiFi() { 
  WiFi.softAP(ssid, pass);             
  Serial.print("Access Point \"");
  Serial.print(ssid);
  Serial.println("\" started\r\n");

  wifiMulti.addAP("somewifi", "somepassword");

  Serial.println("Connecting");
  while (wifiMulti.run() != WL_CONNECTED && WiFi.softAPgetStationNum() < 1) {  
    delay(250);
    Serial.print('.');
  }
  Serial.println("\r\n");
  if(WiFi.softAPgetStationNum() == 0) {      
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID());             
    Serial.print("IP address:\t");
    Serial.print(WiFi.localIP());            
  } else {                                   
    Serial.print("Station connected to ESP8266 AP");
  }
  Serial.println("\r\n");
}

void startWebSocket() { 
  webSocket.begin();                          
  webSocket.onEvent(webSocketEvent);          
  Serial.println("WebSocket server started.");
}

void startMDNS() { 
  MDNS.begin(mdnsName);                        
  Serial.print("mDNS responder started: http://");
  Serial.print(mdnsName);
  Serial.println(".local");
}

void startServer() { 
  server.on("/", handleRoot);                       
  server.onNotFound(handleNotFound);          
  server.begin();                             
  Serial.println("HTTP server started.");
}

void startMotors() {
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  pinMode(in3, OUTPUT);
  pinMode(in4, OUTPUT);
  pinMode(ena, OUTPUT);
  pinMode(enb, OUTPUT);
  analogWrite(ena, currentSpeed);
  analogWrite(enb, currentSpeed);
}

void loop() {
  webSocket.loop();
  server.handleClient();
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) { 
  switch (type) {
    case WStype_DISCONNECTED:             
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {              
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
      }
      break;
    case WStype_TEXT:                   
      Serial.printf("[%u] get Text: %s\n", num, payload);
      if(payload[0] == '#') {
        handleCommand(payload);
      }
      break;
  }
}

void handleCommand(uint8_t * payload) {
  String command = (char *)payload;  
  if(command.startsWith("#direction#")) {
    handleDirectionCommand(extractCommandValue("#direction#", command));
  } else if(command.startsWith("#speed#")) {
    handleSpeedCommand(extractCommandValue("#speed#", command));
  } else {
    Serial.println("Unknown command" + command);
  }
}

String extractCommandValue(String prefix, String command) {
  return command.substring(prefix.length());  
}

void handleDirectionCommand(String commandValue) {
  if(commandValue == "forward") {
     forward();
  } else if(commandValue == "backward") {
     backward();
  } else if(commandValue == "left") {
     left();
  } else if(commandValue == "right") {
     right();
  } else if(commandValue == "stop") {
     stop();
  } else if(commandValue == "turn-right") {
     turnRight();
  } else if(commandValue == "turn-left") {
     turnLeft();
  } else {
    Serial.println("Unknown direction" + commandValue);
  }
}

void handleSpeedCommand(String commandValue) {
  Serial.println("Changing speed to "+ commandValue);
  currentSpeed = commandValue.toInt();
  analogWrite(ena, currentSpeed);
  analogWrite(enb, currentSpeed);
}

void handleRoot() {
 server.send(200, "text/html", INDEX_HTML);
}

void handleNotFound() {
  server.send(404, "text/plain", "404: File Not Found");
}

int getSpeed() {
  return currentSpeed;
}

void forward() {
  Serial.println("Moving forward...");
  analogWrite(ena, currentSpeed);
  analogWrite(enb, currentSpeed);
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);
  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);
  direction = "forward";
}

void backward() {
  Serial.println("Moving backward...");
  analogWrite(ena, currentSpeed);
  analogWrite(enb, currentSpeed);
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);
  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);
  direction = "backward";
}

void left() {
  analogWrite(ena, currentSpeed);
  analogWrite(enb, max((int)(currentSpeed * 0.65), MIN_SPEED));
  if(direction == "backward") {
    Serial.println("Moving backward right.");
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);  
    digitalWrite(in3, LOW);
    digitalWrite(in4, HIGH);
  } else {
    Serial.println("Moving forward right.");
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);  
    digitalWrite(in3, HIGH);
    digitalWrite(in4, LOW);
  }
}

void right() {
  analogWrite(ena, max((int)(currentSpeed * 0.65), MIN_SPEED));
  analogWrite(enb, currentSpeed);
  if(direction == "backward") {
    Serial.println("Moving backward right.");
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
    digitalWrite(in3, LOW);
    digitalWrite(in4, HIGH);  
  } else {
    Serial.println("Moving forward right.");
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
    digitalWrite(in3, HIGH);
    digitalWrite(in4, LOW);  
  }
}

void stop() {
  Serial.println("Stop moving...");
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);
  direction = "none";
}

void turnRight() {
  Serial.println("Turning right");
  analogWrite(ena, max((int)(currentSpeed * 0.5), MIN_SPEED));
  analogWrite(enb, max((int)(currentSpeed * 0.5), MIN_SPEED));
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);
  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);
}

void turnLeft() {
  Serial.println("Turning left");
  analogWrite(ena, max((int)(currentSpeed * 0.5), MIN_SPEED));
  analogWrite(enb, max((int)(currentSpeed * 0.5), MIN_SPEED));
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);
}
