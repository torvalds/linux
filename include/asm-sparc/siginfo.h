/* $Id: siginfo.h,v 1.9 2002/02/08 03:57:18 davem Exp $
 * siginfo.c:
 */

#ifndef _SPARC_SIGINFO_H
#define _SPARC_SIGINFO_H

#define __ARCH_SI_UID_T		unsigned int
#define __ARCH_SI_TRAPNO

#include <asm-generic/siginfo.h>

#define SI_NOINFO	32767		/* no information in siginfo_t */

/*
 * SIGEMT si_codes
 */
#define EMT_TAGOVF	(__SI_FAULT|1)	/* tag overflow */
#define NSIGEMT		1

#endif /* !(_SPARC_SIGINFO_H) */
