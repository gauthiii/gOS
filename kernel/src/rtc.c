#include <rtc.h>

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_INDEX, reg);
    return inb(CMOS_DATA);
}

static int rtc_update_in_progress(void) {
    return cmos_read(0x0A) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)((v & 0x0F) + (v / 16) * 10);
}

/* #26: register 0x32 is the de-facto standard (also what ACPI's FADT
 * "Century" field usually points at) CMOS century register on real
 * hardware, but it's not universally implemented - some CMOS chips/BIOSes
 * leave it as garbage or always 0. Reading it speculatively and only
 * trusting it if it decodes (BCD or binary, matching status_b) to a
 * plausible century (19-21, i.e. covers 1900-2199) means a working century
 * register is honored, while a genuinely absent/garbage one safely falls
 * back to the previous hardcoded "+2000" behavior instead of producing a
 * nonsense year. */
static void read_raw(uint8_t *sec, uint8_t *min, uint8_t *hour, uint8_t *day, uint8_t *month, uint8_t *year, uint8_t *century) {
    *sec = cmos_read(0x00);
    *min = cmos_read(0x02);
    *hour = cmos_read(0x04);
    *day = cmos_read(0x07);
    *month = cmos_read(0x08);
    *year = cmos_read(0x09);
    *century = cmos_read(0x32);
}

void rtc_read(struct rtc_time *out) {
    uint8_t sec, min, hour, day, month, year, century;
    uint8_t sec2, min2, hour2, day2, month2, year2, century2;

    /* The RTC's registers are only guaranteed stable when Status Register
     * A's "update in progress" bit is clear; even then, a read can still
     * straddle an update tick, so the standard technique is to keep
     * re-reading until two consecutive (post-not-updating) reads agree. */
    do {
        while (rtc_update_in_progress()) { }
        read_raw(&sec, &min, &hour, &day, &month, &year, &century);
        while (rtc_update_in_progress()) { }
        read_raw(&sec2, &min2, &hour2, &day2, &month2, &year2, &century2);
    } while (sec != sec2 || min != min2 || hour != hour2 || day != day2 || month != month2 || year != year2 ||
             century != century2);

    uint8_t status_b = cmos_read(0x0B);

    if (!(status_b & 0x04)) { /* bit 2 clear = BCD mode (the common default) */
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        /* Hour's BCD low nibble is minutes-like (0-9 or 0-3 in 12h mode),
         * but the PM flag (bit 7) must be masked off before BCD conversion
         * and re-applied after, since it isn't part of the BCD value. */
        uint8_t pm_bit = hour & 0x80;
        hour = bcd_to_bin(hour & 0x7F) | pm_bit;
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
        century = bcd_to_bin(century);
    }

    if (!(status_b & 0x02) && (hour & 0x80)) {
        /* bit 1 clear = 12-hour mode, and this reading's PM bit is set -
         * convert to 24-hour, keeping midnight/noon correct via modulo. */
        hour = (uint8_t)(((hour & 0x7F) + 12) % 24);
    } else {
        hour &= 0x7F; /* strip any stray PM bit in 24-hour mode */
    }

    out->sec = sec;
    out->min = min;
    out->hour = hour;
    out->day = day;
    out->month = month;
    if (century >= 19 && century <= 21) {
        out->year = (uint16_t)century * 100 + year;
    } else {
        out->year = (uint16_t)year + 2000; /* register 0x32 absent/garbage - fall back to the old assumption */
    }
}
