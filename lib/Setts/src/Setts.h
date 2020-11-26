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
    const char* fileName = "/config.ini";

public:
    int photoInterval;
    void setPhotoInterval(int x) { photoInterval = max(x, minPhotoInterval); }
    int imageResolution;
    void setImageResolution(int res) { imageResolution = constrain(res, 0, maxImageResolution); }
    int brightness;
    void setBrightness(int x) { brightness = constrain(x, minBrightness, maxBrightness); }
    int gain;
    void setGain(int x) { gain = constrain(x, 0, maxGain); }

    const char* getFileName() { return fileName; }

    // Ucitavanje podesavanja iz .ini fajla.
    bool loadSetts();
    // Cuvanje podesavanja u .ini fajl.
    void saveSetts();
};
