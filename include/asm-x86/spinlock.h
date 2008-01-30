#ifndef _X86_SPINLOCK_H_
#define _X86_SPINLOCK_H_

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define CLI_STRING	"cli"
#define STI_STRING	"sti"
#define CLI_STI_CLOBBERS
#define CLI_STI_INPUT_ARGS
#endif /* CONFIG_PARAVIRT */

#ifdef CONFIG_X86_32
# include "spinlock_32.h"
#else
# include "spinlock_64.h"
#endif

#endif
