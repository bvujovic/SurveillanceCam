#pragma once

#include <AiThinkerCam.h>
#include "Setts.h"
#include "ESP32_MailClient.h" // update-ovati ovo sa https://github.com/mobizt/ESP-Mail-Client
#include "CredGmailAdvokat.h"
#include "SD_MMC.h"

// Staticke metode za reakciju kamerice na PIR signal.
class PIR
{
private:
    static const char *tempImgName;
    static String message;

    static void sendCallback(SendStatus msg);
    static bool sendPhoto(const String &fileName, uint8_t storageType);

public:
    // Reakcija na PIR signal.
    // @returns true - uspeh operacije, false - neuspeh.
    static bool pirHandler(AiThinkerCam cam, Setts setts);
    // Poslednja poruka o gresci.
    static String getLastMessage() { return "PIR: " + message; }
};
