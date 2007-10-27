#ifndef _SPARC64_BACKOFF_H
#define _SPARC64_BACKOFF_H

#define BACKOFF_LIMIT	(4 * 1024)

#ifdef CONFIG_SMP

#define BACKOFF_SETUP(reg)	\
	mov	1, reg

#define BACKOFF_SPIN(reg, tmp, label)	\
	mov	reg, tmp; \
88:	brnz,pt	tmp, 88b; \
	 sub	tmp, 1, tmp; \
	cmp	reg, BACKOFF_LIMIT; \
	bg,pn	%xcc, label; \
	 nop; \
	ba,pt	%xcc, label; \
	 sllx	reg, 1, reg;

#else

#define BACKOFF_SETUP(reg)
#define BACKOFF_SPIN(reg, tmp, label) \
	ba,pt	%xcc, label; \
	 nop;

#endif

#endif /* _SPARC64_BACKOFF_H */
