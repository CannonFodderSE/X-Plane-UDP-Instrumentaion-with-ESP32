/*
  X-PlaneUDPInstrumentaion
  Author: Randy S. Bancroft
  Email: CannonFodderSE@gmail.com

  Send and receives X-Plane Datarefs sent via UDP.

  X-Plane will send data back to IP address that sent the original request.
  It broadcasts a beacon signal which is used to determine it IP address.
  There is no need to hard code IP addresses.

  I've test with three different ESP32 boards with different IP 
  addresses and mix of differennt and overlapping Datarefs simultaneously.

  Boards used for testing were a Seeed Xiao ESP32C3, Seeed Xiao ESP32S3
  and an ESP32-S3 WiFi+Bluetooth Internet Of Things Dual Type-C Development
  Board Core Board ESP32-S3-DevKit C N16R8.  See links below.
  I do not get any kick backs from the links.  They are here for reference.
  Also listed below are the 4 and 6 digit TM1637 displays used for testing outputs.

  https://www.seeedstudio.com/Seeed-XIAO-ESP32C3-p-5431.html

  https://www.seeedstudio.com/XIAO-ESP32S3-p-5627.html

  https://www.aliexpress.us/item/3256804892303816.html

  https://www.aliexpress.us/item/3256801874151364.html

  https://www.aliexpress.us/item/3256801873805909.html

  This is a basic template to send and recieve Datarefs.

  Tested with a basic ESP32, Heltec HTIT-WB32 (V1) with built in display.
  It had insuffient memory.

  Chosen Datarefs are stored in datarefs_array[] in the X-Plane
  configuration section below.  Function checkReceivedKey(String key, float value)
  determines what happens when a Dataref is received.

  Function checkSwitch() monitors switches and buttons.  
  switchStructure switches[] in Input/Output Configuration contains 
  Dataref and associated pin configuration.

  This code is in the public domain.
*/

// ESP32 by Espressif Systems
// https://github.com/espressif/arduino-esp32
// Intalled with boards manager
#include <esp_now.h>
// WiFi  installed as part of ESP32 boards package
#include "WiFi.h"
// WiFi  installed as part of ESP32 boards package
#include "AsyncUDP.h"

// espping  by dvarrel, Daniele Colanardi, Marian Craciunescu
// https://github.com/dvarrel/ESPping
// Installed by Library Manager
#include <ESPping.h>

// RingBuffer by Jean-Luc - Locoduino 
// https://github.com/Locoduino/RingBuffer
// Install through library
#include <RingBuf.h>  // RingBuffer by Jean-Luc - Locoduino https://github.com/Locoduino/RingBuffer

// TM16XX LEDs and Buttons
// https://github.com/maxint-rd/TM16xx
// Installed with Library Manager
#include <TM1637.h>
// Installed as part of TM16XX LEDs and Buttons
#include <TM16xxDisplay.h>

// ************************************************************************************************
// Roll over configuration
  // If the millis() count maxes out and rolls over to zero
  // we want to reset the board to prevent unexpected behavior
  // from happening
  // It takes 49 days of continuios use to roll over.
  ulong rollover = 0;
// End Roll over configuration
// ************************************************************************************************

