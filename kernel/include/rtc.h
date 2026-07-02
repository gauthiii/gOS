#ifndef GOS_RTC_H
#define GOS_RTC_H

#include <stdint.h>

/* Milestone 22.1: CMOS real-time clock (ports 0x70/0x71) - no driver
 * "init" needed (the RTC runs off the CMOS battery from power-on,
 * independent of anything gOS does), just a read routine. */

struct rtc_time {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;  /* 0-23, already normalized out of 12-hour+AM/PM if the
                    * hardware happens to be configured that way */
    uint8_t day;
    uint8_t month;
    uint16_t year; /* full year, e.g. 2026 (CMOS only stores the last two
                    * digits - century is assumed to be 2000+) */
};

/* Reads the current date/time, handling the "update in progress" race
 * (re-reads until two consecutive reads agree) and the BCD-vs-binary and
 * 12/24-hour format quirks (checking CMOS Status Register B). */
void rtc_read(struct rtc_time *out);

#endif
