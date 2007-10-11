#ifndef __ASM_MACH_JMR3927_MANGLE_PORT_H
#define __ASM_MACH_JMR3927_MANGLE_PORT_H

extern unsigned long __swizzle_addr_b(unsigned long port);
#define __swizzle_addr_w(port)	(port)
#define __swizzle_addr_l(port)	(port)
#define __swizzle_addr_q(port)	(port)

#define ioswabb(a, x)		(x)
#define __mem_ioswabb(a, x)	(x)
#define ioswabw(a, x)		le16_to_cpu(x)
#define __mem_ioswabw(a, x)	(x)
#define ioswabl(a, x)		le32_to_cpu(x)
#define __mem_ioswabl(a, x)	(x)
#define ioswabq(a, x)		le64_to_cpu(x)
#define __mem_ioswabq(a, x)	(x)

#endif /* __ASM_MACH_JMR3927_MANGLE_PORT_H */
