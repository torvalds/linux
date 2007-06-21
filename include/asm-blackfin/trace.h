/*
 * Common header file for blackfin family of processors.
 *
 */

#ifndef _BLACKFIN_TRACE_
#define _BLACKFIN_TRACE_

#ifndef __ASSEMBLY__
/* Trace Macros for C files */

#define trace_buffer_save(x) \
        do { \
                (x) = bfin_read_TBUFCTL(); \
                bfin_write_TBUFCTL((x) & ~TBUFEN); \
        } while (0)

#define trace_buffer_restore(x) \
        do { \
                bfin_write_TBUFCTL((x));        \
        } while (0)

#else
/* Trace Macros for Assembly files */

#define TRACE_BUFFER_START(preg, dreg) trace_buffer_start(preg, dreg)
#define TRACE_BUFFER_STOP(preg, dreg)  trace_buffer_stop(preg, dreg)

#define trace_buffer_stop(preg, dreg)	\
	preg.L = LO(TBUFCTL);		\
	preg.H = HI(TBUFCTL);		\
	dreg = 0x1;			\
	[preg] = dreg;

#define trace_buffer_start(preg, dreg) \
	preg.L = LO(TBUFCTL);		\
	preg.H = HI(TBUFCTL);		\
	dreg = 0x13;			\
	[preg] = dreg;

#ifdef CONFIG_DEBUG_BFIN_NO_KERN_HWTRACE
# define DEBUG_START_HWTRACE(preg, dreg) trace_buffer_start(preg, dreg)
# define DEBUG_STOP_HWTRACE(preg, dreg) trace_buffer_stop(preg, dreg)

#else
# define DEBUG_START_HWTRACE(preg, dreg)
# define DEBUG_STOP_HWTRACE(preg, dreg)
#endif

#endif /* __ASSEMBLY__ */

#endif				/* _BLACKFIN_TRACE_ */
