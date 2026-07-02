#ifndef RX_CHARSET_H
#define RX_CHARSET_H

#include <stdbool.h>

#define RX_CHARSET_BYTES 32

void rx_charset_clear(unsigned char cls[RX_CHARSET_BYTES]);
void rx_charset_add(unsigned char cls[RX_CHARSET_BYTES], unsigned char c);
bool rx_charset_has(const unsigned char cls[RX_CHARSET_BYTES], unsigned char c);
void rx_charset_add_range(unsigned char cls[RX_CHARSET_BYTES], unsigned char lo, unsigned char hi);
void rx_charset_invert(unsigned char cls[RX_CHARSET_BYTES]);
void rx_charset_add_digit(unsigned char cls[RX_CHARSET_BYTES]);
void rx_charset_add_word(unsigned char cls[RX_CHARSET_BYTES]);
void rx_charset_add_space(unsigned char cls[RX_CHARSET_BYTES]);

#endif
