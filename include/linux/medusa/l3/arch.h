#ifndef _MEDUSA_ARCH_H
#define _MEDUSA_ARCH_H
#include <linux/spinlock.h>
#include <linux/medusa/l3/config.h>

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
#define med_pr_emerg(fmt...) do { } while (0)
#define med_pr_alert(fmt...) do { } while (0)
#define med_pr_crit(fmt...) do { } while (0)
#define med_pr_err(fmt...) do { } while (0)
#define med_pr_warn(fmt...) do { } while (0)
#define med_pr_notice(fmt...) do { } while (0)
#define med_pr_info(fmt...) do { } while (0)
#define med_pr_debug(fmt...) do { } while (0)
#define med_pr_devel(fmt...) do { } while (0)
#else
#define med_pr_emerg(fmt...) pr_emerg(KBUILD_MODNAME " | medusa: " fmt) 
#define med_pr_alert(fmt...) pr_alert(KBUILD_MODNAME " | medusa: " fmt)
#define med_pr_crit(fmt...) pr_crit(KBUILD_MODNAME " | medusa: " fmt)
#define med_pr_err(fmt...) pr_err(KBUILD_MODNAME " | medusa: " fmt)
#define med_pr_warn(fmt...) pr_warn(KBUILD_MODNAME " | medusa: " fmt)
#define med_pr_notice(fmt...) pr_notice(KBUILD_MODNAME " | medusa: " fmt)
#define med_pr_info(fmt...) pr_info(KBUILD_MODNAME " | medusa: " fmt)
#define med_pr_debug(fmt...) pr_debug(KBUILD_MODNAME " | medusa: " fmt)
#define med_pr_devel(fmt...) pr_devel(KBUILD_MODNAME " | medusa: " fmt)
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
