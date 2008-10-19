/* Permission is hereby granted to copy, modify and redistribute this code
 * in terms of the GNU Library General Public License, Version 2 or later,
 * at your option.
 */

/* macros to translate to/from binary and binary-coded decimal (frequently
 * found in RTC chips).
 */

#ifndef _BCD_H
#define _BCD_H

#include <linux/compiler.h>

unsigned bcd2bin(unsigned char val) __attribute_const__;
unsigned char bin2bcd(unsigned val) __attribute_const__;

#endif /* _BCD_H */
