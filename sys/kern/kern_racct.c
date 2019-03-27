/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_sched.h"

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/loginclass.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/racct.h>
#include <sys/resourcevar.h>
#include <sys/sbuf.h>
#include <sys/sched.h>
#include <sys/sdt.h>
#include <sys/smp.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/umtx.h>
#include <machine/smp.h>

#ifdef RCTL
#include <sys/rctl.h>
#endif

#ifdef RACCT

FEATURE(racct, "Resource Accounting");

/*
 * Do not block processes that have their %cpu usage <= pcpu_threshold.
 */
static int pcpu_threshold = 1;
#ifdef RACCT_DEFAULT_TO_DISABLED
bool __read_frequently racct_enable = false;
#else
bool __read_frequently racct_enable = true;
#endif

SYSCTL_NODE(_kern, OID_AUTO, racct, CTLFLAG_RW, 0, "Resource Accounting");
SYSCTL_BOOL(_kern_racct, OID_AUTO, enable, CTLFLAG_RDTUN, &racct_enable,
    0, "Enable RACCT/RCTL");
SYSCTL_UINT(_kern_racct, OID_AUTO, pcpu_threshold, CTLFLAG_RW, &pcpu_threshold,
    0, "Processes with higher %cpu usage than this value can be throttled.");

/*
 * How many seconds it takes to use the scheduler %cpu calculations.  When a
 * process starts, we compute its %cpu usage by dividing its runtime by the
 * process wall clock time.  After RACCT_PCPU_SECS pass, we use the value
 * provided by the scheduler.
 */
#define RACCT_PCPU_SECS		3

struct mtx racct_lock;
MTX_SYSINIT(racct_lock, &racct_lock, "racct lock", MTX_DEF);

static uma_zone_t racct_zone;

static void racct_sub_racct(struct racct *dest, const struct racct *src);
static void racct_sub_cred_locked(struct ucred *cred, int resource,
		uint64_t amount);
static void racct_add_cred_locked(struct ucred *cred, int resource,
		uint64_t amount);

SDT_PROVIDER_DEFINE(racct);
SDT_PROBE_DEFINE3(racct, , rusage, add,
    "struct proc *", "int", "uint64_t");
SDT_PROBE_DEFINE3(racct, , rusage, add__failure,
    "struct proc *", "int", "uint64_t");
SDT_PROBE_DEFINE3(racct, , rusage, add__buf,
    "struct proc *", "const struct buf *", "int");
SDT_PROBE_DEFINE3(racct, , rusage, add__cred,
    "struct ucred *", "int", "uint64_t");
SDT_PROBE_DEFINE3(racct, , rusage, add__force,
    "struct proc *", "int", "uint64_t");
SDT_PROBE_DEFINE3(racct, , rusage, set,
    "struct proc *", "int", "uint64_t");
SDT_PROBE_DEFINE3(racct, , rusage, set__failure,
    "struct proc *", "int", "uint64_t");
SDT_PROBE_DEFINE3(racct, , rusage, set__force,
    "struct proc *", "int", "uint64_t");
SDT_PROBE_DEFINE3(racct, , rusage, sub,
    "struct proc *", "int", "uint64_t");
SDT_PROBE_DEFINE3(racct, , rusage, sub__cred,
    "struct ucred *", "int", "uint64_t");
SDT_PROBE_DEFINE1(racct, , racct, create,
    "struct racct *");
SDT_PROBE_DEFINE1(racct, , racct, destroy,
    "struct racct *");
SDT_PROBE_DEFINE2(racct, , racct, join,
    "struct racct *", "struct racct *");
SDT_PROBE_DEFINE2(racct, , racct, join__failure,
    "struct racct *", "struct racct *");
SDT_PROBE_DEFINE2(racct, , racct, leave,
    "struct racct *", "struct racct *");

int racct_types[] = {
	[RACCT_CPU] =
		RACCT_IN_MILLIONS,
	[RACCT_DATA] =
		RACCT_RECLAIMABLE | RACCT_INHERITABLE | RACCT_DENIABLE,
	[RACCT_STACK] =
		RACCT_RECLAIMABLE | RACCT_INHERITABLE | RACCT_DENIABLE,
	[RACCT_CORE] =
		RACCT_DENIABLE,
	[RACCT_RSS] =
		RACCT_RECLAIMABLE,
	[RACCT_MEMLOCK] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE,
	[RACCT_NPROC] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE,
	[RACCT_NOFILE] =
		RACCT_RECLAIMABLE | RACCT_INHERITABLE | RACCT_DENIABLE,
	[RACCT_VMEM] =
		RACCT_RECLAIMABLE | RACCT_INHERITABLE | RACCT_DENIABLE,
	[RACCT_NPTS] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE | RACCT_SLOPPY,
	[RACCT_SWAP] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE | RACCT_SLOPPY,
	[RACCT_NTHR] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE,
	[RACCT_MSGQQUEUED] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE | RACCT_SLOPPY,
	[RACCT_MSGQSIZE] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE | RACCT_SLOPPY,
	[RACCT_NMSGQ] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE | RACCT_SLOPPY,
	[RACCT_NSEM] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE | RACCT_SLOPPY,
	[RACCT_NSEMOP] =
		RACCT_RECLAIMABLE | RACCT_INHERITABLE | RACCT_DENIABLE,
	[RACCT_NSHM] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE | RACCT_SLOPPY,
	[RACCT_SHMSIZE] =
		RACCT_RECLAIMABLE | RACCT_DENIABLE | RACCT_SLOPPY,
	[RACCT_WALLCLOCK] =
		RACCT_IN_MILLIONS,
	[RACCT_PCTCPU] =
		RACCT_DECAYING | RACCT_DENIABLE | RACCT_IN_MILLIONS,
	[RACCT_READBPS] =
		RACCT_DECAYING,
	[RACCT_WRITEBPS] =
		RACCT_DECAYING,
	[RACCT_READIOPS] =
		RACCT_DECAYING,
	[RACCT_WRITEIOPS] =
		RACCT_DECAYING };