// ************************************************************************************************
// X-Plane configuration
  // Define the data structure for ESPNow packet
  typedef struct rref_struct_out {
    int dref_freq;          // The number of times per second you want X-Plane to send this data
    int dref_sender_index;  // The index the customer is using to define this dataref
    char dref_string[400];  // Dataref string that you are requesting
  } rref_struct_out;

  typedef struct rref_struct_in {
    int dref_sender_index;  // Integer code (dref_sender_index) you sent in for this dataref in the structure above
    float dref_flt_value;   // Dataref value, in machine-native floating-point value, even for integers
  } rref_struct_in;

  // Create structured objects
  // Structure for in coming data
  rref_struct_in DREFIn;

  // Bbuffer for in coming beacon message
  char BECNIn[518];                                  //  Buffer for incoming beacon message

  const char DREF[5]{0x44, 0x52, 0x45, 0x46, 0x2B};  // Dataref signature, hex for "DREF+"
  const char RREF[5]{0x52, 0x52, 0x45, 0x46, 0x2C};  // Dataref signature, hex for "RREF,"
  const char BECN[4]{0x42, 0x45, 0x43, 0x4E};        // Dataref signature, hex for "BECN"
  
  int dref_freq = 10;                                // Number ot times per second you want X-Plane to send data   
  
  // Automatically update all Datarefs this board is maintaining
  ulong upDatePeriodicty = 5000;                     // Send status every upDatePeriodicty milliseconds
  ulong nextUpdate = 0;                              // When next update is to be sent to X-Plane

  //Array containing X-Plane Datarefs we are going to handle
  String datarefs_array[]{
    "sim/cockpit2/gauges/indicators/compass_heading_deg_mag",
    "sim/cockpit2/gauges/indicators/heading_electric_deg_mag_pilot",
    "sim/cockpit/radios/com1_freq_hz",
    "sim/cockpit/radios/com2_freq_hz",
    "sim/cockpit2/gauges/indicators/airspeed_kts_pilot",
    "sim/cockpit2/gauges/indicators/altitude_ft_pilot",
    "sim/cockpit2/gauges/indicators/slip_deg",
    "sim/cockpit2/annunciators/fuel_pressure[0]",
    "sim/cockpit2/gauges/indicators/heading_electric_deg_mag_copilot"
  };

  // Calculate the number of Datarefs in the datarefs_array above
  int datarefs_array_length = sizeof(datarefs_array) / sizeof(String);

// EndX-Plane configuration
// ************************************************************************************************

// ************************************************************************************************
// WiFi Configuration
  #define WIFI_CHANNEL 0  // Should match transmitter

  // SSID and password for ESP-Now Network
  // Ensure the match your Controller board
  const char *ssid = "Your WiFi";
  const char *password = "Your Password";

  IPAddress IP_Address;  //  ESP32's IP Address placeholder
  IPAddress XPlaneIP;    //  X-Plane's IP Address placeholder
  uint16_t Port = 44000;  // ESP32's port to listen on
  uint16_t XPlanePort = 49000;  //  X-Plane always listens on port 49000
  const int UDP_Packet_Size = 509;
  const int RREF_Packet_Size = 413;
  char xmitBuffer[UDP_Packet_Size];  // buffer to hold incoming and outgoing packets
  
  AsyncUDP udp;

  // UDP receiving ring buffer
  // Create a ring buffer to hold incoming UDP packets
  // Adjust maximum number of UDP packets that 
  // can be stored by changing maxBufferElements below
  const int maxBufferElements = 100;
  const int MaxMTU = 1500;  // Maximum size of UDP Packet is the size of MTU (1500)
  // Define generic UDP packet structure
  typedef struct UDPPacketStructure {
    char Packet[MaxMTU];
  } UDPPacketStructure;
  // Buffer to receive incoming UDP packets to place in ringbuffer
  UDPPacketStructure bufferDataIn;
  // Buffer to receive incoming UDP packets pulld from ringbuffer
  UDPPacketStructure bufferDataOut;;
  
  // Create the ring buffer
  RingBuf<UDPPacketStructure, maxBufferElements> UDPBuffer;
  
  // Checks when last data received
  int lastUDPReceivedTime;        // Time in millis() when last valid DREF or RREF was recieved
  const int UDPTimeout = 5000;    // Time in millis() to indicate loss of communications
  const int pingTimeOut = 10000;  // Time in millis() to indicate ping failure
// End WiFi Configuration
// ************************************************************************************************

