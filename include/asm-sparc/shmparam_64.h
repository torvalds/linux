#ifndef _ASMSPARC64_SHMPARAM_H
#define _ASMSPARC64_SHMPARAM_H

#include <asm/spitfire.h>

#define __ARCH_FORCE_SHMLBA	1
/* attach addr a multiple of this */
#define	SHMLBA	((PAGE_SIZE > L1DCACHE_SIZE) ? PAGE_SIZE : L1DCACHE_SIZE)

#endif /* _ASMSPARC64_SHMPARAM_H */