static const fixpt_t RACCT_DECAY_FACTOR = 0.3 * FSCALE;

#ifdef SCHED_4BSD
/*
 * Contains intermediate values for %cpu calculations to avoid using floating
 * point in the kernel.
 * ccpu_exp[k] = FSCALE * (ccpu/FSCALE)^k = FSCALE * exp(-k/20)
 * It is needed only for the 4BSD scheduler, because in ULE, the ccpu equals to
 * zero so the calculations are more straightforward.
 */
fixpt_t ccpu_exp[] = {
	[0] = FSCALE * 1,
	[1] = FSCALE * 0.95122942450071400909,
	[2] = FSCALE * 0.90483741803595957316,
	[3] = FSCALE * 0.86070797642505780722,
	[4] = FSCALE * 0.81873075307798185866,
	[5] = FSCALE * 0.77880078307140486824,
	[6] = FSCALE * 0.74081822068171786606,
	[7] = FSCALE * 0.70468808971871343435,
	[8] = FSCALE * 0.67032004603563930074,
	[9] = FSCALE * 0.63762815162177329314,
	[10] = FSCALE * 0.60653065971263342360,
	[11] = FSCALE * 0.57694981038048669531,
	[12] = FSCALE * 0.54881163609402643262,
	[13] = FSCALE * 0.52204577676101604789,
	[14] = FSCALE * 0.49658530379140951470,
	[15] = FSCALE * 0.47236655274101470713,
	[16] = FSCALE * 0.44932896411722159143,
	[17] = FSCALE * 0.42741493194872666992,
	[18] = FSCALE * 0.40656965974059911188,
	[19] = FSCALE * 0.38674102345450120691,
	[20] = FSCALE * 0.36787944117144232159,
	[21] = FSCALE * 0.34993774911115535467,
	[22] = FSCALE * 0.33287108369807955328,
	[23] = FSCALE * 0.31663676937905321821,
	[24] = FSCALE * 0.30119421191220209664,
	[25] = FSCALE * 0.28650479686019010032,
	[26] = FSCALE * 0.27253179303401260312,
	[27] = FSCALE * 0.25924026064589150757,
	[28] = FSCALE * 0.24659696394160647693,
	[29] = FSCALE * 0.23457028809379765313,
	[30] = FSCALE * 0.22313016014842982893,
	[31] = FSCALE * 0.21224797382674305771,
	[32] = FSCALE * 0.20189651799465540848,
	[33] = FSCALE * 0.19204990862075411423,
	[34] = FSCALE * 0.18268352405273465022,
	[35] = FSCALE * 0.17377394345044512668,
	[36] = FSCALE * 0.16529888822158653829,
	[37] = FSCALE * 0.15723716631362761621,
	[38] = FSCALE * 0.14956861922263505264,
	[39] = FSCALE * 0.14227407158651357185,
	[40] = FSCALE * 0.13533528323661269189,
	[41] = FSCALE * 0.12873490358780421886,
	[42] = FSCALE * 0.12245642825298191021,
	[43] = FSCALE * 0.11648415777349695786,
	[44] = FSCALE * 0.11080315836233388333,
	[45] = FSCALE * 0.10539922456186433678,
	[46] = FSCALE * 0.10025884372280373372,
	[47] = FSCALE * 0.09536916221554961888,
	[48] = FSCALE * 0.09071795328941250337,
	[49] = FSCALE * 0.08629358649937051097,
	[50] = FSCALE * 0.08208499862389879516,
	[51] = FSCALE * 0.07808166600115315231,
	[52] = FSCALE * 0.07427357821433388042,
	[53] = FSCALE * 0.07065121306042958674,
	[54] = FSCALE * 0.06720551273974976512,
	[55] = FSCALE * 0.06392786120670757270,
	[56] = FSCALE * 0.06081006262521796499,
	[57] = FSCALE * 0.05784432087483846296,
	[58] = FSCALE * 0.05502322005640722902,
	[59] = FSCALE * 0.05233970594843239308,
	[60] = FSCALE * 0.04978706836786394297,
	[61] = FSCALE * 0.04735892439114092119,
	[62] = FSCALE * 0.04504920239355780606,
	[63] = FSCALE * 0.04285212686704017991,
	[64] = FSCALE * 0.04076220397836621516,
	[65] = FSCALE * 0.03877420783172200988,
	[66] = FSCALE * 0.03688316740124000544,
	[67] = FSCALE * 0.03508435410084502588,
	[68] = FSCALE * 0.03337326996032607948,
	[69] = FSCALE * 0.03174563637806794323,
	[70] = FSCALE * 0.03019738342231850073,
	[71] = FSCALE * 0.02872463965423942912,
	[72] = FSCALE * 0.02732372244729256080,
	[73] = FSCALE * 0.02599112877875534358,
	[74] = FSCALE * 0.02472352647033939120,
	[75] = FSCALE * 0.02351774585600910823,
	[76] = FSCALE * 0.02237077185616559577,
	[77] = FSCALE * 0.02127973643837716938,
	[78] = FSCALE * 0.02024191144580438847,
	[79] = FSCALE * 0.01925470177538692429,
	[80] = FSCALE * 0.01831563888873418029,
	[81] = FSCALE * 0.01742237463949351138,
	[82] = FSCALE * 0.01657267540176124754,
	[83] = FSCALE * 0.01576441648485449082,
	[84] = FSCALE * 0.01499557682047770621,
	[85] = FSCALE * 0.01426423390899925527,
	[86] = FSCALE * 0.01356855901220093175,
	[87] = FSCALE * 0.01290681258047986886,
	[88] = FSCALE * 0.01227733990306844117,
	[89] = FSCALE * 0.01167856697039544521,
	[90] = FSCALE * 0.01110899653824230649,
	[91] = FSCALE * 0.01056720438385265337,
	[92] = FSCALE * 0.01005183574463358164,
	[93] = FSCALE * 0.00956160193054350793,
	[94] = FSCALE * 0.00909527710169581709,
	[95] = FSCALE * 0.00865169520312063417,
	[96] = FSCALE * 0.00822974704902002884,
	[97] = FSCALE * 0.00782837754922577143,
	[98] = FSCALE * 0.00744658307092434051,
	[99] = FSCALE * 0.00708340892905212004,
	[100] = FSCALE * 0.00673794699908546709,
	[101] = FSCALE * 0.00640933344625638184,
	[102] = FSCALE * 0.00609674656551563610,
	[103] = FSCALE * 0.00579940472684214321,
	[104] = FSCALE * 0.00551656442076077241,
	[105] = FSCALE * 0.00524751839918138427,
	[106] = FSCALE * 0.00499159390691021621,
	[107] = FSCALE * 0.00474815099941147558,
	[108] = FSCALE * 0.00451658094261266798,
	[109] = FSCALE * 0.00429630469075234057,
	[110] = FSCALE * 0.00408677143846406699,
};
#endif

