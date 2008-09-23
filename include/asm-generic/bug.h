#ifndef _ASM_GENERIC_BUG_H
#define _ASM_GENERIC_BUG_H

#include <linux/compiler.h>

#ifdef CONFIG_BUG

#ifdef CONFIG_GENERIC_BUG
#ifndef __ASSEMBLY__
struct bug_entry {
	unsigned long	bug_addr;
#ifdef CONFIG_DEBUG_BUGVERBOSE
	const char	*file;
	unsigned short	line;
#endif
	unsigned short	flags;
};
#endif		/* __ASSEMBLY__ */

#define BUGFLAG_WARNING	(1<<0)
#endif	/* CONFIG_GENERIC_BUG */

#ifndef HAVE_ARCH_BUG
#define BUG() do { \
	printk("BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __FUNCTION__); \
	panic("BUG!"); \
} while (0)
#endif

#ifndef HAVE_ARCH_BUG_ON
#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while(0)
#endif

#ifndef __WARN
#ifndef __ASSEMBLY__
extern void warn_on_slowpath(const char *file, const int line);
extern void warn_slowpath(const char *file, const int line,
		const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#define WANT_WARN_ON_SLOWPATH
#endif
#define __WARN() warn_on_slowpath(__FILE__, __LINE__)
#define __WARN_printf(arg...) warn_slowpath(__FILE__, __LINE__, arg)
#else
#define __WARN_printf(arg...) __WARN()
#endif

#ifndef WARN_ON
#define WARN_ON(condition) ({						\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		__WARN();						\
	unlikely(__ret_warn_on);					\
})
#endif

#ifndef WARN
#define WARN(condition, format...) ({						\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		__WARN_printf(format);					\
	unlikely(__ret_warn_on);					\
})
#endif

#else /* !CONFIG_BUG */
#ifndef HAVE_ARCH_BUG
#define BUG()
#endif

#ifndef HAVE_ARCH_BUG_ON
#define BUG_ON(condition) do { if (condition) ; } while(0)
#endif

#ifndef HAVE_ARCH_WARN_ON
#define WARN_ON(condition) ({						\
	int __ret_warn_on = !!(condition);				\
	unlikely(__ret_warn_on);					\
})
#endif

#ifndef WARN
#define WARN(condition, format...) ({					\
	int __ret_warn_on = !!(condition);				\
	unlikely(__ret_warn_on);					\
})
#endif

#endif

#define WARN_ON_ONCE(condition)	({				\
	static int __warned;					\
	int __ret_warn_once = !!(condition);			\
								\
	if (unlikely(__ret_warn_once))				\
		if (WARN_ON(!__warned)) 			\
			__warned = 1;				\
	unlikely(__ret_warn_once);				\
})

#define WARN_ONCE(condition, format...)	({			\
	static int __warned;					\
	int __ret_warn_once = !!(condition);			\
								\
	if (unlikely(__ret_warn_once))				\
		if (WARN(!__warned, format)) 			\
			__warned = 1;				\
	unlikely(__ret_warn_once);				\
})

#define WARN_ON_RATELIMIT(condition, state)			\
		WARN_ON((condition) && __ratelimit(state))

#ifdef CONFIG_SMP
# define WARN_ON_SMP(x)			WARN_ON(x)
#else
# define WARN_ON_SMP(x)			do { } while (0)
#endif

#endif
