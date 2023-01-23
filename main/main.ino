#include "EspHomeClient.h"
#include <NTPClient.h>
#include <TimeLib.h>
#include <time.h>
#include <Timezone.h> // https://github.com/JChristensen/Timezone
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>

#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define MQTT_BROKER ""
#define MQTT_USERNAME ""
#define MQTT_PASSWORD ""
#define MQTT_CLIENT "qlock-table"

EspHomeClient client(
  WIFI_SSID,
  WIFI_PASSWORD,
  MQTT_BROKER,     // MQTT Broker server ip
  MQTT_USERNAME,   // Optional
  MQTT_PASSWORD,   // Optional
  MQTT_CLIENT      // Client name that uniquely identify your device is used for topic generation
);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 0, 60000);

// Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       // Central European Standard Time
Timezone CE(CEST, CET);

#define LED_PIN 5
#define LED_COUNT 114

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

/**
 * Input time in epoch format and return tm time format
 * by Renzo Mischianti <www.mischianti.org> 
 */
static tm getDateTimeByParams(long time){
    struct tm *newtime;
    const time_t tim = time;
    newtime = localtime(&tim);
    return *newtime;
}
/**
 * Input tm time format and return String with format pattern
 * by Renzo Mischianti <www.mischianti.org>
 */
static String getDateTimeStringByParams(tm *newtime, char* pattern = (char *)"%d/%m/%Y %H:%M:%S"){
    char buffer[30];
    strftime(buffer, 30, pattern, newtime);
    return buffer;
}

static uint8_t splitRGBColor ( uint32_t c, char value )
{
  switch ( value ) {
    case 'r': return (uint8_t)(c >> 16);
    case 'g': return (uint8_t)(c >>  8);
    case 'b': return (uint8_t)(c >>  0);
    default:  return 0;
  }
}
 
static uint32_t saturateColor(uint32_t c, float saturation) 
{  
  uint8_t w = floor((uint8_t)(c >> 24));
  uint8_t r = floor((uint8_t)(c >> 16));
  uint8_t g = floor((uint8_t)(c >> 8));
  uint8_t b = floor((uint8_t)c);

  uint32_t newColor = strip.Color(r + (255 - r) * saturation,g + (255 - g) * saturation, b + (255 - b) * saturation, 0);
  
  return newColor;
}

/**
 * Input time in epoch format format and return String with format pattern
 * by Renzo Mischianti <www.mischianti.org> 
 */
static String getEpochStringByParams(long time, char* pattern = (char *)"%d/%m/%Y %I:%M:%S"){
    tm newtime;
    newtime = getDateTimeByParams(time);
    return getDateTimeStringByParams(&newtime, pattern);
}

/**
 * Check if a certain value is equal or between the given limits
 */
static boolean isBetween(int value, int min, int max) {
  if (value >= min && value <= max) {
    return true;
  }

  return false;
}

/**
 * Check if a certain value is in the given list
 */
static boolean isIn(uint8_t value, uint8_t list[]) {
  for(int i = 0; i < (sizeof(list) / sizeof(list[0])); i++) {
    if (list[i] == value) {
      return true;
    }
  }

  return false;
}

/**
 * Get led number by X and Y coordinates. 0/0 is bottom left.
 */
static int getLedXY(uint8_t startX, uint8_t startY) {
  if (startY % 2 == 0) {
    return (startY) * 11 + startX;
  } else {
    return (startY + 1) * 11 - startX - 1;
  }
}

struct time {
  uint8_t hour;
  uint8_t minute;
};

int lastUpdate = 0;
uint32_t color = strip.Color(0,255,0,0);

uint8_t currentLeds[114];
uint8_t nextLeds[114];

void setup() {   
  Serial.begin(115200);

  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  client.enableDebuggingMessages();

  timeClient.begin();

  // if analog input pin 0 is unconnected, random analog
  // noise will cause the call to randomSeed() to generate
  // different seed numbers each time the sketch runs.
  // randomSeed() will then shuffle the random function.
  randomSeed(analogRead(0));
}

