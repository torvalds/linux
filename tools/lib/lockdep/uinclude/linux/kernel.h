#ifndef _LIBLOCKDEP_LINUX_KERNEL_H_
#define _LIBLOCKDEP_LINUX_KERNEL_H_

#include <linux/export.h>
#include <linux/types.h>
#include <linux/rcu.h>
#include <linux/hardirq.h>
#include <linux/kern_levels.h>
#include <linux/err.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#ifndef container_of
#define container_of(ptr, type, member) ({			\
	const typeof(((type *)0)->member) * __mptr = (ptr);	\
	(type *)((char *)__mptr - offsetof(type, member)); })
#endif

#define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })

#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

static inline int lockdep_warn(int condition)
{
	return condition;
}
#define WARN_ON(x) lockdep_warn(x)
#define WARN_ON_ONCE(x) WARN_ON(x)
#define WARN(x, y...) WARN_ON(x)

#define likely(x) (x)
#define uninitialized_var(x) x
#define __init
#define noinline
#define list_add_tail_rcu list_add_tail
#define list_for_each_entry_rcu list_for_each_entry
#define barrier() 
#define synchronize_sched()

#ifndef CALLER_ADDR0
#define CALLER_ADDR0 ((unsigned long)__builtin_return_address(0))
#endif

#ifndef _RET_IP_
#define _RET_IP_ CALLER_ADDR0
#endif

#ifndef _THIS_IP_
#define _THIS_IP_ ({ __label__ __here; __here: (unsigned long)&&__here; })
#endif

#define pr_err(format, ...) fprintf (stderr, format, ## __VA_ARGS__)
#define pr_warn pr_err

#define current_gfp_context(k) 0

#endif