#define	CCPU_EXP_MAX	110

/*
 * This function is analogical to the getpcpu() function in the ps(1) command.
 * They should both calculate in the same way so that the racct %cpu
 * calculations are consistent with the values showed by the ps(1) tool.
 * The calculations are more complex in the 4BSD scheduler because of the value
 * of the ccpu variable.  In ULE it is defined to be zero which saves us some
 * work.
 */
static uint64_t
racct_getpcpu(struct proc *p, u_int pcpu)
{
	u_int swtime;
#ifdef SCHED_4BSD
	fixpt_t pctcpu, pctcpu_next;
#endif
#ifdef SMP
	struct pcpu *pc;
	int found;
#endif
	fixpt_t p_pctcpu;
	struct thread *td;

	ASSERT_RACCT_ENABLED();

	/*
	 * If the process is swapped out, we count its %cpu usage as zero.
	 * This behaviour is consistent with the userland ps(1) tool.
	 */
	if ((p->p_flag & P_INMEM) == 0)
		return (0);
	swtime = (ticks - p->p_swtick) / hz;

	/*
	 * For short-lived processes, the sched_pctcpu() returns small
	 * values even for cpu intensive processes.  Therefore we use
	 * our own estimate in this case.
	 */
	if (swtime < RACCT_PCPU_SECS)
		return (pcpu);

	p_pctcpu = 0;
	FOREACH_THREAD_IN_PROC(p, td) {
		if (td == PCPU_GET(idlethread))
			continue;
#ifdef SMP
		found = 0;
		STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
			if (td == pc->pc_idlethread) {
				found = 1;
				break;
			}
		}
		if (found)
			continue;
#endif
		thread_lock(td);
#ifdef SCHED_4BSD
		pctcpu = sched_pctcpu(td);
		/* Count also the yet unfinished second. */
		pctcpu_next = (pctcpu * ccpu_exp[1]) >> FSHIFT;
		pctcpu_next += sched_pctcpu_delta(td);
		p_pctcpu += max(pctcpu, pctcpu_next);
#else
		/*
		 * In ULE the %cpu statistics are updated on every
		 * sched_pctcpu() call.  So special calculations to
		 * account for the latest (unfinished) second are
		 * not needed.
		 */
		p_pctcpu += sched_pctcpu(td);
#endif
		thread_unlock(td);
	}

#ifdef SCHED_4BSD
	if (swtime <= CCPU_EXP_MAX)
		return ((100 * (uint64_t)p_pctcpu * 1000000) /
		    (FSCALE - ccpu_exp[swtime]));
#endif

	return ((100 * (uint64_t)p_pctcpu * 1000000) / FSCALE);
}

static void
racct_add_racct(struct racct *dest, const struct racct *src)
{
	int i;

	ASSERT_RACCT_ENABLED();
	RACCT_LOCK_ASSERT();

	/*
	 * Update resource usage in dest.
	 */
	for (i = 0; i <= RACCT_MAX; i++) {
		KASSERT(dest->r_resources[i] >= 0,
		    ("%s: resource %d propagation meltdown: dest < 0",
		    __func__, i));
		KASSERT(src->r_resources[i] >= 0,
		    ("%s: resource %d propagation meltdown: src < 0",
		    __func__, i));
		dest->r_resources[i] += src->r_resources[i];
	}
}

