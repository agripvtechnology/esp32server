# CHANGELOG

This document displays the full changelog of major and minor changes to the codebase.
 
---
## v1 030921
- Initial commit
- Modified codebase from WPA2 Enterprise Example and Restful API Server Example
- Established functional connection with WPA2 Enterprise connection of school network
- Established functional webserver under above connection
- mDNS not working; unable to connect to mDNS hostname
- Linkage with sensors required
- Aesthetic modifications to webpage required

### **Modifications**
main\CMakeLists.txt: 
- Compile 'rest-server.c' and 'main.c' into the same binary.
- Add condition for mounting image onto SPIFFS partition (based on Restful API Server Example).

main\component&#46;mk:
- Certificates from WPA2 Enterprise Example included.

main\Kconfig.projbuild:
- Combined Kconfig files from both examples into a single menu. 
- Set default: VALIDATE_SERVER_CERT = n, WEB_DEPLOY_MODE = WEB_DEPLOY_SF. 
    - NTU Server does not require validation of server cert.
    - Semihost & SD card deployment are not applicable for this scenario (since no JTAG/SD adapter is attached to the board).

main\main.c: 
- Merged both main.c files from both examples.
- Removed loop for IP address check, runs after delay (else it will overlap with other processes).

main\wpa2*:
- Certificates used for WPA2 Enterprise authentication (generated, see example for details).
    - Since VALIDATE_SERVER_CERT = n, don't think these are still in use. Included in binary just to be safe.

CMakeLists.txt:
- Changed project name.

Makefile:
- Changed project name.
- Add condition for mounting image onto SPIFFS partition (based on Restful API Server Example).

partitions.csv: 
- Added based on Restful API Server Example.

sdkconfig.defaults:
- Default flash size changed: 2MB --> 4MB
    - Based on partition table, total size is ~3MB, hence flash size has to be large enough to hold it. Caveat is that the flash size of the ESP32 limits the maximum size of the webpage.
- Partition Table specified: 'partitions.csv'
    - Set to use custom partition table 'partitions.csv'
---
