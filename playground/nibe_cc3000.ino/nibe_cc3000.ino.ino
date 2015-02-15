/**
 * openHAB, the open Home Automation Bus.
 * Copyright (c) 2010-2014, openHAB.org <admin@openhab.org>
 *
 * See the contributors.txt file in the distribution for a
 * full listing of individual contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses>.
 *
 * Additional permission under GNU GPL version 3 section 7
 *
 * If you modify this Program, or any covered work, by linking or
 * combining it with Eclipse (or a modified version of that library),
 * containing parts covered by the terms of the Eclipse Public License
 * (EPL), the licensors of this Program grant you additional permission
 * to convey the resulting work.
 *
 * ----------------------------------------------------------------------------
 *
 *  This ARDUINO application listening data from Nibe F1145/F1245 heat pumps (RS485 bus)
 *  and send valid frames to configurable IP/port address by UDP packets.
 *  Application also acknowledge the valid packets to heat pump.
 *
 *  Ethernet and RS-485 arduino shields are required.
 *
 *  Serial settings: 9600 baud, 8 bits, Parity: none, Stop bits 1
 *
 *  MODBUS module support should be turned ON from the heat pump.
 *
 *  Frame format:
 *  +----+----+----+-----+-----+----+----+-----+
 *  | 5C | 00 | 20 | CMD | LEN |  DATA   | CHK |
 *  +----+----+----+-----+-----+----+----+-----+
 *
 *  Checksum: XOR
 *
 *  When valid data is received (checksum ok),
 *   ACK (0x06) should be sent to the heat pump.
 *  When checksum mismatch,
 *   NAK (0x15) should be sent to the heat pump.
 *
 *  If heat pump does not receive acknowledge in certain time period,
 *  pump will raise an alarm and alarm mode is activated.
 *  Actions on alarm mode can be configured. The different alternatives
 *  are that the Heat pump stops producing hot water (default setting)
 *  and/or reduces the room temperature.
 *
 *  Author: pauli.anttila@gmail.com
 *
 *
 *  2.11.2013	v1.00	Initial version.
 *  3.11.2013   v1.01   
 */

#include <SPI.h> 
#include <Adafruit_CC3000.h>
//#include <EthernetUdp.h>
#include <avr/wdt.h>

// ######### CONFIGURATION #######################

// enable debug printouts, listen printouts e.g. via netcat (nc -l -u 50000)
#define ENABLE_DEBUG

// the media access control (ethernet hardware) address for the shield
//byte mac[] = { 0x90, 0xA2, 0xDA, 0x0F, 0x5D, 0xE1 };


//the IP address for the shield
//byte ip[] = { 192, 168, 1, 201 };    

// target IP address and port where UDP packats are send
IPAddress target_ip(192, 168, 1, 110);
unsigned int udp_port = 9998;

// CC3000 interrupt and control pins
#define ADAFRUIT_CC3000_IRQ   3 // MUST be an interrupt pin!
#define ADAFRUIT_CC3000_VBAT  7 // These can be
#define ADAFRUIT_CC3000_CS   10 // any two pins
// Hardware SPI required for remaining pins.
// On an UNO, SCK = 13, MISO = 12, and MOSI = 11
Adafruit_CC3000 cc3000 = Adafruit_CC3000(
  ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT,
  SPI_CLOCK_DIVIDER);

// WiFi access point credentials
#define WLAN_SSID     "SmileRoom_garage"  // 32 characters max
#define WLAN_PASS     "nockefema"
#define WLAN_SECURITY WLAN_SEC_WPA2 // WLAN_SEC_UNSEC/WLAN_SEC_WEP/WLAN_SEC_WPA/WLAN_SEC_WPA2


Adafruit_CC3000_Client
  client;        // For WiFi connections


// ######### VARIABLES #######################

// direction change pin for RS-485 port
#define directionPin 2

#ifdef ENABLE_DEBUG
char verbose = 0;
char debug_buf[100];
#endif

// state machine states
enum e_state {
  STATE_WAIT_START,
  STATE_WAIT_DATA,
  STATE_OK_MESSAGE_RECEIVED,
  STATE_CRC_FAILURE,
};

e_state state = STATE_WAIT_START;

//EthernetUDP udp;

// message buffer for RS-485 communication  
#define MAX_DATA_LEN 255
byte buffer[MAX_DATA_LEN];

byte index = 0;

// ######### SETUP #######################

void setup()  {
  wdt_enable (WDTO_1S); /* irq */
  
/* cc3000 */
  client = cc3000.connectUDP(target_ip,udp_port);
  
/* slut cc3000 */

  pinMode(directionPin, OUTPUT);
  digitalWrite(directionPin, LOW);
  
  Serial.begin(9600, SERIAL_8N1);
  
//  Ethernet.begin(mac,ip);
//  udp.begin(udp_port);
  
  #ifdef ENABLE_DEBUG
  if (verbose) {
    debugPrint("\nstarted\n");
  }
  #endif
}

// ######### MAIN LOOP #######################

