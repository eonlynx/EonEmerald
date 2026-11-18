#ifndef GUARD_DNS_UTILS_H
#define GUARD_DNS_UTILS_H

/* Visual time phases shared by the overworld DNS and battle backgrounds. */
enum DnsTimeLapse
{
    DNS_TIME_MIDNIGHT,
    DNS_TIME_DAWN,
    DNS_TIME_DAY,
    DNS_TIME_SUNSET,
    DNS_TIME_NIGHTFALL,
    DNS_TIME_NIGHT
};

/* End hours for each of the timelapses */
#define MIDNIGHT_END_HOUR   7       //00 - 07
#define DAWN_END_HOUR       8       //07 - 08
#define DAY_END_HOUR        19      //08 - 19
#define SUNSET_END_HOUR     20      //19 - 20
#define NIGHTFALL_END_HOUR  21      //20 - 21
#define NIGHT_END_HOUR      0       //21 - 00

/* Start and end hour of the window lighting system. */
#define LIGHTNING_START_HOUR    NIGHTFALL_END_HOUR
#define LIGHTNING_END_HOUR      MIDNIGHT_END_HOUR

u8 GetDnsTimeLapse(u8 hour);

#endif /* GUARD_DNS_UTILS_H */
