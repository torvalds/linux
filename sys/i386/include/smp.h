/*-
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD$
 *
 */

#ifndef _MACHINE_SMP_H_
#define _MACHINE_SMP_H_

#ifdef _KERNEL

#ifdef SMP

#ifndef LOCORE

#include <x86/x86_smp.h>

#include <sys/bus.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <x86/apicvar.h>
#include <machine/pcb.h>

/* functions in mpboot.s */
void bootMP(void);

#endif /* !LOCORE */
#endif /* SMP */

#endif /* _KERNEL */
#endif /* _MACHINE_SMP_H_ */
