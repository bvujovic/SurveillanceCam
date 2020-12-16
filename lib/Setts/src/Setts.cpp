#include "Setts.h"

bool Setts::loadSetts()
{
    ini = new EasyINI(fileName);
    if (ini->open(FMOD_READ))
    {
        setDeviceMode(ini->getInt("deviceMode", 0));
        setDeviceName(ini->getString("deviceName", "ESP32-CAM"));
        setIpLastNum(ini->getInt("ipLastNum", 60));
        setPhotoInterval(ini->getInt("photoInterval", 300));
        setImageResolution(ini->getInt("imageResolution", 7));
        setBrightness(ini->getInt("brightness"));
        setGain(ini->getInt("gain"));
        setPhotoWait(ini->getInt("photoWait", 5));
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
        ini->setInt("deviceMode", deviceMode);
        ini->setString("deviceName", deviceName);
        ini->setInt("ipLastNum", ipLastNum);
        ini->setInt("photoInterval", photoInterval);
        ini->setInt("imageResolution", imageResolution);
        ini->setInt("brightness", brightness);
        ini->setInt("gain", gain);
        ini->setInt("photoWait", photoWait);
        ini->close();
    }
}