/* Permission is hereby granted to copy, modify and redistribute this code
 * in terms of the GNU Library General Public License, Version 2 or later,
 * at your option.
 */

/* macros to translate to/from binary and binary-coded decimal (frequently
 * found in RTC chips).
 */

#ifndef _BCD_H
#define _BCD_H

#define BCD2BIN(val)	(((val) & 0x0f) + ((val)>>4)*10)
#define BIN2BCD(val)	((((val)/10)<<4) + (val)%10)

/* backwards compat */
#define BCD_TO_BIN(val) ((val)=BCD2BIN(val))
#define BIN_TO_BCD(val) ((val)=BIN2BCD(val))

#endif /* _BCD_H */
