#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <base64.h>
#include <MD5Builder.h>
#include <WiFiUdp.h>

const char* SSID = "#+#WLAN_SSID#+#";
const char* PSK = "#+#WLAN_PASSWORD#+#";
const char* MQTT_BROKER = "#+#MQTT_BROKER#+#";

// SIP-Server-Daten
const char* sip_server = "#+#SIP_SERVER#+#";
const int sipPort = 5060;
const char* sip_user = "#+#SIP_USER#+#";
const char* sip_password = "#+#SIP_PASSWORD#+#";
const char* sip_domain = "#+#SIP_DOMAIN#+#";
const char* sip_server_ip = "#+#SIP_SERVER_IP#+#";
const int sip_port = 5060;
const int local_sip_port = 5061;

WiFiClient espClient;
PubSubClient client(espClient);
WiFiUDP udp;
IPAddress localIP;


String build_digest_response(String realm, String nonce, String method, String uri) {
  MD5Builder md5;

  // A1 = username:realm:password
  String a1 = String(sip_user) + ":" + realm + ":" + sip_password;
  md5.begin();
  md5.add(a1);
  md5.calculate();
  String ha1 = md5.toString();

  // A2 = method:uri
  String a2 = method + ":" + uri;
  md5.begin();
  md5.add(a2);
  md5.calculate();
  String ha2 = md5.toString();

  // response = MD5(HA1:nonce:HA2)
  String response_input = ha1 + ":" + nonce + ":" + ha2;
  md5.begin();
  md5.add(response_input);
  md5.calculate();
  return md5.toString();
}

char msg[50];
unsigned long lastMsg = 0;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);

  WiFi.begin(SSID, PSK);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("outTopic", "hello world");
      // ... and resubscribe
      client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void sendSIPRegister(String auth = "") {
  String branch = "z9hG4bK" + String(random(100000, 999999));
  String call_id = String(random(10000, 99999));
  String from_tag = String(random(10000, 99999));
  String uri = "sip:" + String(sip_domain);

  String msg = "";
  msg += "REGISTER sip:" + String(sip_domain) + " SIP/2.0\r\n";
  msg += "Via: SIP/2.0/UDP " + localIP.toString() + ":" + String(local_sip_port) + ";branch=" + branch + "\r\n";
  msg += "Max-Forwards: 70\r\n";
  msg += "To: <sip:" + String(sip_user) + "@" + String(sip_domain) + ">\r\n";
  msg += "From: <sip:" + String(sip_user) + "@" + String(sip_domain) + ">;tag=" + from_tag + "\r\n";
  msg += "Call-ID: " + call_id + "@esp8266\r\n";
  msg += "CSeq: 1 REGISTER\r\n";
  msg += "Contact: <sip:" + String(sip_user) + "@" + localIP.toString() + ":" + String(local_sip_port) + ">\r\n";
  msg += "Expires: 3600\r\n";
  msg += "User-Agent: ESP8266 SIP Client\r\n";
  if (auth.length() > 0) {
    msg += "Authorization: " + auth + "\r\n";
  }
  msg += "Content-Length: 0\r\n\r\n";

  udp.beginPacket(sip_server_ip, sip_port);
  udp.write((const uint8_t*)msg.c_str(), msg.length());
  udp.endPacket();

  Serial.println("SIP REGISTER gesendet:");
  Serial.println(msg);
}

void handleSIPResponse() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char buffer[1024];
    int len = udp.read(buffer, 1024);
    if (len > 0) {
      buffer[len] = 0;
      String response = String(buffer);
//      Serial.println("SIP Antwort erhalten:");
//      Serial.println(response);

      if (response.indexOf("401 Unauthorized") != -1) {
        int realm_start = response.indexOf("realm=\"") + 7;
        int realm_end = response.indexOf("\"", realm_start);
        String realm = response.substring(realm_start, realm_end);

        int nonce_start = response.indexOf("nonce=\"") + 7;
        int nonce_end = response.indexOf("\"", nonce_start);
        String nonce = response.substring(nonce_start, nonce_end);

        String uri = "sip:" + String(sip_domain);
        String response_hash = build_digest_response(realm, nonce, "REGISTER", uri);

        String auth = "Digest username=\"" + String(sip_user) + "\", realm=\"" + realm + "\", nonce=\"" + nonce + "\", uri=\"" + uri + "\", response=\"" + response_hash + "\", algorithm=MD5";

        delay(1000);  // kurz warten
        sendSIPRegister(auth);
      }
      if (response.indexOf("INVITE") != -1) {
        Serial.println("INVITE");
        client.publish("outTopic", "INVITE");
      }
    }
  }
}


void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > 30000) {
    lastMsg = now;
    client.publish("outTopic", "hello world");
  }
  handleSIPResponse();
  delay(500);
}
void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(MQTT_BROKER, 1883);
  localIP = WiFi.localIP();

  udp.begin(local_sip_port);
  sendSIPRegister();
}
