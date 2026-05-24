EMI SCAN V3.3 - PlatformIO + LittleFS

Archivos:
- src/main.cpp
- data/index.html
- platformio.ini

Pasos:
1. Abre esta carpeta en VS Code + PlatformIO.
2. Compila: PlatformIO > Build.
3. Sube firmware: PlatformIO > Upload.
4. Sube la página web: PlatformIO > Upload Filesystem Image.
5. En el EMI SCAN entra al modo MATLAB/WiFi.
6. Conéctate a:
   SSID: EMI_SCAN
   PASS: 12345678
7. Abre:
   http://192.168.4.1/

Importante:
Si no haces "Upload Filesystem Image", el ESP32 sí programará el firmware, pero la página index.html no estará cargada.
