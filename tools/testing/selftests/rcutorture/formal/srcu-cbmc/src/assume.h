#ifndef ASSUME_H
#define ASSUME_H

/* Provide an assumption macro that can be disabled for gcc. */
#ifdef RUN
#define assume(x) \
	do { \
		/* Evaluate x to suppress warnings. */ \
		(void) (x); \
	} while (0)

#else
#define assume(x) __CPROVER_assume(x)
#endif

#endif