static void
racct_sub_racct(struct racct *dest, const struct racct *src)
{
	int i;

	ASSERT_RACCT_ENABLED();
	RACCT_LOCK_ASSERT();

	/*
	 * Update resource usage in dest.
	 */
	for (i = 0; i <= RACCT_MAX; i++) {
		if (!RACCT_IS_SLOPPY(i) && !RACCT_IS_DECAYING(i)) {
			KASSERT(dest->r_resources[i] >= 0,
			    ("%s: resource %d propagation meltdown: dest < 0",
			    __func__, i));
			KASSERT(src->r_resources[i] >= 0,
			    ("%s: resource %d propagation meltdown: src < 0",
			    __func__, i));
			KASSERT(src->r_resources[i] <= dest->r_resources[i],
			    ("%s: resource %d propagation meltdown: src > dest",
			    __func__, i));
		}
		if (RACCT_CAN_DROP(i)) {
			dest->r_resources[i] -= src->r_resources[i];
			if (dest->r_resources[i] < 0)
				dest->r_resources[i] = 0;
		}
	}
}

void
racct_create(struct racct **racctp)
{

	if (!racct_enable)
		return;

	SDT_PROBE1(racct, , racct, create, racctp);

	KASSERT(*racctp == NULL, ("racct already allocated"));

	*racctp = uma_zalloc(racct_zone, M_WAITOK | M_ZERO);
}

static void
racct_destroy_locked(struct racct **racctp)
{
	struct racct *racct;
	int i;

	ASSERT_RACCT_ENABLED();

	SDT_PROBE1(racct, , racct, destroy, racctp);

	RACCT_LOCK_ASSERT();
	KASSERT(racctp != NULL, ("NULL racctp"));
	KASSERT(*racctp != NULL, ("NULL racct"));

	racct = *racctp;

	for (i = 0; i <= RACCT_MAX; i++) {
		if (RACCT_IS_SLOPPY(i))
			continue;
		if (!RACCT_IS_RECLAIMABLE(i))
			continue;
		KASSERT(racct->r_resources[i] == 0,
		    ("destroying non-empty racct: "
		    "%ju allocated for resource %d\n",
		    racct->r_resources[i], i));
	}
	uma_zfree(racct_zone, racct);
	*racctp = NULL;
}

void
racct_destroy(struct racct **racct)
{

	if (!racct_enable)
		return;

	RACCT_LOCK();
	racct_destroy_locked(racct);
	RACCT_UNLOCK();
}

/*
 * Increase consumption of 'resource' by 'amount' for 'racct',
 * but not its parents.  Differently from other cases, 'amount' here
 * may be less than zero.
 */
static void
racct_adjust_resource(struct racct *racct, int resource,
    int64_t amount)
{

	ASSERT_RACCT_ENABLED();
	RACCT_LOCK_ASSERT();
	KASSERT(racct != NULL, ("NULL racct"));

	racct->r_resources[resource] += amount;
	if (racct->r_resources[resource] < 0) {
		KASSERT(RACCT_IS_SLOPPY(resource) || RACCT_IS_DECAYING(resource),
		    ("%s: resource %d usage < 0", __func__, resource));
		racct->r_resources[resource] = 0;
	}
	
	/*
	 * There are some cases where the racct %cpu resource would grow
	 * beyond 100% per core.  For example in racct_proc_exit() we add
	 * the process %cpu usage to the ucred racct containers.  If too
	 * many processes terminated in a short time span, the ucred %cpu
	 * resource could grow too much.  Also, the 4BSD scheduler sometimes
	 * returns for a thread more than 100% cpu usage. So we set a sane
	 * boundary here to 100% * the maxumum number of CPUs.
	 */
	if ((resource == RACCT_PCTCPU) &&
	    (racct->r_resources[RACCT_PCTCPU] > 100 * 1000000 * (int64_t)MAXCPU))
		racct->r_resources[RACCT_PCTCPU] = 100 * 1000000 * (int64_t)MAXCPU;
}

static int
racct_add_locked(struct proc *p, int resource, uint64_t amount, int force)
{
#ifdef RCTL
	int error;
#endif

	ASSERT_RACCT_ENABLED();

	/*
	 * We need proc lock to dereference p->p_ucred.
	 */
	PROC_LOCK_ASSERT(p, MA_OWNED);

#ifdef RCTL
	error = rctl_enforce(p, resource, amount);
	if (error && !force && RACCT_IS_DENIABLE(resource)) {
		SDT_PROBE3(racct, , rusage, add__failure, p, resource, amount);
		return (error);
	}
#endif
	racct_adjust_resource(p->p_racct, resource, amount);
	racct_add_cred_locked(p->p_ucred, resource, amount);

	return (0);
}

/*
 * Increase allocation of 'resource' by 'amount' for process 'p'.
 * Return 0 if it's below limits, or errno, if it's not.
 */
int
racct_add(struct proc *p, int resource, uint64_t amount)
{
	int error;

	if (!racct_enable)
		return (0);

	SDT_PROBE3(racct, , rusage, add, p, resource, amount);

	RACCT_LOCK();
	error = racct_add_locked(p, resource, amount, 0);
	RACCT_UNLOCK();
	return (error);
}

/*
 * Increase allocation of 'resource' by 'amount' for process 'p'.
 * Doesn't check for limits and never fails.
 */
void
racct_add_force(struct proc *p, int resource, uint64_t amount)
{

	if (!racct_enable)
		return;

	SDT_PROBE3(racct, , rusage, add__force, p, resource, amount);

	RACCT_LOCK();
	racct_add_locked(p, resource, amount, 1);
	RACCT_UNLOCK();
}

