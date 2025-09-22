/*-
 * Copyright (c) 2009-2012 Microsoft Corp.
 * Copyright (c) 2012 NetApp Inc.
 * Copyright (c) 2012 Citrix Inc.
 * Copyright (c) 2016 Mike Belopuhov <mike@esdenera.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * The OpenBSD port was done under funding by Esdenera Networks GmbH.
 */

#include <sys/param.h>

/* Hyperv requires locked atomic operations */
#ifndef MULTIPROCESSOR
#define _HYPERVMPATOMICS
#define MULTIPROCESSOR
#endif
#include <sys/atomic.h>
#ifdef _HYPERVMPATOMICS
#undef MULTIPROCESSOR
#undef _HYPERVMPATOMICS
#endif

#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/timetc.h>
#include <sys/task.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>

#include <uvm/uvm_extern.h>

#include <machine/i82489var.h>

#include <dev/pv/pvvar.h>
#include <dev/pv/hypervreg.h>
#include <dev/pv/hypervvar.h>

/* Command submission flags */
#define HCF_SLEEPOK	0x0001	/* M_WAITOK */
#define HCF_NOSLEEP	0x0002	/* M_NOWAIT */
#define HCF_NOREPLY	0x0004

struct hv_softc *hv_sc;

int 	hv_match(struct device *, void *, void *);
void	hv_attach(struct device *, struct device *, void *);
void	hv_set_version(struct hv_softc *);
u_int	hv_gettime(struct timecounter *);
int	hv_init_hypercall(struct hv_softc *);
uint64_t hv_hypercall(struct hv_softc *, uint64_t, void *, void *);
int	hv_init_interrupts(struct hv_softc *);
int	hv_init_synic(struct hv_softc *);
int	hv_cmd(struct hv_softc *, void *, size_t, void *, size_t, int);
int	hv_start(struct hv_softc *, struct hv_msg *);
int	hv_reply(struct hv_softc *, struct hv_msg *);
void	hv_wait(struct hv_softc *, int (*done)(struct hv_softc *,
	    struct hv_msg *), struct hv_msg *, void *, const char *);
uint16_t hv_intr_signal(struct hv_softc *, void *);
void	hv_intr(void);
void	hv_event_intr(struct hv_softc *);
void	hv_message_intr(struct hv_softc *);
int	hv_vmbus_connect(struct hv_softc *);
void	hv_channel_response(struct hv_softc *, struct vmbus_chanmsg_hdr *);
void	hv_channel_offer(struct hv_softc *, struct vmbus_chanmsg_hdr *);
void	hv_channel_rescind(struct hv_softc *, struct vmbus_chanmsg_hdr *);
void	hv_channel_delivered(struct hv_softc *, struct vmbus_chanmsg_hdr *);
int	hv_channel_scan(struct hv_softc *);
void	hv_process_offer(struct hv_softc *, struct hv_offer *);
struct hv_channel *
	hv_channel_lookup(struct hv_softc *, uint32_t);
int	hv_channel_ring_create(struct hv_channel *, uint32_t);
void	hv_channel_ring_destroy(struct hv_channel *);
void	hv_channel_pause(struct hv_channel *);
uint	hv_channel_unpause(struct hv_channel *);
uint	hv_channel_ready(struct hv_channel *);
extern void hv_attach_icdevs(struct hv_softc *);
int	hv_attach_devices(struct hv_softc *);

struct {
	int		  hmd_response;
	int		  hmd_request;
	void		(*hmd_handler)(struct hv_softc *,
			    struct vmbus_chanmsg_hdr *);
} hv_msg_dispatch[] = {
	{ 0,					0, NULL },
	{ VMBUS_CHANMSG_CHOFFER,		0, hv_channel_offer },
	{ VMBUS_CHANMSG_CHRESCIND,		0, hv_channel_rescind },
	{ VMBUS_CHANMSG_CHREQUEST,		VMBUS_CHANMSG_CHOFFER,
	  NULL },
	{ VMBUS_CHANMSG_CHOFFER_DONE,		0,
	  hv_channel_delivered },
	{ VMBUS_CHANMSG_CHOPEN,			0, NULL },
	{ VMBUS_CHANMSG_CHOPEN_RESP,		VMBUS_CHANMSG_CHOPEN,
	  hv_channel_response },
	{ VMBUS_CHANMSG_CHCLOSE,		0, NULL },
	{ VMBUS_CHANMSG_GPADL_CONN,		0, NULL },
	{ VMBUS_CHANMSG_GPADL_SUBCONN,		0, NULL },
	{ VMBUS_CHANMSG_GPADL_CONNRESP,		VMBUS_CHANMSG_GPADL_CONN,
	  hv_channel_response },
	{ VMBUS_CHANMSG_GPADL_DISCONN,		0, NULL },
	{ VMBUS_CHANMSG_GPADL_DISCONNRESP,	VMBUS_CHANMSG_GPADL_DISCONN,
	  hv_channel_response },
	{ VMBUS_CHANMSG_CHFREE,			0, NULL },
	{ VMBUS_CHANMSG_CONNECT,		0, NULL },
	{ VMBUS_CHANMSG_CONNECT_RESP,		VMBUS_CHANMSG_CONNECT,
	  hv_channel_response },
	{ VMBUS_CHANMSG_DISCONNECT,		0, NULL },
};

struct timecounter hv_timecounter = {
	.tc_get_timecount = hv_gettime,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 10000000,
	.tc_name = "hyperv",
	.tc_quality = 9001,
	.tc_priv = NULL,
	.tc_user = 0,
};

struct cfdriver hyperv_cd = {
	NULL, "hyperv", DV_DULL
};

const struct cfattach hyperv_ca = {
	sizeof(struct hv_softc), hv_match, hv_attach
};

const struct hv_guid hv_guid_network = {
	{ 0x63, 0x51, 0x61, 0xf8, 0x3e, 0xdf, 0xc5, 0x46,
	  0x91, 0x3f, 0xf2, 0xd2, 0xf9, 0x65, 0xed, 0x0e }
};

const struct hv_guid hv_guid_ide = {
	{ 0x32, 0x26, 0x41, 0x32, 0xcb, 0x86, 0xa2, 0x44,
	  0x9b, 0x5c, 0x50, 0xd1, 0x41, 0x73, 0x54, 0xf5 }
};

const struct hv_guid hv_guid_scsi = {
	{ 0xd9, 0x63, 0x61, 0xba, 0xa1, 0x04, 0x29, 0x4d,
	  0xb6, 0x05, 0x72, 0xe2, 0xff, 0xb1, 0xdc, 0x7f }
};

const struct hv_guid hv_guid_shutdown = {
	{ 0x31, 0x60, 0x0b, 0x0e, 0x13, 0x52, 0x34, 0x49,
	  0x81, 0x8b, 0x38, 0xd9, 0x0c, 0xed, 0x39, 0xdb }
};

const struct hv_guid hv_guid_timesync = {
	{ 0x30, 0xe6, 0x27, 0x95, 0xae, 0xd0, 0x7b, 0x49,
	  0xad, 0xce, 0xe8, 0x0a, 0xb0, 0x17, 0x5c, 0xaf }
};

const struct hv_guid hv_guid_heartbeat = {
	{ 0x39, 0x4f, 0x16, 0x57, 0x15, 0x91, 0x78, 0x4e,
	  0xab, 0x55, 0x38, 0x2f, 0x3b, 0xd5, 0x42, 0x2d }
};

const struct hv_guid hv_guid_kvp = {
	{ 0xe7, 0xf4, 0xa0, 0xa9, 0x45, 0x5a, 0x96, 0x4d,
	  0xb8, 0x27, 0x8a, 0x84, 0x1e, 0x8c, 0x03, 0xe6 }
};

#ifdef HYPERV_DEBUG
const struct hv_guid hv_guid_vss = {
	{ 0x29, 0x2e, 0xfa, 0x35, 0x23, 0xea, 0x36, 0x42,
	  0x96, 0xae, 0x3a, 0x6e, 0xba, 0xcb, 0xa4, 0x40 }
};

const struct hv_guid hv_guid_dynmem = {
	{ 0xdc, 0x74, 0x50, 0x52, 0x85, 0x89, 0xe2, 0x46,
	  0x80, 0x57, 0xa3, 0x07, 0xdc, 0x18, 0xa5, 0x02 }
};

const struct hv_guid hv_guid_mouse = {
	{ 0x9e, 0xb6, 0xa8, 0xcf, 0x4a, 0x5b, 0xc0, 0x4c,
	  0xb9, 0x8b, 0x8b, 0xa1, 0xa1, 0xf3, 0xf9, 0x5a }
};

