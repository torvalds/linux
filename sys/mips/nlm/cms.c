/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2003-2011 Netlogic Microsystems (Netlogic). All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Netlogic Microsystems ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NETLOGIC_BSD */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/limits.h>
#include <sys/bus.h>
#include <sys/sbuf.h>

#include <sys/ktr.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sched.h>
#include <sys/unistd.h>
#include <sys/sysctl.h>
#include <sys/malloc.h>

#include <machine/reg.h>
#include <machine/cpu.h>
#include <machine/hwfunc.h>
#include <machine/mips_opcode.h>
#include <machine/intr_machdep.h>

#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/cop2.h>
#include <mips/nlm/hal/fmn.h>
#include <mips/nlm/hal/pic.h>

#include <mips/nlm/msgring.h>
#include <mips/nlm/interrupt.h>
#include <mips/nlm/xlp.h>

#define	MSGRNG_NSTATIONS	1024
/*
 * Keep track of our message ring handler threads, each core has a
 * different message station. Ideally we will need to start a few
 * message handling threads every core, and wake them up depending on
 * load
 */
struct msgring_thread {
	struct thread	*thread;	/* msgring handler threads */
	int	needed;			/* thread needs to wake up */
};
static struct msgring_thread msgring_threads[XLP_MAX_CORES * XLP_MAX_THREADS];
static struct proc *msgring_proc;	/* all threads are under a proc */

/*
 * The device drivers can register a handler for the messages sent
 * from a station (corresponding to the device).
 */
struct tx_stn_handler {
	msgring_handler action;
	void *arg;
};
static struct tx_stn_handler msgmap[MSGRNG_NSTATIONS];
static struct mtx	msgmap_lock;
uint32_t xlp_msg_thread_mask;
static int xlp_msg_threads_per_core = XLP_MAX_THREADS;

static void create_msgring_thread(int hwtid);
static int msgring_process_fast_intr(void *arg);

/* Debug counters */
static int msgring_nintr[XLP_MAX_CORES * XLP_MAX_THREADS];
static int msgring_wakeup_sleep[XLP_MAX_CORES * XLP_MAX_THREADS];
static int msgring_wakeup_nosleep[XLP_MAX_CORES * XLP_MAX_THREADS];
static int fmn_msgcount[XLP_MAX_CORES * XLP_MAX_THREADS][4];
static int fmn_loops[XLP_MAX_CORES * XLP_MAX_THREADS];

/* Whether polled driver implementation */
static int polled = 0;

/* We do only i/o device credit setup here. CPU credit setup is now
 * moved to xlp_msgring_cpu_init() so that the credits get setup
 * only if the CPU exists. xlp_msgring_cpu_init() gets called from
 * platform_init_ap; and this makes it easy for us to setup CMS
 * credits for various types of XLP chips, with varying number of
 * cpu's and cores.
 */
static void
xlp_cms_credit_setup(int credit)
{
	uint64_t cmspcibase, cmsbase, pcibase;
	uint32_t devoffset;
	int dev, fn, maxqid;
	int src, qid, i;

	for (i = 0; i < XLP_MAX_NODES; i++) {
		cmspcibase = nlm_get_cms_pcibase(i);
		if (!nlm_dev_exists(XLP_IO_CMS_OFFSET(i)))
			continue;
		cmsbase = nlm_get_cms_regbase(i);
		maxqid = nlm_read_reg(cmspcibase, XLP_PCI_DEVINFO_REG0);
		for (dev = 0; dev < 8; dev++) {
			for (fn = 0; fn < 8; fn++) {
				devoffset = XLP_HDR_OFFSET(i, 0, dev, fn);
				if (nlm_dev_exists(devoffset) == 0)
					continue;
				pcibase = nlm_pcicfg_base(devoffset);
				src = nlm_qidstart(pcibase);
				if (src == 0)
					continue;
#if 0 /* Debug */
				printf("Setup CMS credits for queues ");
				printf("[%d to %d] from src %d\n", 0,
				    maxqid, src);
#endif
				for (qid = 0; qid < maxqid; qid++)
					nlm_cms_setup_credits(cmsbase, qid,
					    src, credit);
			}
		}
	}
}

