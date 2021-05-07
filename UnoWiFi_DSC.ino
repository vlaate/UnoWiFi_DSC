/*
  Dual Encoder Digital Setting Circles for Telescopes and the RobotDyn Uno+Wifi board.

  This is a simple gadget to connect two inexpensive incremental optical encoders via WiFi to astronomy Apps such as SkySafari Android/iOS app,
  in order to provide "Push To" style features to dobsonian style telescope mounts including tabletops.

  The RobotDyn Uno+WiFi board may seem an odd choice because it has two microcontrollers on the board: an Arduino Uno, and an ESP8266.
  The reasoning is that there have been reports in CloudyNights of dual encoder systems "missing steps" due to problems with interrupts, specially with high count encoders (which are now common).

  So I wanted to avoid those issues when upgrading from my old (Encoder+Accelerometer) DSC to dual encoders.
  My hope is that by having each microcontroller read one encoder (and communicate via Serial) the risk of dropped interrupts is reduced to basically what the 16Mhz CPU of the ATmega328 can take.

  Copyright (c) 2021 Vladimir Atehortua.
  This program is free software: you can redistribute it and/or modify
  it under the terms of the version 3 GNU General Public License as
  published by the Free Software Foundation.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License along with this program.
  If not, see <http://www.gnu.org/licenses/>
*/

/*
   Pinout:
   Board: RobotDyn Uno+Wifi
   Manufacturer: https://robotdyn.com/uno-wifi-r3-atmega328p-esp8266-32mb-flash-usb-ttl-ch340g-micro-usb.html
   Schematic and pinout: https://robotdyn.com/pub/media/0G-00005215==UNO+WiFi-R3-AT328-ESP8266-CH340G/DOCS/PINOUT==0G-00005215==UNO+WiFi-R3-AT328-ESP8266-CH340G.pdf
   Cheap clone:  https://www.aliexpress.com/item/32820234165.html

   This board contains two microcontrollers: an ATmega328 (or "Arduino UNO") and an ESP8266.
   It has 8 microswitches that have to be used in this way:
   For programming the device:
      Switches 3 and 4 ON (all the others OFF) in order to connect the Arduino UNO via USB to the computer, to program it with this sketch.
      Switches 5, 6 and 7 ON (all the others OFF) in order to connect the ESP8266 via USB to the computer, to program it with this sketch.
   For using the device as a DSC on your telescope:
      Switches 1 and 2 ON (all the others OFF) in order to connect the Arduino and the ESP8266 to each other together via Serial (note: this mode makes the USB port/cable unused).

   External devices:
   GPIO 4 and 12 of ESP8266 connected to the Altitude Encoder
   GPIO 2 and 3 of the UNO connected to the Azimuth Encoder
   6V to 9V power source connected to the power connector. It is important that it not be a 12V power source.

   Software:
   This was tested using:
     Arduino IDE 1.8.13
     ESP8266 Community  2.4.2 (in Arduino Boards Manager). This is an old version but the newer ones gave me troubles.
   To make sure you have same binaries for the eSP8266 board:
   in Arduino IDE: Tools -> Board: XXXXX -> Boards Manager -> Scroll or look for "ESP8266 Community" -> if version is lower than 2.4.2 then click the "Install" button
*/

/** Local copy of the Encoder library */
#include "IRAMEncoder.h"
/**  These IRAMEncoder.* files are Lars Rademacher's fork of Paul Stoffregen's Encoder library, with some code renaming to avoid compiler/version conflicts.
     They contain a fix to Paul Stoffregen's Encoder library that is important for the ESP8266 but hasn't been merged yet by Paul.
     I'm including them here so that users don't need to engage in the complicated process of manually replacing the library.
     Hopefully once Paul merges the ICACHE_RAM_ATTR fix on the library, this will be removed.
*/

