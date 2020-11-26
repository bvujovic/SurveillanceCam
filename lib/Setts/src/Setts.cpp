#include "Setts.h"

bool Setts::loadSetts()
{
    ini = new EasyINI(fileName);
    if (ini->open(FMOD_READ))
    {
        setPhotoInterval(ini->getInt("photoInterval"));
        setImageResolution(ini->getInt("imageResolution"));
        setBrightness(ini->getInt("brightness"));
        setGain(ini->getInt("gain"));
        ini->close();
        return true;
    }
    else
        return false;
}

void Setts::saveSetts()
{
    if (ini->open(FMOD_WRITE))
    {
        ini->setInt("photoInterval", photoInterval);
        ini->setInt("imageResolution", imageResolution);
        ini->setInt("brightness", brightness);
        ini->setInt("gain", gain);
        ini->close();
    }
}