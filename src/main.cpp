#include <Arduino.h>
#define TEST true

#include <Blinky.h>
Blinky blnk(33, false);

#include "Setts.h"
Setts setts;

#include <AiThinkerCam.h>
AiThinkerCam cam;

#include "SD_MMC.h"
char imagePath[50]; // Ovde pakujem ime fajla/slike. Npr. img_YY_MM_DD.jpg

#include "time.h"
#include "lwip/apps/sntp.h"
struct tm ti;
time_t now;
//B RTC_DATA_ATTR int bootCount = 0;

void getTime()
{
    time(&now);
    localtime_r(&now, &ti);
    ti.tm_year += 1900;
    ti.tm_mon++;
    //? da li je sat dobro podesen izmedju 00 i 01 AM
    ti.tm_hour++;
    if (ti.tm_hour == 24)
    {
        ti.tm_hour = 0;
        ti.tm_mday++;
    }
}

void initTime()
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    char *srvr = new char[30];
    strcpy(srvr, "rs.pool.ntp.org");
    sntp_setservername(0, srvr);
    sntp_init();
    ti = {0};
    int retry = 0;
    const int retry_count = 5;
    while (ti.tm_year < 2020 && ++retry < retry_count)
    {
        delay(2000);
        getTime();
    }
}

#include <WiFiServerBasics.h>
WebServer server(80);

void wiFiOn()
{
    WiFi.mode(WIFI_STA);
    ConnectToWiFi();
    initTime();
    SetupIPAddress(60);
}

// void wiFiOff()
// {
//     WiFi.disconnect();
//     WiFi.mode(WIFI_OFF);
//     delay(100);
// }

void PreviewHandler()
{
    camera_fb_t *fb = cam.getFrameBuffer();
    if (!fb)
        server.send_P(404, "text/plain", "Camera capture failed");
    else
    {
        server.send_P(200, "image/jpeg", (char *)fb->buf, fb->len);
        cam.returnFrameBuffer(fb);
    }
}

// http://192.168.0.60/settings?imageResolution=4&brightness=0&gain=0&photoInterval=300
void SaveSettingsHandler()
{
    sensor_t *s = esp_camera_sensor_get();

    setts.setImageResolution(server.arg("imageResolution").toInt());
    s->set_framesize(s, (framesize_t)setts.imageResolution);
    setts.setBrightness(server.arg("brightness").toInt());
    s->set_brightness(s, setts.brightness);
    setts.setGain(server.arg("gain").toInt());
    s->set_gainceiling(s, (gainceiling_t)setts.gain);
    setts.setPhotoInterval(server.arg("photoInterval").toInt());

    setts.saveSetts();
    SendEmptyText(server);
}

void ListSdCardHandler()
{
    String str;
    File dir = SD_MMC.open("/");
    if (dir)
    {
        File f;
        while (f = dir.openNextFile())
        {
            if (strstr(f.name(), ".jpg")) // samo me slike interesuju
                (str += f.name()) += "\n";
            f.close();
        }
        dir.close();
        server.send(200, "text/x-csv", str);
    }
    else
        blnk.blink(500, 4);
}

void SdCardImgHandler()
{
    File f = SD_MMC.open("/" + server.arg("img"), "r");
    if (f)
    {
        server.streamFile(f, "image/jpeg");
        f.close();
    }
    else
        server.send_P(404, "text/plain", "Error reading file");
}

void GetImageName(bool serverReply)
{
    getTime();
    sprintf(imagePath, "/pic_%02d-%02d-%02d_%02d.%02d.%02d.jpg", ti.tm_year, ti.tm_mon, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
    if (true)
        server.send(200, "text/plain", imagePath);
}

enum DeviceMode
{
    DM_WebServer, // Postavke sistema, podesavanje kadra (image preview)
    DM_GetImage   // Slikanje pa spavanje
} deviceMode;

const int pinDeviceMode = 1;

ulong ms;
uint64_t secToUsec(int sec) { return (uint64_t)1000 * 1000 * sec; }

// Temporary problem. ESP will try to continue after reset.
void errorTemp()
{
    blnk.blink(500, 3);
    esp_sleep_enable_timer_wakeup(secToUsec(3));
    esp_deep_sleep_start();
}

// Fatal error. ESP cannot continue working properly.
void errorFatal() { blnk.blink(1000, 0); }

void setup()
{
#if TEST
    blnk.ledOn(true);
#endif
    Serial.begin(115200);
    Serial.println();
    ms = millis();

    if (!setts.loadSetts())
        errorFatal();

    if (!SD_MMC.begin("/sdcard", true)) // 1-bitni mod
        errorFatal();

    esp_err_t err = cam.setup((framesize_t)setts.imageResolution, PIXFORMAT_JPEG);
    if (err != ESP_OK)
        errorTemp();

    pinMode(pinDeviceMode, INPUT_PULLUP);
    deviceMode = digitalRead(pinDeviceMode) ? DM_WebServer : DM_GetImage;
    //T deviceMode = DM_WebServer;

    if (deviceMode == DM_WebServer)
    {
        wiFiOn();
        server.on("/", []() { HandleDataFile(server, "/index.html", "text/html"); });
        server.serveStatic("/webcam.png", SPIFFS, "/webcam.png"); // "image/png"
        server.on(setts.getFileName(), []() { HandleDataFile(server, setts.getFileName(), "text/plain"); });
        server.on("/preview", PreviewHandler);
        server.on("/saveSettings", SaveSettingsHandler);
        server.on("/test", []() { server.send(200, "text/plain", "SurveillanceCam test text."); });
        server.on("/listSdCard", ListSdCardHandler);
        server.on("/sdCardImg", SdCardImgHandler);
        server.on("/getImageName", []() { GetImageName(true); });
        server.on("/reset", []() {
            SendEmptyText(server);
            delay(200);
            esp_sleep_enable_timer_wakeup(secToUsec(TEST ? 5 : 15));
            esp_deep_sleep_start();
        });
        server.begin();
    }

    if (deviceMode == DM_GetImage)
    {
        delay(1000);
        camera_fb_t *fb = cam.getFrameBuffer();
        if (!fb)
            errorTemp();

        GetImageName(false);
        //T strcpy(imagePath, "/img0.jpg");

        File file = SD_MMC.open(imagePath, FILE_WRITE);
        if (!file)
            errorTemp();
        else
        {
            file.write(fb->buf, fb->len);
            file.close();
        }
        cam.returnFrameBuffer(fb);
        SD_MMC.end();

        esp_sleep_enable_timer_wakeup(secToUsec(setts.photoInterval));
        esp_deep_sleep_start();
    }

#if TEST
    blnk.ledOn(false);
#endif
}

void loop()
{
    if (deviceMode == DM_WebServer)
        server.handleClient();
    delay(100);
}