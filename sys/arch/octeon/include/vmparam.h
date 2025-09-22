/*	$OpenBSD: vmparam.h,v 1.3 2017/07/30 16:09:53 visa Exp $ */
/* public domain */
#ifndef _MACHINE_VMPARAM_H_
#define _MACHINE_VMPARAM_H_

#define	VM_PHYSSEG_MAX		8 /* Max number of physical memory segments */
#define	VM_PHYSSEG_STRAT	VM_PSTRAT_BIGFIRST

#include <mips64/vmparam.h>

#endif	/* _MACHINE_VMPARAM_H_ */
