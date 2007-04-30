#ifndef __ASM_AVR32_ARCH_AT32AP_IO_H
#define __ASM_AVR32_ARCH_AT32AP_IO_H

/* For "bizarre" halfword swapping */
#include <linux/byteorder/swabb.h>

#if defined(CONFIG_AP7000_32_BIT_SMC)
# define __swizzle_addr_b(addr)	(addr ^ 3UL)
# define __swizzle_addr_w(addr)	(addr ^ 2UL)
# define __swizzle_addr_l(addr)	(addr)
# define ioswabb(a, x)		(x)
# define ioswabw(a, x)		(x)
# define ioswabl(a, x)		(x)
# define __mem_ioswabb(a, x)	(x)
# define __mem_ioswabw(a, x)	swab16(x)
# define __mem_ioswabl(a, x)	swab32(x)
#elif defined(CONFIG_AP7000_16_BIT_SMC)
# define __swizzle_addr_b(addr)	(addr ^ 1UL)
# define __swizzle_addr_w(addr)	(addr)
# define __swizzle_addr_l(addr)	(addr)
# define ioswabb(a, x)		(x)
# define ioswabw(a, x)		(x)
# define ioswabl(a, x)		swahw32(x)
# define __mem_ioswabb(a, x)	(x)
# define __mem_ioswabw(a, x)	swab16(x)
# define __mem_ioswabl(a, x)	swahb32(x)
#else
# define __swizzle_addr_b(addr)	(addr)
# define __swizzle_addr_w(addr)	(addr)
# define __swizzle_addr_l(addr)	(addr)
# define ioswabb(a, x)		(x)
# define ioswabw(a, x)		swab16(x)
# define ioswabl(a, x)		swab32(x)
# define __mem_ioswabb(a, x)	(x)
# define __mem_ioswabw(a, x)	(x)
# define __mem_ioswabl(a, x)	(x)
#endif

#endif /* __ASM_AVR32_ARCH_AT32AP_IO_H */
