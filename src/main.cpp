#include <Arduino.h>
#define TEST true

#include "driver/rtc_io.h"

#include <Blinky.h>
Blinky led = Blinky::create();

#include "Enums.h"
DeviceMode currentMode;

#include <AiThinkerCam.h>
AiThinkerCam cam;

#include "SD_MMC.h"
char imagePath[50];                       // Ovde pakujem ime fajla/slike. Npr. img_YY_MM_DD.jpg
const char fmtFolder[] = "/%d-%02d-%02d"; // Format za ime foldera sa slikama za dati dan: yyyy-mm-dd

ulong lastWebReq;

#include "Setts.h"
Setts setts;

#include <EasyFS.h>

#include "SleepTimer.h"
RTC_DATA_ATTR SleepTimer timer(-225);
struct tm t;

#include <esp_http_server.h>

//TODO metnuti ovaj kôd u WiFiBasics
#include <WiFi.h>
#include <CredWiFi.h>

bool connectToWiFi()
{
    //T Serial.printf("\nConnecting to: %s\n", WIFI_SSID);
    //T Serial.print("\nConnecting to ");
    //T Serial.print(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int i = 0;
    while (WiFi.status() != WL_CONNECTED && i++ < 10)
    {
        delay(1000);
        //T Serial.print(".");
    }
    //T Serial.print("\nIP address: ");
    //T Serial.println(WiFi.localIP());
    return WiFi.status() == WL_CONNECTED;
}

bool setupIPAddress(int ipLastNum)
{
    // ako je tekuca i trazena IP adresa ista (proveravamo samo poslednji broj), onda nema posla za ovu funkciju
    if (WiFi.localIP()[3] == ipLastNum)
        return true;

    if (WiFi.status() == WL_CONNECTED)
    {
        IPAddress ipa(192, 168, 0, ipLastNum);
        IPAddress gateway(192, 168, 0, 254);
        IPAddress subnet(255, 255, 255, 0);
        return WiFi.config(ipa, gateway, subnet);
        //T Serial.print("IP address set: ");
        //T Serial.println(WiFi.localIP());
    }
    else
        return false;
}

void wiFiOn()
{
    WiFi.mode(WIFI_STA);
    connectToWiFi();
    timer.getNetTime(t);
    setupIPAddress(setts.ipLastNum);
}

const uint K = 1024;
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

#define PART_BOUNDARY "113316879000000000000113316879"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t streamHttpd = NULL;

#define HTTPD_TYPE_TEXT_PLAIN "text/plain"
#define HTTPD_TYPE_IMAGE_PNG "image/png"
#define HTTPD_TYPE_IMAGE_JPG "image/jpeg"

static esp_err_t streamHandler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
        return res;

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
            res = ESP_FAIL;
        else
        {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
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
        //Serial.printf("MJPG: %uB\n",(uint32_t)(_jpg_buf_len));
    }
    return res;
}