void loop() {
  
  wdt_reset ();
  
/*  #ifdef ENABLE_DEBUG
  int packetSize = udp.parsePacket();
  if(packetSize)
  {
    verbose = udp.read();
  }
  #endif*/
  
  switch(state) {
  
    case STATE_WAIT_START:
      if (Serial.available() > 0) {
        byte b = Serial.read();
  
        #ifdef ENABLE_DEBUG
        if (verbose > 2) {
          sprintf(debug_buf, "%02x", b);
          debugPrint(debug_buf);
        }
        #endif
        
        if (b == 0x5C) {
          buffer[0] = b;
          index = 1;
          state = STATE_WAIT_DATA;
          
          #ifdef ENABLE_DEBUG
          if (verbose > 1) {
            debugPrint("Frame start found\n");
          }
          #endif
        }
      }
      break;
      
    case STATE_WAIT_DATA:
      if (Serial.available() > 0) {
        byte b = Serial.read();
  
        #ifdef ENABLE_DEBUG
        if (verbose > 2) {
          sprintf(debug_buf, "%02x", b);
          debugPrint(debug_buf);
        }
        #endif
      
        if (index >= MAX_DATA_LEN)
        {
          // too long message
          state = STATE_WAIT_START;
        }
        else
        {
          buffer[index++] = b;
          
          int msglen = checkNibeMessage(buffer, index);
          
          #ifdef ENABLE_DEBUG
          if (verbose > 1) {
            sprintf(debug_buf, "\ncheckMsg=%d\n", msglen);
            debugPrint(debug_buf);
          }
          #endif
              
          switch (msglen)
          {
            case 0:   break; // Ok, but not ready
            case -1:  state = STATE_WAIT_START; break; // Invalid message
            case -2:  state = STATE_CRC_FAILURE; break; // Checksum error
            default:  state = STATE_OK_MESSAGE_RECEIVED; break;
          }
        }
      }
      break;

    case STATE_CRC_FAILURE:
      #ifdef ENABLE_DEBUG
      if (verbose) {
        debugPrint("CRC failure\n");
      }
      #endif
      
      sendAck();
      state = STATE_WAIT_START;
      break;
    
    case STATE_OK_MESSAGE_RECEIVED:
      #ifdef ENABLE_DEBUG
      if (verbose) {
        debugPrint("Message received\n");
      }
      #endif
      
      sendAck();
      
      // send UDP packet if message is a data packet
      // if data contains 0x5C (start character), message len can be bigger than 0x50 
      if (buffer[0] == 0x5C && buffer[1] == 0x00 && buffer[2] == 0x20 && buffer[3] == 0x68 && buffer[4] >= 0x50)
        sendUdpPacket(buffer, index);
      state = STATE_WAIT_START;
      break;
  }

}


// ######### FUNCTIONS #######################

/*
 * Return:
 *  >0 if valid message received (return message len)
 *   0 if ok, but message not ready
 *  -1 if invalid message
 *  -2 if checksum fails
 */
int checkNibeMessage(const byte* const data, byte len)
{
    #ifdef ENABLE_DEBUG
    if (verbose > 2) {
      sprintf(debug_buf, "\nlen=%u\n", len);
      debugPrint(debug_buf);
    }
    #endif
    
    if (len <= 0)
        return 0;
    
    if (len >= 1)
    {
        if (data[0] != 0x5C)
            return -1;
        
        if (len >= 3)
        {
            if (data[1] != 0x00 && data[2] != 0x20)
                return -1;
        }
        
        if (len >= 6)
        {
            int datalen = data[4];
            
            if (len < datalen + 6)
                return 0;
            
            byte checksum = 0;
            
            // calculate XOR checksum
            for(int i = 2; i < (datalen + 5); i++)
                checksum ^= data[i];
            
            byte msg_checksum = data[datalen + 5];
            
            #ifdef ENABLE_DEBUG
            if (verbose > 1) {
              sprintf(debug_buf, "\nchecksum=%02x, msg_checksum=%02x\n", checksum, msg_checksum);
              debugPrint(debug_buf);
            }
            #endif
                     
            if (checksum != msg_checksum) {
              
              // check special case, if checksum is 0x5C (start character), 
              // heat pump seems to send 0xC5 checksum
              if (checksum != 0x5C && msg_checksum != 0xC5)
                return -2;
            }  
            
            return datalen + 6;
        }
        
    }
    
    return 0;
    
}

void sendAck()
{
  #ifdef ENABLE_DEBUG
  if (verbose) {
    debugPrint("Send ACK\n");
  }
  #endif
  
  digitalWrite(directionPin, HIGH);
  delay(1);
  Serial.write(0x06);
  Serial.flush();
  delay(1);
  digitalWrite(directionPin, LOW);
}


void sendUdpPacket(const byte* const data, int len) {
  
  #ifdef ENABLE_DEBUG
  if (verbose) {
    sprintf(debug_buf, "Sending UDP packet, len=%d\n", len);
    debugPrint(debug_buf);
    if (verbose > 2) {
      int i;
      for (i=0; i<len; i++) {
        sprintf(debug_buf, "%02x", data[i]);
        debugPrint(debug_buf);
      }
      debugPrint("\n");
    }
  }
  #endif
  
 /* udp.beginPacket(target_ip, udp_port);
  udp.write(data, len);
  udp.endPacket();*/ 
  client.write(data,sizeof(data));
}

#ifdef ENABLE_DEBUG
void debugPrint(char* data) {
/*  udp.beginPacket(target_ip, 50000);
  udp.write(data);
  udp.endPacket(); */

}
#endif
