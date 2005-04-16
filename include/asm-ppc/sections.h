#ifdef __KERNEL__
#ifndef _PPC_SECTIONS_H
#define _PPC_SECTIONS_H

#include <asm-generic/sections.h>

#define __pmac __attribute__ ((__section__ (".pmac.text")))
#define __pmacdata __attribute__ ((__section__ (".pmac.data")))
#define __pmacfunc(__argpmac) \
	__argpmac __pmac; \
	__argpmac
	
#define __prep __attribute__ ((__section__ (".prep.text")))
#define __prepdata __attribute__ ((__section__ (".prep.data")))
#define __prepfunc(__argprep) \
	__argprep __prep; \
	__argprep

#define __chrp __attribute__ ((__section__ (".chrp.text")))
#define __chrpdata __attribute__ ((__section__ (".chrp.data")))
#define __chrpfunc(__argchrp) \
	__argchrp __chrp; \
	__argchrp

/* this is actually just common chrp/pmac code, not OF code -- Cort */
#define __openfirmware __attribute__ ((__section__ (".openfirmware.text")))
#define __openfirmwaredata __attribute__ ((__section__ (".openfirmware.data")))
#define __openfirmwarefunc(__argopenfirmware) \
	__argopenfirmware __openfirmware; \
	__argopenfirmware
	
#endif /* _PPC_SECTIONS_H */
#endif /* __KERNEL__ */
