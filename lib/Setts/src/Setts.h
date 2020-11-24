#pragma once

#include <Arduino.h>

// Podesavanja sacuvana u config.ini fajlu.
class Setts
{
private:
    int sleepSeconds;

public:
    bool loadSetts();
    // void saveSetts();

    int getSleepSeconds() { return sleepSeconds; }
};