const struct hv_guid hv_guid_kbd = {
	{ 0x6d, 0xad, 0x12, 0xf9, 0x17, 0x2b, 0xea, 0x48,
	  0xbd, 0x65, 0xf9, 0x27, 0xa6, 0x1c, 0x76, 0x84 }
};

const struct hv_guid hv_guid_video = {
	{ 0x02, 0x78, 0x0a, 0xda, 0x77, 0xe3, 0xac, 0x4a,
	  0x8e, 0x77, 0x05, 0x58, 0xeb, 0x10, 0x73, 0xf8 }
};

const struct hv_guid hv_guid_fc = {
	{ 0x4a, 0xcc, 0x9b, 0x2f, 0x69, 0x00, 0xf3, 0x4a,
	  0xb7, 0x6b, 0x6f, 0xd0, 0xbe, 0x52, 0x8c, 0xda }
};

const struct hv_guid hv_guid_fcopy = {
	{ 0xe3, 0x4b, 0xd1, 0x34, 0xe4, 0xde, 0xc8, 0x41,
	  0x9a, 0xe7, 0x6b, 0x17, 0x49, 0x77, 0xc1, 0x92 }
};

const struct hv_guid hv_guid_pcie = {
	{ 0x1d, 0xf6, 0xc4, 0x44, 0x44, 0x44, 0x00, 0x44,
	  0x9d, 0x52, 0x80, 0x2e, 0x27, 0xed, 0xe1, 0x9f }
};

const struct hv_guid hv_guid_netdir = {
	{ 0x3d, 0xaf, 0x2e, 0x8c, 0xa7, 0x32, 0x09, 0x4b,
	  0xab, 0x99, 0xbd, 0x1f, 0x1c, 0x86, 0xb5, 0x01 }
};

const struct hv_guid hv_guid_rdesktop = {
	{ 0xf4, 0xac, 0x6a, 0x27, 0x15, 0xac, 0x6c, 0x42,
	  0x98, 0xdd, 0x75, 0x21, 0xad, 0x3f, 0x01, 0xfe }
};

/* Automatic Virtual Machine Activation (AVMA) Services */
const struct hv_guid hv_guid_avma1 = {
	{ 0x55, 0xb2, 0x87, 0x44, 0x8c, 0xb8, 0x3f, 0x40,
	  0xbb, 0x51, 0xd1, 0xf6, 0x9c, 0xf1, 0x7f, 0x87 }
};

const struct hv_guid hv_guid_avma2 = {
	{ 0xf4, 0xba, 0x75, 0x33, 0x15, 0x9e, 0x30, 0x4b,
	  0xb7, 0x65, 0x67, 0xac, 0xb1, 0x0d, 0x60, 0x7b }
};

const struct hv_guid hv_guid_avma3 = {
	{ 0xa0, 0x1f, 0x22, 0x99, 0xad, 0x24, 0xe2, 0x11,
	  0xbe, 0x98, 0x00, 0x1a, 0xa0, 0x1b, 0xbf, 0x6e }
};

const struct hv_guid hv_guid_avma4 = {
	{ 0x16, 0x57, 0xe6, 0xf8, 0xb3, 0x3c, 0x06, 0x4a,
	  0x9a, 0x60, 0x18, 0x89, 0xc5, 0xcc, 0xca, 0xb5 }
};
#endif	/* HYPERV_DEBUG */

int
hv_match(struct device *parent, void *match, void *aux)
{
	struct pv_attach_args *pva = aux;
	struct pvbus_hv *hv = &pva->pva_hv[PVBUS_HYPERV];

	if ((hv->hv_major == 0 && hv->hv_minor == 0) || hv->hv_base == 0)
		return (0);

	return (1);
}

void
hv_attach(struct device *parent, struct device *self, void *aux)
{
	struct hv_softc *sc = (struct hv_softc *)self;
	struct pv_attach_args *pva = aux;
	struct pvbus_hv *hv = &pva->pva_hv[PVBUS_HYPERV];

	sc->sc_pvbus = hv;
	sc->sc_dmat = pva->pva_dmat;

	if (!(hv->hv_features & CPUID_HV_MSR_HYPERCALL) ||
	    !(hv->hv_features & CPUID_HV_MSR_SYNIC)) {
		printf(": not functional\n");
		return;
	}

	DPRINTF("\n");

	hv_set_version(sc);

	if (hv->hv_features & CPUID_HV_MSR_TIME_REFCNT)
		tc_init(&hv_timecounter);

	if (hv_init_hypercall(sc))
		return;

	/* Wire it up to the global */
	hv_sc = sc;

	if (hv_init_interrupts(sc))
		return;

	if (hv_vmbus_connect(sc))
		return;

	DPRINTF("%s", sc->sc_dev.dv_xname);
	printf(": protocol %d.%d, features %#x\n",
	    VMBUS_VERSION_MAJOR(sc->sc_proto),
	    VMBUS_VERSION_MINOR(sc->sc_proto),
	    hv->hv_features);

	if (hv_channel_scan(sc))
		return;

	/* Attach heartbeat, KVP and other "internal" services */
	hv_attach_icdevs(sc);

	/* Attach devices with external drivers */
	hv_attach_devices(sc);
}

void
hv_set_version(struct hv_softc *sc)
{
	uint64_t ver;

	/* OpenBSD build date */
	ver = MSR_HV_GUESTID_OSTYPE_OPENBSD;
	ver |= (uint64_t)OpenBSD << MSR_HV_GUESTID_VERSION_SHIFT;
	wrmsr(MSR_HV_GUEST_OS_ID, ver);
}

u_int
hv_gettime(struct timecounter *tc)
{
	u_int now = rdmsr(MSR_HV_TIME_REF_COUNT);

	return (now);
}

void
hv_delay(int usecs)
{
	uint64_t interval, start;

	/* 10 MHz fixed frequency */
	interval = (uint64_t)usecs * 10;
	start = rdmsr(MSR_HV_TIME_REF_COUNT);
	while (rdmsr(MSR_HV_TIME_REF_COUNT) - start < interval)
		CPU_BUSY_CYCLE();
}

int
hv_init_hypercall(struct hv_softc *sc)
{
	extern void *hv_hypercall_page;
	uint64_t msr;
	paddr_t pa;

	sc->sc_hc = &hv_hypercall_page;

	if (!pmap_extract(pmap_kernel(), (vaddr_t)sc->sc_hc, &pa)) {
		printf(": hypercall page PA extraction failed\n");
		return (-1);
	}

	msr = (atop(pa) << MSR_HV_HYPERCALL_PGSHIFT) | MSR_HV_HYPERCALL_ENABLE;
	wrmsr(MSR_HV_HYPERCALL, msr);

	if (!(rdmsr(MSR_HV_HYPERCALL) & MSR_HV_HYPERCALL_ENABLE)) {
		printf(": failed to set up a hypercall page\n");
		return (-1);
	}

	return (0);
}

uint64_t
hv_hypercall(struct hv_softc *sc, uint64_t control, void *input,
    void *output)
{
	paddr_t input_pa = 0, output_pa = 0;
	uint64_t status = 0;

	if (input != NULL &&
	    pmap_extract(pmap_kernel(), (vaddr_t)input, &input_pa) == 0) {
		printf("%s: hypercall input PA extraction failed\n",
		    sc->sc_dev.dv_xname);
		return (~HYPERCALL_STATUS_SUCCESS);
	}

	if (output != NULL &&
	    pmap_extract(pmap_kernel(), (vaddr_t)output, &output_pa) == 0) {
		printf("%s: hypercall output PA extraction failed\n",
		    sc->sc_dev.dv_xname);
		return (~HYPERCALL_STATUS_SUCCESS);
	}

#ifdef __amd64__
	extern uint64_t hv_hypercall_trampoline(uint64_t, paddr_t, paddr_t);
	status = hv_hypercall_trampoline(control, input_pa, output_pa);
#else  /* __i386__ */
	{
		uint32_t control_hi = control >> 32;
		uint32_t control_lo = control & 0xfffffffff;
		uint32_t status_hi = 1;
		uint32_t status_lo = 1;

		__asm__ volatile ("call *%8" :
		    "=d" (status_hi), "=a"(status_lo) :
		    "d" (control_hi), "a" (control_lo),
		    "b" (0), "c" (input_pa), "D" (0), "S" (output_pa),
		    "m" (sc->sc_hc));

		status = status_lo | ((uint64_t)status_hi << 32);
	}
#endif	/* __amd64__ */

	return (status);
}

