#ifndef APWIFIEEPROMMODE_H
#define APWIFIEEPROMMODE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

WebServer server(80);

// Lee un string terminado en '\0'. Una EEPROM vacia (0xFF) devuelve "".
String leerStringDeEEPROM(int direccion)
{
    String cadena = "";
    for (int i = 0; i < 64; i++)
    {
        uint8_t c = EEPROM.read(direccion + i);
        if (c == 0 || c == 0xFF) break;
        cadena += (char)c;
    }
    return cadena;
}

void escribirStringEnEEPROM(int direccion, String cadena)
{
    int n = cadena.length();
    if (n > 63) n = 63;
    for (int i = 0; i < n; i++)
    {
        EEPROM.write(direccion + i, cadena[i]);
    }
    EEPROM.write(direccion + n, '\0');
    EEPROM.commit();
}

void handleRoot()
{
    String html = "<html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'></head><body>";
    html += "<form method='POST' action='/wifi'>";
    html += "Red Wi-Fi: <input type='text' name='ssid'><br>";
    html += "Contraseña: <input type='password' name='password'><br>";
    html += "<input type='submit' value='Conectar'>";
    html += "</form></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
}

int posW = 50;

void handleWifi()
{
    String ssid = server.arg("ssid");
    String password = server.arg("password");

    Serial.print("Conectando a la red Wi-Fi ");
    Serial.println(ssid);   // ya no se imprime la contraseña

    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());   // sin forzar canal

    int cnt = 0;
    while (WiFi.status() != WL_CONNECTED && cnt < 8)
    {
        delay(1000);
        Serial.print(".");
        cnt++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        // Alterna entre dos ranuras (0 y 50) para guardar las 2 ultimas redes
        String varsave = leerStringDeEEPROM(300);
        if (varsave == "a") {
            posW = 0;
            escribirStringEnEEPROM(300, "b");
        } else {
            posW = 50;
            escribirStringEnEEPROM(300, "a");
        }
        escribirStringEnEEPROM(0 + posW, ssid);
        escribirStringEnEEPROM(100 + posW, password);

        Serial.println("Conexion establecida");
        server.send(200, "text/plain; charset=utf-8", "Conexion establecida");
    }
    else
    {
        Serial.println("Conexion no establecida");
        server.send(200, "text/plain; charset=utf-8", "Conexion no establecida");
    }
}

// Intenta conectar con una de las 2 redes guardadas
bool lastRed()
{
    for (int psW = 0; psW <= 50; psW += 50)
    {
        String usu = leerStringDeEEPROM(0 + psW);
        String cla = leerStringDeEEPROM(100 + psW);

        // Saltar ranuras vacias o invalidas
        if (usu.length() == 0 || usu.length() > 32) continue;

        Serial.print("Intentando red: ");
        Serial.println(usu);

        WiFi.disconnect();
        WiFi.begin(usu.c_str(), cla.c_str());

        int cnt = 0;
        while (WiFi.status() != WL_CONNECTED && cnt < 5)
        {
            delay(1000);
            Serial.print(".");
            cnt++;
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println();
            Serial.print("Conectado, IP: ");
            Serial.println(WiFi.localIP());
            return true;
        }
    }
    return false;
}

void initAP(const char *apSsid, const char *apPassword, void (*registrarExtra)() = nullptr)
{
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid, apPassword);

    server.on("/config", handleRoot);              // formulario WiFi
    server.on("/wifi", HTTP_POST, handleWifi);
    if (registrarExtra) registrarExtra();          // rutas de la pagina de monitoreo

    server.begin();
    Serial.println("Servidor web iniciado en 192.168.4.1");
}

void loopAP()
{
    server.handleClient();
}

#endif
