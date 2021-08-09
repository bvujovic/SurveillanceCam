#include <SaveToCloud.h>

bool SaveToCloud::sendPhoto(camera_fb_t *fb, const String& deviceName)
{
    String getAll;
    String getBody;

    WiFiClientSecure client;
    String serverName = "imageuploadbv.azurewebsites.net";
    IPAddress ipAddress(13, 69, 68, 18);

    Serial.println("Connecting to server...");
    client.setInsecure();
    if (!client.connect(ipAddress, 443)) // ili if (!client.connect(serverName.c_str(), 443))
    {
        Serial.println("Connection to server failed.");
        return false;
    }

    Serial.println("Connection successful!");
    String head = "--boundary\r\n"
                  //   "Content-Disposition: form-data; name=\"Name\"\r\n\r\nt.txt\r\n--boundary\r\n"
                  //   "Content-Disposition: form-data; name=\"File\"; filename=\"zec.txt\"\r\n\r\n";
                  //?   "Content-Disposition: form-data; name=\"Name\"\r\n\r\nzec.jpg\r\n--boundary\r\n"
                  "Content-Disposition: form-data; name=\"Camera\"\r\n\r\n" + deviceName + "\r\n--boundary\r\n"
                  "Content-Disposition: form-data; name=\"File\"; filename=\"cam.jpg\"\r\n"
                  "Content-Type: image/jpeg\r\n\r\n";
    // --boundary  Content-Disposition: form-data; name="Name"    test.jpg  --boundary  Content-Disposition: form-data; name="File"; filename="test.jpg"
    String tail = "\r\n--boundary--\r\n";

    // const char *payload = "Peti\r\nKuli\r\nZec :)";
    // uint32_t imageLen = strlen(payload);
    uint32_t imageLen = fb->len;
    Serial.printf("Image length: %u\n", imageLen);
    uint32_t totalLen = imageLen + head.length() + tail.length();
    client.println("POST /api/ImgUpload HTTP/1.1");
    client.println("Host: " + serverName);
    client.println("Connection: close");
    client.println("Content-Type: multipart/form-data;boundary=\"boundary\"");
    client.println("Content-Length: " + String(totalLen) + "\r\n");

    client.print(head);

    uint8_t *fbBuf = fb->buf;
    // uint8_t *fbBuf = (uint8_t *)payload;
    const size_t chunk = 1024; //OG 1024
    for (size_t n = 0; n < imageLen; n += chunk)
    {
        if (n + chunk < imageLen)
        {
            client.write(fbBuf, chunk);
            fbBuf += chunk;
        }
        else if (imageLen % chunk > 0)
        {
            size_t remainder = imageLen % chunk;
            client.write(fbBuf, remainder);
        }
    }
    client.print(tail);

    int timoutTimer = 10000;
    long startTimer = millis();
    boolean state = false;

    while ((startTimer + timoutTimer) > millis())
    {
        Serial.print(".");
        delay(100);
        while (client.available())
        {
            char c = client.read();
            //T Serial.print(c);
            if (c == '\n')
            {
                if (getAll.length() == 0)
                    state = true;
                getAll = "";
            }
            else if (c != '\r')
                getAll += String(c);
            if (state == true)
                getBody += String(c);
            startTimer = millis();
        }
        if (getBody.length() > 0)
            break;
    }
    Serial.println();
    client.stop();
    Serial.println(getBody);
    return true;
}
