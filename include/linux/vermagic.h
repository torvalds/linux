#include <linux/version.h>
#include <linux/module.h>

/* Simply sanity version stamp for modules. */
#ifdef CONFIG_SMP
#define MODULE_VERMAGIC_SMP "SMP "
#else
#define MODULE_VERMAGIC_SMP ""
#endif
#ifdef CONFIG_PREEMPT
#define MODULE_VERMAGIC_PREEMPT "preempt "
#else
#define MODULE_VERMAGIC_PREEMPT ""
#endif
#ifndef MODULE_ARCH_VERMAGIC
#define MODULE_ARCH_VERMAGIC ""
#endif

#define VERMAGIC_STRING 						\
	UTS_RELEASE " "							\
	MODULE_VERMAGIC_SMP MODULE_VERMAGIC_PREEMPT 			\
	MODULE_ARCH_VERMAGIC 						\
	"gcc-" __stringify(__GNUC__) "." __stringify(__GNUC_MINOR__)