int
hv_init_interrupts(struct hv_softc *sc)
{
	struct cpu_info *ci = curcpu();
	int cpu = CPU_INFO_UNIT(ci);

	sc->sc_idtvec = LAPIC_HYPERV_VECTOR;

	TAILQ_INIT(&sc->sc_reqs);
	mtx_init(&sc->sc_reqlck, IPL_NET);

	TAILQ_INIT(&sc->sc_rsps);
	mtx_init(&sc->sc_rsplck, IPL_NET);

	sc->sc_simp[cpu] = km_alloc(PAGE_SIZE, &kv_any, &kp_zero, &kd_nowait);
	if (sc->sc_simp[cpu] == NULL) {
		printf(": failed to allocate SIMP\n");
		return (-1);
	}

	sc->sc_siep[cpu] = km_alloc(PAGE_SIZE, &kv_any, &kp_zero, &kd_nowait);
	if (sc->sc_siep[cpu] == NULL) {
		printf(": failed to allocate SIEP\n");
		km_free(sc->sc_simp[cpu], PAGE_SIZE, &kv_any, &kp_zero);
		return (-1);
	}

	sc->sc_proto = VMBUS_VERSION_WS2008;

	return (hv_init_synic(sc));
}

int
hv_init_synic(struct hv_softc *sc)
{
	struct cpu_info *ci = curcpu();
	int cpu = CPU_INFO_UNIT(ci);
	uint64_t simp, siefp, sctrl, sint;
	paddr_t pa;

	/*
	 * Setup the Synic's message page
	 */
	if (!pmap_extract(pmap_kernel(), (vaddr_t)sc->sc_simp[cpu], &pa)) {
		printf(": SIMP PA extraction failed\n");
		return (-1);
	}
	simp = rdmsr(MSR_HV_SIMP);
	simp &= (1 << MSR_HV_SIMP_PGSHIFT) - 1;
	simp |= (atop(pa) << MSR_HV_SIMP_PGSHIFT);
	simp |= MSR_HV_SIMP_ENABLE;
	wrmsr(MSR_HV_SIMP, simp);

	/*
	 * Setup the Synic's event page
	 */
	if (!pmap_extract(pmap_kernel(), (vaddr_t)sc->sc_siep[cpu], &pa)) {
		printf(": SIEP PA extraction failed\n");
		return (-1);
	}
	siefp = rdmsr(MSR_HV_SIEFP);
	siefp &= (1<<MSR_HV_SIEFP_PGSHIFT) - 1;
	siefp |= (atop(pa) << MSR_HV_SIEFP_PGSHIFT);
	siefp |= MSR_HV_SIEFP_ENABLE;
	wrmsr(MSR_HV_SIEFP, siefp);

	/*
	 * Configure and unmask SINT for message and event flags
	 */
	sint = rdmsr(MSR_HV_SINT0 + VMBUS_SINT_MESSAGE);
	sint = sc->sc_idtvec | MSR_HV_SINT_AUTOEOI |
	    (sint & MSR_HV_SINT_RSVD_MASK);
	wrmsr(MSR_HV_SINT0 + VMBUS_SINT_MESSAGE, sint);

	/* Enable the global synic bit */
	sctrl = rdmsr(MSR_HV_SCONTROL);
	sctrl |= MSR_HV_SCTRL_ENABLE;
	wrmsr(MSR_HV_SCONTROL, sctrl);

	sc->sc_vcpus[cpu] = rdmsr(MSR_HV_VP_INDEX);

	DPRINTF("vcpu%u: SIMP %#llx SIEFP %#llx SCTRL %#llx\n",
	    sc->sc_vcpus[cpu], simp, siefp, sctrl);

	return (0);
}

int
hv_cmd(struct hv_softc *sc, void *cmd, size_t cmdlen, void *rsp,
    size_t rsplen, int flags)
{
	struct hv_msg msg;
	int rv;

	if (cmdlen > VMBUS_MSG_DSIZE_MAX) {
		printf("%s: payload too large (%lu)\n", sc->sc_dev.dv_xname,
		    cmdlen);
		return (EMSGSIZE);
	}

	memset(&msg, 0, sizeof(msg));

	msg.msg_req.hc_dsize = cmdlen;
	memcpy(msg.msg_req.hc_data, cmd, cmdlen);

	if (!(flags & HCF_NOREPLY)) {
		msg.msg_rsp = rsp;
		msg.msg_rsplen = rsplen;
	} else
		msg.msg_flags |= MSGF_NOQUEUE;

	if (flags & HCF_NOSLEEP)
		msg.msg_flags |= MSGF_NOSLEEP;

	if ((rv = hv_start(sc, &msg)) != 0)
		return (rv);
	return (hv_reply(sc, &msg));
}

int
hv_start(struct hv_softc *sc, struct hv_msg *msg)
{
	const int delays[] = { 100, 100, 100, 500, 500, 5000, 5000, 5000 };
	const char *wchan = "hvstart";
	uint16_t status;
	int i, s;

	msg->msg_req.hc_connid = VMBUS_CONNID_MESSAGE;
	msg->msg_req.hc_msgtype = 1;

	if (!(msg->msg_flags & MSGF_NOQUEUE)) {
		mtx_enter(&sc->sc_reqlck);
		TAILQ_INSERT_TAIL(&sc->sc_reqs, msg, msg_entry);
		mtx_leave(&sc->sc_reqlck);
	}

	for (i = 0; i < nitems(delays); i++) {
		status = hv_hypercall(sc, HYPERCALL_POST_MESSAGE,
		    &msg->msg_req, NULL);
		if (status == HYPERCALL_STATUS_SUCCESS)
			break;
		if (msg->msg_flags & MSGF_NOSLEEP) {
			delay(delays[i]);
			s = splnet();
			hv_intr();
			splx(s);
		} else {
			tsleep_nsec(wchan, PRIBIO, wchan,
			    USEC_TO_NSEC(delays[i]));
		}
	}
	if (status != 0) {
		printf("%s: posting vmbus message failed with %d\n",
		    sc->sc_dev.dv_xname, status);
		if (!(msg->msg_flags & MSGF_NOQUEUE)) {
			mtx_enter(&sc->sc_reqlck);
			TAILQ_REMOVE(&sc->sc_reqs, msg, msg_entry);
			mtx_leave(&sc->sc_reqlck);
		}
		return (EIO);
	}

	return (0);
}

static int
hv_reply_done(struct hv_softc *sc, struct hv_msg *msg)
{
	struct hv_msg *m;

	mtx_enter(&sc->sc_rsplck);
	TAILQ_FOREACH(m, &sc->sc_rsps, msg_entry) {
		if (m == msg) {
			mtx_leave(&sc->sc_rsplck);
			return (1);
		}
	}
	mtx_leave(&sc->sc_rsplck);
	return (0);
}

int
hv_reply(struct hv_softc *sc, struct hv_msg *msg)
{
	if (msg->msg_flags & MSGF_NOQUEUE)
		return (0);

	hv_wait(sc, hv_reply_done, msg, msg, "hvreply");

	mtx_enter(&sc->sc_rsplck);
	TAILQ_REMOVE(&sc->sc_rsps, msg, msg_entry);
	mtx_leave(&sc->sc_rsplck);

	return (0);
}

void
hv_wait(struct hv_softc *sc, int (*cond)(struct hv_softc *, struct hv_msg *),
    struct hv_msg *msg, void *wchan, const char *wmsg)
{
	int s;

	KASSERT(cold ? msg->msg_flags & MSGF_NOSLEEP : 1);

	while (!cond(sc, msg)) {
		if (msg->msg_flags & MSGF_NOSLEEP) {
			delay(1000);
			s = splnet();
			hv_intr();
			splx(s);
		} else {
			tsleep_nsec(wchan, PRIBIO, wmsg ? wmsg : "hvwait",
			    USEC_TO_NSEC(1000));
		}
	}
}

uint16_t
hv_intr_signal(struct hv_softc *sc, void *con)
{
	uint64_t status;

	status = hv_hypercall(sc, HYPERCALL_SIGNAL_EVENT, con, NULL);
	return ((uint16_t)status);
}

void
hv_intr(void)
{
	struct hv_softc *sc = hv_sc;

	hv_event_intr(sc);
	hv_message_intr(sc);
}

