#include <Arduino.h>
#define TEST true

#include <Blinky.h>
Blinky blnk(33, false);

//B #include <EasyINI.h>
#include "Setts.h"
Setts setts;

#include <AiThinkerCam.h>
AiThinkerCam cam;

#include "FS.h"
#include "SD_MMC.h"
char filePath[50]; // Ovde pakujem ime fajla. Npr. img_XX_YY.jpg

enum DeviceMode
{
    WebServer, // Postavke sistema, podesavanje kadra (image preview)
    GetImage   // Slikanje pa spavanje
} deviceMode;

const int pinDeviceMode = 15;

ulong ms;
uint64_t secToUsec(int sec) { return (uint64_t)1000 * 1000 * sec; }

// Temporary problem. ESP will try to continue after reset.
void errorTemp()
{
    blnk.blink(500, 5);
    esp_sleep_enable_timer_wakeup(secToUsec(10));
    esp_deep_sleep_start();
}

// Fatal error. ESP cannot continue working properly.
void errorFatal() { blnk.blink(1000, 0); }

void setup()
{
    Serial.begin(115200);
    Serial.println();
    ms = millis();

    pinMode(pinDeviceMode, INPUT_PULLUP);
// Serial.println(digitalRead(pinDeviceMode));
#if TEST
    deviceMode = DeviceMode::WebServer;
#else
    deviceMode = digitalRead(pinDeviceMode) ? DeviceMode::WebServer : DeviceMode::GetImage;
#endif

    if (deviceMode == DeviceMode::WebServer)
    {
        if (!setts.loadSetts())
            errorFatal();
        //...

        // EasyINI ini("/config.ini");
        // if(ini.open(FMOD_READ))
        // {
        //     int secs = ini.getInt("sleep_seconds");
        //     Serial.println(secs);
        //     ini.close();
        // }
        // else
        //     errorFatal();
    }

    if (deviceMode == DeviceMode::GetImage)
    {

        if (!SD_MMC.begin("/sdcard", true)) // 1-bitni mod
            errorFatal();

        esp_err_t err = cam.setup(FRAMESIZE_SVGA, PIXFORMAT_JPEG);
        if (err != ESP_OK)
        {
            // Serial.println("Greska pri inicijalizaciji kamere: ");
            // Serial.println(esp_err_to_name(err));
            errorTemp();
        }

        camera_fb_t *fb = cam.getFrameBuffer();
        if (!fb)
            errorTemp();

        // getTime();
        // sprintf(filePath, "/pic_%02d-%02d-%02d_%02d.%02d.%02d.jpg", ti.tm_year, ti.tm_mon, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
        // tprintln(filePath);
        strcpy(filePath, "/img0.jpg");

        File file = SD_MMC.open(filePath, FILE_WRITE);
        if (!file)
            errorTemp();
        else
        {
            Serial.println("Writting file...");
            file.write(fb->buf, fb->len);
            Serial.println("File written.");
            file.close();
        }
        cam.returnFrameBuffer(fb);
        SD_MMC.end();

        esp_sleep_enable_timer_wakeup(secToUsec(TEST ? 8 : 5 * 60));
        esp_deep_sleep_start();
    }
}

void loop()
{
    delay(100);
}