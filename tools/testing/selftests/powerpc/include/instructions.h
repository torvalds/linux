#ifndef _SELFTESTS_POWERPC_INSTRUCTIONS_H
#define _SELFTESTS_POWERPC_INSTRUCTIONS_H

#include <stdio.h>
#include <stdlib.h>

/* This defines the "copy" instruction from Power ISA 3.0 Book II, section 4.4. */
#define __COPY(RA, RB, L) \
	(0x7c00060c | (RA) << (31-15) | (RB) << (31-20) | (L) << (31-10))
#define COPY(RA, RB, L) \
	.long __COPY((RA), (RB), (L))

static inline void copy(void *i)
{
	asm volatile(str(COPY(0, %0, 0))";"
			:
			: "b" (i)
			: "memory"
		    );
}

static inline void copy_first(void *i)
{
	asm volatile(str(COPY(0, %0, 1))";"
			:
			: "b" (i)
			: "memory"
		    );
}

/* This defines the "paste" instruction from Power ISA 3.0 Book II, section 4.4. */
#define __PASTE(RA, RB, L, RC) \
	(0x7c00070c | (RA) << (31-15) | (RB) << (31-20) | (L) << (31-10) | (RC) << (31-31))
#define PASTE(RA, RB, L, RC) \
	.long __PASTE((RA), (RB), (L), (RC))

static inline int paste(void *i)
{
	int cr;

	asm volatile(str(PASTE(0, %1, 0, 0))";"
			"mfcr %0;"
			: "=r" (cr)
			: "b" (i)
			: "memory"
		    );
	return cr;
}

static inline int paste_last(void *i)
{
	int cr;

	asm volatile(str(PASTE(0, %1, 1, 1))";"
			"mfcr %0;"
			: "=r" (cr)
			: "b" (i)
			: "memory"
		    );
	return cr;
}

#define PPC_INST_COPY                  __COPY(0, 0, 0)
#define PPC_INST_COPY_FIRST            __COPY(0, 0, 1)
#define PPC_INST_PASTE                 __PASTE(0, 0, 0, 0)
#define PPC_INST_PASTE_LAST            __PASTE(0, 0, 1, 1)

#endif /* _SELFTESTS_POWERPC_INSTRUCTIONS_H */