void
hv_event_intr(struct hv_softc *sc)
{
	struct vmbus_evtflags *evt;
	struct cpu_info *ci = curcpu();
	int cpu = CPU_INFO_UNIT(ci);
	int bit, row, maxrow, chanid;
	struct hv_channel *ch;
	u_long *revents, pending;

	evt = (struct vmbus_evtflags *)sc->sc_siep[cpu] +
	    VMBUS_SINT_MESSAGE;
	if ((sc->sc_proto == VMBUS_VERSION_WS2008) ||
	    (sc->sc_proto == VMBUS_VERSION_WIN7)) {
		if (!test_bit(0, &evt->evt_flags[0]))
			return;
		clear_bit(0, &evt->evt_flags[0]);
		maxrow = VMBUS_CHAN_MAX_COMPAT / VMBUS_EVTFLAG_LEN;
		/*
		 * receive size is 1/2 page and divide that by 4 bytes
		 */
		revents = sc->sc_revents;
	} else {
		maxrow = nitems(evt->evt_flags);
		/*
		 * On Host with Win8 or above, the event page can be
		 * checked directly to get the id of the channel
		 * that has the pending interrupt.
		 */
		revents = &evt->evt_flags[0];
	}

	for (row = 0; row < maxrow; row++) {
		if (revents[row] == 0)
			continue;
		pending = atomic_swap_ulong(&revents[row], 0);
		for (bit = 0; pending > 0; pending >>= 1, bit++) {
			if ((pending & 1) == 0)
				continue;
			chanid = (row * LONG_BIT) + bit;
			/* vmbus channel protocol message */
			if (chanid == 0)
				continue;
			ch = hv_channel_lookup(sc, chanid);
			if (ch == NULL) {
				printf("%s: unhandled event on %d\n",
				    sc->sc_dev.dv_xname, chanid);
				continue;
			}
			if (ch->ch_state != HV_CHANSTATE_OPENED) {
				printf("%s: channel %d is not active\n",
				    sc->sc_dev.dv_xname, chanid);
				continue;
			}
			ch->ch_evcnt.ec_count++;
			hv_channel_schedule(ch);
		}
	}
}

void
hv_message_intr(struct hv_softc *sc)
{
	struct vmbus_message *msg;
	struct vmbus_chanmsg_hdr *hdr;
	struct cpu_info *ci = curcpu();
	int cpu = CPU_INFO_UNIT(ci);

	for (;;) {
		msg = (struct vmbus_message *)sc->sc_simp[cpu] +
		    VMBUS_SINT_MESSAGE;
		if (msg->msg_type == VMBUS_MSGTYPE_NONE)
			break;

		hdr = (struct vmbus_chanmsg_hdr *)msg->msg_data;
		if (hdr->chm_type >= VMBUS_CHANMSG_COUNT) {
			printf("%s: unhandled message type %u flags %#x\n",
			    sc->sc_dev.dv_xname, hdr->chm_type,
			    msg->msg_flags);
			goto skip;
		}
		if (hv_msg_dispatch[hdr->chm_type].hmd_handler)
			hv_msg_dispatch[hdr->chm_type].hmd_handler(sc, hdr);
		else
			printf("%s: unhandled message type %u\n",
			    sc->sc_dev.dv_xname, hdr->chm_type);
 skip:
		msg->msg_type = VMBUS_MSGTYPE_NONE;
		virtio_membar_sync();
		if (msg->msg_flags & VMBUS_MSGFLAG_PENDING)
			wrmsr(MSR_HV_EOM, 0);
	}
}

void
hv_channel_response(struct hv_softc *sc, struct vmbus_chanmsg_hdr *rsphdr)
{
	struct hv_msg *msg;
	struct vmbus_chanmsg_hdr *reqhdr;
	int req;

	req = hv_msg_dispatch[rsphdr->chm_type].hmd_request;
	mtx_enter(&sc->sc_reqlck);
	TAILQ_FOREACH(msg, &sc->sc_reqs, msg_entry) {
		reqhdr = (struct vmbus_chanmsg_hdr *)&msg->msg_req.hc_data;
		if (reqhdr->chm_type == req) {
			TAILQ_REMOVE(&sc->sc_reqs, msg, msg_entry);
			break;
		}
	}
	mtx_leave(&sc->sc_reqlck);
	if (msg != NULL) {
		memcpy(msg->msg_rsp, rsphdr, msg->msg_rsplen);
		mtx_enter(&sc->sc_rsplck);
		TAILQ_INSERT_TAIL(&sc->sc_rsps, msg, msg_entry);
		mtx_leave(&sc->sc_rsplck);
		wakeup(msg);
	}
}

