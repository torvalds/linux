/*	$OpenBSD: intr.h,v 1.57 2025/05/10 09:54:17 visa Exp $ */

/*
 * Copyright (c) 1997 Per Fogelstrom, Opsycon AB and RTMX Inc, USA.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed under OpenBSD by
 *	Per Fogelstrom, Opsycon AB, Sweden for RTMX Inc, North Carolina USA.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _POWERPC_INTR_H_
#define _POWERPC_INTR_H_

#define	IPL_NONE	0
#define	IPL_SOFTCLOCK	2
#define	IPL_SOFTNET	3
#define	IPL_SOFTTTY	4
#define	IPL_BIO		5
#define	IPL_NET		6
#define	IPL_TTY		7
#define	IPL_VM		8
#define	IPL_AUDIO	9
#define	IPL_CLOCK	10
#define	IPL_SCHED	11
#define	IPL_HIGH	12
#define	IPL_NUM		13

#define	IPL_MPFLOOR	IPL_TTY
#define	IPL_MPSAFE	0x100

#define	IST_NONE	0
#define	IST_PULSE	1
#define	IST_EDGE	2
#define	IST_LEVEL	3

#define __USE_MI_SOFTINTR

#include <sys/softintr.h>

#if defined(_KERNEL) && !defined(_LOCORE)

#include <sys/evcount.h>
#include <machine/atomic.h>

#define	PPC_NIRQ	66
#define	PPC_CLK_IRQ	64
#define	PPC_STAT_IRQ	65

int	splraise(int);
int	spllower(int);
void	splx(int);

typedef int (ppc_splraise_t) (int);
typedef int (ppc_spllower_t) (int);
typedef void (ppc_splx_t) (int);

extern struct ppc_intr_func {
	ppc_splraise_t *raise;
	ppc_spllower_t *lower;
	ppc_splx_t *x;
}ppc_intr_func;

extern int ppc_smask[IPL_NUM];

void ppc_smask_init(void);
char *ppc_intr_typename(int type);

void do_pending_int(void);

/* SPL asserts */
#ifdef DIAGNOSTIC
/*
 * Although this function is implemented in MI code, it must be in this MD
 * header because we don't want this header to include MI includes.
 */
void splassert_fail(int, int, const char *);
extern int splassert_ctl;
void splassert_check(int, const char *);
#define splassert(__wantipl) do {			\
	if (splassert_ctl > 0) {			\
		splassert_check(__wantipl, __func__);	\
	}						\
} while (0)
#define splsoftassert(wantipl) splassert(wantipl)
#else
#define splassert(wantipl)	do { /* nada */ } while (0)
#define splsoftassert(wantipl)	do { /* nada */ } while (0)
#endif

#define	set_sint(p)	atomic_setbits_int(&curcpu()->ci_ipending, p)

#define	splbio()	splraise(IPL_BIO)
#define	splnet()	splraise(IPL_NET)
#define	spltty()	splraise(IPL_TTY)
#define	splaudio()	splraise(IPL_AUDIO)
#define	splclock()	splraise(IPL_CLOCK)
#define	splvm()		splraise(IPL_VM)
#define	splsched()	splhigh()
#define	splstatclock()	splhigh()
#define	splsoftclock()	splraise(IPL_SOFTCLOCK)
#define	splsoftnet()	splraise(IPL_SOFTNET)
#define	splsofttty()	splraise(IPL_SOFTTTY)

#define	SI_TO_IRQBIT(x) (1 << (x))

void	softintr(int);
void	dosoftint(int);

#define	splhigh()	splraise(IPL_HIGH)
#define	spl0()		spllower(IPL_NONE)

/*
 *	Interrupt control struct used to control the ICU setup.
 */

struct intrhand {
	TAILQ_ENTRY(intrhand) ih_list;
	int		(*ih_fun)(void *);
	void		*ih_arg;
	struct evcount	ih_count;
	int		ih_type;
	int		ih_level;
	int		ih_flags;
	int		ih_irq;
	const char	*ih_what;
};

struct intrq {
	TAILQ_HEAD(, intrhand) iq_list; /* handler list */
	int iq_ipl;			/* IPL_ to mask while handling */
	int iq_ist;			/* share type */
};

extern int ppc_configed_intr_cnt;
#define	MAX_PRECONF_INTR 16
extern struct intrhand ppc_configed_intr[MAX_PRECONF_INTR];

void intr_barrier(void *);

#define PPC_IPI_NOP		0
#define PPC_IPI_DDB		1

void ppc_send_ipi(struct cpu_info *, int);

#endif /* _LOCORE */
#endif /* _POWERPC_INTR_H_ */
