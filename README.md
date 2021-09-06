# Agri-PV Webserver
##### Current version: v1

This code is based upon the **ESP-IDF framework** coded with **Visual Studio Code** on **Windows 10**. It is verified working with the **ESP32-DEVKIT V1**, and has been modified from the [WPA2 Enterprise Example](https://github.com/espressif/esp-idf/tree/master/examples/wifi/wpa2_enterprise) and the [HTTP Restful API Server Example](https://github.com/espressif/esp-idf/tree/master/examples/protocols/http_server/restful_server). 

---
### Modifications
Changes are detailed in the CHANGELOG file.
- Initial commit
- Modified codebase from WPA2 Enterprise Example and Restful API Server Example
- Established functional connection with WPA2 Enterprise connection of school network
- Established functional webserver under above connection
- mDNS not working; unable to connect to mDNS hostname
- Linkage with sensors required
- Aesthetic modifications to webpage required
---
### Prerequisites
- ESP-IDF Framework to be [installed](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html)
- NPM (via Node.js) to be [installed](https://nodejs.org/en/download/)
---
### Build Instructions
Assuming the pre-requisites have been installed successfully with default configurations:

*Website*
- After the webpage design work has been completed, the webpage has to be compiled.
    
    a. Navigate to your local directory and open a command prompt there.
    b. Run the following commands:

        npm install
        npm run build
    
    c. Once the `dist` directory has been created in `front\web-demo`, the binary is ready to compile.

*ESP32*
1. Launch the ESP-IDF Command Prompt and navigate to your  directory containing the project.

2. Set the ESP32 chip as the target and run the project configuration utility menuconfig by running:

        idf.py set-target esp32
        idf.py menuconfig

3. Navigate to the **WPA2 Enterprise Configuration** menu item and key in your user details and password.

4. Build the project by running: 

        idf.py build

5. Ensure your device is connected to your computer, and flash the binary to your device (where `PORT` = your ESP32's serial port name -- refer to the [ESP-IDF setup](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/#get-started-connect)):

        idf.py -p PORT flash

6. Check if the code is working by running:

        idf.py -p PORT monitor

7. You can combine steps 4-6 by running:

        idf.py -p PORT flash monitor