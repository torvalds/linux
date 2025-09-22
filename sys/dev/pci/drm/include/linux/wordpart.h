/* Public domain. */

#ifndef _LINUX_WORDPART_H
#define _LINUX_WORDPART_H

#define lower_32_bits(n)	((u32)(n))
#define upper_32_bits(_val)	((u32)(((_val) >> 16) >> 16))

#endif
