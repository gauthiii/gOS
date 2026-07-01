#ifndef GOS_SERIAL_H
#define GOS_SERIAL_H

void serial_init(void);
void serial_write_char(char c);
void serial_write_string(const char *s);
void serial_write_hex64(unsigned long long v);
void serial_write_uint(unsigned long long v);

#endif
