#include <Arduino.h>
#define TEST true

#include "driver/rtc_io.h"

#include <Blinky.h>
Blinky blnk(33, false);

#include "Enums.h"

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

ulong lastWebReq;

#include "Setts.h"
Setts setts;

#include "StringLogger.h"
const int cntMsgs = 5;
StringLogger<cntMsgs> *msgs;

#include <AiThinkerCam.h>
AiThinkerCam cam;

#include "SD_MMC.h"
char imagePath[50];                       // Ovde pakujem ime fajla/slike. Npr. img_YY_MM_DD.jpg
const char fmtFolder[] = "/%d-%02d-%02d"; // Format za ime foldera sa slikama za dati dan: yyyy-mm-dd

#include "SleepTimer.h"
//B RTC_DATA_ATTR SleepTimer timer(120, 4, -150);
RTC_DATA_ATTR SleepTimer timer(-200);
struct tm t;

#include <WiFiServerBasics.h>
WebServer server(80);

void wiFiOn()
{
    WiFi.mode(WIFI_STA);
    ConnectToWiFi();
    // msgs->add(String(t.getLocalTime()));
    // t.getNetTime();
    // msgs->addSave(String(t.getLocalTime()));
    timer.getNetTime(t);
    SetupIPAddress(setts.ipLastNum);
}

void PreviewHandler()
{
    lastWebReq = millis();
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
    lastWebReq = millis();
    setts.setDeviceName(server.arg("deviceName"));
    setts.setIpLastNum(server.arg("ipLastNum").toInt());
    setts.setImageResolution(server.arg("imageResolution").toInt());
    setts.setBrightness(server.arg("brightness").toInt());
    setts.setGain(server.arg("gain").toInt());
    setts.setPhotoInterval(server.arg("photoInterval").toInt());
    setts.setPhotoWait(server.arg("photoWait").toInt());
    setts.saveSetts();
    SendEmptyText(server);
    //B timer.setWakeEvery(setts.photoInterval);

    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, (framesize_t)setts.imageResolution);
    s->set_brightness(s, setts.brightness);
    s->set_gainceiling(s, (gainceiling_t)setts.gain);
}

// Ako ne postoji, kreira se folder sa proslenjenim datumom. Naziv foldera je u imagePath.
void CreateFolderIN(int y, int m, int d)
{
    sprintf(imagePath, fmtFolder, y, m, d);
    if (!SD_MMC.exists(imagePath))
        SD_MMC.mkdir(imagePath);
}

// /listSdCard?y=2020&m=12&d=15
void ListSdCardHandler()
{
    lastWebReq = millis();
    CreateFolderIN(server.arg("y").toInt(), server.arg("m").toInt(), server.arg("d").toInt());
    char strHour[5] = "";
    if (server.arg("h") != "")
        sprintf(strHour, "/%02d.", (int)server.arg("h").toInt());

    String str;
    File dir = SD_MMC.open(imagePath);

    if (dir)
    {
        File f;
        while (f = dir.openNextFile())
        {
            if (strHour[0] == '\0' || strstr(f.name(), strHour))
                (str += f.name()) += "\n";
            // f.close(); //todo mozda f.close() ne treba i/ili to usporava sistem
        }
        dir.close();
        server.send(200, "text/x-csv", str);
    }
    else
        blnk.blink(500, 4);
}

void SdCardImgHandler()
{
    lastWebReq = millis();
    File f = SD_MMC.open("/" + server.arg("img"), "r");
    if (f)
    {
        server.streamFile(f, "image/jpeg");
        f.close();
    }
    else
        server.send_P(404, "text/plain", "Error reading file");
}

void GetImageName()
{
    CreateFolderIN(t.tm_year, t.tm_mon, t.tm_mday);
    if (setts.photoInterval % 60 != 0)
        sprintf(imagePath, "%s/%02d.%02d.%02d.jpg", imagePath, t.tm_hour, t.tm_min, t.tm_sec);
    else
        sprintf(imagePath, "%s/%02d.%02d.jpg", imagePath, t.tm_hour, t.tm_min);
}

void GoToSleep()
{
    esp_sleep_enable_timer_wakeup(timer.usecToSleep(t));
    esp_deep_sleep_start();
}

void ResetHandler()
{
    SendEmptyText(server);
    delay(500);

    // Turns off the ESP32-CAM white on-board LED (flash) connected to GPIO 4
    gpio_num_t pinLedFlash = GPIO_NUM_4;
    pinMode(pinLedFlash, OUTPUT);
    digitalWrite(pinLedFlash, LOW);
    rtc_gpio_hold_en(pinLedFlash);

    GoToSleep();
}

void MsgsHandler()
{
    lastWebReq = millis();
    server.send(200, "text/plain", msgs->readFromFile());
}

void setup()
{
    ms = millis();
    if (!setts.loadSetts())
        errorFatal();
    setts.deviceMode = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) ? DM_GetImage : DM_WebServer;

    msgs = new StringLogger<cntMsgs>("/msgs.log");

    if (!SD_MMC.begin("/sdcard", true)) // 1-bitni mod
        errorFatal();

    timer.setWakeEvery(setts.photoInterval);
    timer.setNetTimeCheck(60 * 60 / setts.photoInterval); // provera vremena ide na svaki sat
    if (!timer.isInitialized())
        timer.setCountNetTimeCheck(0);

    esp_err_t err = cam.setup((framesize_t)setts.imageResolution, PIXFORMAT_JPEG);
    if (err == ESP_OK)
    {
        sensor_t *s = esp_camera_sensor_get();
        s->set_brightness(s, setts.brightness);
        s->set_gainceiling(s, (gainceiling_t)setts.gain);
    }
    else
        errorTemp();

    if (setts.deviceMode == DM_WebServer)
    {
        wiFiOn();
        server.on("/", []() { lastWebReq = millis(); HandleDataFile(server, "/index.html", "text/html"); });
        server.serveStatic("/webcam.png", SPIFFS, "/webcam.png"); // "image/png"
        server.on(setts.getFileName(), []() { lastWebReq = millis(); HandleDataFile(server, setts.getFileName(), "text/plain"); });
        server.on("/preview", PreviewHandler);
        server.on("/saveSettings", SaveSettingsHandler);
        server.on("/test", []() { server.send(200, "text/plain", "SurveillanceCam test text."); });
        server.on("/listSdCard", ListSdCardHandler);
        server.on("/sdCardImg", SdCardImgHandler);
        server.on("/reset", ResetHandler);
        server.on("/msgs", MsgsHandler);
        server.begin();
        lastWebReq = millis();
    }
}

void loop()
{
    if (setts.deviceMode == DM_WebServer)
    {
        server.handleClient();
        if (millis() - lastWebReq > 2 * 60 * 1000UL) //todo od ovoga napraviti sett ili const
            GoToSleep();
    }

    if (setts.deviceMode == DM_GetImage && millis() - ms > setts.photoWait * 1000)
    {
#if TEST
        blnk.ledOn(true);
#endif
        if (timer.shouldGetNetTime())
        {
            blnk.blink(500, 4);
            ConnectToWiFi();
            timer.getNetTime(t);
            msgs->addSave("CoefError: " + String(timer.getCoefError()));
        }
        else
        {
            blnk.blinkOk();
            timer.getLocalTime(t);
        }

        camera_fb_t *fb = cam.getFrameBuffer();
        if (!fb)
            errorTemp();

        GetImageName();

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

#if TEST
        blnk.ledOn(false);
#endif
        GoToSleep();
    }

    delay(100);
}