static void
racct_add_cred_locked(struct ucred *cred, int resource, uint64_t amount)
{
	struct prison *pr;

	ASSERT_RACCT_ENABLED();

	racct_adjust_resource(cred->cr_ruidinfo->ui_racct, resource, amount);
	for (pr = cred->cr_prison; pr != NULL; pr = pr->pr_parent)
		racct_adjust_resource(pr->pr_prison_racct->prr_racct, resource,
		    amount);
	racct_adjust_resource(cred->cr_loginclass->lc_racct, resource, amount);
}

/*
 * Increase allocation of 'resource' by 'amount' for credential 'cred'.
 * Doesn't check for limits and never fails.
 */
void
racct_add_cred(struct ucred *cred, int resource, uint64_t amount)
{

	if (!racct_enable)
		return;

	SDT_PROBE3(racct, , rusage, add__cred, cred, resource, amount);

	RACCT_LOCK();
	racct_add_cred_locked(cred, resource, amount);
	RACCT_UNLOCK();
}

/*
 * Account for disk IO resource consumption.  Checks for limits,
 * but never fails, due to disk limits being undeniable.
 */
void
racct_add_buf(struct proc *p, const struct buf *bp, int is_write)
{

	ASSERT_RACCT_ENABLED();
	PROC_LOCK_ASSERT(p, MA_OWNED);

	SDT_PROBE3(racct, , rusage, add__buf, p, bp, is_write);

	RACCT_LOCK();
	if (is_write) {
		racct_add_locked(curproc, RACCT_WRITEBPS, bp->b_bcount, 1);
		racct_add_locked(curproc, RACCT_WRITEIOPS, 1, 1);
	} else {
		racct_add_locked(curproc, RACCT_READBPS, bp->b_bcount, 1);
		racct_add_locked(curproc, RACCT_READIOPS, 1, 1);
	}
	RACCT_UNLOCK();
}

static int
racct_set_locked(struct proc *p, int resource, uint64_t amount, int force)
{
	int64_t old_amount, decayed_amount, diff_proc, diff_cred;
#ifdef RCTL
	int error;
#endif

	ASSERT_RACCT_ENABLED();

	/*
	 * We need proc lock to dereference p->p_ucred.
	 */
	PROC_LOCK_ASSERT(p, MA_OWNED);

	old_amount = p->p_racct->r_resources[resource];
	/*
	 * The diffs may be negative.
	 */
	diff_proc = amount - old_amount;
	if (resource == RACCT_PCTCPU) {
		/*
		 * Resources in per-credential racct containers may decay.
		 * If this is the case, we need to calculate the difference
		 * between the new amount and the proportional value of the
		 * old amount that has decayed in the ucred racct containers.
		 */
		decayed_amount = old_amount * RACCT_DECAY_FACTOR / FSCALE;
		diff_cred = amount - decayed_amount;
	} else
		diff_cred = diff_proc;
#ifdef notyet
	KASSERT(diff_proc >= 0 || RACCT_CAN_DROP(resource),
	    ("%s: usage of non-droppable resource %d dropping", __func__,
	     resource));
#endif
#ifdef RCTL
	if (diff_proc > 0) {
		error = rctl_enforce(p, resource, diff_proc);
		if (error && !force && RACCT_IS_DENIABLE(resource)) {
			SDT_PROBE3(racct, , rusage, set__failure, p, resource,
			    amount);
			return (error);
		}
	}
#endif
	racct_adjust_resource(p->p_racct, resource, diff_proc);
	if (diff_cred > 0)
		racct_add_cred_locked(p->p_ucred, resource, diff_cred);
	else if (diff_cred < 0)
		racct_sub_cred_locked(p->p_ucred, resource, -diff_cred);

	return (0);
}

/*
 * Set allocation of 'resource' to 'amount' for process 'p'.
 * Return 0 if it's below limits, or errno, if it's not.
 *
 * Note that decreasing the allocation always returns 0,
 * even if it's above the limit.
 */
int
racct_set_unlocked(struct proc *p, int resource, uint64_t amount)
{
	int error;

	ASSERT_RACCT_ENABLED();
	PROC_LOCK(p);
	error = racct_set(p, resource, amount);
	PROC_UNLOCK(p);
	return (error);
}

int
racct_set(struct proc *p, int resource, uint64_t amount)
{
	int error;

	if (!racct_enable)
		return (0);

	SDT_PROBE3(racct, , rusage, set__force, p, resource, amount);

	RACCT_LOCK();
	error = racct_set_locked(p, resource, amount, 0);
	RACCT_UNLOCK();
	return (error);
}

void
racct_set_force(struct proc *p, int resource, uint64_t amount)
{

	if (!racct_enable)
		return;

	SDT_PROBE3(racct, , rusage, set, p, resource, amount);

	RACCT_LOCK();
	racct_set_locked(p, resource, amount, 1);
	RACCT_UNLOCK();
}

/*
 * Returns amount of 'resource' the process 'p' can keep allocated.
 * Allocating more than that would be denied, unless the resource
 * is marked undeniable.  Amount of already allocated resource does
 * not matter.
 */
uint64_t
racct_get_limit(struct proc *p, int resource)
{
#ifdef RCTL
	uint64_t available;

	if (!racct_enable)
		return (UINT64_MAX);

	RACCT_LOCK();
	available = rctl_get_limit(p, resource);
	RACCT_UNLOCK();

	return (available);
#else

	return (UINT64_MAX);
#endif
}

/*
 * Returns amount of 'resource' the process 'p' can keep allocated.
 * Allocating more than that would be denied, unless the resource
 * is marked undeniable.  Amount of already allocated resource does
 * matter.
 */