/**
    Wi-Fi SETTINGS
    Some users want to retain Internet connection while using DSC systems.

    To facilitate this, the DSC will first attempt to connect to an external WiFi (for example: your home's WiFi, or your phone's hotspot/tethering).
    If the DSC fails to connect to the external WiFi (for example: wrong password), then the DSC will create it's own WiFi Access Point.

    Keep in mind:  When using an external WiFi or mobile hotspot, you need a means to find out the (unpredictable) IP address that the network will assign dynamically to your DSC, using methods such as checking your router's admin page / DHCP list.
    When using the internal Access Point, the IP address of the DSC (for use in SkySafari or astro app) will always be 1.2.3.4 port 4030
*/

/* EXTERNAL WiFi */
#define External_WiFi_SSID      "MyHomeWiFi"      // Name of the external WiFi network (SSID)
#define External_WiFi_Password  "myPassword"   // Password to use for connecting to the external WiFi

/* INTERNAL WiFi */
#define WiFi_Access_Point_Name  "TelescopeDSC"  // When the DSC can't connect to the external Wi-Fi it will launch an access point (WiFi SSID) with this name
#define WiFi_Access_Point_Passw "Fluorite"      // to protect the internal WiFi, this must be at least 8 characters long, otherwise the access point will launch passwordless

/* Encoders description*/
#define AZIMUTH_TOTAL_STEPS     10200           // For Azimuth, the total number of steps in a full rotation of the mount (including any gear reduction)
#define ALTITUDE_TOTAL_STEPS    10200           // For Altitude, the total number of steps in a full rotation of the mount (including any gear reduction)

#define MAX_SRV_CLIENTS         3               // How many clients can connect simultaneously to the DSC.

#if defined(ESP8266)    // This marks the beginning of the part of the program that goes into the ESP8266 half of the "RobotDyn Arduino One+WiFi" device:
#include <ESP8266WiFi.h>
WiFiServer server(4030);                        // 4030 is the TCP port Skysafari usually expects for DSCs
WiFiClient serverClients[MAX_SRV_CLIENTS];
# define BUFFER_SIZE  80                        // buffer to receive Encoder data from the other mircocontroller (Arduino)
char buf[BUFFER_SIZE];

long Azimuth_pos;     // here we store the Azimuth position (that will be retrieved via Serial from the Arduino mucrocontroller)
long Altitude_pos;    // here we store the Altitude position, read from the Altitude Encoder

// In the RobotDyn Uno+Wifi, the only "interrupt capable pins" of the ESP8266 that are exposed are GPIO4 and GPIO12:
IRAMEncoder AltitudeEncoder(4, 12);  // Create instance of the Encoder. Remember that these are GPIO4 and GPIO12 of the ESP8266, not of the Arduino (ESP pins at the lower right of the board)

