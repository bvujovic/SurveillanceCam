#pragma once

#include <Arduino.h>
#include <EasyINI.h>
#include "Enums.h"

// Podesavanja sacuvana u config fajlu.
class Setts
{
private:
    EasyINI *ini;
    const char *fileName = "/config.ini";

public:
    DeviceMode deviceMode;
    void setDeviceMode(int x) { deviceMode = (DeviceMode)x; }
    void setDeviceMode(DeviceMode x) { deviceMode = x; }
    String deviceName; // Naziv uredjaja: soba, kuhinja, Perina kamera i sl.
    void setDeviceName(String s) { deviceName = s; }
    int ipLastNum; // Poslednji broj u lokalnoj IP adresi: 192.168.0.x
    void setIpLastNum(int x) { ipLastNum = constrain(x, 60, 69); }

    int photoInterval; // Cekanje (u sec) izmedju 2 slikanja.
    void setPhotoInterval(int x) { photoInterval = max(x, 5); }
    int imageResolution;
    void setImageResolution(int x) { imageResolution = constrain(x, 0, 10); }
    int brightness;
    void setBrightness(int x) { brightness = constrain(x, -2, 2); }
    int gain;
    void setGain(int x) { gain = constrain(x, 0, 6); }
    int photoWait; // Cekanje (u sec) od startovanja ESP-a do pravljenja slike. Ovo utice na kvalitet slike.
    void setPhotoWait(int x) { photoWait = constrain(x, 0, 10); }

    const char *getFileName() { return fileName; }

    // Ucitavanje podesavanja iz .ini fajla.
    bool loadSetts();
    // Cuvanje podesavanja u .ini fajl.
    void saveSetts();
};