void onConnectionEstablished() {
  String currentValue = String(splitRGBColor(color, 'r')) + "," + String(splitRGBColor(color, 'g')) + "," + String(splitRGBColor(color, 'b'));
  client.publish(STAT, "color", currentValue);
  
  client.subscribe("color", [] (const String &payload)  {
    Serial.print("Received color: ");
    Serial.println(payload);
    
    char rgba[25];
    
    strcpy(rgba, payload.c_str());

    uint8_t red, green, blue, white, counter;
    char *token = strtok(rgba, ",");
    while( token != NULL ) {
      if (counter == 0) {
        red = atoi(token);
      } else if (counter == 1) {
        green = atoi(token);
      } else if (counter == 2) {
        blue = atoi(token);
      } else if (counter == 3) {
        white = atoi(token);
      }

      counter++;
          
      token = strtok(NULL, ",");
    }
      
    color = strip.Color(red, green, blue, white);

    struct time t = getTime();

    setNextLeds(t.hour, t.minute);

    showNextLeds();
  });
}

void loop() {
  client.loop();

  if (client.isWifiConnected()) {
    timeClient.update();
    
    struct time t = getTime();
    int currentTimeStamp = t.hour * 100 + t.minute;

    if (lastUpdate != currentTimeStamp) {
      setNextLeds(t.hour, t.minute);

      if (currentTimeStamp % 5 == 0 || lastUpdate == 0) {
        Serial.println("Start transition");
        transition();        
      } else {
        showNextLeds();
      }
      
      lastUpdate = currentTimeStamp;
    }

    delay(1000 * 1);    
  }
}

struct time getTime() {
  String timeStr = getEpochStringByParams(CE.toLocal(timeClient.getEpochTime()), "%I:%M:%S");
  struct time t;

  int indexFirst = timeStr.indexOf(':');
  t.hour = timeStr.substring(0, indexFirst).toInt();

  int indexSecond = timeStr.indexOf(':', indexFirst + 1);
  t.minute = timeStr.substring(indexFirst + 1, indexSecond).toInt();

  return t;
}

