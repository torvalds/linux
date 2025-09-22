/*	$OpenBSD: intr.h,v 1.9 2014/04/01 20:27:14 mpi Exp $	*/

#include <powerpc/intr.h>

#ifndef _LOCORE
extern int intr_shared_edge;

void install_extint(void (*handler)(void));
void openpic_set_priority(int, int);
#endif
