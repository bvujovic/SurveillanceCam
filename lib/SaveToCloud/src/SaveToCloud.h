#pragma once

#include <Arduino.h>
#include <AiThinkerCam.h>
#include <WiFiClientSecure.h>

class SaveToCloud
{
public:
    static bool sendPhoto(camera_fb_t *fb, const String &deviceName);
};
