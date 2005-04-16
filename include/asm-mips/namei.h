#ifndef _ASM_NAMEI_H
#define _ASM_NAMEI_H

#include <linux/personality.h>
#include <linux/stddef.h>

#define IRIX_EMUL	"/usr/gnemul/irix/"
#define RISCOS_EMUL	"/usr/gnemul/riscos/"

static inline char *__emul_prefix(void)
{
	switch (current->personality) {
	case PER_IRIX32:
	case PER_IRIXN32:
	case PER_IRIX64:
		return IRIX_EMUL;

	case PER_RISCOS:
		return RISCOS_EMUL;

	default:
		return NULL;
	}
}

#endif /* _ASM_NAMEI_H */
