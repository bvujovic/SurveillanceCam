#pragma once

// Nacin rada aparata: Veb server, periodicno slikanje,...
enum DeviceMode
{
    DM_WebServer = 0,      // Postavke sistema, podesavanje kadra (image preview)
    DM_PeriodicCard = 11,  // Slikanje, cuvanje na SD karticu, spavanje zadati period
    DM_PeriodicCloud = 12, // Slikanje, slanje slike na cloud pa spavanje odredjeni period
    DM_PIR = 2,            // Budjenje na PIR senzor, slanje slike na mejl...
};
