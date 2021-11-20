#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

// #define ROOT_DIR_PREFIX "/System"

void setup()
{
    Serial.begin(115200);
    Serial.println("\nPocetak.");
    String str;
    if (!SD_MMC.begin("/sdcard", true)) // 1-bitni mod
    {
        Serial.println("sd card error");
        return;
    }

    //* brisanje foldera bez i sa fajlovima unutri. test: nepostojeci, bez fajlova, sa fajlovima
    // 2021-08-27 sa fajlovima  -> 0,
    // 2021-08-28 prazan        -> 1
    // 2021-08-29 nepostojeci   -> 0, msg: /2021-08-29 does not exists or is a file
    {
        File dir = SD_MMC.open("/2021-08-27");
        File f = dir;
        while (f = dir.openNextFile())
        {
            Serial.println(f.name());
            SD_MMC.remove(f.name());
        }
        Serial.println("Kraj");
        bool res = SD_MMC.rmdir("/2021-08-27");
        Serial.printf("brisanje res: %d\n", res);
        return;
    }

    Serial.println("Otvaranje...");
    // File dir = SD_MMC.open("/2021-08-11"); // fajlovi za dati DIR
    File dir = SD_MMC.open("/"); // folderi na SD kartici
    Serial.println("Prolazak kroz dir/fajlove...");
    // const size_t prefixSize = sizeof(ROOT_DIR_PREFIX) - 1;
    if (dir)
    {
        //T Serial.print("DIR: ");
        //T Serial.println(dir.name());
        File f = dir;
        while (f = dir.openNextFile())
        {
            // if (strHour[0] == '\0' || strstr(f.name(), strHour))
            // (str += f.name()) += "\n";

            // donji if sprecava ispisivanje foldera /System Volume Information
            // if (strncmp(f.name(), ROOT_DIR_PREFIX, prefixSize))
            Serial.println(f.name());
        }
        //B dir.close();
        //B Serial.println(str);
        Serial.println("Kraj");
    }
    else
        Serial.println("Faaail.");

    //         sdCardImg?img=/2021-08-21/03.46.36.jpg
    // http://192.168.0.60/

    // "listSdCard?y=2021&m=8&d=21"
    // "listSdCard?y=2021&m=8&d=21&h=3"
    // "/2021-08-21/03.46.36.jpg
    // /2021-08-21/03.46.57.jpg
    // "
}

void loop()
{
    delay(100);
}