#ifndef _MEDUSA_ARCH_H
#define _MEDUSA_ARCH_H
#include <linux/spinlock.h>
#include <linux/medusa/l3/config.h>

#pragma GCC optimize ("Og")

/* data locks */
#define MED_DECLARE_LOCK_DATA(name)	extern rwlock_t name
#define MED_LOCK_DATA(name)		DEFINE_RWLOCK(name)
#define MED_LOCK_R(name)		{ 			\
					barrier(); 		\
					read_lock(&name);	\
					barrier();		\
					}
					
#define MED_LOCK_W(name)		{			\
					barrier();		\
					write_lock(&name);	\
					barrier();		\
					}
#define MED_UNLOCK_R(name)		{			\
					barrier();		\
					read_unlock(&name); 	\
					barrier();		\
					}
#define MED_UNLOCK_W(name)		{			\
					barrier();		\
					write_unlock(&name);	\
					barrier();		\
					}

/* debug output */
#ifdef CONFIG_MEDUSA_QUIET
#define MED_PRINTF(fmt...) do { } while (0)
#else
#define MED_PRINTF(fmt...) printk("medusa: " fmt)
#endif

/* u_intX_t */
#include <linux/medusa/l3/arch_types.h>

/* memcpy */

/* non-atomic bit set/test operations */
#include <asm/bitops.h>
#define MED_SET_BIT(bit,ptr) __set_bit((bit),(void *)(ptr))
#define MED_CLR_BIT(bit,ptr) clear_bit((bit),(void *)(ptr))
#define MED_TST_BIT(bit,ptr) test_bit((bit),(void *)(ptr))

/* sanity checks for decision */
#include <linux/sched.h>
#include <linux/interrupt.h>
#define ARCH_CANNOT_DECIDE(x) (in_interrupt() || current->pid == 0)

/* linkage */ /* FIXME: is this needed? */
#include <linux/module.h>

#define MEDUSA_EXPORT_SYMBOL(symname) EXPORT_SYMBOL(x)
#define MEDUSA_INIT_FUNC(symname) module_init(symname)
#define MEDUSA_EXIT_FUNC(symname) module_exit(symname)
#define MEDUSA_KETCHUP MODULE_LICENSE("GPL");

#endif