// ************************************************************************************************
// Display Configuration
  // Module connection pins (Digital Pins) definitions
  // GPIO pin number
  // 4-digit display
  #define CLK4_1 3
  #define DIO4_1 4
  // 6-digit display
  #define CLK6_1 5
  #define DIO6_1 6
  

  //TM16xx(byte dataPin, byte clockPin, byte strobePin, byte maxDisplays, byte nDigitsUsed, bool activateDisplay=true,	byte intensity=7);
  // 4-digit display
  TM1637 module4_1(DIO4_1, CLK4_1, 4, true, 7);
  // 6-digit display
  TM1637 module6_1(DIO6_1, DIO6_1, 6, true, 7);

  // 4-digit display
  TM16xxDisplay display4_1(&module4_1, 4);  
  // 6-digit display
  TM16xxDisplay display6_1(&module6_1, 6);  

// End Display Configuration
// ************************************************************************************************

// ************************************************************************************************
// Input/Output Configuration
  // Variables that will change depending on switch state:
  bool currentState = false;
  const int isActivated = LOW;
  const int isDeactivated = HIGH;

  // Debounce variables to prevent switch noise from creating multiple switch changes on a single change
  unsigned long debounce = 0;
  unsigned long debounceThreshold = 150;

  // Switch/button array using structures
  struct switchStructure {
    char datarefKey[100];    // contains Dataref key
    int16_t activated;       // Value when switch is set
    int currentState;        // Last read state of switch
    unsigned long debounce;  // Millis time since last read
    uint8_t Pin;             // GPIO pin used for switch
    uint8_t PullUp;          // Indicate if pin configured for internal pull-up resistor
    int16_t multipleInput;   // Holds current value for switches that have multiple inputs, i.e. ignition key switch
  } switchStructure;

  // Initialize of an array of switch structures in accordance with switchStructure above
  struct switchStructure switches[] = {
    { "sim/cockpit/electrical/nav_lights_on", 1, 1, 0, 7, INPUT_PULLUP, 0 }
  };

  // Calculate the number of switches in the switches above
  int switch_array_length = sizeof(switches) / sizeof(switchStructure);

// End Input/Output Configuration
// ************************************************************************************************

void setup() {
  // Set up Serial Monitor
  Serial.begin(921600);

  //WiFi setup
  WiFiSetup();
  
  // Display setup
  displaySetup();

  // Hardware setup for switches and buttons
  hardwareSetup();
  
  // Send request for Datarefs to X-Plane
  requestRREF();
  
}

void loop() {
  // Check for millis() rollover
  checkForRollOver();

  // Check to see if we are still receiving data from X-Plane
  switch(millis() > lastUDPReceivedTime + UDPTimeout){
    case true:
      {
        // No DREFF/RREF data received for UDPTimeout milliseconds
        Serial.println("loss of signal");
        switch(Ping.ping(XPlaneIP)){ // Ping X-Plane's computer IP
          case true:                 // Computer responded
            {
              if(millis() > lastUDPReceivedTime + UDPTimeout + pingTimeOut){
                Serial.println("Loss connection to X-Plane");
                WiFi.disconnect();       // Reset WiFi
                XPlaneIP = {0,0,0,0};    // Reset X-Plane's detected IP
                delay(100);              // Wait while WiFi resets
                WiFiSetup();             // Reconnect WiFi
                requestRREF();           // Request RREF's from X-Plane
              }
            }
            break;
          default:                   // Computer failed to reply
          {
            Serial.println("Loss connection to X-Plane's Host!!!");
            WiFi.disconnect();       // Reset WiFi
            XPlaneIP = {0,0,0,0};    // Reset X-Plane's detected IP
            delay(100);              // Wait while WiFi resets
            WiFiSetup();             // Reconnect WiFi
            requestRREF();           // Request RREF's from X-Plane
        }
      }
        break;
    default:
      break;
      
    }
    
  }
  
  // Check for and get incoming data in ring buffer
  // .pop returns true if data is available
  // Temporary buffer to hold UDP packet
  switch(UDPBuffer.pop(bufferDataOut)){
    // Data available
    case true:
      {
        // Read packet header to determine type of data
        char header[5];
        memcpy(&header, bufferDataOut.Packet, 5);
        switch(!memcmp(header, DREF, 5)){
          case true:
            Serial.println("Header = DREF");
            processDREF();
            break;
          default:
            break;
        }
        switch(!memcmp(&header, RREF, 5)){
          case true:
            Serial.println("Header = RREF");
            processRREF();
            break;
          default:
            break;
        }
        
      }
      break;
    // No data available
    // Nothing to see here
    // Go about your business
    default:
      break;
  }

  // Look for changes in switches/buttons
  checkSwitch();

  // See upDatePeriodicty variable in "X-Plane configuration" above
  // Send X-Plane's and our IP addreses to serial port
  switch (nextUpdate < millis()) {
    case true:
      {
          nextUpdate = millis() + upDatePeriodicty;  // Send next update in a number of milliseconds set by updatePeriodicty
          Serial.print("My IP is ");
          Serial.print(IP_Address);
          Serial.print("\tX-Plane's IP is ");
          Serial.println(XPlaneIP);
      }
      break;
    default:
      break;
  }
}