void
xlp_msgring_cpu_init(int node, int cpu, int credit)
{
	uint64_t cmspcibase = nlm_get_cms_pcibase(node);
	uint64_t cmsbase = nlm_get_cms_regbase(node);
	int qid, maxqid, src;

	maxqid = nlm_read_reg(cmspcibase, XLP_PCI_DEVINFO_REG0);

	/* cpu credit setup is done only from thread-0 of each core */
	if((cpu % 4) == 0) {
		src = cpu << 2; /* each thread has 4 vc's */
		for (qid = 0; qid < maxqid; qid++)
			nlm_cms_setup_credits(cmsbase, qid, src, credit);
	}
}

/*
 * Drain out max_messages for the buckets set in the bucket mask.
 * Use max_msgs = 0 to drain out all messages.
 */
int
xlp_handle_msg_vc(u_int vcmask, int max_msgs)
{
	struct nlm_fmn_msg msg;
	int srcid = 0, size = 0, code = 0;
	struct tx_stn_handler *he;
	uint32_t mflags, status;
	int n_msgs = 0, vc, m, hwtid;
	u_int msgmask;

	hwtid = nlm_cpuid();
	for (;;) {
		/* check if VC empty */
		mflags = nlm_save_flags_cop2();
		status = nlm_read_c2_msgstatus1();
		nlm_restore_flags(mflags);

		msgmask = ((status >> 24) & 0xf) ^ 0xf;
		msgmask &= vcmask;
		if (msgmask == 0)
			    break;
		m = 0;
		for (vc = 0; vc < 4; vc++) {
			if ((msgmask & (1 << vc)) == 0)
				continue;

			mflags = nlm_save_flags_cop2();
			status = nlm_fmn_msgrcv(vc, &srcid, &size, &code,
			    &msg);
			nlm_restore_flags(mflags);
			if (status != 0)	/*  no msg or error */
				continue;
			if (srcid < 0 || srcid >= 1024) {
				printf("[%s]: bad src id %d\n", __func__,
				    srcid);
				continue;
			}
			he = &msgmap[srcid];
			if(he->action != NULL)
				(he->action)(vc, size, code, srcid, &msg,
				he->arg);
#if 0
			else
				printf("[%s]: No Handler for msg from stn %d,"
				    " vc=%d, size=%d, msg0=%jx, droppinge\n",
				    __func__, srcid, vc, size,
				    (uintmax_t)msg.msg[0]);
#endif
			fmn_msgcount[hwtid][vc] += 1;
			m++;	/* msgs handled in this iter */
		}
		if (m == 0)
			break;	/* nothing done in this iter */
		n_msgs += m;
		if (max_msgs > 0 && n_msgs >= max_msgs)
			break;
	}

	return (n_msgs);
}

static void
xlp_discard_msg_vc(u_int vcmask)
{
	struct nlm_fmn_msg msg;
	int srcid = 0, size = 0, code = 0, vc;
	uint32_t mflags, status;

	for (vc = 0; vc < 4; vc++) {
		for (;;) {
			mflags = nlm_save_flags_cop2();
			status = nlm_fmn_msgrcv(vc, &srcid,
			    &size, &code, &msg);
			nlm_restore_flags(mflags);

			/* break if there is no msg or error */
			if (status != 0)
				break;
		}
	}
}

void
xlp_cms_enable_intr(int node, int cpu, int type, int watermark)
{
	uint64_t cmsbase;
	int i, qid;

	cmsbase = nlm_get_cms_regbase(node);

	for (i = 0; i < 4; i++) {
		qid = (i + (cpu * 4)) & 0x7f;
		nlm_cms_per_queue_level_intr(cmsbase, qid, type, watermark);
		nlm_cms_per_queue_timer_intr(cmsbase, qid, 0x1, 0);
	}
}