void setNextLeds(int hour, int minute) {    
    // Clear next led array
    memset(nextLeds,0,sizeof(nextLeds));
    
    String text;

    // Es 
    nextLeds[108] = 1;
    nextLeds[109] = 1;
    text += "ES";

    // IST
    nextLeds[104] = 1;
    nextLeds[105] = 1;
    nextLeds[106] = 1;
    
    text += " IST";

    if (isBetween(minute, 5, 9) || isBetween(minute, 55, 59)) {
      // FÜNF
      nextLeds[99] = 1;
      nextLeds[100] = 1;
      nextLeds[101] = 1;
      nextLeds[102] = 1;
      
      text += " FÜNF";
    } else if (isBetween(minute, 10, 14) || isBetween(minute, 50, 54)) {
      // ZEHN
      nextLeds[88] = 1;
      nextLeds[89] = 1;
      nextLeds[90] = 1;
      nextLeds[91] = 1;
      
      text += " ZEHN";
    } else if (isBetween(minute, 15, 19) || isBetween(minute, 45, 49)) {
      // VIERTEL
      nextLeds[77] = 1;
      nextLeds[78] = 1;
      nextLeds[79] = 1;
      nextLeds[80] = 1;
      nextLeds[81] = 1;
      nextLeds[82] = 1;
      nextLeds[83] = 1;
      
      text += " VIERTEL";
    } else if (isBetween(minute, 20, 24) || isBetween(minute, 40, 44)) {
      // ZWANZIG
      nextLeds[92] = 1;
      nextLeds[93] = 1;
      nextLeds[94] = 1;
      nextLeds[95] = 1;
      nextLeds[96] = 1;
      nextLeds[97] = 1;
      nextLeds[98] = 1;     
      
      text += " ZWANZIG";
    } else if (isBetween(minute, 25, 29)) {
      // FÜNF VOR HALB
      nextLeds[99] = 1;
      nextLeds[100] = 1;
      nextLeds[101] = 1;
      nextLeds[102] = 1;

      nextLeds[72] = 1;
      nextLeds[73] = 1;
      nextLeds[74] = 1;

      nextLeds[62] = 1;
      nextLeds[63] = 1;
      nextLeds[64] = 1;
      nextLeds[65] = 1;
      
      text += " FÜNF VOR HALB";
    } else if (isBetween(minute, 30, 34)) {
      // HALB
      nextLeds[62] = 1;
      nextLeds[63] = 1;
      nextLeds[64] = 1;
      nextLeds[65] = 1;
      
      text += " HALB";
    } else if (isBetween(minute, 35, 39)) {
      // FÜNF NACH HALB
      nextLeds[99] = 1;
      nextLeds[100] = 1;
      nextLeds[101] = 1;
      nextLeds[102] = 1;

      nextLeds[68] = 1;
      nextLeds[69] = 1;
      nextLeds[70] = 1;
      nextLeds[71] = 1;
      
      nextLeds[62] = 1;
      nextLeds[63] = 1;
      nextLeds[64] = 1;
      nextLeds[65] = 1;
      
      text += " FÜNF NACH HALB";
    }

    if (isBetween(minute, 5, 24)) {
      // NACH
      nextLeds[68] = 1;
      nextLeds[69] = 1;
      nextLeds[70] = 1;
      nextLeds[71] = 1;
      
      text += " NACH";
    } else if (isBetween(minute, 40, 59)) {
      nextLeds[72] = 1;
      nextLeds[73] = 1;
      nextLeds[74] = 1;
      
      text += " VOR";
    }

    if (minute >= 25) {
      hour += 1;
    }

    if (hour > 12) {
      hour = hour - 12;
    }

    if(hour == 1) {
      if (minute < 5) {
        nextLeds[46] = 1;
        nextLeds[47] = 1;
        nextLeds[48] = 1;
        
        text += " EIN";
      } else {
        nextLeds[46] = 1;
        nextLeds[47] = 1;
        nextLeds[48] = 1;
        nextLeds[49] = 1;
      
        text += " EINS";
      }
    } else if (hour == 2) {
      nextLeds[44] = 1;
      nextLeds[45] = 1;
      nextLeds[46] = 1;
      nextLeds[47] = 1;
      
      text += " ZWEI";
    } else if (hour == 3) {
      nextLeds[39] = 1;
      nextLeds[40] = 1;
      nextLeds[41] = 1;
      nextLeds[42] = 1;
      
      text += " DREI";
    } else if (hour == 4) {
      nextLeds[29] = 1;
      nextLeds[30] = 1;
      nextLeds[31] = 1;
      nextLeds[32] = 1;
      
      text += " VIER";
    } else if (hour == 5) {
      nextLeds[33] = 1;
      nextLeds[34] = 1;
      nextLeds[35] = 1;
      nextLeds[36] = 1;
      
      text += " FÜNF";
    } else if (hour == 6) {
      nextLeds[1] = 1;
      nextLeds[2] = 1;
      nextLeds[3] = 1;
      nextLeds[4] = 1;
      nextLeds[5] = 1;
      
      text += " SECHS";
    } else if (hour == 7) {
      nextLeds[49] = 1;
      nextLeds[50] = 1;
      nextLeds[51] = 1;
      nextLeds[52] = 1;
      nextLeds[53] = 1;
      nextLeds[54] = 1;
      
      text += " SIEBEN";
    } else if (hour == 8) {
      nextLeds[17] = 1;
      nextLeds[18] = 1;
      nextLeds[19] = 1;
      nextLeds[20] = 1;
      
      text += " ACHT";
    } else if (hour == 9) {
      nextLeds[25] = 1;
      nextLeds[26] = 1;
      nextLeds[27] = 1;
      nextLeds[28] = 1;
      
      text += " NEUN";
    } else if (hour == 10) {
      nextLeds[13] = 1;
      nextLeds[14] = 1;
      nextLeds[15] = 1;
      nextLeds[16] = 1;
      
      text += " ZEHN";
    } else if (hour == 11) {
      nextLeds[22] = 1;
      nextLeds[23] = 1;
      nextLeds[24] = 1;
      
      text += " ELF";
    } else if (hour == 12) {
      nextLeds[56] = 1;
      nextLeds[57] = 1;
      nextLeds[58] = 1;
      nextLeds[59] = 1;
      nextLeds[60] = 1;
      
      text += " ZWÖLF";
    }

    // Uhr
    if (minute <= 4) {
      nextLeds[8] = 1;
      nextLeds[9] = 1;
      nextLeds[10] = 1;
      
      text += " UHR";
    }

    // Dots
    int dots = minute % 5;
    for (int i = 0; i < dots; i++) {
      nextLeds[110 + i] = 1;
    }
    
    if (dots > 0) {
      text += " PUNKT: ";
      text += dots;
    }

    Serial.println(text);
}

