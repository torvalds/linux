/* SPDX-License-Identifier: GPL-2.0 */
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

/* This defines the prefixed load/store instructions */
#ifdef __ASSEMBLY__
#  define stringify_in_c(...)	__VA_ARGS__
#else
#  define __stringify_in_c(...)	#__VA_ARGS__
#  define stringify_in_c(...)	__stringify_in_c(__VA_ARGS__) " "
#endif

#define __PPC_RA(a)	(((a) & 0x1f) << 16)
#define __PPC_RS(s)	(((s) & 0x1f) << 21)
#define __PPC_RT(t)	__PPC_RS(t)
#define __PPC_PREFIX_R(r)	(((r) & 0x1) << 20)

#define PPC_PREFIX_MLS			0x06000000
#define PPC_PREFIX_8LS			0x04000000

#define PPC_INST_LBZ			0x88000000
#define PPC_INST_LHZ			0xa0000000
#define PPC_INST_LHA			0xa8000000
#define PPC_INST_LWZ			0x80000000
#define PPC_INST_STB			0x98000000
#define PPC_INST_STH			0xb0000000
#define PPC_INST_STW			0x90000000
#define PPC_INST_STD			0xf8000000
#define PPC_INST_LFS			0xc0000000
#define PPC_INST_LFD			0xc8000000
#define PPC_INST_STFS			0xd0000000
#define PPC_INST_STFD			0xd8000000

#define PREFIX_MLS(instr, t, a, r, d)	stringify_in_c(.balign 64, , 4;)		\
					stringify_in_c(.long PPC_PREFIX_MLS |		\
						       __PPC_PREFIX_R(r) |		\
						       (((d) >> 16) & 0x3ffff);)	\
					stringify_in_c(.long (instr)  |			\
						       __PPC_RT(t) |			\
						       __PPC_RA(a) |			\
						       ((d) & 0xffff);\n)

#define PREFIX_8LS(instr, t, a, r, d)	stringify_in_c(.balign 64, , 4;)		\
					stringify_in_c(.long PPC_PREFIX_8LS |		\
						       __PPC_PREFIX_R(r) |		\
						       (((d) >> 16) & 0x3ffff);)	\
					stringify_in_c(.long (instr)  |			\
						       __PPC_RT(t) |			\
						       __PPC_RA(a) |			\
						       ((d) & 0xffff);\n)

/* Prefixed Integer Load/Store instructions */
#define PLBZ(t, a, r, d)		PREFIX_MLS(PPC_INST_LBZ, t, a, r, d)
#define PLHZ(t, a, r, d)		PREFIX_MLS(PPC_INST_LHZ, t, a, r, d)
#define PLHA(t, a, r, d)		PREFIX_MLS(PPC_INST_LHA, t, a, r, d)
#define PLWZ(t, a, r, d)		PREFIX_MLS(PPC_INST_LWZ, t, a, r, d)
#define PLWA(t, a, r, d)		PREFIX_8LS(0xa4000000, t, a, r, d)
#define PLD(t, a, r, d)			PREFIX_8LS(0xe4000000, t, a, r, d)
#define PLQ(t, a, r, d)			PREFIX_8LS(0xe0000000, t, a, r, d)
#define PSTB(s, a, r, d)		PREFIX_MLS(PPC_INST_STB, s, a, r, d)
#define PSTH(s, a, r, d)		PREFIX_MLS(PPC_INST_STH, s, a, r, d)
#define PSTW(s, a, r, d)		PREFIX_MLS(PPC_INST_STW, s, a, r, d)
#define PSTD(s, a, r, d)		PREFIX_8LS(0xf4000000, s, a, r, d)
#define PSTQ(s, a, r, d)		PREFIX_8LS(0xf0000000, s, a, r, d)

/* Prefixed Floating-Point Load/Store Instructions */
#define PLFS(frt, a, r, d)		PREFIX_MLS(PPC_INST_LFS, frt, a, r, d)
#define PLFD(frt, a, r, d)		PREFIX_MLS(PPC_INST_LFD, frt, a, r, d)
#define PSTFS(frs, a, r, d)		PREFIX_MLS(PPC_INST_STFS, frs, a, r, d)
#define PSTFD(frs, a, r, d)		PREFIX_MLS(PPC_INST_STFD, frs, a, r, d)

/* Prefixed VSX Load/Store Instructions */
#define PLXSD(vrt, a, r, d)		PREFIX_8LS(0xa8000000, vrt, a, r, d)
#define PLXSSP(vrt, a, r, d)		PREFIX_8LS(0xac000000, vrt, a, r, d)
#define PLXV0(s, a, r, d)		PREFIX_8LS(0xc8000000, s, a, r, d)
#define PLXV1(s, a, r, d)		PREFIX_8LS(0xcc000000, s, a, r, d)
#define PSTXSD(vrs, a, r, d)		PREFIX_8LS(0xb8000000, vrs, a, r, d)
#define PSTXSSP(vrs, a, r, d)		PREFIX_8LS(0xbc000000, vrs, a, r, d)
#define PSTXV0(s, a, r, d)		PREFIX_8LS(0xd8000000, s, a, r, d)
#define PSTXV1(s, a, r, d)		PREFIX_8LS(0xdc000000, s, a, r, d)

#endif /* _SELFTESTS_POWERPC_INSTRUCTIONS_H */
