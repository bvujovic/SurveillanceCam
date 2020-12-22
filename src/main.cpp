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

#include <AiThinkerCam.h>
AiThinkerCam cam;

#include "SD_MMC.h"
char imagePath[50];                       // Ovde pakujem ime fajla/slike. Npr. img_YY_MM_DD.jpg
const char fmtFolder[] = "/%d-%02d-%02d"; // Format za ime foldera sa slikama za dati dan: yyyy-mm-dd

#include "time.h"
#include "lwip/apps/sntp.h"
struct tm ti;                       // Tekuce/trenutno vreme.
RTC_DATA_ATTR int timeInitHour = 0; // Sat poslednjeg uzimanja tacnog vremena sa interneta.
// struct tm tiInit; // Vreme poslednjeg uzimanja tacnog vremena sa interneta.

void getTime()
{
    time_t now;
    time(&now);
    localtime_r(&now, &ti);
    ti.tm_year += 1900;
    ti.tm_mon++;
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
    if (retry >= retry_count)
        errorFatal();
    else
        timeInitHour = ti.tm_hour;
}

#include <WiFiServerBasics.h>
WebServer server(80);

void wiFiOn()
{
    WiFi.mode(WIFI_STA);
    ConnectToWiFi();
    initTime();
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
            // f.close(); //* mozda f.close() ne treba i/ili to usporava sistem
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

void GetImageName(bool serverReply)
{
    getTime();
    CreateFolderIN(ti.tm_year, ti.tm_mon, ti.tm_mday);
    if (setts.photoInterval % 60 != 0)
        sprintf(imagePath, "%s/%02d.%02d.%02d.jpg", imagePath, ti.tm_hour, ti.tm_min, ti.tm_sec);
    else
        sprintf(imagePath, "%s/%02d.%02d.jpg", imagePath, ti.tm_hour, ti.tm_min);

    if (serverReply)
        server.send(200, "text/plain", imagePath);
}

int SleepSeconds(int m, int s, int itv)
{
    if (itv % 60 != 0) // ako itv ne predstavlja tacan broj minuta, onda nema namestanja na pocetak minuta
        return itv;
    int x = itv / 60;            // 300/60 = 5
    int nextMin = m / x * x + x; // 56/5 * 5 + 5 = 11*5 + 5 = 55 + 5 = 60
    int min = nextMin - m;       // 60 - 56 = 4
    int sec = min * 60 - s;      // 4*60 - 30 = 240 - 30 = 210
    return sec;
}

void GoToSleep()
{
    getTime();
    int sec = SleepSeconds(ti.tm_min, ti.tm_sec, setts.photoInterval);
    esp_sleep_enable_timer_wakeup(secToUsec(sec));
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

void setup()
{
    // #if TEST
    //     blnk.ledOn(true);
    // #endif
    Serial.begin(115200);
    Serial.println();
    ms = millis();

    if (!setts.loadSetts())
        errorFatal();

    setts.deviceMode = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) ? DM_GetImage : DM_WebServer;

    if (!SD_MMC.begin("/sdcard", true)) // 1-bitni mod
        errorFatal();

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
        server.on("/getImageName", []() { GetImageName(true); }); //* zrelo za brisanje
        server.on("/reset", ResetHandler);
        server.begin();
        lastWebReq = millis();
    }

    getTime();
    //B if (ti.tm_hour != timeInitHour)
    int blinks = timeInitHour - ti.tm_hour + 1;
    blnk.blinkIrregular(200, 600, blinks > 5 ? 5 : blinks);

    // #if TEST
    //     blnk.ledOn(false);
    // #endif
}

void loop()
{
    if (setts.deviceMode == DM_WebServer)
    {
        server.handleClient();
        if (millis() - lastWebReq > 2 * 60 * 1000UL) //* od ovoga napraviti sett ili const
            GoToSleep();
    }

    if (setts.deviceMode == DM_GetImage && millis() - ms > setts.photoWait * 1000)
    {
#if TEST
        blnk.ledOn(true);
#endif
        camera_fb_t *fb = cam.getFrameBuffer();
        if (!fb)
            errorTemp();

        GetImageName(false);

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
        //B getTime();
        GoToSleep();
    }

    delay(100);
}