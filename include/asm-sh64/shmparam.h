#ifndef __ASM_SH64_SHMPARAM_H
#define __ASM_SH64_SHMPARAM_H

/*
 * Set this to a sensible safe default, we'll work out the specifics for the
 * align mask from the cache descriptor at run-time.
 */
#define	SHMLBA	0x4000

#define __ARCH_FORCE_SHMLBA

#endif /* __ASM_SH64_SHMPARAM_H */