uint64_t
racct_get_available(struct proc *p, int resource)
{
#ifdef RCTL
	uint64_t available;

	if (!racct_enable)
		return (UINT64_MAX);

	RACCT_LOCK();
	available = rctl_get_available(p, resource);
	RACCT_UNLOCK();

	return (available);
#else

	return (UINT64_MAX);
#endif
}

/*
 * Returns amount of the %cpu resource that process 'p' can add to its %cpu
 * utilization.  Adding more than that would lead to the process being
 * throttled.
 */
static int64_t
racct_pcpu_available(struct proc *p)
{
#ifdef RCTL
	uint64_t available;

	ASSERT_RACCT_ENABLED();

	RACCT_LOCK();
	available = rctl_pcpu_available(p);
	RACCT_UNLOCK();

	return (available);
#else

	return (INT64_MAX);
#endif
}

/*
 * Decrease allocation of 'resource' by 'amount' for process 'p'.
 */
void
racct_sub(struct proc *p, int resource, uint64_t amount)
{

	if (!racct_enable)
		return;

	SDT_PROBE3(racct, , rusage, sub, p, resource, amount);

	/*
	 * We need proc lock to dereference p->p_ucred.
	 */
	PROC_LOCK_ASSERT(p, MA_OWNED);
	KASSERT(RACCT_CAN_DROP(resource),
	    ("%s: called for non-droppable resource %d", __func__, resource));

	RACCT_LOCK();
	KASSERT(amount <= p->p_racct->r_resources[resource],
	    ("%s: freeing %ju of resource %d, which is more "
	     "than allocated %jd for %s (pid %d)", __func__, amount, resource,
	    (intmax_t)p->p_racct->r_resources[resource], p->p_comm, p->p_pid));

	racct_adjust_resource(p->p_racct, resource, -amount);
	racct_sub_cred_locked(p->p_ucred, resource, amount);
	RACCT_UNLOCK();
}

static void
racct_sub_cred_locked(struct ucred *cred, int resource, uint64_t amount)
{
	struct prison *pr;

	ASSERT_RACCT_ENABLED();

	racct_adjust_resource(cred->cr_ruidinfo->ui_racct, resource, -amount);
	for (pr = cred->cr_prison; pr != NULL; pr = pr->pr_parent)
		racct_adjust_resource(pr->pr_prison_racct->prr_racct, resource,
		    -amount);
	racct_adjust_resource(cred->cr_loginclass->lc_racct, resource, -amount);
}

/*
 * Decrease allocation of 'resource' by 'amount' for credential 'cred'.
 */
void
racct_sub_cred(struct ucred *cred, int resource, uint64_t amount)
{

	if (!racct_enable)
		return;

	SDT_PROBE3(racct, , rusage, sub__cred, cred, resource, amount);

#ifdef notyet
	KASSERT(RACCT_CAN_DROP(resource),
	    ("%s: called for resource %d which can not drop", __func__,
	     resource));
#endif

	RACCT_LOCK();
	racct_sub_cred_locked(cred, resource, amount);
	RACCT_UNLOCK();
}

/*
 * Inherit resource usage information from the parent process.
 */
int
racct_proc_fork(struct proc *parent, struct proc *child)
{
	int i, error = 0;

	if (!racct_enable)
		return (0);

	/*
	 * Create racct for the child process.
	 */
	racct_create(&child->p_racct);

	PROC_LOCK(parent);
	PROC_LOCK(child);
	RACCT_LOCK();

#ifdef RCTL
	error = rctl_proc_fork(parent, child);
	if (error != 0)
		goto out;
#endif

	/* Init process cpu time. */
	child->p_prev_runtime = 0;
	child->p_throttled = 0;

	/*
	 * Inherit resource usage.
	 */
	for (i = 0; i <= RACCT_MAX; i++) {
		if (parent->p_racct->r_resources[i] == 0 ||
		    !RACCT_IS_INHERITABLE(i))
			continue;

		error = racct_set_locked(child, i,
		    parent->p_racct->r_resources[i], 0);
		if (error != 0)
			goto out;
	}

	error = racct_add_locked(child, RACCT_NPROC, 1, 0);
	error += racct_add_locked(child, RACCT_NTHR, 1, 0);

out:
	RACCT_UNLOCK();
	PROC_UNLOCK(child);
	PROC_UNLOCK(parent);

	if (error != 0)
		racct_proc_exit(child);

	return (error);
}

/*
 * Called at the end of fork1(), to handle rules that require the process
 * to be fully initialized.
 */
void
racct_proc_fork_done(struct proc *child)
{

	if (!racct_enable)
		return;

#ifdef RCTL
	PROC_LOCK(child);
	RACCT_LOCK();
	rctl_enforce(child, RACCT_NPROC, 0);
	rctl_enforce(child, RACCT_NTHR, 0);
	RACCT_UNLOCK();
	PROC_UNLOCK(child);
#endif
}