static int
msgring_process_fast_intr(void *arg)
{
	struct msgring_thread *mthd;
	struct thread *td;
	int	cpu;

	cpu = nlm_cpuid();
	mthd = &msgring_threads[cpu];
	msgring_nintr[cpu]++;
	td = mthd->thread;

	/* clear pending interrupts */
	nlm_write_c0_eirr(1ULL << IRQ_MSGRING);

	/* wake up the target thread */
	mthd->needed = 1;
	thread_lock(td);
	if (TD_AWAITING_INTR(td)) {
		msgring_wakeup_sleep[cpu]++;
		TD_CLR_IWAIT(td);
		sched_add(td, SRQ_INTR);
	} else
		msgring_wakeup_nosleep[cpu]++;

	thread_unlock(td);

	return (FILTER_HANDLED);
}

static void
msgring_process(void * arg)
{
	volatile struct msgring_thread *mthd;
	struct thread *td;
	uint32_t mflags, msgstatus1;
	int hwtid, nmsgs;

	hwtid = (intptr_t)arg;
	mthd = &msgring_threads[hwtid];
	td = mthd->thread;
	KASSERT(curthread == td,
	    ("%s:msg_ithread and proc linkage out of sync", __func__));

	/* First bind this thread to the right CPU */
	thread_lock(td);
	sched_bind(td, xlp_hwtid_to_cpuid[hwtid]);
	thread_unlock(td);

	if (hwtid != nlm_cpuid())
		printf("Misscheduled hwtid %d != cpuid %d\n", hwtid,
		    nlm_cpuid());

	xlp_discard_msg_vc(0xf);
	xlp_msgring_cpu_init(nlm_nodeid(), nlm_cpuid(), CMS_DEFAULT_CREDIT);
	if (polled == 0) {
		mflags = nlm_save_flags_cop2();
		nlm_fmn_cpu_init(IRQ_MSGRING, 0, 0, 0, 0, 0);
		nlm_restore_flags(mflags);
		xlp_cms_enable_intr(nlm_nodeid(), nlm_cpuid(), 0x2, 0);
		/* clear pending interrupts.
		 *  they will get re-raised if still valid */
		nlm_write_c0_eirr(1ULL << IRQ_MSGRING);
	}

	/* start processing messages */
	for (;;) {
		atomic_store_rel_int(&mthd->needed, 0);
		nmsgs = xlp_handle_msg_vc(0xf, 0);

		/* sleep */
		if (polled == 0) {
			/* clear VC-pend bits */
			mflags = nlm_save_flags_cop2();
			msgstatus1 = nlm_read_c2_msgstatus1();
			msgstatus1 |= (0xf << 16);
			nlm_write_c2_msgstatus1(msgstatus1);
			nlm_restore_flags(mflags);

			thread_lock(td);
			if (mthd->needed) {
				thread_unlock(td);
				continue;
			}
			sched_class(td, PRI_ITHD);
			TD_SET_IWAIT(td);
			mi_switch(SW_VOL, NULL);
			thread_unlock(td);
		} else
			pause("wmsg", 1);

		fmn_loops[hwtid]++;
	}
}

static void
create_msgring_thread(int hwtid)
{
	struct msgring_thread *mthd;
	struct thread *td;
	int	error;

	mthd = &msgring_threads[hwtid];
	error = kproc_kthread_add(msgring_process, (void *)(uintptr_t)hwtid,
	    &msgring_proc, &td, RFSTOPPED, 2, "msgrngproc",
	    "msgthr%d", hwtid);
	if (error)
		panic("kproc_kthread_add() failed with %d", error);
	mthd->thread = td;

	thread_lock(td);
	sched_class(td, PRI_ITHD);
	sched_add(td, SRQ_INTR);
	thread_unlock(td);
}