void WiFiSetup() {
  // Set ESP32 to Wi-Fi Station mode
  WiFi.mode(WIFI_STA);
  // Set up WiFi using SSID, password and WiFi Channel from the "WiFi Configuration" section above
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed");
    while (1) {
      delay(1000);
    }
  }

  IP_Address = WiFi.localIP();
      
  // Listen for X-Plane's beacon
  if (udp.listenMulticast(IPAddress(239, 255, 1, 1), 49707)) {
    Serial.print("UDP Listening on IP: ");
    Serial.println(WiFi.localIP());
    udp.onPacket([](AsyncUDPPacket packet) {
      // Copy received packet into beacon buffer
      memcpy(&BECNIn, packet.data(), packet.length());
    
      // Check for valid becon header
      // Is it from the master X-Plane host
      switch(!memcmp(&BECNIn, BECN, 4) && BECNIn[15] == 1){
        case true:
          XPlaneIP = packet.remoteIP();
          break;
        default:
          break;
      }
    });
  }
  Serial.println();
  Serial.println("Waiting for X-Plane's beacon...");
  while (!XPlaneIP){
    delay(1000);
    Serial.println("Continuing to wait for X-Plane's beacon...");
  }
  Serial.println("X-Plane's beacon receive!");
  Serial.print("My IP is ");
  Serial.print(IP_Address);
  Serial.print("\tX-Plane's IP is ");
  Serial.println(XPlaneIP);
  
  // Configure UDP
  UDPSetup();
}

void UDPSetup() {
  // Set up UDP to listen on port set in "WiFi Configuration"
  if (udp.listen(WiFi.localIP(), Port)) {
    // If UDP packet received goto UDPReceived function
    UDPReceived();
  }
}
    
void UDPReceived() {
  // UDP packet received
  udp.onPacket([](AsyncUDPPacket packet) {
    // Get packet data size
    int DataLength = packet.length();
    // Fill temp buffer with 0xFF
    memset(&bufferDataIn, 0xFF, MaxMTU);
    // Copy header from UDP data to temp variable
    memcpy(&bufferDataIn, packet.data(), packet.length());
    switch(!UDPBuffer.push(bufferDataIn)){
      case true:
        Serial.println("Failed to add data to UDPBuffer...");
        break;
      default:
        break;
    }
    
  });
}

void processDREF(){
  // Valid Dataref packet found
  int valueStartPosition = 5;                                                        // start after header
  int floatLength = 4;                                                               // Length of floating point variable in bytes
  char bufferValue[4];                                                               // To hold Dataref's floating point value (four bytes)
  memcpy(&bufferValue, bufferDataOut.Packet + valueStartPosition, floatLength);      // Copy DREF value to bufferValue variable

  char bufferKey[200];                                                               // To hold Dataref's key name
  memcpy(&bufferKey, bufferDataOut.Packet + valueStartPosition + floatLength, 200);  // Copy DREF key to bufferkey variable

  float xp_value = 0;                                                                // Temporary variable to hold Dataref float value
  String xp_key = "";                                                                // Temporary variable to hold Dataref key value
  memcpy(&xp_value, bufferValue, sizeof(bufferValue));                               // Convert value buffer to floating point variable
  xp_key = bufferKey;                                                                // Convert key buffer to string variable
  
  checkReceivedKey(check_dataref_key(xp_key), xp_value);                             // Check to see if Dataref is one we handle and process it
}