void
hv_channel_offer(struct hv_softc *sc, struct vmbus_chanmsg_hdr *hdr)
{
	struct hv_offer *co;

	co = malloc(sizeof(*co), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (co == NULL) {
		printf("%s: failed to allocate an offer object\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	memcpy(&co->co_chan, hdr, sizeof(co->co_chan));

	mtx_enter(&sc->sc_offerlck);
	SIMPLEQ_INSERT_TAIL(&sc->sc_offers, co, co_entry);
	mtx_leave(&sc->sc_offerlck);
}

void
hv_channel_rescind(struct hv_softc *sc, struct vmbus_chanmsg_hdr *hdr)
{
	const struct vmbus_chanmsg_chrescind *cmd;

	cmd = (const struct vmbus_chanmsg_chrescind *)hdr;
	printf("%s: revoking channel %u\n", sc->sc_dev.dv_xname,
	    cmd->chm_chanid);
}

void
hv_channel_delivered(struct hv_softc *sc, struct vmbus_chanmsg_hdr *hdr)
{
	atomic_setbits_int(&sc->sc_flags, HSF_OFFERS_DELIVERED);
	wakeup(&sc->sc_offers);
}

int
hv_vmbus_connect(struct hv_softc *sc)
{
	const uint32_t versions[] = {
		VMBUS_VERSION_WIN10,
		VMBUS_VERSION_WIN8_1, VMBUS_VERSION_WIN8,
		VMBUS_VERSION_WIN7, VMBUS_VERSION_WS2008
	};
	struct vmbus_chanmsg_connect cmd;
	struct vmbus_chanmsg_connect_resp rsp;
	paddr_t epa, mpa1, mpa2;
	int i;

	sc->sc_events = km_alloc(PAGE_SIZE, &kv_any, &kp_zero, &kd_nowait);
	if (sc->sc_events == NULL) {
		printf(": failed to allocate channel port events page\n");
		goto errout;
	}
	if (!pmap_extract(pmap_kernel(), (vaddr_t)sc->sc_events, &epa)) {
		printf(": channel port events page PA extraction failed\n");
		goto errout;
	}

	sc->sc_wevents = (u_long *)sc->sc_events;
	sc->sc_revents = (u_long *)((caddr_t)sc->sc_events + (PAGE_SIZE >> 1));

	sc->sc_monitor[0] = km_alloc(PAGE_SIZE, &kv_any, &kp_zero, &kd_nowait);
	if (sc->sc_monitor[0] == NULL) {
		printf(": failed to allocate monitor page 1\n");
		goto errout;
	}
	if (!pmap_extract(pmap_kernel(), (vaddr_t)sc->sc_monitor[0], &mpa1)) {
		printf(": monitor page 1 PA extraction failed\n");
		goto errout;
	}

	sc->sc_monitor[1] = km_alloc(PAGE_SIZE, &kv_any, &kp_zero, &kd_nowait);
	if (sc->sc_monitor[1] == NULL) {
		printf(": failed to allocate monitor page 2\n");
		goto errout;
	}
	if (!pmap_extract(pmap_kernel(), (vaddr_t)sc->sc_monitor[1], &mpa2)) {
		printf(": monitor page 2 PA extraction failed\n");
		goto errout;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.chm_hdr.chm_type = VMBUS_CHANMSG_CONNECT;
	cmd.chm_evtflags = (uint64_t)epa;
	cmd.chm_mnf1 = (uint64_t)mpa1;
	cmd.chm_mnf2 = (uint64_t)mpa2;

	memset(&rsp, 0, sizeof(rsp));

	for (i = 0; i < nitems(versions); i++) {
		cmd.chm_ver = versions[i];
		if (hv_cmd(sc, &cmd, sizeof(cmd), &rsp, sizeof(rsp),
		    HCF_NOSLEEP)) {
			DPRINTF("%s: CONNECT failed\n",
			    sc->sc_dev.dv_xname);
			goto errout;
		}
		if (rsp.chm_done) {
			sc->sc_flags |= HSF_CONNECTED;
			sc->sc_proto = versions[i];
			sc->sc_handle = VMBUS_GPADL_START;
			break;
		}
	}
	if (i == nitems(versions)) {
		printf("%s: failed to negotiate protocol version\n",
		    sc->sc_dev.dv_xname);
		goto errout;
	}

	return (0);

 errout:
	if (sc->sc_events) {
		km_free(sc->sc_events, PAGE_SIZE, &kv_any, &kp_zero);
		sc->sc_events = NULL;
		sc->sc_wevents = NULL;
		sc->sc_revents = NULL;
	}
	if (sc->sc_monitor[0]) {
		km_free(sc->sc_monitor[0], PAGE_SIZE, &kv_any, &kp_zero);
		sc->sc_monitor[0] = NULL;
	}
	if (sc->sc_monitor[1]) {
		km_free(sc->sc_monitor[1], PAGE_SIZE, &kv_any, &kp_zero);
		sc->sc_monitor[1] = NULL;
	}
	return (-1);
}

#ifdef HYPERV_DEBUG
static inline char *
guidprint(struct hv_guid *a)
{
	/* 3     0  5  4 7 6  8 9  10        15 */
	/* 33221100-5544-7766-9988-FFEEDDCCBBAA */
	static char buf[16 * 2 + 4 + 1];
	int i, j = 0;

	for (i = 3; i != -1; i -= 1, j += 2)
		snprintf(&buf[j], 3, "%02x", (uint8_t)a->data[i]);
	buf[j++] = '-';
	for (i = 5; i != 3; i -= 1, j += 2)
		snprintf(&buf[j], 3, "%02x", (uint8_t)a->data[i]);
	buf[j++] = '-';
	for (i = 7; i != 5; i -= 1, j += 2)
		snprintf(&buf[j], 3, "%02x", (uint8_t)a->data[i]);
	buf[j++] = '-';
	for (i = 8; i < 10; i += 1, j += 2)
		snprintf(&buf[j], 3, "%02x", (uint8_t)a->data[i]);
	buf[j++] = '-';
	for (i = 10; i < 16; i += 1, j += 2)
		snprintf(&buf[j], 3, "%02x", (uint8_t)a->data[i]);
	return (&buf[0]);
}
#endif	/* HYPERV_DEBUG */

void
hv_guid_sprint(struct hv_guid *guid, char *str, size_t size)
{
	const struct {
		const struct hv_guid	*guid;
		const char		*ident;
	} map[] = {
		{ &hv_guid_network,	"network" },
		{ &hv_guid_ide,		"ide" },
		{ &hv_guid_scsi,	"scsi" },
		{ &hv_guid_shutdown,	"shutdown" },
		{ &hv_guid_timesync,	"timesync" },
		{ &hv_guid_heartbeat,	"heartbeat" },
		{ &hv_guid_kvp,		"kvp" },
#ifdef HYPERV_DEBUG
		{ &hv_guid_vss,		"vss" },
		{ &hv_guid_dynmem,	"dynamic-memory" },
		{ &hv_guid_mouse,	"mouse" },
		{ &hv_guid_kbd,		"keyboard" },
		{ &hv_guid_video,	"video" },
		{ &hv_guid_fc,		"fiber-channel" },
		{ &hv_guid_fcopy,	"file-copy" },
		{ &hv_guid_pcie,	"pcie-passthrough" },
		{ &hv_guid_netdir,	"network-direct" },
		{ &hv_guid_rdesktop,	"remote-desktop" },
		{ &hv_guid_avma1,	"avma-1" },
		{ &hv_guid_avma2,	"avma-2" },
		{ &hv_guid_avma3,	"avma-3" },
		{ &hv_guid_avma4,	"avma-4" },
#endif
	};
	int i;

	for (i = 0; i < nitems(map); i++) {
		if (memcmp(guid, map[i].guid, sizeof(*guid)) == 0) {
			strlcpy(str, map[i].ident, size);
			return;
		}
	}
#ifdef HYPERV_DEBUG
	strlcpy(str, guidprint(guid), size);
#endif
}

static int
hv_channel_scan_done(struct hv_softc *sc, struct hv_msg *msg __unused)
{
	return (sc->sc_flags & HSF_OFFERS_DELIVERED);
}

int
hv_channel_scan(struct hv_softc *sc)
{
	struct vmbus_chanmsg_hdr hdr;
	struct vmbus_chanmsg_choffer rsp;
	struct hv_offer *co;

	SIMPLEQ_INIT(&sc->sc_offers);
	mtx_init(&sc->sc_offerlck, IPL_NET);

	memset(&hdr, 0, sizeof(hdr));
	hdr.chm_type = VMBUS_CHANMSG_CHREQUEST;

	if (hv_cmd(sc, &hdr, sizeof(hdr), &rsp, sizeof(rsp),
	    HCF_NOSLEEP | HCF_NOREPLY)) {
		DPRINTF("%s: CHREQUEST failed\n", sc->sc_dev.dv_xname);
		return (-1);
	}

	hv_wait(sc, hv_channel_scan_done, (struct hv_msg *)&hdr,
	    &sc->sc_offers, "hvscan");

	TAILQ_INIT(&sc->sc_channels);
	mtx_init(&sc->sc_channelck, IPL_NET);

	mtx_enter(&sc->sc_offerlck);
	while (!SIMPLEQ_EMPTY(&sc->sc_offers)) {
		co = SIMPLEQ_FIRST(&sc->sc_offers);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_offers, co_entry);
		mtx_leave(&sc->sc_offerlck);

		hv_process_offer(sc, co);
		free(co, M_DEVBUF, sizeof(*co));

		mtx_enter(&sc->sc_offerlck);
	}
	mtx_leave(&sc->sc_offerlck);

	return (0);
}

void
hv_process_offer(struct hv_softc *sc, struct hv_offer *co)
{
	struct hv_channel *ch, *nch;

	nch = malloc(sizeof(*nch), M_DEVBUF, M_ZERO | M_NOWAIT);
	if (nch == NULL) {
		printf("%s: failed to allocate memory for the channel\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	nch->ch_sc = sc;
	hv_guid_sprint(&co->co_chan.chm_chtype, nch->ch_ident,
	    sizeof(nch->ch_ident));

	/*
	 * By default we setup state to enable batched reading.
	 * A specific service can choose to disable this prior
	 * to opening the channel.
	 */
	nch->ch_flags |= CHF_BATCHED;

	KASSERT((((vaddr_t)&nch->ch_monprm) & 0x7) == 0);
	memset(&nch->ch_monprm, 0, sizeof(nch->ch_monprm));
	nch->ch_monprm.mp_connid = VMBUS_CONNID_EVENT;

	if (sc->sc_proto != VMBUS_VERSION_WS2008)
		nch->ch_monprm.mp_connid = co->co_chan.chm_connid;

	if (co->co_chan.chm_flags1 & VMBUS_CHOFFER_FLAG1_HASMNF) {
		nch->ch_mgroup = co->co_chan.chm_montrig / VMBUS_MONTRIG_LEN;
		nch->ch_mindex = co->co_chan.chm_montrig % VMBUS_MONTRIG_LEN;
		nch->ch_flags |= CHF_MONITOR;
	}

	nch->ch_id = co->co_chan.chm_chanid;

	memcpy(&nch->ch_type, &co->co_chan.chm_chtype, sizeof(ch->ch_type));
	memcpy(&nch->ch_inst, &co->co_chan.chm_chinst, sizeof(ch->ch_inst));

	mtx_enter(&sc->sc_channelck);
	TAILQ_FOREACH(ch, &sc->sc_channels, ch_entry) {
		if (!memcmp(&ch->ch_type, &nch->ch_type, sizeof(ch->ch_type)) &&
		    !memcmp(&ch->ch_inst, &nch->ch_inst, sizeof(ch->ch_inst)))
			break;
	}
	if (ch != NULL) {
		if (co->co_chan.chm_subidx == 0) {
			printf("%s: unknown offer \"%s\"\n",
			    sc->sc_dev.dv_xname, nch->ch_ident);
			mtx_leave(&sc->sc_channelck);
			free(nch, M_DEVBUF, sizeof(*nch));
			return;
		}
#ifdef HYPERV_DEBUG
		printf("%s: subchannel %u for \"%s\"\n", sc->sc_dev.dv_xname,
		    co->co_chan.chm_subidx, ch->ch_ident);
#endif
		mtx_leave(&sc->sc_channelck);
		free(nch, M_DEVBUF, sizeof(*nch));
		return;
	}

	nch->ch_state = HV_CHANSTATE_OFFERED;

	TAILQ_INSERT_TAIL(&sc->sc_channels, nch, ch_entry);
	mtx_leave(&sc->sc_channelck);

#ifdef HYPERV_DEBUG
	printf("%s: channel %u: \"%s\"", sc->sc_dev.dv_xname, nch->ch_id,
	    nch->ch_ident);
	if (nch->ch_flags & CHF_MONITOR)
		printf(", monitor %u\n", co->co_chan.chm_montrig);
	else
		printf("\n");
#endif
}

struct hv_channel *
hv_channel_lookup(struct hv_softc *sc, uint32_t relid)
{
	struct hv_channel *ch;

	TAILQ_FOREACH(ch, &sc->sc_channels, ch_entry) {
		if (ch->ch_id == relid)
			return (ch);
	}
	return (NULL);
}

int
hv_channel_ring_create(struct hv_channel *ch, uint32_t buflen)
{
	struct hv_softc *sc = ch->ch_sc;

	buflen = roundup(buflen, PAGE_SIZE) + sizeof(struct vmbus_bufring);
	ch->ch_ring = km_alloc(2 * buflen, &kv_any, &kp_zero, cold ?
	    &kd_nowait : &kd_waitok);
	if (ch->ch_ring == NULL) {
		printf("%s: failed to allocate channel ring\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}
	ch->ch_ring_size = 2 * buflen;

	memset(&ch->ch_wrd, 0, sizeof(ch->ch_wrd));
	ch->ch_wrd.rd_ring = (struct vmbus_bufring *)ch->ch_ring;
	ch->ch_wrd.rd_size = buflen;
	ch->ch_wrd.rd_dsize = buflen - sizeof(struct vmbus_bufring);
	mtx_init(&ch->ch_wrd.rd_lock, IPL_NET);

	memset(&ch->ch_rrd, 0, sizeof(ch->ch_rrd));
	ch->ch_rrd.rd_ring = (struct vmbus_bufring *)((uint8_t *)ch->ch_ring +
	    buflen);
	ch->ch_rrd.rd_size = buflen;
	ch->ch_rrd.rd_dsize = buflen - sizeof(struct vmbus_bufring);
	mtx_init(&ch->ch_rrd.rd_lock, IPL_NET);

	if (hv_handle_alloc(ch, ch->ch_ring, 2 * buflen, &ch->ch_ring_gpadl)) {
		printf("%s: failed to obtain a PA handle for the ring\n",
		    sc->sc_dev.dv_xname);
		hv_channel_ring_destroy(ch);
		return (-1);
	}

	return (0);
}

void
hv_channel_ring_destroy(struct hv_channel *ch)
{
	km_free(ch->ch_ring, ch->ch_ring_size, &kv_any, &kp_zero);
	ch->ch_ring = NULL;
	hv_handle_free(ch, ch->ch_ring_gpadl);

	memset(&ch->ch_wrd, 0, sizeof(ch->ch_wrd));
	memset(&ch->ch_rrd, 0, sizeof(ch->ch_rrd));
}

int
hv_channel_open(struct hv_channel *ch, size_t buflen, void *udata,
    size_t udatalen, void (*handler)(void *), void *arg)
{
	struct hv_softc *sc = ch->ch_sc;
	struct vmbus_chanmsg_chopen cmd;
	struct vmbus_chanmsg_chopen_resp rsp;
	int rv;

	if (ch->ch_ring == NULL &&
	    hv_channel_ring_create(ch, buflen)) {
		DPRINTF("%s: failed to create channel ring\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.chm_hdr.chm_type = VMBUS_CHANMSG_CHOPEN;
	cmd.chm_openid = ch->ch_id;
	cmd.chm_chanid = ch->ch_id;
	cmd.chm_gpadl = ch->ch_ring_gpadl;
	cmd.chm_txbr_pgcnt = ch->ch_wrd.rd_size >> PAGE_SHIFT;
	cmd.chm_vcpuid = ch->ch_vcpu;

	if (udata && udatalen > 0)
		memcpy(cmd.chm_udata, udata, udatalen);

	memset(&rsp, 0, sizeof(rsp));

	ch->ch_handler = handler;
	ch->ch_ctx = arg;

	ch->ch_state = HV_CHANSTATE_OPENED;

	rv = hv_cmd(sc, &cmd, sizeof(cmd), &rsp, sizeof(rsp),
	    cold ? HCF_NOSLEEP : HCF_SLEEPOK);
	if (rv) {
		hv_channel_ring_destroy(ch);
		DPRINTF("%s: CHOPEN failed with %d\n",
		    sc->sc_dev.dv_xname, rv);
		ch->ch_handler = NULL;
		ch->ch_ctx = NULL;
		ch->ch_state = HV_CHANSTATE_OFFERED;
		return (-1);
	}

	return (0);
}

int
hv_channel_close(struct hv_channel *ch)
{
	struct hv_softc *sc = ch->ch_sc;
	struct vmbus_chanmsg_chclose cmd;
	int rv;

	memset(&cmd, 0, sizeof(cmd));
	cmd.chm_hdr.chm_type = VMBUS_CHANMSG_CHCLOSE;
	cmd.chm_chanid = ch->ch_id;

	ch->ch_state = HV_CHANSTATE_CLOSING;
	rv = hv_cmd(sc, &cmd, sizeof(cmd), NULL, 0, HCF_NOREPLY);
	if (rv) {
		DPRINTF("%s: CHCLOSE failed with %d\n",
		    sc->sc_dev.dv_xname, rv);
		return (-1);
	}
	ch->ch_state = HV_CHANSTATE_CLOSED;
	hv_channel_ring_destroy(ch);
	return (0);
}

static inline void
hv_channel_setevent(struct hv_softc *sc, struct hv_channel *ch)
{
	struct vmbus_mon_trig *mtg;

	/* Each uint32_t represents 32 channels */
	set_bit(ch->ch_id, sc->sc_wevents);
	if (ch->ch_flags & CHF_MONITOR) {
		mtg = &sc->sc_monitor[1]->mnf_trigs[ch->ch_mgroup];
		set_bit(ch->ch_mindex, &mtg->mt_pending);
	} else
		hv_intr_signal(sc, &ch->ch_monprm);
}

void
hv_channel_intr(void *arg)
{
	struct hv_channel *ch = arg;

	if (hv_channel_ready(ch))
		ch->ch_handler(ch->ch_ctx);

	if (hv_channel_unpause(ch) == 0)
		return;

	hv_channel_pause(ch);
	hv_channel_schedule(ch);
}

int
hv_channel_setdeferred(struct hv_channel *ch, const char *name)
{
	ch->ch_taskq = taskq_create(name, 1, IPL_NET, TASKQ_MPSAFE);
	if (ch->ch_taskq == NULL)
		return (-1);
	task_set(&ch->ch_task, hv_channel_intr, ch);
	return (0);
}

void
hv_channel_schedule(struct hv_channel *ch)
{
	if (ch->ch_handler) {
		if (!cold && (ch->ch_flags & CHF_BATCHED)) {
			hv_channel_pause(ch);
			task_add(ch->ch_taskq, &ch->ch_task);
		} else
			ch->ch_handler(ch->ch_ctx);
	}
}

static inline void
hv_ring_put(struct hv_ring_data *wrd, uint8_t *data, uint32_t datalen)
{
	int left = MIN(datalen, wrd->rd_dsize - wrd->rd_prod);

	memcpy(&wrd->rd_ring->br_data[wrd->rd_prod], data, left);
	memcpy(&wrd->rd_ring->br_data[0], data + left, datalen - left);
	wrd->rd_prod += datalen;
	if (wrd->rd_prod >= wrd->rd_dsize)
		wrd->rd_prod -= wrd->rd_dsize;
}

static inline void
hv_ring_get(struct hv_ring_data *rrd, uint8_t *data, uint32_t datalen,
    int peek)
{
	int left = MIN(datalen, rrd->rd_dsize - rrd->rd_cons);

	memcpy(data, &rrd->rd_ring->br_data[rrd->rd_cons], left);
	memcpy(data + left, &rrd->rd_ring->br_data[0], datalen - left);
	if (!peek) {
		rrd->rd_cons += datalen;
		if (rrd->rd_cons >= rrd->rd_dsize)
			rrd->rd_cons -= rrd->rd_dsize;
	}
}

static inline void
hv_ring_avail(struct hv_ring_data *rd, uint32_t *towrite, uint32_t *toread)
{
	uint32_t ridx = rd->rd_ring->br_rindex;
	uint32_t widx = rd->rd_ring->br_windex;
	uint32_t r, w;

	if (widx >= ridx)
		w = rd->rd_dsize - (widx - ridx);
	else
		w = ridx - widx;
	r = rd->rd_dsize - w;
	if (towrite)
		*towrite = w;
	if (toread)
		*toread = r;
}

int
hv_ring_write(struct hv_ring_data *wrd, struct iovec *iov, int iov_cnt,
    int *needsig)
{
	uint64_t indices = 0;
	uint32_t avail, oprod, datalen = sizeof(indices);
	int i;

	for (i = 0; i < iov_cnt; i++)
		datalen += iov[i].iov_len;

	KASSERT(datalen <= wrd->rd_dsize);

	hv_ring_avail(wrd, &avail, NULL);
	if (avail <= datalen) {
		DPRINTF("%s: avail %u datalen %u\n", __func__, avail, datalen);
		return (EAGAIN);
	}

	oprod = wrd->rd_prod;

	for (i = 0; i < iov_cnt; i++)
		hv_ring_put(wrd, iov[i].iov_base, iov[i].iov_len);

	indices = (uint64_t)oprod << 32;
	hv_ring_put(wrd, (uint8_t *)&indices, sizeof(indices));

	virtio_membar_sync();
	wrd->rd_ring->br_windex = wrd->rd_prod;
	virtio_membar_sync();

	/* Signal when the ring transitions from being empty to non-empty */
	if (wrd->rd_ring->br_imask == 0 &&
	    wrd->rd_ring->br_rindex == oprod)
		*needsig = 1;
	else
		*needsig = 0;

	return (0);
}

int
hv_channel_send(struct hv_channel *ch, void *data, uint32_t datalen,
    uint64_t rid, int type, uint32_t flags)
{
	struct hv_softc *sc = ch->ch_sc;
	struct vmbus_chanpkt cp;
	struct iovec iov[3];
	uint32_t pktlen, pktlen_aligned;
	uint64_t zeropad = 0;
	int rv, needsig = 0;

	pktlen = sizeof(cp) + datalen;
	pktlen_aligned = roundup(pktlen, sizeof(uint64_t));

	cp.cp_hdr.cph_type = type;
	cp.cp_hdr.cph_flags = flags;
	VMBUS_CHANPKT_SETLEN(cp.cp_hdr.cph_hlen, sizeof(cp));
	VMBUS_CHANPKT_SETLEN(cp.cp_hdr.cph_tlen, pktlen_aligned);
	cp.cp_hdr.cph_tid = rid;

	iov[0].iov_base = &cp;
	iov[0].iov_len = sizeof(cp);

	iov[1].iov_base = data;
	iov[1].iov_len = datalen;

	iov[2].iov_base = &zeropad;
	iov[2].iov_len = pktlen_aligned - pktlen;

	mtx_enter(&ch->ch_wrd.rd_lock);
	rv = hv_ring_write(&ch->ch_wrd, iov, 3, &needsig);
	mtx_leave(&ch->ch_wrd.rd_lock);
	if (rv == 0 && needsig)
		hv_channel_setevent(sc, ch);

	return (rv);
}

int
hv_channel_send_sgl(struct hv_channel *ch, struct vmbus_gpa *sgl,
    uint32_t nsge, void *data, uint32_t datalen, uint64_t rid)
{
	struct hv_softc *sc = ch->ch_sc;
	struct vmbus_chanpkt_sglist cp;
	struct iovec iov[4];
	uint32_t buflen, pktlen, pktlen_aligned;
	uint64_t zeropad = 0;
	int rv, needsig = 0;

	buflen = sizeof(struct vmbus_gpa) * nsge;
	pktlen = sizeof(cp) + datalen + buflen;
	pktlen_aligned = roundup(pktlen, sizeof(uint64_t));

	cp.cp_hdr.cph_type = VMBUS_CHANPKT_TYPE_GPA;
	cp.cp_hdr.cph_flags = VMBUS_CHANPKT_FLAG_RC;
	VMBUS_CHANPKT_SETLEN(cp.cp_hdr.cph_hlen, sizeof(cp) + buflen);
	VMBUS_CHANPKT_SETLEN(cp.cp_hdr.cph_tlen, pktlen_aligned);
	cp.cp_hdr.cph_tid = rid;
	cp.cp_gpa_cnt = nsge;
	cp.cp_rsvd = 0;

	iov[0].iov_base = &cp;
	iov[0].iov_len = sizeof(cp);

	iov[1].iov_base = sgl;
	iov[1].iov_len = buflen;

	iov[2].iov_base = data;
	iov[2].iov_len = datalen;

	iov[3].iov_base = &zeropad;
	iov[3].iov_len = pktlen_aligned - pktlen;

	mtx_enter(&ch->ch_wrd.rd_lock);
	rv = hv_ring_write(&ch->ch_wrd, iov, 4, &needsig);
	mtx_leave(&ch->ch_wrd.rd_lock);
	if (rv == 0 && needsig)
		hv_channel_setevent(sc, ch);

	return (rv);
}

int
hv_channel_send_prpl(struct hv_channel *ch, struct vmbus_gpa_range *prpl,
    uint32_t nprp, void *data, uint32_t datalen, uint64_t rid)
{
	struct hv_softc *sc = ch->ch_sc;
	struct vmbus_chanpkt_prplist cp;
	struct iovec iov[4];
	uint32_t buflen, pktlen, pktlen_aligned;
	uint64_t zeropad = 0;
	int rv, needsig = 0;

	buflen = sizeof(struct vmbus_gpa_range) * (nprp + 1);
	pktlen = sizeof(cp) + datalen + buflen;
	pktlen_aligned = roundup(pktlen, sizeof(uint64_t));

	cp.cp_hdr.cph_type = VMBUS_CHANPKT_TYPE_GPA;
	cp.cp_hdr.cph_flags = VMBUS_CHANPKT_FLAG_RC;
	VMBUS_CHANPKT_SETLEN(cp.cp_hdr.cph_hlen, sizeof(cp) + buflen);
	VMBUS_CHANPKT_SETLEN(cp.cp_hdr.cph_tlen, pktlen_aligned);
	cp.cp_hdr.cph_tid = rid;
	cp.cp_range_cnt = 1;
	cp.cp_rsvd = 0;

	iov[0].iov_base = &cp;
	iov[0].iov_len = sizeof(cp);

	iov[1].iov_base = prpl;
	iov[1].iov_len = buflen;

	iov[2].iov_base = data;
	iov[2].iov_len = datalen;

	iov[3].iov_base = &zeropad;
	iov[3].iov_len = pktlen_aligned - pktlen;

	mtx_enter(&ch->ch_wrd.rd_lock);
	rv = hv_ring_write(&ch->ch_wrd, iov, 4, &needsig);
	mtx_leave(&ch->ch_wrd.rd_lock);
	if (rv == 0 && needsig)
		hv_channel_setevent(sc, ch);

	return (rv);
}

int
hv_ring_peek(struct hv_ring_data *rrd, void *data, uint32_t datalen)
{
	uint32_t avail;

	KASSERT(datalen <= rrd->rd_dsize);

	hv_ring_avail(rrd, NULL, &avail);
	if (avail < datalen)
		return (EAGAIN);

	hv_ring_get(rrd, (uint8_t *)data, datalen, 1);
	return (0);
}

int
hv_ring_read(struct hv_ring_data *rrd, void *data, uint32_t datalen,
    uint32_t offset)
{
	uint64_t indices;
	uint32_t avail;

	KASSERT(datalen <= rrd->rd_dsize);

	hv_ring_avail(rrd, NULL, &avail);
	if (avail < datalen) {
		DPRINTF("%s: avail %u datalen %u\n", __func__, avail, datalen);
		return (EAGAIN);
	}

	if (offset) {
		rrd->rd_cons += offset;
		if (rrd->rd_cons >= rrd->rd_dsize)
			rrd->rd_cons -= rrd->rd_dsize;
	}

	hv_ring_get(rrd, (uint8_t *)data, datalen, 0);
	hv_ring_get(rrd, (uint8_t *)&indices, sizeof(indices), 0);

	virtio_membar_sync();
	rrd->rd_ring->br_rindex = rrd->rd_cons;

	return (0);
}

int
hv_channel_recv(struct hv_channel *ch, void *data, uint32_t datalen,
    uint32_t *rlen, uint64_t *rid, int raw)
{
	struct vmbus_chanpkt_hdr cph;
	uint32_t offset, pktlen;
	int rv;

	*rlen = 0;

	mtx_enter(&ch->ch_rrd.rd_lock);

	if ((rv = hv_ring_peek(&ch->ch_rrd, &cph, sizeof(cph))) != 0) {
		mtx_leave(&ch->ch_rrd.rd_lock);
		return (rv);
	}

	offset = raw ? 0 : VMBUS_CHANPKT_GETLEN(cph.cph_hlen);
	pktlen = VMBUS_CHANPKT_GETLEN(cph.cph_tlen) - offset;
	if (pktlen > datalen) {
		mtx_leave(&ch->ch_rrd.rd_lock);
		printf("%s: pktlen %u datalen %u\n", __func__, pktlen, datalen);
		return (EINVAL);
	}

	rv = hv_ring_read(&ch->ch_rrd, data, pktlen, offset);
	if (rv == 0) {
		*rlen = pktlen;
		*rid = cph.cph_tid;
	}

	mtx_leave(&ch->ch_rrd.rd_lock);

	return (rv);
}

static inline void
hv_ring_mask(struct hv_ring_data *rd)
{
	virtio_membar_sync();
	rd->rd_ring->br_imask = 1;
	virtio_membar_sync();
}

static inline void
hv_ring_unmask(struct hv_ring_data *rd)
{
	virtio_membar_sync();
	rd->rd_ring->br_imask = 0;
	virtio_membar_sync();
}

void
hv_channel_pause(struct hv_channel *ch)
{
	hv_ring_mask(&ch->ch_rrd);
}

uint
hv_channel_unpause(struct hv_channel *ch)
{
	uint32_t avail;

	hv_ring_unmask(&ch->ch_rrd);
	hv_ring_avail(&ch->ch_rrd, NULL, &avail);

	return (avail);
}

uint
hv_channel_ready(struct hv_channel *ch)
{
	uint32_t avail;

	hv_ring_avail(&ch->ch_rrd, NULL, &avail);

	return (avail);
}

/* How many PFNs can be referenced by the header */
#define HV_NPFNHDR	((VMBUS_MSG_DSIZE_MAX -	\
	  sizeof(struct vmbus_chanmsg_gpadl_conn)) / sizeof(uint64_t))

/* How many PFNs can be referenced by the body */
#define HV_NPFNBODY	((VMBUS_MSG_DSIZE_MAX -	\
	  sizeof(struct vmbus_chanmsg_gpadl_subconn)) / sizeof(uint64_t))

int
hv_handle_alloc(struct hv_channel *ch, void *buffer, uint32_t buflen,
    uint32_t *handle)
{
	struct hv_softc *sc = ch->ch_sc;
	struct vmbus_chanmsg_gpadl_conn *hdr;
	struct vmbus_chanmsg_gpadl_subconn *cmd;
	struct vmbus_chanmsg_gpadl_connresp rsp;
	struct hv_msg *msg;
	int i, j, last, left, rv;
	int bodylen = 0, ncmds = 0, pfn = 0;
	int waitflag = cold ? M_NOWAIT : M_WAITOK;
	uint64_t *frames;
	paddr_t pa;
	caddr_t body;
	/* Total number of pages to reference */
	int total = atop(buflen);
	/* Number of pages that will fit the header */
	int inhdr = MIN(total, HV_NPFNHDR);

	KASSERT((buflen & (PAGE_SIZE - 1)) == 0);

	if ((msg = malloc(sizeof(*msg), M_DEVBUF, M_ZERO | waitflag)) == NULL)
		return (ENOMEM);

	/* Prepare array of frame addresses */
	if ((frames = mallocarray(total, sizeof(*frames), M_DEVBUF, M_ZERO |
	    waitflag)) == NULL) {
		free(msg, M_DEVBUF, sizeof(*msg));
		return (ENOMEM);
	}
	for (i = 0; i < total; i++) {
		if (!pmap_extract(pmap_kernel(), (vaddr_t)buffer +
		    PAGE_SIZE * i, &pa)) {
			free(msg, M_DEVBUF, sizeof(*msg));
			free(frames, M_DEVBUF, total * sizeof(*frames));
			return (EFAULT);
		}
		frames[i] = atop(pa);
	}

	msg->msg_req.hc_dsize = sizeof(struct vmbus_chanmsg_gpadl_conn) +
	    inhdr * sizeof(uint64_t);
	hdr = (struct vmbus_chanmsg_gpadl_conn *)msg->msg_req.hc_data;
	msg->msg_rsp = &rsp;
	msg->msg_rsplen = sizeof(rsp);
	if (waitflag == M_NOWAIT)
		msg->msg_flags = MSGF_NOSLEEP;

	left = total - inhdr;

	/* Allocate additional gpadl_body structures if required */
	if (left > 0) {
		ncmds = MAX(1, left / HV_NPFNBODY + left % HV_NPFNBODY);
		bodylen = ncmds * VMBUS_MSG_DSIZE_MAX;
		body = malloc(bodylen, M_DEVBUF, M_ZERO | waitflag);
		if (body == NULL) {
			free(msg, M_DEVBUF, sizeof(*msg));
			free(frames, M_DEVBUF, atop(buflen) * sizeof(*frames));
			return (ENOMEM);
		}
	}

	*handle = atomic_inc_int_nv(&sc->sc_handle);

	hdr->chm_hdr.chm_type = VMBUS_CHANMSG_GPADL_CONN;
	hdr->chm_chanid = ch->ch_id;
	hdr->chm_gpadl = *handle;

	/* Single range for a contiguous buffer */
	hdr->chm_range_cnt = 1;
	hdr->chm_range_len = sizeof(struct vmbus_gpa_range) + total *
	    sizeof(uint64_t);
	hdr->chm_range.gpa_ofs = 0;
	hdr->chm_range.gpa_len = buflen;

	/* Fit as many pages as possible into the header */
	for (i = 0; i < inhdr; i++)
		hdr->chm_range.gpa_page[i] = frames[pfn++];

	for (i = 0; i < ncmds; i++) {
		cmd = (struct vmbus_chanmsg_gpadl_subconn *)(body +
		    VMBUS_MSG_DSIZE_MAX * i);
		cmd->chm_hdr.chm_type = VMBUS_CHANMSG_GPADL_SUBCONN;
		cmd->chm_gpadl = *handle;
		last = MIN(left, HV_NPFNBODY);
		for (j = 0; j < last; j++)
			cmd->chm_gpa_page[j] = frames[pfn++];
		left -= last;
	}

	rv = hv_start(sc, msg);
	if (rv != 0) {
		DPRINTF("%s: GPADL_CONN failed\n", sc->sc_dev.dv_xname);
		goto out;
	}
	for (i = 0; i < ncmds; i++) {
		int cmdlen = sizeof(*cmd);
		cmd = (struct vmbus_chanmsg_gpadl_subconn *)(body +
		    VMBUS_MSG_DSIZE_MAX * i);
		/* Last element can be short */
		if (i == ncmds - 1)
			cmdlen += last * sizeof(uint64_t);
		else
			cmdlen += HV_NPFNBODY * sizeof(uint64_t);
		rv = hv_cmd(sc, cmd, cmdlen, NULL, 0, waitflag | HCF_NOREPLY);
		if (rv != 0) {
			DPRINTF("%s: GPADL_SUBCONN (iteration %d/%d) failed "
			    "with %d\n", sc->sc_dev.dv_xname, i, ncmds, rv);
			goto out;
		}
	}
	rv = hv_reply(sc, msg);
	if (rv != 0)
		DPRINTF("%s: GPADL allocation failed with %d\n",
		    sc->sc_dev.dv_xname, rv);

 out:
	free(msg, M_DEVBUF, sizeof(*msg));
	free(frames, M_DEVBUF, total * sizeof(*frames));
	if (bodylen > 0)
		free(body, M_DEVBUF, bodylen);
	if (rv != 0)
		return (rv);

	KASSERT(*handle == rsp.chm_gpadl);

	return (0);
}

void
hv_handle_free(struct hv_channel *ch, uint32_t handle)
{
	struct hv_softc *sc = ch->ch_sc;
	struct vmbus_chanmsg_gpadl_disconn cmd;
	struct vmbus_chanmsg_gpadl_disconn rsp;
	int rv;

	memset(&cmd, 0, sizeof(cmd));
	cmd.chm_hdr.chm_type = VMBUS_CHANMSG_GPADL_DISCONN;
	cmd.chm_chanid = ch->ch_id;
	cmd.chm_gpadl = handle;

	rv = hv_cmd(sc, &cmd, sizeof(cmd), &rsp, sizeof(rsp), cold ?
	    HCF_NOSLEEP : 0);
	if (rv)
		DPRINTF("%s: GPADL_DISCONN failed with %d\n",
		    sc->sc_dev.dv_xname, rv);
}

static int
hv_attach_print(void *aux, const char *name)
{
	struct hv_attach_args *aa = aux;

	if (name)
		printf("\"%s\" at %s", aa->aa_ident, name);

	return (UNCONF);
}

int
hv_attach_devices(struct hv_softc *sc)
{
	struct hv_dev *dv;
	struct hv_channel *ch;

	SLIST_INIT(&sc->sc_devs);
	mtx_init(&sc->sc_devlck, IPL_NET);

	TAILQ_FOREACH(ch, &sc->sc_channels, ch_entry) {
		if (ch->ch_state != HV_CHANSTATE_OFFERED)
			continue;
		if (!(ch->ch_flags & CHF_MONITOR))
			continue;
		dv = malloc(sizeof(*dv), M_DEVBUF, M_ZERO | M_NOWAIT);
		if (dv == NULL) {
			printf("%s: failed to allocate device object\n",
			    sc->sc_dev.dv_xname);
			return (-1);
		}
		dv->dv_aa.aa_parent = sc;
		dv->dv_aa.aa_type = &ch->ch_type;
		dv->dv_aa.aa_inst = &ch->ch_inst;
		dv->dv_aa.aa_ident = ch->ch_ident;
		dv->dv_aa.aa_chan = ch;
		dv->dv_aa.aa_dmat = sc->sc_dmat;
		mtx_enter(&sc->sc_devlck);
		SLIST_INSERT_HEAD(&sc->sc_devs, dv, dv_entry);
		mtx_leave(&sc->sc_devlck);
		config_found((struct device *)sc, &dv->dv_aa, hv_attach_print);
	}
	return (0);
}

void
hv_evcount_attach(struct hv_channel *ch, const char *name)
{
	struct hv_softc *sc = ch->ch_sc;

	evcount_attach(&ch->ch_evcnt, name, &sc->sc_idtvec);
}
