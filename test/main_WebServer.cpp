#include <Arduino.h>
#define TEST true

#include "driver/rtc_io.h"

#include <Blinky.h>
Blinky led = Blinky::create();

#include "PIR.h"
#include <SaveToCloud.h>

#include "Enums.h"
DeviceMode currentMode;

const ulong SEC = 1000;     // Broj milisekundi u jednoj sekundi.
const ulong MIN = 60 * SEC; // Broj milisekundi u jednom minutu.
ulong msStart;              // Vreme (u ms) pocetka izvrsavanja skeca - setup().
uint64_t secToUsec(int sec) { return (uint64_t)sec * SEC * 1000; }

// Temporary problem. ESP will try to continue after reset.
void errorTemp()
{
    led.blink(500, 3);
    esp_sleep_enable_timer_wakeup(secToUsec(3));
    esp_deep_sleep_start();
}

// Fatal error. ESP cannot continue working properly.
void errorFatal() { led.blink(SEC, 0); }

ulong lastWebReq;

#include "Setts.h"
Setts setts;

#include <EasyFS.h>

#include <AiThinkerCam.h>
AiThinkerCam cam;

#include "SD_MMC.h"
char imagePath[50];                       // Ovde pakujem ime fajla/slike. Npr. img_YY_MM_DD.jpg
const char fmtFolder[] = "/%d-%02d-%02d"; // Format za ime foldera sa slikama za dati dan: yyyy-mm-dd

#include "SleepTimer.h"
RTC_DATA_ATTR SleepTimer timer(-225);
struct tm t;

#include <WiFiServerBasics.h>
WebServer server(80);

void wiFiOn()
{
    WiFi.mode(WIFI_STA);
    ConnectToWiFi();
    timer.getNetTime(t);
    SetupIPAddress(setts.ipLastNum);
}

// Prikaz slike koju je u datom trenutku uhvatila kamera.
void previewHandler()
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
void saveSettingsHandler()
{
    lastWebReq = millis();
    //B int mode = server.arg("deviceMode").toInt();
    setts.setDeviceMode(server.arg("deviceMode").toInt());
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
void createFolderIN(int y, int m, int d)
{
    sprintf(imagePath, fmtFolder, y, m, d);
    if (currentMode != DM_PeriodicCloud)
        if (!SD_MMC.exists(imagePath))
            SD_MMC.mkdir(imagePath);
}

// Razne informacije o stanju aparata: trenutno vreme, prostor na fajl sistemu, [sd kartici]...
// /getInfo
void getInfo()
{
#define FMT_START "%-14s"
    String s;
    // vreme
    timer.getLocalTime(t);
    char str[30];
    sprintf(str, FMT_START "%04d-%02d-%02d\n", "Date", t.tm_year, t.tm_mon, t.tm_mday);
    s += str;
    sprintf(str, FMT_START "%02d:%02d:%02d\n", "Time", t.tm_hour, t.tm_min, t.tm_sec);
    s += str;
    // storage na flash-u
    size_t total = SPIFFS.totalBytes(), used = SPIFFS.usedBytes();
    sprintf(str, FMT_START "%dkB\n", "SPIFFS total", total / 1024);
    s += str;
    sprintf(str, FMT_START "%dkB\n", "SPIFFS free", (total - used) / 1024);
    s += str;
    server.send(200, "text/plain", s);
}

// /listSdCard?y=2020&m=12&d=15
void listSdCardHandler()
{
    lastWebReq = millis();

    char strHour[5] = "";
    bool listFolders;
    if (server.args() > 0) // ima argumenata -> listanje datog foldera (datum)
    {
        listFolders = false;
        //? zasto bih kod pretrage SD kartice kreirao folder za slike ako on vec ne postoji?
        createFolderIN(server.arg("y").toInt(), server.arg("m").toInt(), server.arg("d").toInt());
        if (server.arg("h") != "")
            sprintf(strHour, "/%02d.", (int)server.arg("h").toInt());
    }
    else // poziv ove funkcije bez argumenata -> listanje root foldera
    {
        listFolders = true;
        strcpy(imagePath, "/");
    }

    String str;
    File dir = SD_MMC.open(imagePath);

    if (dir)
    {
        File f;
        while (f = dir.openNextFile())
        {
            if (listFolders || strHour[0] == '\0' || strstr(f.name(), strHour))
                (str += f.name()) += "\n";
        }
        //B dir.close();
        server.send(200, "text/x-csv", str);
    }
    else
        led.blink(500, 4);
}

// Vracanje slike za dat naziv fajla.
void sdCardImgHandler()
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

// Brisanje fajla/slike prosledjenog imena. Pretpostavljam da je img ovog oblika: 2021-08-27/05.28.50.jpg
void delImgHandler()
{
    lastWebReq = millis();
    SD_MMC.remove("/" + server.arg("img"));
    SendEmptyText(server);
}

// Brisanje foldera prosledjenog imena. folder=/2021-08-27
void delFolderHandler()
{
    lastWebReq = millis();
    String folder = "/" + server.arg("folder");
    File dir = SD_MMC.open(folder);
    File f = dir;
    while (f = dir.openNextFile())
        SD_MMC.remove(f.name());
    f.close();
    SD_MMC.rmdir(folder);
    SendEmptyText(server);
}

// Upisivanje imana fajla/slike na osnovu trenutnog vremena. Kreira se novi folder za sliku ako je potrebno.
void makeImageName()
{
    createFolderIN(t.tm_year, t.tm_mon, t.tm_mday);
    if (setts.photoInterval % 60 != 0)
        sprintf(imagePath, "%s/%02d.%02d.%02d.jpg", imagePath, t.tm_hour, t.tm_min, t.tm_sec);
    else
        sprintf(imagePath, "%s/%02d.%02d.jpg", imagePath, t.tm_hour, t.tm_min);
}

// Prikaz kompletnog /msgs.log fajla.
void msgsHandler()
{
    lastWebReq = millis();
    server.send(200, "text/plain", EasyFS::readf());
}

// Uspavljivanje ESP32 CAM-a.
void goToSleep(DeviceMode dm)
{
    // Turns off the ESP32-CAM white on-board LED (flash) connected to GPIO 4
    gpio_num_t pinLedFlash = GPIO_NUM_4;
    pinMode(pinLedFlash, OUTPUT);
    digitalWrite(pinLedFlash, LOW);
    rtc_gpio_hold_en(pinLedFlash);

    if (dm == DM_PIR)
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_12, 1);
    else // default je DM_PeriodicXXX
        esp_sleep_enable_timer_wakeup(timer.usecToSleep(t));
    esp_deep_sleep_start();
}