void processRREF(){
  // Create structure to hold RREF
  rref_struct_in RREFData;
  // There can be up to 183 RREF's in a MTU
  // Loop through RREF's starting after RREF headder
  for(int d = 5; d < MaxMTU; d += 8){
    // Copy RREF into RREF structure
    memcpy(&RREFData,bufferDataOut.Packet + d, 8);
    // Unused space in data packet was filled with 0xFF in UDPReceived() when packet was received
    // Four byte integerr with MSB set will be a negative number
    // Check for valid RREF data 
    switch(RREFData.dref_sender_index >= 0){
      case true:
      {
        checkReceivedKey(RREFData.dref_sender_index, RREFData.dref_flt_value); 
      }
        break;
      default:
        // No more RREF's 
        // Set d to MaxMTU so we don't process remaining packet with no valid data
        d = MaxMTU;
        break;  
    }
  }
}

void UDPSendDREF(const char msgType[], float var, char* key) {
  // Fill transmit buffer with zeros
  memset(xmitBuffer, 0x00, UDP_Packet_Size);
  // Put message type in transmit buffer char msgType[4];
  memcpy(xmitBuffer, msgType, 4);
  // Put zeroByte in transmit buffer char after message type zeroByte[1]{ 0x00 };
  memset(xmitBuffer + 4, 0x00, 1);
  // Put Dataref value in transmit buffer float var;
  memcpy(xmitBuffer + 5, &var, 4);
  // Pur Dataref key in transmit buffer
  strncpy(xmitBuffer + 9, key, 45);
  // Send assemble packet UDP packet out
  // .writeTo returns number of bytes sent once data transfer has completed
  size_t bytesSent = udp.writeTo((uint8_t*)xmitBuffer, UDP_Packet_Size, XPlaneIP, XPlanePort);
  
}

void UDPSendRREF(int index, char* key) {
  // Fill transmit buffer with zeros
  memset(xmitBuffer, 0x00, RREF_Packet_Size);
  // Put message type, RREF, in transmit buffer char msgType[4];
  memcpy(xmitBuffer, RREF, 4);
  // Put zeroByte in transmit buffer char after message type zeroByte[1]{ 0x00 };
  memset(xmitBuffer + 4, 0x00, 1);
  // Put Dataref frequency in transmit buffer int dref_freq;
  memcpy(xmitBuffer + 5, &dref_freq, 4);
  // Put Dataref key in transmit buffer
  memcpy(xmitBuffer + 9, &index, 4);
  // Put Dataref key in transmit buffer
  strncpy(xmitBuffer + 13, key, 400);
  // Send assembled UDP packet out
  // .writeTo returns number of bytes sent once data transfer has completed
  size_t bytesSent = udp.writeTo((uint8_t*)xmitBuffer, RREF_Packet_Size, XPlaneIP, XPlanePort);
  
}

void requestRREF(){
  // Dataref request when connecting to X-Plane
  // Loop through datarefs_array in X-Plane configuration above
  for(int r = 0; r < datarefs_array_length; r++){
    String str = datarefs_array[r];
    char str_array[str.length()];
    
    // Convert string to character array
    str.toCharArray(str_array, str.length() + 1);
    char* RREFOut = str_array;
    
    UDPSendRREF(r, RREFOut);
  }
  
}

void displaySetup() {
  // Four digit display
  module4_1.clearDisplay();
  module4_1.setupDisplay(true, 7);
  
  // Six digit display
  module6_1.clearDisplay();
  module6_1.setupDisplay(true, 7);
}

void hardwareSetup() {
  // Initialize the pushbutton pins as set in the structured switches array above
  for (int p = 0; p < switch_array_length; p++) {
    // Set pin mode
    pinMode(switches[p].Pin, switches[0].PullUp);
    // Set currentState for each pin in array
    switches[p].currentState = digitalRead(switches[p].Pin);
  }
}