static esp_err_t captureHandler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    fb = esp_camera_fb_get();
    if (!fb)
    {
        Serial.println("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, HTTPD_TYPE_IMAGE_JPG);
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=preview.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

static esp_err_t imgHandler(httpd_req_t *req)
{
    char *fileName = (char *)req->uri;
    //T Serial.printf("imgHandler %s\n", fileName);
    while (*fileName != 0 && *fileName != '=')
        fileName++;
    if (*fileName == 0)
        return httpd_resp_send_404(req);
    fileName++;
    if (!SPIFFS.exists(fileName))
        return httpd_resp_send_404(req);
    File f = SPIFFS.open(fileName);
    size_t len = f.size();
    char *buf = (char *)malloc(len);
    f.readBytes(buf, len);
    f.close();
    httpd_resp_set_type(req, HTTPD_TYPE_IMAGE_PNG);
    esp_err_t res = httpd_resp_send(req, buf, len);
    free(buf);
    return res;
}

static esp_err_t indexHandler(httpd_req_t *req)
{
    String s = EasyFS::read("/index.html");
    return httpd_resp_sendstr(req, s.c_str());
}

char *getArgString(const char *url, const char *arg)
{
    //T Serial.println(arg);
    if (url == NULL)
        return NULL;
    char *p = strstr(url, arg);
    if (p == NULL)
        return NULL;
    p = strchr(p + 1, '=');
    if (p == NULL)
        return NULL;
    p++;
    char *pEnd = p;
    while (*pEnd != '&' && *pEnd != 0)
        pEnd++;
    char *res = (char *)malloc(pEnd - p + 1);
    strncpy(res, p, pEnd - p);
    *(res + (pEnd - p)) = 0;
    //T Serial.println(res);
    return res;
}

int getArgInt(const char *url, const char *arg)
{
    char *s = getArgString(url, arg);
    if (s == NULL)
        return 0;
    int res = atoi(s);
    free(s);
    return res;
}

///settsSave?imageResolution=4&brightness=0&gain=0&photoInterval=300
// /settsSave?deviceMode=11&imageResolution=8&brightness=0&gain=0&photoInterval=10&photoWait=2&ipLastNum=61&deviceName=Kujna
static esp_err_t settsSave(httpd_req_t *req)
{
    lastWebReq = millis();

    char *url = (char *)req->uri;
    Serial.println(url);
    setts.setDeviceMode(getArgInt(url, "deviceMode"));
    setts.setDeviceName(getArgString(url, "deviceName"));
    setts.setIpLastNum(getArgInt(url, "ipLastNum"));
    setts.setImageResolution(getArgInt(url, "imageResolution"));
    setts.setBrightness(getArgInt(url, "brightness"));
    setts.setGain(getArgInt(url, "gain"));
    setts.setPhotoInterval(getArgInt(url, "photoInterval"));
    setts.setPhotoWait(getArgInt(url, "photoWait"));
    setts.saveSetts();

    sensor_t *s = esp_camera_sensor_get();
    s->set_framesize(s, (framesize_t)setts.imageResolution);
    s->set_brightness(s, setts.brightness);
    s->set_gainceiling(s, (gainceiling_t)setts.gain);

    return httpd_resp_sendstr(req, "");
}

// Razne informacije o stanju aparata: trenutno vreme, prostor na fajl sistemu, [sd kartici]...
static esp_err_t getInfo(httpd_req_t *req)
{
#define FMT_START "%-14s"
    String s;
    // datum i vreme
    timer.getLocalTime(t);
    char str[30];
    sprintf(str, FMT_START "%04d-%02d-%02d\n", "Date", t.tm_year, t.tm_mon, t.tm_mday);
    s += str;
    sprintf(str, FMT_START "%02d:%02d:%02d\n", "Time", t.tm_hour, t.tm_min, t.tm_sec);
    s += str;
    // ukupan i slobodan storage na flash-u
    size_t total = SPIFFS.totalBytes(), used = SPIFFS.usedBytes();
    sprintf(str, FMT_START "%dkB\n", "SPIFFS total", total / K);
    s += str;
    sprintf(str, FMT_START "%dkB\n", "SPIFFS free", (total - used) / K);
    s += str;
    // heap (RAM) memorija
    sprintf(str, FMT_START "%ukB\n", "heap total", ESP.getHeapSize() / K);
    s += str;
    sprintf(str, FMT_START "%uB\n", "heap free", ESP.getFreeHeap());
    s += str;
    sprintf(str, FMT_START "%uB\n", "heap free min", ESP.getMinFreeHeap());
    s += str;
    // Serial.println(ESP.getChipCores());      2
    // Serial.println(ESP.getChipModel());      ESP32-D0WDQ5
    // Serial.println(ESP.getChipRevision());   1
    // Serial.println(ESP.getCpuFreqMHz());     240

    httpd_resp_set_type(req, HTTPD_TYPE_TEXT_PLAIN);
    return httpd_resp_sendstr(req, s.c_str());
}

static esp_err_t text(httpd_req_t *req, const char *fileName)
{
    String s = EasyFS::read(fileName);
    if (!strstr(fileName, ".html")) // ako u fileName nema ".html" -> onda vracam obican tekst
        httpd_resp_set_type(req, HTTPD_TYPE_TEXT_PLAIN);
    return httpd_resp_sendstr(req, s.c_str());
}

static esp_err_t textHandler(httpd_req_t *req)
{
    char *url = (char *)req->uri;
    //T Serial.printf("textHandler %s\n", url);
    if (strstr(url, "settsLoad"))
        return text(req, "/config.ini");
    if (strstr(url, "settsSave"))
        return settsSave(req);
    if (strstr(url, "msgs"))
        return text(req, "/msgs.log");
    if (strstr(url, "getInfo"))
        return getInfo(req);
    return httpd_resp_send_404(req);
}

// Ako ne postoji, kreira se folder sa proslenjenim datumom. Naziv foldera je u imagePath.
void createFolderIN(int y, int m, int d)
{
    sprintf(imagePath, fmtFolder, y, m, d);
    if (currentMode != DM_PeriodicCloud)
        if (!SD_MMC.exists(imagePath))
            SD_MMC.mkdir(imagePath);
}

// Uspavljivanje ESP32 CAM-a.
void goToSleep(DeviceMode dm)
{
    // Turns off the ESP32-CAM white on-board LED (flash) connected to GPIO 4
    gpio_num_t pinLedFlash = GPIO_NUM_4;
    pinMode(pinLedFlash, OUTPUT);
    digitalWrite(pinLedFlash, LOW);
    rtc_gpio_hold_en(pinLedFlash);

    //T Serial.println(dm);
    if (dm == DM_PIR)
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_12, 1);
    else // default je DM_PeriodicXXX
        esp_sleep_enable_timer_wakeup(timer.usecToSleep(t));
    esp_deep_sleep_start();
}

static esp_err_t reset(httpd_req_t *req)
{
    httpd_resp_sendstr(req, "");
    delay(500);
    goToSleep(setts.deviceMode);
    return ESP_OK;
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

// /act?sdCardList&y=2021&m=12&d=15&h=20
static esp_err_t sdCardList(httpd_req_t *req)
{
    lastWebReq = millis();
    char *url = (char *)req->uri;
    char strHour[5] = "";
    bool listFolders;
    //Bif (server.args() > 0)
    if (strstr(url, "&y=")) // ako je dat 'y' (i 'm' i 'd') -> listanje datog foldera (datum)
    {
        listFolders = false;
        //B
        // createFolderIN(server.arg("y").toInt(), server.arg("m").toInt(), server.arg("d").toInt());
        // if (server.arg("h") != "")
        //     sprintf(strHour, "/%02d.", (int)server.arg("h").toInt());
        //? zasto bih kod pretrage SD kartice kreirao folder za slike ako on vec ne postoji?
        createFolderIN(getArgInt(url, "y="), getArgInt(url, "m="), getArgInt(url, "d="));
        if (getArgInt(url, "h="))
            sprintf(strHour, "/%02d.", getArgInt(url, "h="));
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
            if (listFolders || strHour[0] == '\0' || strstr(f.name(), strHour))
                (str += f.name()) += "\n";
        //B server.send(200, "text/x-csv", str);
        httpd_resp_set_type(req, "text/x-csv");
        return httpd_resp_sendstr(req, str.c_str());
    }
    return httpd_resp_send_404(req);
}

static esp_err_t actHandler(httpd_req_t *req)
{
    char *url = (char *)req->uri;
    //T Serial.printf("actHandler %s\n", url);
    if (strstr(url, "?reset"))
        return reset(req);
    if (strstr(url, "?sdCardList"))
        return sdCardList(req);
    return httpd_resp_send_404(req);
}

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t indexUri = {.uri = "/", .method = HTTP_GET, .handler = indexHandler};
    httpd_uri_t textUri = {.uri = "/text", .method = HTTP_GET, .handler = textHandler};
    httpd_uri_t actUri = {.uri = "/act", .method = HTTP_GET, .handler = actHandler};
    httpd_uri_t imgUri = {.uri = "/img", .method = HTTP_GET, .handler = imgHandler};
    httpd_uri_t captureUri = {.uri = "/capture", .method = HTTP_GET, .handler = captureHandler};
    httpd_uri_t streamUri = {.uri = "/stream", .method = HTTP_GET, .handler = streamHandler};
    // server.on("/sdCardImg", sdCardImgHandler);
    // server.on("/delImg", delImgHandler);
    // server.on("/delFolder", delFolderHandler);

    if (httpd_start(&streamHttpd, &config) == ESP_OK)
    {
        //* config.max_uri_handlers = 12;   ne radi, i dalje je max uri handlera 8
        httpd_register_uri_handler(streamHttpd, &indexUri);
        httpd_register_uri_handler(streamHttpd, &textUri);
        httpd_register_uri_handler(streamHttpd, &actUri);
        httpd_register_uri_handler(streamHttpd, &imgUri);
        httpd_register_uri_handler(streamHttpd, &captureUri);
        httpd_register_uri_handler(streamHttpd, &streamUri);
        //T Serial.println(esp_err_to_name(httpd_register_uri_handler(streamHttpd, &resetUri)));
    }
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

    // if (currentMode == DM_PIR)
    //     pir();

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
        connectToWiFi();
    if (currentMode == DM_WebServer)
    {
        wiFiOn();
        startCameraServer();
        setupIPAddress(setts.ipLastNum);
    }
}

void loop()
{
    if (currentMode == DM_WebServer)
    {
        //B server.handleClient();
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
            connectToWiFi();
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
        // if (currentMode == DM_PeriodicCloud) // slanje slike na cloud
        // {
        //     if (!SaveToCloud::sendPhoto(fb, setts.deviceName))
        //         errorTemp();
        // }

        cam.returnFrameBuffer(fb);

#if TEST
        led.off();
#endif
        goToSleep(currentMode);
    }
    delay(10);
}
