#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#define _MYSQL_LOGLEVEL_ 0
#include <MySQL_Generic.h>

#include <my_settings.h>

const char* mqtt_server = MQTT_SERV;
const int mqtt_port = MQTT_PORT;

const char* mqtt_topic = "elektromer/data";

// 📡 MQTT témata
const char* topic_main = "elektromer/data";
const char* topic_t1 = "elektromer/tarif1";
const char* topic_t2 = "elektromer/tarif2";
const char* topic_i = "elektromer/proud";
const char* topic_u = "elektromer/napeti";
const char* topic_T = "elektromer/actualtarif";

unsigned long int nextRequest = 0;
const int delayRead = 10;  // minutes

// mysql
IPAddress server_addr(192, 168, 2, 1);  // IP of the MySQL *server* here
char mysql_user[] = "home_sensor";      // MySQL user login username
char mysql_password[] = "sensor";       // MySQL user login password
char mysql_default_db[] = "home_sensors";

const char INIT_TABLE[] = "CREATE TABLE IF NOT EXISTS home_sensors.elektromer(time datetime, t1 decimal(9,3), t2 decimal(9,3), PRIMARY KEY (time))";
char INSERT_DATA[] = "INSERT INTO home_sensors.elektromer ( time, t1, t2) VALUES ( NOW(), %s, %s)";
char query[128];

int mysql_attempt = 0;

String T1 = "";
String T2 = "";


WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

MySQL_Connection mysqlConn((Client*)&wifiClient);

bool switchedTo9600 = false;
#define INIT_BAUD 300
#define READ_BAUD 9600

void sendInitSequence() {
  nextRequest = millis() + delayRead * 60 * 1000;
  Serial.end();
  Serial.begin(INIT_BAUD, SERIAL_7E1);
  switchedTo9600 = false;
  delay(500);
    while (Serial.available() > 0) {
    Serial.read();
  }
  Serial.print("/?!\r\n");
  Serial.flush();
}


void sendDataRequest() {
  while (Serial.available() > 0) {
    Serial.read();
  }
  Serial.write(0x06);     // ACK
  Serial.write('0');      // Start char
  Serial.write('0' + 5);  // Speed character
  Serial.write('0');      // Mode control char
  Serial.write(0x0D);     // CR
  Serial.write(0x0A);     // LF
  Serial.flush();
  Serial.end();
  delay(100);
  Serial.begin(READ_BAUD, SERIAL_7E1);
  switchedTo9600 = true;
}


void reconnectMQTT() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect("ESP_elektromer")) {
    } else {
      ArduinoOTA.handle();
      delay(3000);
    }
  }
}

void setupOTA() {
  ArduinoOTA.begin();
}

String extractValue(String line) {
  int start = line.indexOf('(') + 1;
  int end = line.indexOf('*');
  int coma = line.indexOf(',');
  if (coma > 0) line[coma] = '.';
  return (start > 0 && end > start) ? line.substring(start, end) : "";
}

void processLine(String line) {
  line.trim();
  if (line.length() == 0) return;

  //  mqttClient.publish(topic_main, line.c_str());

  if (line.indexOf("1.8.1(") >= 0) {
    T1 = extractValue(line);
    mqttClient.publish(topic_t1, T1.c_str(), 1);  // Tarif 1
  };
  if (line.indexOf("1.8.2(") >= 0) {
    T2 = extractValue(line);
    mqttClient.publish(topic_t2, T2.c_str(), 1);  // Tarif 2
  };
  //if (line.indexOf("31.7.0") >= 0) mqttClient.publish(topic_i, extractValue(line).c_str(), 1);   // Proud
  //if (line.indexOf("32.7.0") >= 0) mqttClient.publish(topic_u, extractValue(line).c_str(), 1);   // Napětí
  //if (line.indexOf(".2.2") >= 0) mqttClient.publish(topic_T, extractValue(line).c_str(), 1);     // aktualni tarif
}

void setup() {
  Serial.begin(INIT_BAUD, SERIAL_7E1);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  mqttClient.setServer(mqtt_server, mqtt_port);

  setupOTA();
}

void loop() {
  ArduinoOTA.handle();

  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  if (millis() > nextRequest) sendInitSequence();


  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');

    if (line.startsWith("/D695D80321793")) sendDataRequest();

    if (switchedTo9600) {
      processLine(line);
    }
  }
  if (T1 != "" && T2 != "") {
    if (mysqlConn.connect(server_addr, 3306, mysql_user, mysql_password, mysql_default_db)) {
      MySQL_Query* cur_mem = new MySQL_Query(&mysqlConn);
      cur_mem->execute(INIT_TABLE);
      sprintf(query, INSERT_DATA, T1.c_str(), T2.c_str());
      cur_mem->execute(query);
      T1 = "";
      T2 = "";

    } else {
      mysql_attempt++;
      if (mysql_attempt > 20) {
        T1 = "";
        T2 = "";
        mysql_attempt = 0;
      } else {
        delay(1000);
      }
    };
    mysqlConn.close();
  };

  delay(10);
}