void checkForRollOver() {
  // Check for millis() rollover
  // Take 49 days to occur
  switch (millis() < rollover) {
    case true:
      // reboot ESP32
      ESP.restart();
      break;
    default:
      rollover = millis();
      break;
  }
}

void checkReceivedKey(int key, float value) {
  // Here we do what is need for each Dataref we handle
  // Create your own functions to light indicators,
  // send data to displays, move stepper motors or servos
  // for gauges or buzzers, etc...
  // Need to have as many caseses as array elements
  String vstring = String(value, 1);  // Convert float value to string with one decimal place
  int svalueLen = vstring.length();   // Length of vstring
  String temp ="";                    // temp string variable
  lastUDPReceivedTime = millis();
  switch (key) {
    case 0:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref

      // This example is receiving Magnetic Compass bearing
      // and displaing it on a 4-digit display designated as display4_1
      // Pad left side of Vstring with space to rigth align in 4 digit display
      while (vstring.length() < 5) {
        vstring = " " + vstring;
      }
      // Send to display
      display4_1.println(vstring);
      break;

    case 1:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref
            //Serial.println(key + " = " + String(value));
      break;

    case 2:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref
      // This example is receiving radios com1 frequency in khz
      // and displaing it on a 6-digit display designated as display6_1
      {
        vstring = String(value / 100, 2);
        // Pad left side of Vstring with space to rigth align in 6 digit display
        while (vstring.length() < 7) {
          vstring = " " + vstring;
        }

        display6_1.println(stringToSixDigit(vstring));
      }
      break;

    case 3:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref
            //Serial.println(key + " = " + String(value));
      break;

    case 4:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref
            //Serial.println(key + " = " + String(value));
      break;

    case 5:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref
            //Serial.println(key + " = " + String(value));
      break;

    case 6:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref
            //Serial.println(key + " = " + String(value));
      break;

    case 7:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref
            //Serial.println(key + " = " + String(value));
      break;

    case 8:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref
            //Serial.println(key + " = " + String(value));
      break;

    case 9:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref
            //Serial.println(key + " = " + String(value));
      break;

    case 10:  // matched Dataref in datarefs_array
      // code what to do for selected Dataref
            //Serial.println(key + " = " + String(value));
      break;

    default:
      // code what to do if no Dataref is matched
      // usually nothing
        //Serial.println(key + " = " + String(value));
      break;
  }
}

int check_dataref_key(String key) {
  // Check received Dataref key to see if it matches any we are handling
  // Returns element array index number
  // Returns -1 if Dataref is not found
  int arrayElement = 0;
  while (true) {
    switch (key == datarefs_array[arrayElement]) {
      // Found Dataref
      // Return index number in array
      case true:
        return arrayElement;
        break;
      default:
        break;
    }
    switch (arrayElement > datarefs_array_length) {
      // Reached end of array and Dataref not found
      // return -1
      case true:
        return -1;
        break;
      default:
        break;
    }
    arrayElement++;
  }
  // Catch all for other conditions
  // Should never get here
  return -1;
}

void checkSwitch() {
  // Check switches for state change
  // Send changes to Controller
  for (int s = 0; s < switch_array_length; s++) {
    int buttonState = 0;  // variable for reading the pushbutton status

    // Have we passed the bounce range since last reading?
    // If so, read button
    unsigned long timer = millis() - switches[s].debounce;
    switch (timer > debounceThreshold) {
      case true:
        {
          // read the state of the pushbutton value:
          buttonState = digitalRead(switches[s].Pin);
          int16_t activatedValue = switches[s].activated;
          int16_t currentState = switches[s].currentState;
          // ButtonState has changed
          // Update array and send appropriate message to controller
          switch (buttonState != currentState) {
            case true:
              switches[s].debounce = millis();  // Reset debounce
              switch (buttonState) {
                case isActivated:
                  switches[s].currentState = isActivated;
                  switches[s].multipleInput = activatedValue;
                  deactivateMatchingDataref(s);  // Update all matching Dataref key in array
                  SetMultiplesForDataref(s, true);
                  break;
                default:
                  switches[s].currentState = isDeactivated;
                  switches[s].multipleInput = 0;  // Update all matching Dataref key in array
                  SetMultiplesForDataref(s, false);
                  break;
              }
              break;
          }
        }
        break;
      default:
        break;
    }
  }
}

