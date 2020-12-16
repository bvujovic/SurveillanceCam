#include <Arduino.h>
#define TEST true

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

#include "Setts.h"
Setts setts;

#include <AiThinkerCam.h>
AiThinkerCam cam;

#include "SD_MMC.h"
char imagePath[50];                       // Ovde pakujem ime fajla/slike. Npr. img_YY_MM_DD.jpg
const char fmtFolder[] = "/%d-%02d-%02d"; // Format za ime foldera sa slikama za dati dan: yyyy-mm-dd

#include "time.h"
#include "lwip/apps/sntp.h"
struct tm ti;
time_t now;

void getTime()
{
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

    setts.setDeviceName(server.arg("deviceName"));
    setts.setIpLastNum(server.arg("ipLastNum").toInt());
    setts.setImageResolution(server.arg("imageResolution").toInt());
    s->set_framesize(s, (framesize_t)setts.imageResolution);
    setts.setBrightness(server.arg("brightness").toInt());
    s->set_brightness(s, setts.brightness);
    setts.setGain(server.arg("gain").toInt());
    s->set_gainceiling(s, (gainceiling_t)setts.gain);
    setts.setPhotoInterval(server.arg("photoInterval").toInt());
    setts.setPhotoWait(server.arg("photoWait").toInt());

    setts.saveSetts();
    SendEmptyText(server);
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
    CreateFolderIN(server.arg("y").toInt(), server.arg("m").toInt(), server.arg("d").toInt());

    String str;
    //B File dir = SD_MMC.open("/");
    File dir = SD_MMC.open(imagePath);
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
    CreateFolderIN(ti.tm_year, ti.tm_mon, ti.tm_mday);
    if (TEST)
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

void SleepSecondsHandler()
{
    int m = server.arg("m").toInt();
    int s = server.arg("s").toInt();
    int itv = server.arg("itv").toInt();
    int sec = SleepSeconds(m, s, itv);

    String str = "m=" + String(m) + "s=" + String(s) + "itv=" + String(itv);
    str += "sec=" + String(sec);
    server.send(200, "text/plain", str);
}

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

    //B pinMode(pinDeviceMode, INPUT_PULLUP);
    //B deviceMode = digitalRead(pinDeviceMode) ? DM_WebServer : DM_GetImage;
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    setts.deviceMode = (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) ? DM_GetImage : DM_WebServer;

    if (!SD_MMC.begin("/sdcard", true)) // 1-bitni mod
        errorFatal();

    esp_err_t err = cam.setup((framesize_t)setts.imageResolution, PIXFORMAT_JPEG);
    if (err != ESP_OK)
        errorTemp();

    if (setts.deviceMode == DM_WebServer)
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
        server.on("/ss", SleepSecondsHandler);
        server.on("/reset", []() {
            SendEmptyText(server);
            delay(500);
            //? esp_sleep_enable_timer_wakeup(secToUsec(sec));
            esp_sleep_enable_timer_wakeup(secToUsec(TEST ? 5 : 15));
            esp_deep_sleep_start();
        });
        server.begin();
    }

#if TEST
    blnk.ledOn(false);
#endif
}

void loop()
{
    if (setts.deviceMode == DM_WebServer)
        server.handleClient();

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
        // esp_sleep_enable_timer_wakeup(secToUsec(setts.photoInterval));
        //? getTime 
        int sec = SleepSeconds(ti.tm_min, ti.tm_sec, setts.photoInterval);
        esp_sleep_enable_timer_wakeup(secToUsec(sec));
        esp_deep_sleep_start();
    }

    delay(100);
}