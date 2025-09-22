/*	$OpenBSD: kern_xxx.c,v 1.42 2025/06/16 20:21:33 kettenis Exp $	*/
/*	$NetBSD: kern_xxx.c,v 1.32 1996/04/22 01:38:41 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)kern_xxx.c	8.2 (Berkeley) 11/14/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/reboot.h>
#include <sys/signalvar.h>
#include <sys/syscallargs.h>
#include <sys/task.h>

int rebooting = 0;

int
sys_reboot(struct proc *p, void *v, register_t *retval)
{
	struct sys_reboot_args /* {
		syscallarg(int) opt;
	} */ *uap = v;
	int error;

	if ((error = suser(p)) != 0)
		return (error);

#ifdef MULTIPROCESSOR
	sched_stop_secondary_cpus();
	KASSERT(CPU_IS_PRIMARY(curcpu()));
#endif
	reboot(SCARG(uap, opt));
	/* NOTREACHED */
	return (0);
}

__dead void
reboot(int howto)
{
	KASSERT((howto & RB_NOSYNC) || curproc != NULL);

	stop_periodic_resettodr();

	rebooting = 1;

	boot(howto);
	/* NOTREACHED */
}

void
do_powerdown(void *arg)
{
	extern int allowpowerdown;

	if (allowpowerdown == 1) {
		allowpowerdown = 0;
		prsignal(initprocess, SIGUSR2);
	}
}

struct task powerdown_task = TASK_INITIALIZER(do_powerdown, NULL);

void
powerbutton_event(void)
{
#ifdef SUSPEND
	if (resuming())
		return;
#endif

	task_add(systq, &powerdown_task);
}

#if !defined(NO_PROPOLICE) && !defined(_RET_PROTECTOR)
void __stack_smash_handler(char [], int __attribute__((unused)));

void
__stack_smash_handler(char func[], int damaged)
{
	panic("smashed stack in %s", func);
}
#endif

#ifdef SYSCALL_DEBUG
#include <sys/proc.h>
#include <sys/syscall.h>

#define	SCDEBUG_CALLS		0x0001	/* show calls */
#define	SCDEBUG_RETURNS		0x0002	/* show returns */
#define	SCDEBUG_ALL		0x0004	/* even syscalls that are implemented */
#define	SCDEBUG_SHOWARGS	0x0008	/* show arguments to calls */

int	scdebug = SCDEBUG_CALLS|SCDEBUG_RETURNS|SCDEBUG_SHOWARGS;

extern const char *const syscallnames[];

void
scdebug_call(struct proc *p, register_t code, const register_t args[])
{
	struct process *pr;
	int i;

	if (!(scdebug & SCDEBUG_CALLS))
		return;

	if (!(scdebug & SCDEBUG_ALL || code < 0 || code >= SYS_MAXSYSCALL ||
	     sysent[code].sy_call == sys_nosys))
		return;

	pr = p->p_p;
	printf("proc %d (%s): num ", pr->ps_pid, pr->ps_comm);
	if (code < 0 || code >= SYS_MAXSYSCALL)
		printf("OUT OF RANGE (%ld)", code);
	else {
		printf("%ld call: %s", code, syscallnames[code]);
		if (scdebug & SCDEBUG_SHOWARGS) {
			printf("(");
			for (i = 0; i < sysent[code].sy_argsize / sizeof(register_t);
			    i++)
				printf("%s0x%lx", i == 0 ? "" : ", ", args[i]);
			printf(")");
		}
	}
	printf("\n");
}

void
scdebug_ret(struct proc *p, register_t code, int error,
    const register_t retval[])
{
	struct process *pr;

	if (!(scdebug & SCDEBUG_RETURNS))
		return;

	if (!(scdebug & SCDEBUG_ALL || code < 0 || code >= SYS_MAXSYSCALL ||
	    sysent[code].sy_call == sys_nosys))
		return;

	pr = p->p_p;
	printf("proc %d (%s): num ", pr->ps_pid, pr->ps_comm);
	if (code < 0 || code >= SYS_MAXSYSCALL)
		printf("OUT OF RANGE (%ld)", code);
	else if (code == SYS_lseek)
		printf("%ld ret: err = %d, rv = 0x%llx", code,
		    error, *(off_t *)retval);
	else
		printf("%ld ret: err = %d, rv = 0x%lx", code,
		    error, *retval);
	printf("\n");
}
#endif /* SYSCALL_DEBUG */
