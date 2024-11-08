#include <Arduino.h>
#include "driver/adc.h" 
#include "esp_adc_cal.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <cstdio>
#include "secret.hpp"
#include <Wire.h>
#include <BH1750.h>


BH1750 lightMeter;

const int Ti = 60000;
const int T = 100;

const int Tl = 1000;
const int wait = Tl/T;

static int waitCount = 0;
static float lux = 0;
static float prev = 0;

bool initialized = false;

esp_adc_cal_characteristics_t adcChar;

const char* tmp_temp_template = "%s#TYPE hydroponics_water_temp_celsius gauge\nhydroponics_water_temp_celsius %f\n";
const char* tmp_lux_template = "%s#TYPE hydroponics_light_lux gauge\nhydroponics_light_lux %f\n";

uint32_t get_voltage_mV(){
    uint32_t voltage;
    // ADC1_CH6の電圧値を取得
    esp_adc_cal_get_voltage(ADC_CHANNEL_6, &adcChar, &voltage);
    return voltage;
}

float calculate_resistance(uint32_t voltage){
  const uint32_t R1 = 100000;
  const float VCC = 3.30;
  const float fvol = ((float)voltage)/1000;
  return R1*(VCC-fvol)/fvol;
}

float calculate_temperature(float resistance){
  // Calculate temperature using the Beta parameter equation
  float tempK = 1 / (((log(resistance / 100000)) / 4303) + (1 / (25 + 273.15)));
  float tempC = tempK - 273.15;
  return tempC;
}

float get_temperature(){
    uint32_t voltage = get_voltage_mV();
    float resistance = calculate_resistance(voltage);
    return calculate_temperature(resistance);
}

AsyncWebServer server(80);

void setup(){

    Wire.begin();
    lightMeter.begin();
    if (!lightMeter.measurementReady()) {
      delay(100);
      Serial.println("waiting lightmeter");
    }
    Serial.begin(115200);
    Serial.print("Connecting to ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.println("connecting");
    }
    Serial.println();
    Serial.println("WiFi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    adc_power_on();
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adcChar);
    server.on("/metrics", HTTP_ANY, [](AsyncWebServerRequest *request){
        char buf[810];
        memset(buf, 0, 810);
        sprintf(buf, tmp_temp_template, "", prev);
        sprintf(buf, tmp_lux_template, buf, lux);
        request->send(200, "text/plain", buf);
    });
    server.begin();

    // set initial temperature by averaging 10 measurements
    float sum = 0;
    for (int i = 0; i < 10; i++) {
        float temperature = get_temperature();
        sum += temperature;
        delay(T);
    }
    prev = sum/10;
    initialized = true;
}

void loop() {
  if (!initialized) {
    delay(100);
    return;
  }
  float temperature = get_temperature();
  float now = prev*Ti/(T+Ti)+temperature*T/(T+Ti);
  prev = now;

  if (waitCount == wait) {
    lux = lightMeter.readLightLevel();
    waitCount = 0;
  }
  waitCount++;
  delay(T);
}
