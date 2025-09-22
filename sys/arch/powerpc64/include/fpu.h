/*	$OpenBSD: fpu.h,v 1.4 2022/03/24 18:42:05 kettenis Exp $	*/

/* public domain */

#ifndef _MACHINE_FPU_H
#define _MACHINE_FPU_H

#ifdef _KERNEL

void	save_vsx(struct proc *);
void	restore_vsx(struct proc *);

int	fpu_sigcode(struct proc *);

#endif

#endif /* _MACHINE_FPU_H_ */
