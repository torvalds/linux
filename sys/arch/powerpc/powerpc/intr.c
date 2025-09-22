/*	$OpenBSD: intr.c,v 1.13 2025/05/10 09:54:17 visa Exp $	*/

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
#include <sys/param.h>

#include <machine/cpu.h>
#include <machine/intr.h>

int ppc_dflt_splraise(int);
int ppc_dflt_spllower(int);
void ppc_dflt_splx(int);

/* provide a function for asm code to call */
#undef splraise
#undef spllower
#undef splx

int ppc_smask[IPL_NUM];

void
ppc_smask_init()
{
        int i;

        for (i = IPL_NONE; i <= IPL_HIGH; i++)  {
                ppc_smask[i] = 0;
                if (i < IPL_SOFTCLOCK)
                        ppc_smask[i] |= SI_TO_IRQBIT(SOFTINTR_CLOCK);
                if (i < IPL_SOFTNET)
                        ppc_smask[i] |= SI_TO_IRQBIT(SOFTINTR_NET);
                if (i < IPL_SOFTTTY)
                        ppc_smask[i] |= SI_TO_IRQBIT(SOFTINTR_TTY);
        }
}

int
splraise(int newcpl)
{
	return ppc_intr_func.raise(newcpl);
}

int
spllower(int newcpl)
{
	return ppc_intr_func.lower(newcpl);
}

void
splx(int newcpl)
{
	ppc_intr_func.x(newcpl);
}

/*
 * functions with 'default' behavior to use before the real
 * interrupt controller attaches
 */

int
ppc_dflt_splraise(int newcpl)
{
        struct cpu_info *ci = curcpu();
        int oldcpl;

        oldcpl = ci->ci_cpl;
        if (newcpl < oldcpl)
                newcpl = oldcpl;
        ci->ci_cpl = newcpl;

        return (oldcpl);
}

int
ppc_dflt_spllower(int newcpl)
{
        struct cpu_info *ci = curcpu();
        int oldcpl;

        oldcpl = ci->ci_cpl;

        splx(newcpl);

        return (oldcpl);
}

void
ppc_dflt_splx(int newcpl)
{
        struct cpu_info *ci = curcpu();

        ci->ci_cpl = newcpl;

	if (ci->ci_dec_deferred && newcpl < IPL_CLOCK) {
		ppc_mtdec(0);
		ppc_mtdec(UINT32_MAX);	/* raise DEC exception */
	}

        if (ci->ci_ipending & ppc_smask[newcpl])
		dosoftint(newcpl);
}

struct ppc_intr_func ppc_intr_func =
{
        ppc_dflt_splraise,
	ppc_dflt_spllower,
	ppc_dflt_splx
};

char *
ppc_intr_typename(int type)
{
	switch (type) {
	case IST_NONE :
		return ("none");
	case IST_PULSE:
		return ("pulsed");
	case IST_EDGE:
		return ("edge-triggered");
	case IST_LEVEL:
		return ("level-triggered");
	default:
		return ("unknown");
	}
}

void
intr_barrier(void *ih)
{
	sched_barrier(NULL);
}

#ifdef DIAGNOSTIC
void
splassert_check(int wantipl, const char *func)
{
	struct cpu_info *ci = curcpu();

	if (ci->ci_cpl < wantipl)
		splassert_fail(wantipl, ci->ci_cpl, func);

	if (wantipl == IPL_NONE && ci->ci_idepth != 0)
		splassert_fail(-1, ci->ci_idepth, func);
}
#endif
