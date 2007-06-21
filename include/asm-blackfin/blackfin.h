/*
 * Common header file for blackfin family of processors.
 *
 */

#ifndef _BLACKFIN_H_
#define _BLACKFIN_H_

#define LO(con32) ((con32) & 0xFFFF)
#define lo(con32) ((con32) & 0xFFFF)
#define HI(con32) (((con32) >> 16) & 0xFFFF)
#define hi(con32) (((con32) >> 16) & 0xFFFF)

#include <asm/mach/blackfin.h>
#include <asm/bfin-global.h>

#ifndef __ASSEMBLY__

/* SSYNC implementation for C file */
#if defined(ANOMALY_05000312) && defined(ANOMALY_05000244)
static inline void SSYNC (void)
{
	int _tmp;
	__asm__ __volatile__ ("cli %0;\n\t"
			"nop;nop;\n\t"
			"ssync;\n\t"
			"sti %0;\n\t"
			:"=d"(_tmp):);
}
#elif defined(ANOMALY_05000312) && !defined(ANOMALY_05000244)
static inline void SSYNC (void)
{
	int _tmp;
	__asm__ __volatile__ ("cli %0;\n\t"
			"ssync;\n\t"
			"sti %0;\n\t"
			:"=d"(_tmp):);
}
#elif !defined(ANOMALY_05000312) && defined(ANOMALY_05000244)
static inline void SSYNC (void)
{
	__asm__ __volatile__ ("nop; nop; nop;\n\t"
			"ssync;\n\t"
			::);
}
#elif !defined(ANOMALY_05000312) && !defined(ANOMALY_05000244)
static inline void SSYNC (void)
{
	__asm__ __volatile__ ("ssync;\n\t");
}
#endif

/* CSYNC implementation for C file */
#if defined(ANOMALY_05000312) && defined(ANOMALY_05000244)
static inline void CSYNC (void)
{
	int _tmp;
	__asm__ __volatile__ ("cli %0;\n\t"
			"nop;nop;\n\t"
			"csync;\n\t"
			"sti %0;\n\t"
			:"=d"(_tmp):);
}
#elif defined(ANOMALY_05000312) && !defined(ANOMALY_05000244)
static inline void CSYNC (void)
{
	int _tmp;
	__asm__ __volatile__ ("cli %0;\n\t"
			"csync;\n\t"
			"sti %0;\n\t"
			:"=d"(_tmp):);
}
#elif !defined(ANOMALY_05000312) && defined(ANOMALY_05000244)
static inline void CSYNC (void)
{
	__asm__ __volatile__ ("nop; nop; nop;\n\t"
			"ssync;\n\t"
			::);
}
#elif !defined(ANOMALY_05000312) && !defined(ANOMALY_05000244)
static inline void CSYNC (void)
{
	__asm__ __volatile__ ("csync;\n\t");
}
#endif

#else  /* __ASSEMBLY__ */

/* SSYNC & CSYNC implementations for assembly files */

#define ssync(x) SSYNC(x)
#define csync(x) CSYNC(x)

#if defined(ANOMALY_05000312) && defined(ANOMALY_05000244)
#define SSYNC(scratch) cli scratch; nop; nop; SSYNC; sti scratch;
#define CSYNC(scratch) cli scratch; nop; nop; CSYNC; sti scratch;

#elif defined(ANOMALY_05000312) && !defined(ANOMALY_05000244)
#define SSYNC(scratch) cli scratch; nop; nop; SSYNC; sti scratch;
#define CSYNC(scratch) cli scratch; nop; nop; CSYNC; sti scratch;

#elif !defined(ANOMALY_05000312) && defined(ANOMALY_05000244)
#define SSYNC(scratch) nop; nop; nop; SSYNC;
#define CSYNC(scratch) nop; nop; nop; CSYNC;

#elif !defined(ANOMALY_05000312) && !defined(ANOMALY_05000244)
#define SSYNC(scratch) SSYNC;
#define CSYNC(scratch) CSYNC;

#endif /* ANOMALY_05000312 & ANOMALY_05000244 handling */

#endif /* __ASSEMBLY__ */

#endif				/* _BLACKFIN_H_ */
