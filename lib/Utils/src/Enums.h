#pragma once

// Nacin rada aparata: Veb server, periodicno slikanje,...
enum DeviceMode
{
    DM_WebServer = 0, // Postavke sistema, podesavanje kadra (image preview)
    DM_GetImage = 1,  // Slikanje pa spavanje odredjeni period
    DM_PIR = 2,       // Budjenje na PIR senzor, slanje slike na mejl...
};
