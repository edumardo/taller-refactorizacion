#include <DHTesp.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>

// Setup before run
const char * SSID     = "YOUR_SSID";
const char * PASSWORD = "YOUR_PASSWORD";

const uint8_t ENCODERBUTTONPUSHED = 19;
const uint8_t WIFISERVERPORT      = 303;
const uint8_t LCDADDRESS          = 0x3F;
const int BAUDRATE                = 115200;

const uint8_t SWITCHPIN           = 14;
const uint8_t CLOCKPIN            = 13;
const uint8_t DATAPIN             = 12;
const uint8_t LEDPIN              = 16;
const uint8_t DHTPIN              = 11;

volatile int EncodeCTR;
volatile int EncoderChange;
volatile int SwitchCtr;

const int NUMREADINGS = 10;
int readings[NUMREADINGS];
int counter;
int setPoint;
int insteonStatus;

WiFiServer server(WIFISERVERPORT);
LiquidCrystal_I2C lcd(LCDADDRESS, 16, 2);
DHTesp dht;

void Switch()
{
    static unsigned long DebounceTimer;

    if((unsigned long)(millis() - DebounceTimer) >= 100)
    {
        DebounceTimer = millis();
        if(!SwitchCtr)
        {
            SwitchCtr++;
            EncodeCTR = ENCODERBUTTONPUSHED;
        }
    }
}

void Encode()
{
    static unsigned long DebounceTimer;

    if((unsigned long)(millis() - DebounceTimer) >= 100)
    {
        DebounceTimer = millis();
        if(digitalRead(DATAPIN) == LOW)
        {
            EncodeCTR++;
        }
        else
        {
            EncodeCTR--;
        }
        EncoderChange++;
    }
}

/**
 * IMPORTANT NOTE: This code integrates with Insteon on/off module. If another method is used to turn the heater/boiler ON/OFF
 * then this section section will need to be modified to suit the needs (e.g. IoT relays, Vera, etc.)
 */
void InsteonON()
{
    HTTPClient http;

    http.begin("http://192.168.0.XX:XXX/3?02621FF5870F11FF=I=3");
    http.addHeader("Content-Type", "text/plain");

    int httpCode = http.POST("Message from ESP8266");
    String payload = http.getString();

    Serial.println(httpCode);
    Serial.println(payload);

    http.end();
}

void InsteonOFF()
{
    HTTPClient http;

    http.begin("http://192.168.0.XX:XXX/3?02621FF5870F13FF=I=3");
    http.addHeader("Content-Type", "text/plain");

    int httpCode = http.POST("Message from ESP8266");
    String payload = http.getString();

    Serial.println(httpCode);
    Serial.println(payload);

    http.end();
}

void setup()
{
    counter = 0;

    Serial.begin(115200);
    delay(10);

    pinMode(SWITCHPIN, INPUT_PULLUP);
    pinMode(CLOCKPIN, INPUT);
    pinMode(DATAPIN, INPUT);
    pinMode(LEDPIN, OUTPUT);
    digitalWrite(LEDPIN, LOW);

    attachInterrupt(digitalPinToInterrupt(SWITCHPIN), Switch, FALLING);
    attachInterrupt(digitalPinToInterrupt(CLOCKPIN), Encode, FALLING);

    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(SSID);
    WiFi.begin(SSID, PASSWORD);

    while(WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println(WiFi.localIP());

    server.begin();
    Serial.println("Server started");

    lcd.begin(16, 2);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.print("Room Temp:");
    lcd.setCursor(0, 1);
    lcd.print("setPoint:");

    dht.setup(DHTPIN, DHTesp::DHT11);

    delay(10);
}

void loop()
{
    if(EncoderChange || SwitchCtr)
    {
        EncoderChange = 0;
        Serial.print("EncodeCTR: ");
        Serial.print(EncodeCTR);
        Serial.println();
        SwitchCtr = 0;
    }

    readings[counter] = dht.getTemperature();
    counter++;

    if(counter >= NUMREADINGS)
    {
        counter = 0;
    }

    float total = 0;
    for(int i = 0; i < NUMREADINGS; i++)
    {
        total += readings[i];
    }
    float temperature = total / NUMREADINGS;

    setPoint = EncodeCTR;

    Serial.print("\tAVG:\t");
    Serial.println(temperature, 0);
    delay(1000);

    lcd.setCursor(11, 0);
    lcd.print(temperature, 0);
    lcd.print(" C ");
    lcd.setCursor(11, 1);
    lcd.print(setPoint);
    lcd.print(" C ");

    if(temperature <= setPoint - 1)
    {
        if(insteonStatus != 1)
        {
            InsteonON();
            digitalWrite(LEDPIN, HIGH);
            delay(500);
            insteonStatus = 1;
        }
    }

    if(temperature >= setPoint + 1)
    {
        if(insteonStatus != 0)
        {
            InsteonOFF();
            digitalWrite(LEDPIN, LOW);
            delay(500);
            insteonStatus = 0;
        }
    }

    WiFiClient client = server.available();
    if(!client)
    {
        return;
    }

    while(!client.available())
    {
        delay(1);
    }

    String request = client.readStringUntil('\r');
    client.flush();

    if(request.indexOf("") != -10)
    {
        if(request.indexOf("/+") != -1)
        {
            EncodeCTR = EncodeCTR + 1;
            Serial.println("You clicked +");
            Serial.println(EncodeCTR);
        }
        if(request.indexOf("/-") != -1)
        {
            EncodeCTR = EncodeCTR - 1;
            Serial.println("You clicked -");
            Serial.println(EncodeCTR);
        }
        if(request.indexOf("/ON") != -1)
        {
            EncodeCTR = temperature + 2;
            Serial.println("You clicked Boiler On");
            Serial.println(EncodeCTR);
        }
        if(request.indexOf("/OFF") != -1)
        {
            EncodeCTR = ENCODERBUTTONPUSHED;
            Serial.println("You clicked Boiler Off");
            Serial.println(EncodeCTR);
        }
    }
    else
    {
        Serial.println("invalid request");
        client.stop();
        return;
    }

    String s = "HTTP/1.1 200 OK\r\n";
    s += "Content-Type: text/html\r\n\r\n";
    s += "<!DOCTYPE HTML>\r\n<html>\r\n";
    s += "<p>setPoint Temperature <a href='/+'\"><button>+</button></a>&nbsp;<a href='/-'\"><button>-</button></a></p>";
    s += "<p>Boiler Status <a href='/ON'\"><button>ON</button></a>&nbsp;<a href='/OFF'\"><button>OFF</button></a></p>";
    client.flush();
    client.print(s);
    delay(1);

    client.println();
    client.print("Room Temperature = ");
    client.println(temperature);
    client.println();
    client.print("setPoint = ");
    client.println(setPoint);
    delay(1);
}
