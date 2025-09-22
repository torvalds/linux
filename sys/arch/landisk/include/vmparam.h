/*	$OpenBSD: vmparam.h,v 1.4 2011/05/30 22:25:21 oga Exp $	*/
/*	$NetBSD: vmparam.h,v 1.1 2006/09/01 21:26:18 uwe Exp $	*/

#ifndef _MACHINE_VMPARAM_H_
#define _MACHINE_VMPARAM_H_

#include <sh/vmparam.h>

#define	KERNBASE		0x8c000000

#define VM_PHYSSEG_MAX		1
#define	VM_PHYSSEG_NOADD
#define	VM_PHYSSEG_STRAT	VM_PSTRAT_RANDOM

#endif /* _MACHINE_VMPARAM_H_ */
