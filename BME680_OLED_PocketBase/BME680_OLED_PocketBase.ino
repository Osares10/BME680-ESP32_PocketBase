/***************************************************************************
    This is a library for the BME680 gas, humidity, temperature & pressure sensor

    Designed specifically to work with the Adafruit BME680 Breakout
    ----> http://www.adafruit.com/products/3660

    These sensors use I2C or SPI to communicate, 2 or 4 pins are required
    to interface.

    Adafruit invests time and resources providing this open source code,
    please support Adafruit and open-source hardware by purchasing products
    from Adafruit!

    Written by Limor Fried & Kevin Townsend for Adafruit Industries.
    BSD license, all text above must be included in any redistribution
 ***************************************************************************/

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_BME680.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <sys/time.h>
#include <HTTPClient.h>
#include <esp_sleep.h>

#define LED 2

#define BME_SCK 13
#define BME_MISO 12
#define BME_MOSI 11
#define BME_CS 10

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME680 bme; // I2C

Adafruit_SSD1306 display = Adafruit_SSD1306(128, 32, &Wire);

String timeUTC;
bool wifiConnected = true;

const char* ssid = "SSID"; // Network SSID
const char* password = "password"; // Network password

const char* ntpServer = "pool.ntp.org"; // NTP server
const long gmtOffset_sec = 0; // Offset from GMT
const int daylightOffset_sec = 0; // Offset from daylight savings time

void setup() {
    Serial.begin(9600);
    Serial.println("Starting BME680...");

    Serial.println("Starting Wi-Fi..."); // Print a message to the serial monitor
    wifiConnected = connectToWifi(); // Obtain the Wi-Fi connection status

    if (wifiConnected) {
        Serial.println("Connection to Wi-Fi successful"); // Print a message to the serial monitor
        Serial.println("Starting time sync..."); // Print a message to the serial monitor
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        while (!time(nullptr)) {
            delay(1000);
            Serial.println("Waiting for time sync...");
        }
        Serial.println("Time synced");
    } else {
        Serial.println("Connection to Wi-Fi failed"); // Print a message to the serial monitor
        Serial.println("Proceeding without Wi-Fi..."); // Print a message to the serial monitor
    }


    // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)
    // init done
    display.display();
    delay(100);
    display.clearDisplay();
    display.display();
    display.setTextSize(1);
    display.setTextColor(WHITE);

    if (!bme.begin()) {
        Serial.println("Could not find a valid BME680 sensor, check wiring!");
        while (1); // Freeze the program
    }

    // Set up oversampling and filter initialization
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150); // 320*C for 150 ms

    pinMode(LED, OUTPUT); // Set the LED pin as an output

    Serial.println("Setup complete");
}

void loop() {
    digitalWrite(LED, HIGH); // Turn the LED on (Note that LOW is the voltage level

    display.setCursor(0,0);
    display.clearDisplay();

    if (! bme.performReading()) {
        Serial.println("Failed to perform BME680 reading");
        return;
    }

    printToSerial(); // Print to serial monitor
    printToDisplay(); // Print to OLED display

    if (!wifiConnected) { // If Wi-Fi is not connected, wait 10 minutes and try again
        Serial.println("Retrying in 10 minutes...");
        digitalWrite(LED, LOW); // Turn the LED off by making the voltage HIGH
        delay(10 * 60 * 1000); // Wait 10 minutes

        esp_restart(); // Restart the ESP32
    } 

    timeUTC = getUTCTime(); // Get UTC time from NTP server

    sendToPocketBase(); // Send data to PocketBase

    Serial.println("Updating in 10 minutes...");
    digitalWrite(LED, LOW); // Turn the LED off by making the voltage HIGH
    
    delay(10 * 60 * 1000); // Wait 10 minutes
}

void printToSerial() { // Print to serial monitor
    Serial.print("Temperature = "); Serial.print(bme.temperature); Serial.println(" *C");
    Serial.print("Pressure = "); Serial.print(bme.pressure / (20 * 133.32239)); Serial.println(" inHg");
    Serial.print("Humidity = "); Serial.print(bme.humidity); Serial.println(" %");
    Serial.print("Gas = "); Serial.print(bme.gas_resistance / 1000.0); Serial.println(" KOhms");
}

void printToDisplay() { // Print to OLED display
    display.setCursor(0,0);
    display.clearDisplay();
    display.print("Temperature: "); display.print(bme.temperature); display.println(" *C");
    display.print("Pressure: "); display.print(bme.pressure / (20 * 133.32239)); display.println(" inHg");
    display.print("Humidity: "); display.print(bme.humidity); display.println(" %");
    display.print("Gas: "); display.print(bme.gas_resistance / 1000.0); display.println(" KOhms");
    display.display();
}

bool connectToWifi() { // Connect to the Wi-Fi network
    Serial.print("Connecting to ");
    Serial.println(ssid);

    unsigned long startTime = millis(); // Get the current time

    WiFi.begin(ssid, password); // Connect to the network

    while (WiFi.status() != WL_CONNECTED) { // Wait for the Wi-Fi to connect
        delay(500);
        Serial.print(".");
        if (millis() - startTime > 60000) { // If it's been more than 1 minute
            Serial.println("");
            Serial.println("WiFi connection timed out");
            return false; // Return false
        }
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP()); // Print the local IP address

    return true; // Return true if connection was successful
}

String getUTCTime() { // Get UTC time from NTP server
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    time_t now = tv.tv_sec;
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    char buffer[30];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%03ldZ",
        timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
        timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
        tv.tv_usec / 1000);
    return String(buffer);
}

void sendToPocketBase() { // Send data to PocketBase
    Serial.println("Sending data to PocketBase...");

    HTTPClient http;

    http.begin("https://custom.domain/api/collections/collection_name/records"); // Specify the URL
    http.addHeader("Content-Type", "application/json"); // Specify content-type header

    // Create the JSON payload
    String payload = "{\"time\": \"" + timeUTC + "\", \"temperature\": \"" + bme.temperature + "\", \"pressure\": \"" + bme.pressure / (20 * 133.32239) + "\", \"humidity\": \"" + bme.humidity + "\", \"gas\": \"" + bme.gas_resistance / 1000.0 + "\"}";

    int httpCode = http.POST(payload); // Send the request

    if(httpCode == 200) { // Check the returning code
        Serial.println("Data sent to PocketBase successfully");
    } else { // If the code is not 200, something went wrong
        Serial.print("Error sending data to PocketBase, returned code: ");
        Serial.println(httpCode);

        Serial.println("Restarting ESP32...");
        delay(1000); // Wait for the serial output to finish
        esp_restart(); // Restart the ESP32
    }

    http.end(); // Close connection
}