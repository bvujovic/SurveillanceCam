#pragma once

#include <Arduino.h>
#include <EasyINI.h>

// Podesavanja sacuvana u config fajlu.
class Setts
{
private:
    EasyINI *ini;
    const int minPhotoInterval = 5;
    const int maxImageResolution = 10;
    const int minBrightness = -2;
    const int maxBrightness = 2;
    const int maxGain = 6;
    const int maxPhotoWait = 10;
    const char* fileName = "/config.ini";

public:
    int photoInterval; // Cekanje (u sec) izmedju 2 slikanja.
    void setPhotoInterval(int x) { photoInterval = max(x, minPhotoInterval); }
    int imageResolution;
    void setImageResolution(int res) { imageResolution = constrain(res, 0, maxImageResolution); }
    int brightness;
    void setBrightness(int x) { brightness = constrain(x, minBrightness, maxBrightness); }
    int gain;
    void setGain(int x) { gain = constrain(x, 0, maxGain); }
    int photoWait; // Cekanje (u sec) od startovanja ESP-a do pravljenja slike. Ovo utice na kvalitet slike.
    void setPhotoWait(int x) { photoWait = constrain(x, 0, maxPhotoWait); }

    const char* getFileName() { return fileName; }

    // Ucitavanje podesavanja iz .ini fajla.
    bool loadSetts();
    // Cuvanje podesavanja u .ini fajl.
    void saveSetts();
};
