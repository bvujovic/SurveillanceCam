#include "PIR.h"
#include <WiFiServerBasics.h>

const char *PIR::tempImgName = "/temp.jpg";
String PIR::message = "";

bool PIR::pirHandler(AiThinkerCam cam, Setts setts)
{
    message = "";

    // slikanje
    esp_err_t err = cam.setup(framesize_t::FRAMESIZE_SVGA, PIXFORMAT_JPEG);
    if (err == ESP_OK)
    {
        sensor_t *s = esp_camera_sensor_get();
        s->set_brightness(s, setts.brightness);
        s->set_gainceiling(s, (gainceiling_t)setts.gain);
    }
    else
    {
        message = "cam.setup 1 fail";
        return false;
    }

    delay(2000);
    camera_fb_t *fb = cam.getFrameBuffer();
    if (!fb)
        message = "cam.getFrameBuffer 1 fail";
    else
    {
        SPIFFS.begin();
        File fp = SPIFFS.open(tempImgName, FILE_WRITE);
        if (!fp)
            message = "SPIFFS.open fail";
        else
        {
            fp.write(fb->buf, fb->len);
            fp.close();
        }
        cam.returnFrameBuffer(fb);
        cam.deinit();
    }

    // slanje na mejl
    ConnectToWiFi();
    if (!sendPhoto(tempImgName, MailClientStorageType::SPIFFS))
    {
        message = "Error sending Email: " + MailClient.smtpErrorReason() + ", " + message;
        // + message na kraju reda je za poslednju poruku napravljenu unutar sendPhoto()
        return false;
    }
    SPIFFS.end();

    // slikanje na svakih x sec i stavljanje na kt.info i/ili sd karticu
    if (!SD_MMC.begin("/sdcard", true)) // 1-bitni mod
    {
        message = "SD Card Mount Failed";
        return false;
    }
    //T Serial.println("sd card ****** cam init");
    err = cam.setup(framesize_t::FRAMESIZE_XGA, PIXFORMAT_JPEG);
    if (err != ESP_OK)
    {
        message = "cam.setup 2 fail";
        return false;
    }
    char imgName[15];
    int cntImgName = 1;

    while (millis() < 30000)
    {
        camera_fb_t *fb = cam.getFrameBuffer();
        if (!fb)
            message = "cam.getFrameBuffer 2 fail";
        else
        {
            Serial.println("pic" + String(cntImgName));
            sprintf(imgName, "/pic%d.jpg", cntImgName++);
            File fp = SD_MMC.open(imgName, FILE_WRITE);
            if (!fp)
                message = "SD_MMC.open fail";
            else
            {
                fp.write(fb->buf, fb->len);
                fp.close();
            }
            cam.returnFrameBuffer(fb);
        }
        delay(3000);
    }
    cam.deinit();
    SD_MMC.end();

    return message == "";
}

void PIR::sendCallback(SendStatus msg) { message = msg.info(); }

bool PIR::sendPhoto(const String &fileName, uint8_t storageType)
{
    if (storageType == MailClientStorageType::SPIFFS)
    {
        File fp = SPIFFS.open(tempImgName, FILE_READ);
        if (!fp)
        {
            message = "sendPhoto SPIFFS.open fail";
            return false;
        }
        if (fp.size() == 0)
        {
            message = "Pic size is 0.";
            return false;
        }
    }
    SMTPData smtpData;
    smtpData.setLogin(gmailSmtpServer, gmailSmtpServerPort, gmailAdvokatUser, gmailAdvokatPass);
    smtpData.setSender("ESP32-CAM", gmailAdvokatUser);
    smtpData.setPriority(3);
    smtpData.setSubject("ESP32-CAM Photo");
    smtpData.setMessage("<h2>Photo captured with ESP32-CAM and attached in this email.</h2>", true);
    smtpData.addRecipient("bojan.prog@gmail.com");
    //* smtpData.addRecipient("bv.net@outlook.com");
    smtpData.addAttachFile(fileName, "image/jpg");
    smtpData.setFileStorageType(storageType);
    smtpData.setSendCallback(sendCallback);
    bool res = MailClient.sendMail(smtpData);
    smtpData.empty();
    return res;
}