void resetHandler()
{
    SendEmptyText(server);
    delay(500);
    goToSleep(setts.deviceMode);
}

void pir()
{
    led.blinkErrorMinor();
    if (!PIR::pirHandler(cam, setts))
    {
        EasyFS::addf(PIR::getLastMessage());
        led.blinkErrorMajor();
    }
    else
        led.blinkErrorMinor();

    goToSleep(DM_PIR);
}

#include <esp_http_server.h>
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];

    static int64_t last_frame = 0;
    if (!last_frame)
        last_frame = esp_timer_get_time();

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
        return res;

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            if (fb->format != PIXFORMAT_JPEG)
            {
                bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                esp_camera_fb_return(fb);
                fb = NULL;
                if (!jpeg_converted)
                {
                    Serial.println("JPEG compression failed");
                    res = ESP_FAIL;
                }
            }
            else
            {
                _jpg_buf_len = fb->len;
                _jpg_buf = fb->buf;
            }
        }
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
            break;
    }

    last_frame = 0;
    return res;
}
httpd_handle_t stream_httpd = NULL;

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};

    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    // config.server_port += 1;
    // config.ctrl_port += 1;
    Serial.printf("Starting stream server on port: '%d'\n", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
        httpd_register_uri_handler(stream_httpd, &stream_uri);
}

void setup()
{
    Serial.begin(115200);
    timer.getLocalTime(t);
    EasyFS::setFileName("/msgs.log");
    EasyFS::addf("wake " + String(timer.getCountNetTimeCheck()) + " - " + String(t.tm_min) + ":" + String(t.tm_sec));

    msStart = millis();
    if (!setts.loadSetts())
        errorFatal();
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_TIMER) // tajmer -> periodicno slikanje (i slanje na SD card ili cloud)
        currentMode = setts.deviceMode;
    else
        currentMode = (cause == ESP_SLEEP_WAKEUP_EXT0) ? DM_PIR : DM_WebServer;

    if (currentMode == DM_PIR)
        pir();

    if (currentMode != DM_PeriodicCloud)
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

    if (currentMode == DM_PeriodicCloud)
        ConnectToWiFi();
    if (currentMode == DM_WebServer)
    {
        wiFiOn();
        server.on("/", []()
                  {
                      lastWebReq = millis();
                      HandleDataFile(server, "/index.html", "text/html");
                  });
        server.serveStatic("/webcam.png", SPIFFS, "/webcam.png"); // "image/png"
        server.on(setts.getFileName(), []()
                  {
                      lastWebReq = millis();
                      HandleDataFile(server, setts.getFileName(), "text/plain");
                  });
        server.on("/preview", previewHandler);
        server.on("/saveSettings", saveSettingsHandler);
        server.on("/test", []()
                  { server.send(200, "text/plain", "SurveillanceCam test text."); });
        server.on("/listSdCard", listSdCardHandler);
        server.on("/sdCardImg", sdCardImgHandler);
        server.on("/delImg", delImgHandler);
        server.on("/delFolder", delFolderHandler);
        server.on("/reset", resetHandler);
        server.on("/msgs", msgsHandler);
        server.on("/getInfo", getInfo);
        // server.on("/stream", []() { stream_handler(); });
        server.begin();
        lastWebReq = millis();
    }
}

void loop()
{
    if (currentMode == DM_WebServer)
    {
        server.handleClient();
        if (millis() - lastWebReq > 2 * MIN)
            goToSleep(currentMode);
    }

    if ((currentMode == DM_PeriodicCard || currentMode == DM_PeriodicCloud) && millis() > msStart + setts.photoWait * SEC)
    {
#if TEST
        led.on();
#endif
        if (timer.shouldGetNetTime())
        {
            led.blink(500, 4);
            ConnectToWiFi();
            timer.getNetTime(t);
            EasyFS::addf("CoefError: " + String(timer.getCoefError()));
        }
        else
        {
            led.blinkOk();
            timer.getLocalTime(t);
        }

        camera_fb_t *fb = cam.getFrameBuffer();
        if (!fb)
            errorTemp();
        makeImageName();

        if (currentMode == DM_PeriodicCard) // cuvanje slike na SD karticu
        {
            File file = SD_MMC.open(imagePath, FILE_WRITE);
            if (!file)
                errorTemp();
            else
            {
                file.write(fb->buf, fb->len);
                file.close();
            }
            SD_MMC.end();
        }
        if (currentMode == DM_PeriodicCloud) // slanje slike na cloud
        {
            if (!SaveToCloud::sendPhoto(fb, setts.deviceName))
                errorTemp();
        }

        cam.returnFrameBuffer(fb);

#if TEST
        led.off();
#endif
        goToSleep(currentMode);
    }

    delay(100);
}