void deactivateMatchingDataref(int key) {
  // Switches may have multiple inputs
  // For example ignition switch has 5 positions with 4 inputs
  // Positions are OFF, LEFT, RIGHT, BOTH and START
  // Only one associated input is active per position
  // Inputs attached to LEFT, RIGHT, BOTH and START
  // with OFF having no input
  // loop through array and update current state for all inputs for associate Dataref
  int16_t activatedValue = switches[key].activated;
  for (int multiples = 0; multiples < switch_array_length; multiples++) {
    switch (String(switches[multiples].datarefKey) == String(switches[key].datarefKey)) {
      case true:
        switch (multiples != key) {
          case true:
            switches[multiples].currentState = isDeactivated;
            break;
          default:
            break;
        }

        break;
      default:
        break;
    }
  }
}

void SetMultiplesForDataref(int key, bool active) {
  // Switches may have multiple inputs
  // For example ignition switch has 5 positions with 4 inputs
  // Positions are OFF, LEFT, RIGHT, BOTH and START
  // Only one associated input is active per position
  // Inputs attached to LEFT, RIGHT, BOTH and START
  // with OFF having no input
  // loop through array and update multipleInput for all inputs for associate Dataref
  String input = String(switches[key].datarefKey);
  int16_t newValue = 0;
  switch(active){
    case true:
      newValue = switches[key].activated;
      break;
    default:
      break;  
  }
  for (int scan = 0; scan < switch_array_length; scan++) {
    String matrix = String(switches[scan].datarefKey);
    switch(matrix == input){
      case true:
        switches[scan].multipleInput = newValue;
        break;
      default:
        break;
    }
  }
}

void sendCurrentStatusForAllSwitches() {
  // Send the current status for all Datarefs in Array
  // Ensures X-Plane has current switch positions
  // Periodicity is set in loop statement
  // Recommend every 5 to 10 seconds to prevent over saturating X-Plane's input
  for (int current = 0; current < switch_array_length; current++) {
    String currentDataref = switches[current].datarefKey;
    int16_t currentState = switches[current].multipleInput;
    for (int lookup = switch_array_length - 1; lookup >= current; lookup--) {  // Run backwards through array so we don't look up elements already processed
      String lookupDataref = switches[lookup].datarefKey;
      switch (currentDataref == lookupDataref) {                               // Did we find another element with the same Dataref
        case true:                                                             // Found another element with the same Dataref
          switch(current == lookup){
            case true:
              //sendESPNow(currentDataref, currentState);                        // Send activated state message to Controller
              break;
            default:
              lookup = current;
              break;
          }
          break;
        default:                                                               // No other element with matching Dataref
          break;
      }
    }
  }
}

String stringToSixDigit(String in) {
  // Place digits in correct order on 6-digit TM1637 display
  // Displays I tested with the digit would be rearranged
  // Sent -> 123456
  // Displayed -> 321654
  // int decPt = in.indexOf('.');
  int decPt = in.indexOf('.');
  String temp = in.substring(0, decPt) + in.substring(decPt + 1);
  int crossRefTab[]{2, 1, 0, 5, 4, 3, 6};
  char buff[8];
  buff[0] = temp[2];
  buff[1] = temp[1];
  buff[2] = temp[0];
  buff[3] = temp[5];
  buff[4] = temp[4];
  buff[5] = temp[3];
  buff[6] = 0x00;
  buff[7] = 0x00;
  decPt = crossRefTab[decPt - 1]+1;
  memmove(buff + decPt + 1, buff + decPt, 6-decPt);
  memset (buff + decPt, '.',1);
  //Serial.println("decpt = " + String(decPt) + " | " + String(buff));
  return String(buff);
}