void transition() {  
  uint8_t startPositions[11];
  uint8_t lengths[11];
  
  uint8_t iterations;

  // Get random start positions, length of lines and evaluate the toatl itarations needed to complete animation for all columns
  for (int i = 0; i < 11; i++) {
    startPositions[i] = random(9, 13);
    lengths[i] = random(5,13);

    if (startPositions[i] + lengths[i] > iterations) {
      iterations = startPositions[i] + lengths[i];
    }
  }

  uint8_t interLeds[114];
  uint8_t additionalLeds[114];  

  // Add all current active leds to the additionalLeds, they should be active till they are "hit" by the line in the same column
  for(int i = 0; i < (sizeof(currentLeds) / sizeof(currentLeds[0])); i++) {
    if (isBetween(i, 0, 109)) {
      additionalLeds[i] = currentLeds[i];      
    }
  }

  // Start the main transition cycles
  for (int i = 0; i <= iterations; i++) {   
    // Clear the intermediate led array
    memset(interLeds, 0, sizeof(interLeds));

    for (int col = 0; col < 11; col++) {
      for (int row = 0; row < lengths[col]; row++) {
        int led = getLedXY(col, startPositions[col] - i + row);

        if (isBetween(led, 0, 109)) {
          interLeds[led] = 1;
        } 
      }
    }

    strip.clear();
  
    for(int i = 0; i < (sizeof(interLeds) / sizeof(interLeds[0])); i++) {         
      if (interLeds[i] == 1) {
        additionalLeds[i] = 0;
        
        float saturation = random(0, 100) / 100.0;
        uint32_t newColor = color;
        if (saturation < 0.5 && i <= 109 && nextLeds[i] != 1) {
            newColor = saturateColor(color, saturation);
        }

        strip.setPixelColor(i, newColor);      
      }

      if (nextLeds[i] == 1) {
        additionalLeds[i] = 1;
      }
    }

    for(int i = 0; i < (sizeof(additionalLeds) / sizeof(additionalLeds[0])); i++) {
      if (additionalLeds[i] == 1) {
        float saturation = random(0, 100) / 100.0;
        uint32_t newColor = color;
        if (saturation < 0.5 && i <= 109 && nextLeds[i] != 1) {
            newColor = saturateColor(color, saturation);
        }

        strip.setPixelColor(i, newColor);
      }
    }

    strip.show();

    delay(130);
  }
}

void showNextLeds() {
  strip.clear();

  for(int i = 0; i < (sizeof(nextLeds) / sizeof(nextLeds[0])); i++) {
    if (nextLeds[i] == 1) {
      strip.setPixelColor(i, color);      
    }
  
    currentLeds[i] = nextLeds[i];
  }
  
  strip.show();
}