void
racct_proc_exit(struct proc *p)
{
	struct timeval wallclock;
	uint64_t pct_estimate, pct, runtime;
	int i;

	if (!racct_enable)
		return;

	PROC_LOCK(p);
	/*
	 * We don't need to calculate rux, proc_reap() has already done this.
	 */
	runtime = cputick2usec(p->p_rux.rux_runtime);
#ifdef notyet
	KASSERT(runtime >= p->p_prev_runtime, ("runtime < p_prev_runtime"));
#else
	if (runtime < p->p_prev_runtime)
		runtime = p->p_prev_runtime;
#endif
	microuptime(&wallclock);
	timevalsub(&wallclock, &p->p_stats->p_start);
	if (wallclock.tv_sec > 0 || wallclock.tv_usec > 0) {
		pct_estimate = (1000000 * runtime * 100) /
		    ((uint64_t)wallclock.tv_sec * 1000000 +
		    wallclock.tv_usec);
	} else
		pct_estimate = 0;
	pct = racct_getpcpu(p, pct_estimate);

	RACCT_LOCK();
	racct_set_locked(p, RACCT_CPU, runtime, 0);
	racct_add_cred_locked(p->p_ucred, RACCT_PCTCPU, pct);

	KASSERT(p->p_racct->r_resources[RACCT_RSS] == 0,
	    ("process reaped with %ju allocated for RSS\n",
	    p->p_racct->r_resources[RACCT_RSS]));
	for (i = 0; i <= RACCT_MAX; i++) {
		if (p->p_racct->r_resources[i] == 0)
			continue;
		if (!RACCT_IS_RECLAIMABLE(i))
			continue;
		racct_set_locked(p, i, 0, 0);
	}

#ifdef RCTL
	rctl_racct_release(p->p_racct);
#endif
	racct_destroy_locked(&p->p_racct);
	RACCT_UNLOCK();
	PROC_UNLOCK(p);
}

/*
 * Called after credentials change, to move resource utilisation
 * between raccts.
 */
void
racct_proc_ucred_changed(struct proc *p, struct ucred *oldcred,
    struct ucred *newcred)
{
	struct uidinfo *olduip, *newuip;
	struct loginclass *oldlc, *newlc;
	struct prison *oldpr, *newpr, *pr;

	if (!racct_enable)
		return;

	PROC_LOCK_ASSERT(p, MA_OWNED);

	newuip = newcred->cr_ruidinfo;
	olduip = oldcred->cr_ruidinfo;
	newlc = newcred->cr_loginclass;
	oldlc = oldcred->cr_loginclass;
	newpr = newcred->cr_prison;
	oldpr = oldcred->cr_prison;

	RACCT_LOCK();
	if (newuip != olduip) {
		racct_sub_racct(olduip->ui_racct, p->p_racct);
		racct_add_racct(newuip->ui_racct, p->p_racct);
	}
	if (newlc != oldlc) {
		racct_sub_racct(oldlc->lc_racct, p->p_racct);
		racct_add_racct(newlc->lc_racct, p->p_racct);
	}
	if (newpr != oldpr) {
		for (pr = oldpr; pr != NULL; pr = pr->pr_parent)
			racct_sub_racct(pr->pr_prison_racct->prr_racct,
			    p->p_racct);
		for (pr = newpr; pr != NULL; pr = pr->pr_parent)
			racct_add_racct(pr->pr_prison_racct->prr_racct,
			    p->p_racct);
	}
	RACCT_UNLOCK();
}

void
racct_move(struct racct *dest, struct racct *src)
{

	ASSERT_RACCT_ENABLED();

	RACCT_LOCK();
	racct_add_racct(dest, src);
	racct_sub_racct(src, src);
	RACCT_UNLOCK();
}

void
racct_proc_throttled(struct proc *p)
{

	ASSERT_RACCT_ENABLED();

	PROC_LOCK(p);
	while (p->p_throttled != 0) {
		msleep(p->p_racct, &p->p_mtx, 0, "racct",
		    p->p_throttled < 0 ? 0 : p->p_throttled);
		if (p->p_throttled > 0)
			p->p_throttled = 0;
	}
	PROC_UNLOCK(p);
}

/*
 * Make the process sleep in userret() for 'timeout' ticks.  Setting
 * timeout to -1 makes it sleep until woken up by racct_proc_wakeup().
 */
void
racct_proc_throttle(struct proc *p, int timeout)
{
	struct thread *td;
#ifdef SMP
	int cpuid;
#endif

	KASSERT(timeout != 0, ("timeout %d", timeout));
	ASSERT_RACCT_ENABLED();
	PROC_LOCK_ASSERT(p, MA_OWNED);

	/*
	 * Do not block kernel processes.  Also do not block processes with
	 * low %cpu utilization to improve interactivity.
	 */
	if ((p->p_flag & (P_SYSTEM | P_KPROC)) != 0)
		return;

	if (p->p_throttled < 0 || (timeout > 0 && p->p_throttled > timeout))
		return;

	p->p_throttled = timeout;

	FOREACH_THREAD_IN_PROC(p, td) {
		thread_lock(td);
		switch (td->td_state) {
		case TDS_RUNQ:
			/*
			 * If the thread is on the scheduler run-queue, we can
			 * not just remove it from there.  So we set the flag
			 * TDF_NEEDRESCHED for the thread, so that once it is
			 * running, it is taken off the cpu as soon as possible.
			 */
			td->td_flags |= TDF_NEEDRESCHED;
			break;
		case TDS_RUNNING:
			/*
			 * If the thread is running, we request a context
			 * switch for it by setting the TDF_NEEDRESCHED flag.
			 */
			td->td_flags |= TDF_NEEDRESCHED;
#ifdef SMP
			cpuid = td->td_oncpu;
			if ((cpuid != NOCPU) && (td != curthread))
				ipi_cpu(cpuid, IPI_AST);
#endif
			break;
		default:
			break;
		}
		thread_unlock(td);
	}
}

static void
racct_proc_wakeup(struct proc *p)
{

	ASSERT_RACCT_ENABLED();

	PROC_LOCK_ASSERT(p, MA_OWNED);

	if (p->p_throttled != 0) {
		p->p_throttled = 0;
		wakeup(p->p_racct);
	}
}

