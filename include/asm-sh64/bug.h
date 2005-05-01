#ifndef __ASM_SH64_BUG_H
#define __ASM_SH64_BUG_H

#include <linux/config.h>

/*
 * Tell the user there is some problem, then force a segfault (in process
 * context) or a panic (interrupt context).
 */
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	*(volatile int *)0 = 0; \
} while (0)

#define BUG_ON(condition) do { \
	if (unlikely((condition)!=0)) \
		BUG(); \
} while(0)

#define WARN_ON(condition) do { \
	if (unlikely((condition)!=0)) { \
		printk("Badness in %s at %s:%d\n", __FUNCTION__, __FILE__, __LINE__); \
		dump_stack(); \
	} \
} while (0)

#endif /* __ASM_SH64_BUG_H */

