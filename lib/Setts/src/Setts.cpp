#include "Setts.h"
#include <EasyINI.h>

bool Setts::loadSetts()
{
    EasyINI ini("/config.ini");
    if (ini.open(FMOD_READ))
    {
        sleepSeconds = ini.getInt("sleep_seconds");
        ini.close();
        return true;
    }
    else
        return false;
}