static void
racct_decay_callback(struct racct *racct, void *dummy1, void *dummy2)
{
	int64_t r_old, r_new;

	ASSERT_RACCT_ENABLED();
	RACCT_LOCK_ASSERT();

#ifdef RCTL
	rctl_throttle_decay(racct, RACCT_READBPS);
	rctl_throttle_decay(racct, RACCT_WRITEBPS);
	rctl_throttle_decay(racct, RACCT_READIOPS);
	rctl_throttle_decay(racct, RACCT_WRITEIOPS);
#endif

	r_old = racct->r_resources[RACCT_PCTCPU];

	/* If there is nothing to decay, just exit. */
	if (r_old <= 0)
		return;

	r_new = r_old * RACCT_DECAY_FACTOR / FSCALE;
	racct->r_resources[RACCT_PCTCPU] = r_new;
}

static void
racct_decay_pre(void)
{

	RACCT_LOCK();
}

static void
racct_decay_post(void)
{

	RACCT_UNLOCK();
}

static void
racct_decay(void)
{

	ASSERT_RACCT_ENABLED();

	ui_racct_foreach(racct_decay_callback, racct_decay_pre,
	    racct_decay_post, NULL, NULL);
	loginclass_racct_foreach(racct_decay_callback, racct_decay_pre,
	    racct_decay_post, NULL, NULL);
	prison_racct_foreach(racct_decay_callback, racct_decay_pre,
	    racct_decay_post, NULL, NULL);
}

static void
racctd(void)
{
	struct thread *td;
	struct proc *p;
	struct timeval wallclock;
	uint64_t pct, pct_estimate, runtime;

	ASSERT_RACCT_ENABLED();

	for (;;) {
		racct_decay();

		sx_slock(&allproc_lock);

		sx_slock(&zombproc_lock);
		LIST_FOREACH(p, &zombproc, p_list) {
			PROC_LOCK(p);
			racct_set(p, RACCT_PCTCPU, 0);
			PROC_UNLOCK(p);
		}
		sx_sunlock(&zombproc_lock);

		FOREACH_PROC_IN_SYSTEM(p) {
			PROC_LOCK(p);
			if (p->p_state != PRS_NORMAL) {
				PROC_UNLOCK(p);
				continue;
			}

			microuptime(&wallclock);
			timevalsub(&wallclock, &p->p_stats->p_start);
			PROC_STATLOCK(p);
			FOREACH_THREAD_IN_PROC(p, td)
				ruxagg(p, td);
			runtime = cputick2usec(p->p_rux.rux_runtime);
			PROC_STATUNLOCK(p);
#ifdef notyet
			KASSERT(runtime >= p->p_prev_runtime,
			    ("runtime < p_prev_runtime"));
#else
			if (runtime < p->p_prev_runtime)
				runtime = p->p_prev_runtime;
#endif
			p->p_prev_runtime = runtime;
			if (wallclock.tv_sec > 0 || wallclock.tv_usec > 0) {
				pct_estimate = (1000000 * runtime * 100) /
				    ((uint64_t)wallclock.tv_sec * 1000000 +
				    wallclock.tv_usec);
			} else
				pct_estimate = 0;
			pct = racct_getpcpu(p, pct_estimate);
			RACCT_LOCK();
#ifdef RCTL
			rctl_throttle_decay(p->p_racct, RACCT_READBPS);
			rctl_throttle_decay(p->p_racct, RACCT_WRITEBPS);
			rctl_throttle_decay(p->p_racct, RACCT_READIOPS);
			rctl_throttle_decay(p->p_racct, RACCT_WRITEIOPS);
#endif
			racct_set_locked(p, RACCT_PCTCPU, pct, 1);
			racct_set_locked(p, RACCT_CPU, runtime, 0);
			racct_set_locked(p, RACCT_WALLCLOCK,
			    (uint64_t)wallclock.tv_sec * 1000000 +
			    wallclock.tv_usec, 0);
			RACCT_UNLOCK();
			PROC_UNLOCK(p);
		}

		/*
		 * To ensure that processes are throttled in a fair way, we need
		 * to iterate over all processes again and check the limits
		 * for %cpu resource only after ucred racct containers have been
		 * properly filled.
		 */
		FOREACH_PROC_IN_SYSTEM(p) {
			PROC_LOCK(p);
			if (p->p_state != PRS_NORMAL) {
				PROC_UNLOCK(p);
				continue;
			}

			if (racct_pcpu_available(p) <= 0) {
				if (p->p_racct->r_resources[RACCT_PCTCPU] >
				    pcpu_threshold)
					racct_proc_throttle(p, -1);
			} else if (p->p_throttled == -1) {
				racct_proc_wakeup(p);
			}
			PROC_UNLOCK(p);
		}
		sx_sunlock(&allproc_lock);
		pause("-", hz);
	}
}

static struct kproc_desc racctd_kp = {
	"racctd",
	racctd,
	NULL
};

static void
racctd_init(void)
{
	if (!racct_enable)
		return;

	kproc_start(&racctd_kp);
}
SYSINIT(racctd, SI_SUB_RACCTD, SI_ORDER_FIRST, racctd_init, NULL);

static void
racct_init(void)
{
	if (!racct_enable)
		return;

	racct_zone = uma_zcreate("racct", sizeof(struct racct),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
	/*
	 * XXX: Move this somewhere.
	 */
	prison0.pr_prison_racct = prison_racct_find("0");
}
SYSINIT(racct, SI_SUB_RACCT, SI_ORDER_FIRST, racct_init, NULL);

#endif /* !RACCT */
