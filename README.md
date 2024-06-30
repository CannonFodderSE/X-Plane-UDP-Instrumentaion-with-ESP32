# X-Plane-UDP-Instrumentaion-with-ESP32

Redesign of my ESP-Now implementation of X-Plane instrumentation for DIY cockpits.
After receiving feedback from my ESP-Now method, I realized I did not fully
comprehend how X-Plane UDP functioned.  With a bit better understanding I rewrote
it to only use UDP.

  Send and receives X-Plane Datarefs sent via UDP.

  X-Plane will send data back to IP address that sent the original request.
  It broadcasts a beacon signal which is used to determine its IP address.
  There is no need to hard code IP addresses.

  I've test with three different ESP32 boards with different IP 
  addresses and mix of different and overlapping Datarefs simultaneously.

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

  Tested with a basic ESP32, Heltec HTIT-WB32 (V1) with built in display.
  It had insuffient memory.

  This is a basic template to send and recieve Datarefs.

  Chosen Datarefs are stored in datarefs_array[] in the X-Plane
  configuration section below.  Function checkReceivedKey(String key, float value)
  determines what happens when a Dataref is received.

  Function checkSwitch() monitors switches and buttons.  
  switchStructure switches[] in Input/Output Configuration contains 
  Dataref and associated pin configuration.

  This code is in the public domain.