void setup()
{
  Serial.begin(115200);
  delay(2000);

  WiFi.mode(WIFI_STA);          // Start in Station mode to connect to an existing WiFi
  WiFi.begin(External_WiFi_SSID, External_WiFi_Password);
  delay(5000);                  // Give it enough time (5 seconds) for the connection to the external WiFi to succeed or fail
  if (WiFi.status() != WL_CONNECTED)  // if the connection to an external WiFi failed, then launch our own WiFi Access Point:
  {
    Serial.println("Failed to connect to WiFi, starting AP");
    WiFi.mode(WIFI_AP);
    IPAddress ip(1, 2, 3, 4);
    IPAddress gateway(1, 2, 3, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(ip, gateway, subnet);
    WiFi.softAP(WiFi_Access_Point_Name, WiFi_Access_Point_Passw);
    Serial.println(WiFi.softAPIP());
  }
  else
  {
    Serial.println(WiFi.localIP());
  }

  server.begin();             // Start the TCP server that waits for Apps such as SkySafari to connect.
  server.setNoDelay(true);
}

void loop()
{
  if (readFromSerial(buf) > 0)    // if a new value arrived from the Arduino (via serial) we read it, and that's the Azimuth encoder value
  {
    Azimuth_pos = (atol(buf));
  }
  Altitude_pos = AltitudeEncoder.read();  // we read the Encoder plugged to the ESP8266

  attendTcpRequests();
}

void attendTcpRequests()
{
  uint8_t i;
  // check for new clients trying to connect
  if (server.hasClient()) {
    for (i = 0; i < MAX_SRV_CLIENTS; i++) {
      // find a free or disconnected spot:
      if (!serverClients[i] || !serverClients[i].connected()) {
        if (serverClients[i]) {
          serverClients[i].stop();
        }
        serverClients[i] = server.available();
        // the new client has connected
        break;
      }
    }
    // when there are no free/disconnected spots, reject the new client:
    if (i == MAX_SRV_CLIENTS) {
      WiFiClient serverClient = server.available();
      serverClient.stop();
    }
  }

  // check what the client sends for "Basic Encoder protocol" commands:
  for (i = 0; i < MAX_SRV_CLIENTS; i++)
  {
    if (serverClients[i] && serverClients[i].connected())
    {
      if (serverClients[i].available())
      {
        char character = serverClients[i].read(); // read the first character received, usually the command
        char response[30];
        switch (character)
        {
          case 'Q':   // the Query command, sent by SkySafari and others as the "Basic Encoder protocol" to query for encoder values.
            sprintf(response, "%i\t%i\t\n", Azimuth_pos, Altitude_pos);
            serverClients[i].println(response);
            break;
          case 'H':   // 'H' - request for encoder resolution, e.g. 10000-10000\n
            snprintf(response, 20, "%u-%u", AZIMUTH_TOTAL_STEPS, ALTITUDE_TOTAL_STEPS);
            serverClients[i].println(response);
            break;
          case 'G':   // This is not expected to be sent by SkySafari, but if a web browser is pointed to http://1.2.3.4:4030/?  then the request will start with "GET....". This was to help debug via web wifi+web browser.
            int contentSize = sprintf(response, "%i\t%i\t\n", Azimuth_pos, Altitude_pos);
            serverClients[i].write("HTTP/1.1 200 OK\r\nContent-type: text/plain\r\nContent-length:");
            serverClients[i].write(contentSize);
            serverClients[i].write("\r\n\r\n");
            serverClients[i].println(response);
            while (serverClients[i].available())
            {
              character = serverClients[i].read();
            }
            serverClients[i].flush();
            serverClients[i].stop();
            break;
        }
      }
    }
  }
}

/**
   Read the data sent by the Arduino (which contains the reading from the Azimuth encoder.
   The arduino sends the long value with Serial.println() which means means ascii and separated by "end of line" characters.
   To read it, we need to accumulate the characters received into an array before we can attempt to convert them to long with atol():
   When this returns zero it means the array is ready to be used (all digits have been received):
*/
int readFromSerial(char *buffer)
{
  static int pos = 0;
  int rpos;
  int character = Serial.read();

  if (character > 0)
  {
    switch (character)
    {
      case '\r':
        break;
      case '\n':
        rpos = pos;
        pos = 0;
        return rpos;
      default:
        if (pos < BUFFER_SIZE - 1)
        {
          buffer[pos++] = character;
          buffer[pos] = 0;
        }
    }
  }
  return 0;
}

#else   // The code for ESP8266 ends here. Now begins the code that will be executed on the "Arduino Uno" part of the RobotDyn Uno+WiFi


IRAMEncoder AzimuthEncoder(2, 3);  // in Arduino UNO only pins 2 and 3 are interrupt capable
void setup()
{
  Serial.begin(115200);
  delay(3000);
}

void loop()
{
  delay(100);                             // let's sample the encoder 10 times per second (every 100 ms)
  Serial.println(AzimuthEncoder.read());  // and send the value via Serial to the ESP8266
}
#endif