int
register_msgring_handler(int startb, int endb, msgring_handler action,
    void *arg)
{
	int	i;

	if (bootverbose)
		printf("Register handler %d-%d %p(%p)\n",
		    startb, endb, action, arg);
	KASSERT(startb >= 0 && startb <= endb && endb < MSGRNG_NSTATIONS,
	    ("Invalid value for bucket range %d,%d", startb, endb));

	mtx_lock_spin(&msgmap_lock);
	for (i = startb; i <= endb; i++) {
		KASSERT(msgmap[i].action == NULL,
		   ("Bucket %d already used [action %p]", i, msgmap[i].action));
		msgmap[i].action = action;
		msgmap[i].arg = arg;
	}
	mtx_unlock_spin(&msgmap_lock);
	return (0);
}

/*
 * Initialize the messaging subsystem.
 *
 * Message Stations are shared among all threads in a cpu core, this
 * has to be called once from every core which is online.
 */
static void
xlp_msgring_config(void *arg)
{
	void *cookie;
	unsigned int thrmask, mask;
	int i;

	/* used polled handler for Ax silion */
	if (nlm_is_xlp8xx_ax())
		polled = 1;

	/* Don't poll on all threads, if polled */
	if (polled)
		xlp_msg_threads_per_core -= 1;

	mtx_init(&msgmap_lock, "msgring", NULL, MTX_SPIN);
	if (xlp_threads_per_core < xlp_msg_threads_per_core)
		xlp_msg_threads_per_core = xlp_threads_per_core;
	thrmask = ((1 << xlp_msg_threads_per_core) - 1);
	mask = 0;
	for (i = 0; i < XLP_MAX_CORES; i++) {
		mask <<= XLP_MAX_THREADS;
		mask |= thrmask;
	}
	xlp_msg_thread_mask = xlp_hw_thread_mask & mask;
#if 0
	printf("CMS Message handler thread mask %#jx\n",
	    (uintmax_t)xlp_msg_thread_mask);
#endif
	xlp_cms_credit_setup(CMS_DEFAULT_CREDIT);
	create_msgring_thread(0);
	cpu_establish_hardintr("msgring", msgring_process_fast_intr, NULL,
	    NULL, IRQ_MSGRING, INTR_TYPE_NET, &cookie);
}

/*
 * Start message ring processing threads on other CPUs, after SMP start
 */
static void
start_msgring_threads(void *arg)
{
	int	hwt;

	for (hwt = 1; hwt < XLP_MAX_CORES * XLP_MAX_THREADS; hwt++) {
		if ((xlp_msg_thread_mask & (1 << hwt)) == 0)
			continue;
		create_msgring_thread(hwt);
	}
}

SYSINIT(xlp_msgring_config, SI_SUB_DRIVERS, SI_ORDER_FIRST,
    xlp_msgring_config, NULL);
SYSINIT(start_msgring_threads, SI_SUB_SMP, SI_ORDER_MIDDLE,
    start_msgring_threads, NULL);

/*
 * DEBUG support, XXX: static buffer, not locked
 */
static int
sys_print_debug(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	int error, i;

	sbuf_new_for_sysctl(&sb, NULL, 64, req);
	sbuf_printf(&sb, 
	    "\nID     vc0       vc1       vc2     vc3     loops\n");
	for (i = 0; i < 32; i++) {
		if ((xlp_hw_thread_mask & (1 << i)) == 0)
			continue;
		sbuf_printf(&sb, "%2d: %8d %8d %8d %8d %8d\n", i,
		    fmn_msgcount[i][0], fmn_msgcount[i][1],
		    fmn_msgcount[i][2], fmn_msgcount[i][3],
		    fmn_loops[i]);
	}
	error = sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (error);
}

SYSCTL_PROC(_debug, OID_AUTO, msgring, CTLTYPE_STRING | CTLFLAG_RD, 0, 0,
    sys_print_debug, "A", "msgring debug info");
