#include <Arduino.h>
#define TEST true

#include "driver/rtc_io.h"

#include <Blinky.h>
Blinky led = Blinky::create();

#include <AiThinkerCam.h>
AiThinkerCam cam;

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
    Serial.printf("\nConnecting to: %s\n", WIFI_SSID);
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

static esp_err_t previewHandler(httpd_req_t *req)
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

static esp_err_t textHandler(httpd_req_t *req, const char *fileName)
{
    String s = EasyFS::read(fileName);
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT_PLAIN);
    return httpd_resp_sendstr(req, s.c_str());
}

static esp_err_t confHandler(httpd_req_t *req) { return textHandler(req, "/config.ini"); }
static esp_err_t msgsHandler(httpd_req_t *req) { return textHandler(req, "/msgs.log"); }

static esp_err_t indexHandler(httpd_req_t *req)
{
    //B
    // const char *html = "<html><body> <button onclick='stream.src=\"/stream\"'>Start</button> <button onclick='window.stop()'>Stop</button> <img id=\"stream\" > </body></html>";
    // return httpd_resp_sendstr(req, html);
    String s = EasyFS::read("/index.html");
    return httpd_resp_sendstr(req, s.c_str());
}

static esp_err_t getInfoHandler(httpd_req_t *req) { return textHandler(req, "/config.ini"); }

static esp_err_t imgHandler(httpd_req_t *req)
{
    char *fileName = (char *)req->uri;
    while (*fileName != 0 && *fileName != '=')
        fileName++;
    if (*fileName == 0)
        return httpd_resp_send_404(req);
    fileName++;
    if (!SPIFFS.exists(fileName))
        return httpd_resp_send_404(req);
    File f = SPIFFS.open(fileName);
    size_t len = f.size();
    uint8_t *buf = (uint8_t *)malloc(len);
    f.read(buf, len);
    //TODO f.readBytes(buf, len); da li moze da se radi sa char-ovima
    f.close();
    httpd_resp_set_type(req, HTTPD_TYPE_IMAGE_PNG);
    esp_err_t res = httpd_resp_send(req, (char *)buf, len);
    free(buf);
    return res;
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

void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t indexUri = {.uri = "/", .method = HTTP_GET, .handler = indexHandler};
    httpd_uri_t confUri = {.uri = "/conf", .method = HTTP_GET, .handler = confHandler};
    httpd_uri_t msgsUri = {.uri = "/msgs", .method = HTTP_GET, .handler = msgsHandler};
    httpd_uri_t getInfoUri = {.uri = "/getInfo", .method = HTTP_GET, .handler = getInfoHandler};
    httpd_uri_t imgUri = {.uri = "/bin", .method = HTTP_GET, .handler = imgHandler};
    httpd_uri_t previewUri = {.uri = "/preview", .method = HTTP_GET, .handler = previewHandler};
    httpd_uri_t streamUri = {.uri = "/stream", .method = HTTP_GET, .handler = streamHandler};

    if (httpd_start(&streamHttpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(streamHttpd, &indexUri);
        httpd_register_uri_handler(streamHttpd, &confUri);
        httpd_register_uri_handler(streamHttpd, &msgsUri);
        httpd_register_uri_handler(streamHttpd, &getInfoUri);
        httpd_register_uri_handler(streamHttpd, &imgUri);
        httpd_register_uri_handler(streamHttpd, &previewUri);
        httpd_register_uri_handler(streamHttpd, &streamUri);
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
    SPIFFS.begin();

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

    connectToWiFi();
    startCameraServer();
    setupIPAddress(60);
}

void loop()
{
    delay(10);
}
