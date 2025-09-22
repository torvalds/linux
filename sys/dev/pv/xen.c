/*	$OpenBSD: xen.c,v 1.100 2024/11/27 02:38:35 jsg Exp $	*/

/*
 * Copyright (c) 2015, 2016, 2017 Mike Belopuhov
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>

/* Xen requires locked atomic operations */
#ifndef MULTIPROCESSOR
#define _XENMPATOMICS
#define MULTIPROCESSOR
#endif
#include <sys/atomic.h>
#ifdef _XENMPATOMICS
#undef MULTIPROCESSOR
#undef _XENMPATOMICS
#endif

#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/refcnt.h>
#include <sys/malloc.h>
#include <sys/stdint.h>
#include <sys/device.h>
#include <sys/task.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>

#include <uvm/uvm_extern.h>

#include <machine/i82489var.h>

#include <dev/pv/pvvar.h>
#include <dev/pv/pvreg.h>
#include <dev/pv/xenreg.h>
#include <dev/pv/xenvar.h>

/* #define XEN_DEBUG */

#ifdef XEN_DEBUG
#define DPRINTF(x...)		printf(x)
#else
#define DPRINTF(x...)
#endif

struct xen_softc *xen_sc;

int	xen_init_hypercall(struct xen_softc *);
int	xen_getfeatures(struct xen_softc *);
int	xen_init_info_page(struct xen_softc *);
int	xen_init_cbvec(struct xen_softc *);
int	xen_init_interrupts(struct xen_softc *);
void	xen_intr_dispatch(void *);
int	xen_init_grant_tables(struct xen_softc *);
struct xen_gntent *
	xen_grant_table_grow(struct xen_softc *);
int	xen_grant_table_alloc(struct xen_softc *, grant_ref_t *);
void	xen_grant_table_free(struct xen_softc *, grant_ref_t);
void	xen_grant_table_enter(struct xen_softc *, grant_ref_t, paddr_t,
	    int, int);
void	xen_grant_table_remove(struct xen_softc *, grant_ref_t);
void	xen_disable_emulated_devices(struct xen_softc *);

int 	xen_match(struct device *, void *, void *);
void	xen_attach(struct device *, struct device *, void *);
void	xen_deferred(struct device *);
void	xen_control(void *);
void	xen_hotplug(void *);
void	xen_resume(struct device *);
int	xen_activate(struct device *, int);
int	xen_attach_device(struct xen_softc *, struct xen_devlist *,
	    const char *, const char *);
int	xen_probe_devices(struct xen_softc *);

int	xen_bus_dmamap_create(bus_dma_tag_t, bus_size_t, int, bus_size_t,
	    bus_size_t, int, bus_dmamap_t *);
void	xen_bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
int	xen_bus_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *, bus_size_t,
	    struct proc *, int);
int	xen_bus_dmamap_load_mbuf(bus_dma_tag_t, bus_dmamap_t, struct mbuf *,
	    int);
void	xen_bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
void	xen_bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t,
	    bus_size_t, int);

int	xs_attach(struct xen_softc *);

struct cfdriver xen_cd = {
	NULL, "xen", DV_DULL
};

const struct cfattach xen_ca = {
	sizeof(struct xen_softc), xen_match, xen_attach, NULL, xen_activate
};

struct bus_dma_tag xen_bus_dma_tag = {
	NULL,
	xen_bus_dmamap_create,
	xen_bus_dmamap_destroy,
	xen_bus_dmamap_load,
	xen_bus_dmamap_load_mbuf,
	NULL,
	NULL,
	xen_bus_dmamap_unload,
	xen_bus_dmamap_sync,
	_bus_dmamem_alloc,
	NULL,
	_bus_dmamem_free,
	_bus_dmamem_map,
	_bus_dmamem_unmap,
	NULL,
};

int
xen_match(struct device *parent, void *match, void *aux)
{
	struct pv_attach_args *pva = aux;
	struct pvbus_hv *hv = &pva->pva_hv[PVBUS_XEN];

	if (hv->hv_base == 0)
		return (0);

	return (1);
}

void
xen_attach(struct device *parent, struct device *self, void *aux)
{
	struct pv_attach_args *pva = (struct pv_attach_args *)aux;
	struct pvbus_hv *hv = &pva->pva_hv[PVBUS_XEN];
	struct xen_softc *sc = (struct xen_softc *)self;

	sc->sc_base = hv->hv_base;
	sc->sc_dmat = pva->pva_dmat;

	if (xen_init_hypercall(sc))
		return;

	/* Wire it up to the global */
	xen_sc = sc;

	if (xen_getfeatures(sc))
		return;

	if (xen_init_info_page(sc))
		return;

	xen_init_cbvec(sc);

	if (xen_init_interrupts(sc))
		return;

	if (xen_init_grant_tables(sc))
		return;

	if (xs_attach(sc))
		return;

	xen_probe_devices(sc);

	/* pvbus(4) key/value interface */
	hv->hv_kvop = xs_kvop;
	hv->hv_arg = sc;

	xen_disable_emulated_devices(sc);

	config_mountroot(self, xen_deferred);
}

void
xen_deferred(struct device *self)
{
	struct xen_softc *sc = (struct xen_softc *)self;

	if (!(sc->sc_flags & XSF_CBVEC)) {
		DPRINTF("%s: callback vector hasn't been established\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	xen_intr_enable();

	if (xs_watch(sc, "control", "shutdown", &sc->sc_ctltsk,
	    xen_control, sc))
		printf("%s: failed to setup shutdown control watch\n",
		    sc->sc_dev.dv_xname);
}

void
xen_control(void *arg)
{
	struct xen_softc *sc = arg;
	struct xs_transaction xst;
	char action[128];
	int error;

	memset(&xst, 0, sizeof(xst));
	xst.xst_id = 0;
	xst.xst_cookie = sc->sc_xs;

	error = xs_getprop(sc, "control", "shutdown", action, sizeof(action));
	if (error) {
		if (error != ENOENT)
			printf("%s: failed to process control event\n",
			    sc->sc_dev.dv_xname);
		return;
	}

	if (strlen(action) == 0)
		return;

	/* Acknowledge the event */
	xs_setprop(sc, "control", "shutdown", "", 0);

	if (strcmp(action, "halt") == 0 || strcmp(action, "poweroff") == 0) {
		pvbus_shutdown(&sc->sc_dev);
	} else if (strcmp(action, "reboot") == 0) {
		pvbus_reboot(&sc->sc_dev);
	} else if (strcmp(action, "crash") == 0) {
		panic("xen told us to do this");
	} else if (strcmp(action, "suspend") == 0) {
		/* Not implemented yet */
	} else {
		printf("%s: unknown shutdown event \"%s\"\n",
		    sc->sc_dev.dv_xname, action);
	}
}

void
xen_resume(struct device *self)
{
}

int
xen_activate(struct device *self, int act)
{
	int rv = 0;

	switch (act) {
	case DVACT_RESUME:
		xen_resume(self);
		break;
	}
	return (rv);
}

int
xen_init_hypercall(struct xen_softc *sc)
{
	extern void *xen_hypercall_page;
	uint32_t regs[4];
	paddr_t pa;

	/* Get hypercall page configuration MSR */
	CPUID(sc->sc_base + CPUID_OFFSET_XEN_HYPERCALL,
	    regs[0], regs[1], regs[2], regs[3]);

	/* We don't support more than one hypercall page */
	if (regs[0] != 1) {
		printf(": requested %u hypercall pages\n", regs[0]);
		return (-1);
	}

	sc->sc_hc = &xen_hypercall_page;

	if (!pmap_extract(pmap_kernel(), (vaddr_t)sc->sc_hc, &pa)) {
		printf(": hypercall page PA extraction failed\n");
		return (-1);
	}
	wrmsr(regs[1], pa);

	return (0);
}

int
xen_hypercall(struct xen_softc *sc, int op, int argc, ...)
{
	va_list ap;
	ulong argv[5];
	int i;

	if (argc < 0 || argc > 5)
		return (-1);
	va_start(ap, argc);
	for (i = 0; i < argc; i++)
		argv[i] = (ulong)va_arg(ap, ulong);
	va_end(ap);
	return (xen_hypercallv(sc, op, argc, argv));
}

int
xen_hypercallv(struct xen_softc *sc, int op, int argc, ulong *argv)
{
	ulong hcall;
	int rv = 0;

	hcall = (ulong)sc->sc_hc + op * 32;

#if defined(XEN_DEBUG) && disabled
	{
		int i;

		printf("hypercall %d", op);
		if (argc > 0) {
			printf(", args {");
			for (i = 0; i < argc; i++)
				printf(" %#lx", argv[i]);
			printf(" }\n");
		} else
			printf("\n");
	}
#endif

	switch (argc) {
	case 0: {
		HYPERCALL_RES1;
		__asm__ volatile (			\
			  HYPERCALL_LABEL		\
			: HYPERCALL_OUT1		\
			: HYPERCALL_PTR(hcall)		\
			: HYPERCALL_CLOBBER		\
		);
		HYPERCALL_RET(rv);
		break;
	}
	case 1: {
		HYPERCALL_RES1; HYPERCALL_RES2;
		HYPERCALL_ARG1(argv[0]);
		__asm__ volatile (			\
			  HYPERCALL_LABEL		\
			: HYPERCALL_OUT1 HYPERCALL_OUT2	\
			: HYPERCALL_IN1			\
			, HYPERCALL_PTR(hcall)		\
			: HYPERCALL_CLOBBER		\
		);
		HYPERCALL_RET(rv);
		break;
	}
	case 2: {
		HYPERCALL_RES1; HYPERCALL_RES2; HYPERCALL_RES3;
		HYPERCALL_ARG1(argv[0]); HYPERCALL_ARG2(argv[1]);
		__asm__ volatile (			\
			  HYPERCALL_LABEL		\
			: HYPERCALL_OUT1 HYPERCALL_OUT2	\
			  HYPERCALL_OUT3		\
			: HYPERCALL_IN1	HYPERCALL_IN2	\
			, HYPERCALL_PTR(hcall)		\
			: HYPERCALL_CLOBBER		\
		);
		HYPERCALL_RET(rv);
		break;
	}
	case 3: {
		HYPERCALL_RES1; HYPERCALL_RES2; HYPERCALL_RES3;
		HYPERCALL_RES4;
		HYPERCALL_ARG1(argv[0]); HYPERCALL_ARG2(argv[1]);
		HYPERCALL_ARG3(argv[2]);
		__asm__ volatile (			\
			  HYPERCALL_LABEL		\
			: HYPERCALL_OUT1 HYPERCALL_OUT2	\
			  HYPERCALL_OUT3 HYPERCALL_OUT4	\
			: HYPERCALL_IN1	HYPERCALL_IN2	\
			  HYPERCALL_IN3			\
			, HYPERCALL_PTR(hcall)		\
			: HYPERCALL_CLOBBER		\
		);
		HYPERCALL_RET(rv);
		break;
	}
	case 4: {
		HYPERCALL_RES1; HYPERCALL_RES2; HYPERCALL_RES3;
		HYPERCALL_RES4; HYPERCALL_RES5;
		HYPERCALL_ARG1(argv[0]); HYPERCALL_ARG2(argv[1]);
		HYPERCALL_ARG3(argv[2]); HYPERCALL_ARG4(argv[3]);
		__asm__ volatile (			\
			  HYPERCALL_LABEL		\
			: HYPERCALL_OUT1 HYPERCALL_OUT2	\
			  HYPERCALL_OUT3 HYPERCALL_OUT4	\
			  HYPERCALL_OUT5		\
			: HYPERCALL_IN1	HYPERCALL_IN2	\
			  HYPERCALL_IN3	HYPERCALL_IN4	\
			, HYPERCALL_PTR(hcall)		\
			: HYPERCALL_CLOBBER		\
		);
		HYPERCALL_RET(rv);
		break;
	}
	case 5: {
		HYPERCALL_RES1; HYPERCALL_RES2; HYPERCALL_RES3;
		HYPERCALL_RES4; HYPERCALL_RES5; HYPERCALL_RES6;
		HYPERCALL_ARG1(argv[0]); HYPERCALL_ARG2(argv[1]);
		HYPERCALL_ARG3(argv[2]); HYPERCALL_ARG4(argv[3]);
		HYPERCALL_ARG5(argv[4]);
		__asm__ volatile (			\
			  HYPERCALL_LABEL		\
			: HYPERCALL_OUT1 HYPERCALL_OUT2	\
			  HYPERCALL_OUT3 HYPERCALL_OUT4	\
			  HYPERCALL_OUT5 HYPERCALL_OUT6	\
			: HYPERCALL_IN1	HYPERCALL_IN2	\
			  HYPERCALL_IN3	HYPERCALL_IN4	\
			  HYPERCALL_IN5			\
			, HYPERCALL_PTR(hcall)		\
			: HYPERCALL_CLOBBER		\
		);
		HYPERCALL_RET(rv);
		break;
	}
	default:
		DPRINTF("%s: wrong number of arguments: %d\n", __func__, argc);
		rv = -1;
		break;
	}
	return (rv);
}

int
xen_getfeatures(struct xen_softc *sc)
{
	struct xen_feature_info xfi;

	memset(&xfi, 0, sizeof(xfi));
	if (xen_hypercall(sc, XC_VERSION, 2, XENVER_get_features, &xfi) < 0) {
		printf(": failed to fetch features\n");
		return (-1);
	}
	sc->sc_features = xfi.submap;
#ifdef XEN_DEBUG
	printf(": features %b", sc->sc_features,
	    "\20\014DOM0\013PIRQ\012PVCLOCK\011CBVEC\010GNTFLAGS\007HMA"
	    "\006PTUPD\005PAE4G\004SUPERVISOR\003AUTOPMAP\002WDT\001WPT");
#else
	printf(": features %#x", sc->sc_features);
#endif
	return (0);
}

#ifdef XEN_DEBUG
void
xen_print_info_page(void)
{
	struct xen_softc *sc = xen_sc;
	struct shared_info *s = sc->sc_ipg;
	struct vcpu_info *v;
	int i;

	virtio_membar_sync();
	for (i = 0; i < XEN_LEGACY_MAX_VCPUS; i++) {
		v = &s->vcpu_info[i];
		if (!v->evtchn_upcall_pending && !v->evtchn_upcall_mask &&
		    !v->evtchn_pending_sel && !v->time.version &&
		    !v->time.tsc_timestamp && !v->time.system_time &&
		    !v->time.tsc_to_system_mul && !v->time.tsc_shift)
			continue;
		printf("vcpu%d:\n"
		    "   upcall_pending=%02x upcall_mask=%02x pending_sel=%#lx\n"
		    "   time version=%u tsc=%llu system=%llu\n"
		    "   time mul=%u shift=%d\n",
		    i, v->evtchn_upcall_pending, v->evtchn_upcall_mask,
		    v->evtchn_pending_sel, v->time.version,
		    v->time.tsc_timestamp, v->time.system_time,
		    v->time.tsc_to_system_mul, v->time.tsc_shift);
	}
	printf("pending events: ");
	for (i = 0; i < nitems(s->evtchn_pending); i++) {
		if (s->evtchn_pending[i] == 0)
			continue;
		printf(" %d:%#lx", i, s->evtchn_pending[i]);
	}
	printf("\nmasked events: ");
	for (i = 0; i < nitems(s->evtchn_mask); i++) {
		if (s->evtchn_mask[i] == 0xffffffffffffffffULL)
			continue;
		printf(" %d:%#lx", i, s->evtchn_mask[i]);
	}
	printf("\nwc ver=%u sec=%u nsec=%u\n", s->wc_version, s->wc_sec,
	    s->wc_nsec);
	printf("arch maxpfn=%lu framelist=%lu nmi=%lu\n", s->arch.max_pfn,
	    s->arch.pfn_to_mfn_frame_list, s->arch.nmi_reason);
}
#endif	/* XEN_DEBUG */

int
xen_init_info_page(struct xen_softc *sc)
{
	struct xen_add_to_physmap xatp;
	paddr_t pa;

	sc->sc_ipg = malloc(PAGE_SIZE, M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sc_ipg == NULL) {
		printf(": failed to allocate shared info page\n");
		return (-1);
	}
	if (!pmap_extract(pmap_kernel(), (vaddr_t)sc->sc_ipg, &pa)) {
		printf(": shared info page PA extraction failed\n");
		free(sc->sc_ipg, M_DEVBUF, PAGE_SIZE);
		return (-1);
	}
	xatp.domid = DOMID_SELF;
	xatp.idx = 0;
	xatp.space = XENMAPSPACE_shared_info;
	xatp.gpfn = atop(pa);
	if (xen_hypercall(sc, XC_MEMORY, 2, XENMEM_add_to_physmap, &xatp)) {
		printf(": failed to register shared info page\n");
		free(sc->sc_ipg, M_DEVBUF, PAGE_SIZE);
		return (-1);
	}
	return (0);
}

int
xen_init_cbvec(struct xen_softc *sc)
{
	struct xen_hvm_param xhp;

	if ((sc->sc_features & XENFEAT_CBVEC) == 0)
		return (ENOENT);

	xhp.domid = DOMID_SELF;
	xhp.index = HVM_PARAM_CALLBACK_IRQ;
	xhp.value = HVM_CALLBACK_VECTOR(LAPIC_XEN_VECTOR);
	if (xen_hypercall(sc, XC_HVM, 2, HVMOP_set_param, &xhp)) {
		/* Will retry with the xspd(4) PCI interrupt */
		return (ENOENT);
	}
	DPRINTF(", idtvec %d", LAPIC_XEN_VECTOR);

	sc->sc_flags |= XSF_CBVEC;

	return (0);
}

int
xen_init_interrupts(struct xen_softc *sc)
{
	int i;

	sc->sc_irq = LAPIC_XEN_VECTOR;

	/*
	 * Clear all pending events and mask all interrupts
	 */
	for (i = 0; i < nitems(sc->sc_ipg->evtchn_pending); i++) {
		sc->sc_ipg->evtchn_pending[i] = 0;
		sc->sc_ipg->evtchn_mask[i] = ~0UL;
	}

	SLIST_INIT(&sc->sc_intrs);

	mtx_init(&sc->sc_islck, IPL_NET);

	return (0);
}

static int
xen_evtchn_hypercall(struct xen_softc *sc, int cmd, void *arg, size_t len)
{
	struct evtchn_op compat;
	int error;

	error = xen_hypercall(sc, XC_EVTCHN, 2, cmd, arg);
	if (error == -ENOXENSYS) {
		memset(&compat, 0, sizeof(compat));
		compat.cmd = cmd;
		memcpy(&compat.u, arg, len);
		error = xen_hypercall(sc, XC_OEVTCHN, 1, &compat);
	}
	return (error);
}

static inline void
xen_intsrc_add(struct xen_softc *sc, struct xen_intsrc *xi)
{
	refcnt_init(&xi->xi_refcnt);
	mtx_enter(&sc->sc_islck);
	SLIST_INSERT_HEAD(&sc->sc_intrs, xi, xi_entry);
	mtx_leave(&sc->sc_islck);
}

static inline struct xen_intsrc *
xen_intsrc_acquire(struct xen_softc *sc, evtchn_port_t port)
{
	struct xen_intsrc *xi = NULL;

	mtx_enter(&sc->sc_islck);
	SLIST_FOREACH(xi, &sc->sc_intrs, xi_entry) {
		if (xi->xi_port == port) {
			refcnt_take(&xi->xi_refcnt);
			break;
		}
	}
	mtx_leave(&sc->sc_islck);
	return (xi);
}

static inline void
xen_intsrc_release(struct xen_softc *sc, struct xen_intsrc *xi)
{
	refcnt_rele_wake(&xi->xi_refcnt);
}

static inline struct xen_intsrc *
xen_intsrc_remove(struct xen_softc *sc, evtchn_port_t port)
{
	struct xen_intsrc *xi;

	mtx_enter(&sc->sc_islck);
	SLIST_FOREACH(xi, &sc->sc_intrs, xi_entry) {
		if (xi->xi_port == port) {
			SLIST_REMOVE(&sc->sc_intrs, xi, xen_intsrc, xi_entry);
			break;
		}
	}
	mtx_leave(&sc->sc_islck);
	if (xi != NULL)
		refcnt_finalize(&xi->xi_refcnt, "xenisrm");
	return (xi);
}

static inline void
xen_intr_mask_acquired(struct xen_softc *sc, struct xen_intsrc *xi)
{
	xi->xi_masked = 1;
	set_bit(xi->xi_port, &sc->sc_ipg->evtchn_mask[0]);
}

static inline int
xen_intr_unmask_release(struct xen_softc *sc, struct xen_intsrc *xi)
{
	struct evtchn_unmask eu;

	xi->xi_masked = 0;
	if (!test_bit(xi->xi_port, &sc->sc_ipg->evtchn_mask[0])) {
		xen_intsrc_release(sc, xi);
		return (0);
	}
	eu.port = xi->xi_port;
	xen_intsrc_release(sc, xi);
	return (xen_evtchn_hypercall(sc, EVTCHNOP_unmask, &eu, sizeof(eu)));
}

void
xen_intr_ack(void)
{
	struct xen_softc *sc = xen_sc;
	struct shared_info *s = sc->sc_ipg;
	struct cpu_info *ci = curcpu();
	struct vcpu_info *v = &s->vcpu_info[CPU_INFO_UNIT(ci)];

	v->evtchn_upcall_pending = 0;
	virtio_membar_sync();
}

void
xen_intr(void)
{
	struct xen_softc *sc = xen_sc;
	struct xen_intsrc *xi;
	struct shared_info *s = sc->sc_ipg;
	struct cpu_info *ci = curcpu();
	struct vcpu_info *v = &s->vcpu_info[CPU_INFO_UNIT(ci)];
	ulong pending, selector;
	int port, bit, row;

	v->evtchn_upcall_pending = 0;
	selector = atomic_swap_ulong(&v->evtchn_pending_sel, 0);

	for (row = 0; selector > 0; selector >>= 1, row++) {
		if ((selector & 1) == 0)
			continue;
		if ((sc->sc_ipg->evtchn_pending[row] &
		    ~(sc->sc_ipg->evtchn_mask[row])) == 0)
			continue;
		pending = atomic_swap_ulong(&sc->sc_ipg->evtchn_pending[row],
		    0) & ~(sc->sc_ipg->evtchn_mask[row]);
		for (bit = 0; pending > 0; pending >>= 1, bit++) {
			if ((pending & 1) == 0)
				continue;
			port = (row * LONG_BIT) + bit;
			if ((xi = xen_intsrc_acquire(sc, port)) == NULL) {
				printf("%s: unhandled interrupt on port %d\n",
				    sc->sc_dev.dv_xname, port);
				continue;
			}
			xi->xi_evcnt.ec_count++;
			xen_intr_mask_acquired(sc, xi);
			if (!task_add(xi->xi_taskq, &xi->xi_task))
				xen_intsrc_release(sc, xi);
		}
	}
}

void
xen_intr_schedule(xen_intr_handle_t xih)
{
	struct xen_softc *sc = xen_sc;
	struct xen_intsrc *xi;

	if ((xi = xen_intsrc_acquire(sc, (evtchn_port_t)xih)) != NULL) {
		xen_intr_mask_acquired(sc, xi);
		if (!task_add(xi->xi_taskq, &xi->xi_task))
			xen_intsrc_release(sc, xi);
	}
}

/*
 * This code achieves two goals: 1) makes sure that *after* masking
 * the interrupt source we're not getting more task_adds: sched_barrier
 * will take care of that, and 2) makes sure that the interrupt task
 * has finished executing the current task and won't be called again:
 * it sets up a barrier task to await completion of the current task
 * and relies on the interrupt masking to prevent submission of new
 * tasks in the future.
 */
void
xen_intr_barrier(xen_intr_handle_t xih)
{
	struct xen_softc *sc = xen_sc;
	struct xen_intsrc *xi;

	sched_barrier(NULL);

	if ((xi = xen_intsrc_acquire(sc, (evtchn_port_t)xih)) != NULL) {
		taskq_barrier(xi->xi_taskq);
		xen_intsrc_release(sc, xi);
	}
}

void
xen_intr_signal(xen_intr_handle_t xih)
{
	struct xen_softc *sc = xen_sc;
	struct xen_intsrc *xi;
	struct evtchn_send es;

	if ((xi = xen_intsrc_acquire(sc, (evtchn_port_t)xih)) != NULL) {
		es.port = xi->xi_port;
		xen_intsrc_release(sc, xi);
		xen_evtchn_hypercall(sc, EVTCHNOP_send, &es, sizeof(es));
	}
}

int
xen_intr_establish(evtchn_port_t port, xen_intr_handle_t *xih, int domain,
    void (*handler)(void *), void *arg, char *name)
{
	struct xen_softc *sc = xen_sc;
	struct xen_intsrc *xi;
	struct evtchn_alloc_unbound eau;
#ifdef notyet
	struct evtchn_bind_vcpu ebv;
#endif
#if defined(XEN_DEBUG) && disabled
	struct evtchn_status es;
#endif

	if (port && (xi = xen_intsrc_acquire(sc, port)) != NULL) {
		xen_intsrc_release(sc, xi);
		DPRINTF("%s: interrupt handler has already been established "
		    "for port %u\n", sc->sc_dev.dv_xname, port);
		return (-1);
	}

	xi = malloc(sizeof(*xi), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (xi == NULL)
		return (-1);

	xi->xi_port = (evtchn_port_t)*xih;

	xi->xi_handler = handler;
	xi->xi_ctx = arg;

	xi->xi_taskq = taskq_create(name, 1, IPL_NET, TASKQ_MPSAFE);
	if (!xi->xi_taskq) {
		printf("%s: failed to create interrupt task for %s\n",
		    sc->sc_dev.dv_xname, name);
		free(xi, M_DEVBUF, sizeof(*xi));
		return (-1);
	}
	task_set(&xi->xi_task, xen_intr_dispatch, xi);

	if (port == 0) {
		/* We're being asked to allocate a new event port */
		memset(&eau, 0, sizeof(eau));
		eau.dom = DOMID_SELF;
		eau.remote_dom = domain;
		if (xen_evtchn_hypercall(sc, EVTCHNOP_alloc_unbound, &eau,
		    sizeof(eau)) != 0) {
			DPRINTF("%s: failed to allocate new event port\n",
			    sc->sc_dev.dv_xname);
			free(xi, M_DEVBUF, sizeof(*xi));
			return (-1);
		}
		*xih = xi->xi_port = eau.port;
	} else {
		*xih = xi->xi_port = port;
		/*
		 * The Event Channel API didn't open this port, so it is not
		 * responsible for closing it automatically on unbind.
		 */
		xi->xi_noclose = 1;
	}

#ifdef notyet
	/* Bind interrupt to VCPU#0 */
	memset(&ebv, 0, sizeof(ebv));
	ebv.port = xi->xi_port;
	ebv.vcpu = 0;
	if (xen_evtchn_hypercall(sc, EVTCHNOP_bind_vcpu, &ebv, sizeof(ebv))) {
		printf("%s: failed to bind interrupt on port %u to vcpu%d\n",
		    sc->sc_dev.dv_xname, ebv.port, ebv.vcpu);
	}
#endif

	evcount_attach(&xi->xi_evcnt, name, &sc->sc_irq);

	xen_intsrc_add(sc, xi);

	/* Mask the event port */
	set_bit(xi->xi_port, &sc->sc_ipg->evtchn_mask[0]);

#if defined(XEN_DEBUG) && disabled
	memset(&es, 0, sizeof(es));
	es.dom = DOMID_SELF;
	es.port = xi->xi_port;
	if (xen_evtchn_hypercall(sc, EVTCHNOP_status, &es, sizeof(es))) {
		printf("%s: failed to obtain status for port %d\n",
		    sc->sc_dev.dv_xname, es.port);
	}
	printf("%s: port %u bound to vcpu%u", sc->sc_dev.dv_xname,
	    es.port, es.vcpu);
	if (es.status == EVTCHNSTAT_interdomain)
		printf(": domain %d port %u\n", es.u.interdomain.dom,
		    es.u.interdomain.port);
	else if (es.status == EVTCHNSTAT_unbound)
		printf(": domain %d\n", es.u.unbound.dom);
	else if (es.status == EVTCHNSTAT_pirq)
		printf(": pirq %u\n", es.u.pirq);
	else if (es.status == EVTCHNSTAT_virq)
		printf(": virq %u\n", es.u.virq);
	else
		printf("\n");
#endif

	return (0);
}

int
xen_intr_disestablish(xen_intr_handle_t xih)
{
	struct xen_softc *sc = xen_sc;
	evtchn_port_t port = (evtchn_port_t)xih;
	struct evtchn_close ec;
	struct xen_intsrc *xi;

	if ((xi = xen_intsrc_remove(sc, port)) == NULL)
		return (-1);

	evcount_detach(&xi->xi_evcnt);

	taskq_destroy(xi->xi_taskq);

	set_bit(xi->xi_port, &sc->sc_ipg->evtchn_mask[0]);
	clear_bit(xi->xi_port, &sc->sc_ipg->evtchn_pending[0]);

	if (!xi->xi_noclose) {
		ec.port = xi->xi_port;
		if (xen_evtchn_hypercall(sc, EVTCHNOP_close, &ec, sizeof(ec))) {
			DPRINTF("%s: failed to close event port %u\n",
			    sc->sc_dev.dv_xname, xi->xi_port);
		}
	}

	free(xi, M_DEVBUF, sizeof(*xi));
	return (0);
}

void
xen_intr_dispatch(void *arg)
{
	struct xen_softc *sc = xen_sc;
	struct xen_intsrc *xi = arg;

	if (xi->xi_handler)
		xi->xi_handler(xi->xi_ctx);

	xen_intr_unmask_release(sc, xi);
}

void
xen_intr_enable(void)
{
	struct xen_softc *sc = xen_sc;
	struct xen_intsrc *xi;
	struct evtchn_unmask eu;

	mtx_enter(&sc->sc_islck);
	SLIST_FOREACH(xi, &sc->sc_intrs, xi_entry) {
		if (!xi->xi_masked) {
			eu.port = xi->xi_port;
			if (xen_evtchn_hypercall(sc, EVTCHNOP_unmask, &eu,
			    sizeof(eu)))
				printf("%s: unmasking port %u failed\n",
				    sc->sc_dev.dv_xname, xi->xi_port);
			virtio_membar_sync();
			if (test_bit(xi->xi_port, &sc->sc_ipg->evtchn_mask[0]))
				printf("%s: port %u is still masked\n",
				    sc->sc_dev.dv_xname, xi->xi_port);
		}
	}
	mtx_leave(&sc->sc_islck);
}

void
xen_intr_mask(xen_intr_handle_t xih)
{
	struct xen_softc *sc = xen_sc;
	evtchn_port_t port = (evtchn_port_t)xih;
	struct xen_intsrc *xi;

	if ((xi = xen_intsrc_acquire(sc, port)) != NULL) {
		xen_intr_mask_acquired(sc, xi);
		xen_intsrc_release(sc, xi);
	}
}

int
xen_intr_unmask(xen_intr_handle_t xih)
{
	struct xen_softc *sc = xen_sc;
	evtchn_port_t port = (evtchn_port_t)xih;
	struct xen_intsrc *xi;

	if ((xi = xen_intsrc_acquire(sc, port)) != NULL)
		return (xen_intr_unmask_release(sc, xi));

	return (0);
}

int
xen_init_grant_tables(struct xen_softc *sc)
{
	struct gnttab_query_size gqs;

	gqs.dom = DOMID_SELF;
	if (xen_hypercall(sc, XC_GNTTAB, 3, GNTTABOP_query_size, &gqs, 1)) {
		printf(": failed the query for grant table pages\n");
		return (-1);
	}
	if (gqs.nr_frames == 0 || gqs.nr_frames > gqs.max_nr_frames) {
		printf(": invalid number of grant table pages: %u/%u\n",
		    gqs.nr_frames, gqs.max_nr_frames);
		return (-1);
	}

	sc->sc_gntmax = gqs.max_nr_frames;

	sc->sc_gnt = mallocarray(sc->sc_gntmax + 1, sizeof(struct xen_gntent),
	    M_DEVBUF, M_ZERO | M_NOWAIT);
	if (sc->sc_gnt == NULL) {
		printf(": failed to allocate grant table lookup table\n");
		return (-1);
	}

	mtx_init(&sc->sc_gntlck, IPL_NET);

	if (xen_grant_table_grow(sc) == NULL) {
		free(sc->sc_gnt, M_DEVBUF, sc->sc_gntmax *
		    sizeof(struct xen_gntent));
		return (-1);
	}

	printf(", %d grant table frames", sc->sc_gntmax);

	xen_bus_dma_tag._cookie = sc;

	return (0);
}

struct xen_gntent *
xen_grant_table_grow(struct xen_softc *sc)
{
	struct xen_add_to_physmap xatp;
	struct xen_gntent *ge;
	void *va;
	paddr_t pa;

	if (sc->sc_gntcnt == sc->sc_gntmax) {
		printf("%s: grant table frame allotment limit reached\n",
		    sc->sc_dev.dv_xname);
		return (NULL);
	}

	va = km_alloc(PAGE_SIZE, &kv_any, &kp_zero, &kd_nowait);
	if (va == NULL)
		return (NULL);
	if (!pmap_extract(pmap_kernel(), (vaddr_t)va, &pa)) {
		printf("%s: grant table page PA extraction failed\n",
		    sc->sc_dev.dv_xname);
		km_free(va, PAGE_SIZE, &kv_any, &kp_zero);
		return (NULL);
	}

	mtx_enter(&sc->sc_gntlck);

	ge = &sc->sc_gnt[sc->sc_gntcnt];
	ge->ge_table = va;

	xatp.domid = DOMID_SELF;
	xatp.idx = sc->sc_gntcnt;
	xatp.space = XENMAPSPACE_grant_table;
	xatp.gpfn = atop(pa);
	if (xen_hypercall(sc, XC_MEMORY, 2, XENMEM_add_to_physmap, &xatp)) {
		printf("%s: failed to add a grant table page\n",
		    sc->sc_dev.dv_xname);
		km_free(ge->ge_table, PAGE_SIZE, &kv_any, &kp_zero);
		mtx_leave(&sc->sc_gntlck);
		return (NULL);
	}
	ge->ge_start = sc->sc_gntcnt * GNTTAB_NEPG;
	/* First page has 8 reserved entries */
	ge->ge_reserved = ge->ge_start == 0 ? GNTTAB_NR_RESERVED_ENTRIES : 0;
	ge->ge_free = GNTTAB_NEPG - ge->ge_reserved;
	ge->ge_next = ge->ge_reserved;
	mtx_init(&ge->ge_lock, IPL_NET);

	sc->sc_gntcnt++;
	mtx_leave(&sc->sc_gntlck);

	return (ge);
}

int
xen_grant_table_alloc(struct xen_softc *sc, grant_ref_t *ref)
{
	struct xen_gntent *ge;
	int i;

	/* Start with a previously allocated table page */
	ge = &sc->sc_gnt[sc->sc_gntcnt - 1];
	if (ge->ge_free > 0) {
		mtx_enter(&ge->ge_lock);
		if (ge->ge_free > 0)
			goto search;
		mtx_leave(&ge->ge_lock);
	}

	/* Try other existing table pages */
	for (i = 0; i < sc->sc_gntcnt; i++) {
		ge = &sc->sc_gnt[i];
		if (ge->ge_free == 0)
			continue;
		mtx_enter(&ge->ge_lock);
		if (ge->ge_free > 0)
			goto search;
		mtx_leave(&ge->ge_lock);
	}

 alloc:
	/* Allocate a new table page */
	if ((ge = xen_grant_table_grow(sc)) == NULL)
		return (-1);

	mtx_enter(&ge->ge_lock);
	if (ge->ge_free == 0) {
		/* We were not fast enough... */
		mtx_leave(&ge->ge_lock);
		goto alloc;
	}

 search:
	for (i = ge->ge_next;
	     /* Math works here because GNTTAB_NEPG is a power of 2 */
	     i != ((ge->ge_next + GNTTAB_NEPG - 1) & (GNTTAB_NEPG - 1));
	     i++) {
		if (i == GNTTAB_NEPG)
			i = 0;
		if (ge->ge_reserved && i < ge->ge_reserved)
			continue;
		if (ge->ge_table[i].frame != 0)
			continue;
		*ref = ge->ge_start + i;
		ge->ge_table[i].flags = GTF_invalid;
		ge->ge_table[i].frame = 0xffffffff; /* Mark as taken */
		if ((ge->ge_next = i + 1) == GNTTAB_NEPG)
			ge->ge_next = ge->ge_reserved;
		ge->ge_free--;
		mtx_leave(&ge->ge_lock);
		return (0);
	}
	mtx_leave(&ge->ge_lock);

	panic("page full, sc %p gnt %p (%d) ge %p", sc, sc->sc_gnt,
	    sc->sc_gntcnt, ge);
	return (-1);
}

void
xen_grant_table_free(struct xen_softc *sc, grant_ref_t ref)
{
	struct xen_gntent *ge;

#ifdef XEN_DEBUG
	if (ref > sc->sc_gntcnt * GNTTAB_NEPG)
		panic("unmanaged ref %u sc %p gnt %p (%d)", ref, sc,
		    sc->sc_gnt, sc->sc_gntcnt);
#endif
	ge = &sc->sc_gnt[ref / GNTTAB_NEPG];
	mtx_enter(&ge->ge_lock);
#ifdef XEN_DEBUG
	if (ref < ge->ge_start || ref > ge->ge_start + GNTTAB_NEPG) {
		mtx_leave(&ge->ge_lock);
		panic("out of bounds ref %u ge %p start %u sc %p gnt %p",
		    ref, ge, ge->ge_start, sc, sc->sc_gnt);
	}
#endif
	ref -= ge->ge_start;
	if (ge->ge_table[ref].flags != GTF_invalid) {
		mtx_leave(&ge->ge_lock);
		panic("reference %u is still in use, flags %#x frame %#x",
		    ref + ge->ge_start, ge->ge_table[ref].flags,
		    ge->ge_table[ref].frame);
	}
	ge->ge_table[ref].frame = 0;
	ge->ge_next = ref;
	ge->ge_free++;
	mtx_leave(&ge->ge_lock);
}

void
xen_grant_table_enter(struct xen_softc *sc, grant_ref_t ref, paddr_t pa,
    int domain, int flags)
{
	struct xen_gntent *ge;

#ifdef XEN_DEBUG
	if (ref > sc->sc_gntcnt * GNTTAB_NEPG)
		panic("unmanaged ref %u sc %p gnt %p (%d)", ref, sc,
		    sc->sc_gnt, sc->sc_gntcnt);
#endif
	ge = &sc->sc_gnt[ref / GNTTAB_NEPG];
#ifdef XEN_DEBUG
	if (ref < ge->ge_start || ref > ge->ge_start + GNTTAB_NEPG) {
		panic("out of bounds ref %u ge %p start %u sc %p gnt %p",
		    ref, ge, ge->ge_start, sc, sc->sc_gnt);
	}
#endif
	ref -= ge->ge_start;
	if (ge->ge_table[ref].flags != GTF_invalid) {
		panic("reference %u is still in use, flags %#x frame %#x",
		    ref + ge->ge_start, ge->ge_table[ref].flags,
		    ge->ge_table[ref].frame);
	}
	ge->ge_table[ref].frame = atop(pa);
	ge->ge_table[ref].domid = domain;
	virtio_membar_sync();
	ge->ge_table[ref].flags = GTF_permit_access | flags;
	virtio_membar_sync();
}

void
xen_grant_table_remove(struct xen_softc *sc, grant_ref_t ref)
{
	struct xen_gntent *ge;
	uint32_t flags, *ptr;
	int loop;

#ifdef XEN_DEBUG
	if (ref > sc->sc_gntcnt * GNTTAB_NEPG)
		panic("unmanaged ref %u sc %p gnt %p (%d)", ref, sc,
		    sc->sc_gnt, sc->sc_gntcnt);
#endif
	ge = &sc->sc_gnt[ref / GNTTAB_NEPG];
#ifdef XEN_DEBUG
	if (ref < ge->ge_start || ref > ge->ge_start + GNTTAB_NEPG) {
		panic("out of bounds ref %u ge %p start %u sc %p gnt %p",
		    ref, ge, ge->ge_start, sc, sc->sc_gnt);
	}
#endif
	ref -= ge->ge_start;
	/* Invalidate the grant reference */
	virtio_membar_sync();
	ptr = (uint32_t *)&ge->ge_table[ref];
	flags = (ge->ge_table[ref].flags & ~(GTF_reading|GTF_writing)) |
	    (ge->ge_table[ref].domid << 16);
	loop = 0;
	while (atomic_cas_uint(ptr, flags, GTF_invalid) != flags) {
		if (loop++ > 10) {
			panic("grant table reference %u is held "
			    "by domain %d: frame %#x flags %#x",
			    ref + ge->ge_start, ge->ge_table[ref].domid,
			    ge->ge_table[ref].frame, ge->ge_table[ref].flags);
		}
#if (defined(__amd64__) || defined(__i386__))
		__asm volatile("pause": : : "memory");
#endif
	}
	ge->ge_table[ref].frame = 0xffffffff;
}

int
xen_bus_dmamap_create(bus_dma_tag_t t, bus_size_t size, int nsegments,
    bus_size_t maxsegsz, bus_size_t boundary, int flags, bus_dmamap_t *dmamp)
{
	struct xen_softc *sc = t->_cookie;
	struct xen_gntmap *gm;
	int i, error;

	if (maxsegsz < PAGE_SIZE)
		return (EINVAL);

	/* Allocate a dma map structure */
	error = bus_dmamap_create(sc->sc_dmat, size, nsegments, maxsegsz,
	    boundary, flags, dmamp);
	if (error)
		return (error);
	/* Allocate an array of grant table pa<->ref maps */
	gm = mallocarray(nsegments, sizeof(struct xen_gntmap), M_DEVBUF,
	    M_ZERO | ((flags & BUS_DMA_NOWAIT) ? M_NOWAIT : M_WAITOK));
	if (gm == NULL) {
		bus_dmamap_destroy(sc->sc_dmat, *dmamp);
		*dmamp = NULL;
		return (ENOMEM);
	}
	/* Wire it to the dma map */
	(*dmamp)->_dm_cookie = gm;
	/* Claim references from the grant table */
	for (i = 0; i < (*dmamp)->_dm_segcnt; i++) {
		if (xen_grant_table_alloc(sc, &gm[i].gm_ref)) {
			xen_bus_dmamap_destroy(t, *dmamp);
			*dmamp = NULL;
			return (ENOBUFS);
		}
	}
	return (0);
}

void
xen_bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t map)
{
	struct xen_softc *sc = t->_cookie;
	struct xen_gntmap *gm;
	int i;

	gm = map->_dm_cookie;
	for (i = 0; i < map->_dm_segcnt; i++) {
		if (gm[i].gm_ref == 0)
			continue;
		xen_grant_table_free(sc, gm[i].gm_ref);
	}
	free(gm, M_DEVBUF, map->_dm_segcnt * sizeof(struct xen_gntmap));
	bus_dmamap_destroy(sc->sc_dmat, map);
}

int
xen_bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags)
{
	struct xen_softc *sc = t->_cookie;
	struct xen_gntmap *gm = map->_dm_cookie;
	int i, domain, error;

	domain = flags >> 16;
	flags &= 0xffff;
	error = bus_dmamap_load(sc->sc_dmat, map, buf, buflen, p, flags);
	if (error)
		return (error);
	for (i = 0; i < map->dm_nsegs; i++) {
		xen_grant_table_enter(sc, gm[i].gm_ref, map->dm_segs[i].ds_addr,
		    domain, flags & BUS_DMA_WRITE ? GTF_readonly : 0);
		gm[i].gm_paddr = map->dm_segs[i].ds_addr;
		map->dm_segs[i].ds_addr = gm[i].gm_ref;
	}
	return (0);
}

int
xen_bus_dmamap_load_mbuf(bus_dma_tag_t t, bus_dmamap_t map, struct mbuf *m0,
    int flags)
{
	struct xen_softc *sc = t->_cookie;
	struct xen_gntmap *gm = map->_dm_cookie;
	int i, domain, error;

	domain = flags >> 16;
	flags &= 0xffff;
	error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m0, flags);
	if (error)
		return (error);
	for (i = 0; i < map->dm_nsegs; i++) {
		xen_grant_table_enter(sc, gm[i].gm_ref, map->dm_segs[i].ds_addr,
		    domain, flags & BUS_DMA_WRITE ? GTF_readonly : 0);
		gm[i].gm_paddr = map->dm_segs[i].ds_addr;
		map->dm_segs[i].ds_addr = gm[i].gm_ref;
	}
	return (0);
}

void
xen_bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
	struct xen_softc *sc = t->_cookie;
	struct xen_gntmap *gm = map->_dm_cookie;
	int i;

	for (i = 0; i < map->dm_nsegs; i++) {
		if (gm[i].gm_paddr == 0)
			continue;
		xen_grant_table_remove(sc, gm[i].gm_ref);
		map->dm_segs[i].ds_addr = gm[i].gm_paddr;
		gm[i].gm_paddr = 0;
	}
	bus_dmamap_unload(sc->sc_dmat, map);
}

void
xen_bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t addr,
    bus_size_t size, int op)
{
	if ((op == (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE)) ||
	    (op == (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE)))
		virtio_membar_sync();
}

static int
xen_attach_print(void *aux, const char *name)
{
	struct xen_attach_args *xa = aux;

	if (name)
		printf("\"%s\" at %s: %s", xa->xa_name, name, xa->xa_node);

	return (UNCONF);
}

int
xen_attach_device(struct xen_softc *sc, struct xen_devlist *xdl,
    const char *name, const char *unit)
{
	struct xen_attach_args xa;
	struct xen_device *xdv;
	unsigned long long res;

	memset(&xa, 0, sizeof(xa));
	xa.xa_dmat = &xen_bus_dma_tag;

	strlcpy(xa.xa_name, name, sizeof(xa.xa_name));
	snprintf(xa.xa_node, sizeof(xa.xa_node), "device/%s/%s", name, unit);

	if (xs_getprop(sc, xa.xa_node, "backend", xa.xa_backend,
	    sizeof(xa.xa_backend))) {
		DPRINTF("%s: failed to identify \"backend\" for "
		    "\"%s\"\n", sc->sc_dev.dv_xname, xa.xa_node);
	}

	if (xs_getnum(sc, xa.xa_node, "backend-id", &res) || res > UINT16_MAX) {
		DPRINTF("%s: invalid \"backend-id\" for \"%s\"\n",
		    sc->sc_dev.dv_xname, xa.xa_node);
	}
	if (res <= UINT16_MAX)
		xa.xa_domid = (uint16_t)res;

	xdv = malloc(sizeof(struct xen_device), M_DEVBUF, M_ZERO | M_NOWAIT);
	if (xdv == NULL)
		return (ENOMEM);

	strlcpy(xdv->dv_unit, unit, sizeof(xdv->dv_unit));
	LIST_INSERT_HEAD(&xdl->dl_devs, xdv, dv_entry);

	xdv->dv_dev = config_found((struct device *)sc, &xa, xen_attach_print);

	return (0);
}

int
xen_probe_devices(struct xen_softc *sc)
{
	struct xen_devlist *xdl;
	struct xs_transaction xst;
	struct iovec *iovp1 = NULL, *iovp2 = NULL;
	int i, j, error, iov1_cnt = 0, iov2_cnt = 0;
	char path[256];

	memset(&xst, 0, sizeof(xst));
	xst.xst_id = 0;
	xst.xst_cookie = sc->sc_xs;

	if ((error = xs_cmd(&xst, XS_LIST, "device", &iovp1, &iov1_cnt)) != 0)
		return (error);

	for (i = 0; i < iov1_cnt; i++) {
		if (strcmp("suspend", (char *)iovp1[i].iov_base) == 0)
			continue;
		snprintf(path, sizeof(path), "device/%s",
		    (char *)iovp1[i].iov_base);
		if ((error = xs_cmd(&xst, XS_LIST, path, &iovp2,
		    &iov2_cnt)) != 0)
			goto out;
		if ((xdl = malloc(sizeof(struct xen_devlist), M_DEVBUF,
		    M_ZERO | M_NOWAIT)) == NULL) {
			error = ENOMEM;
			goto out;
		}
		xdl->dl_xen = sc;
		strlcpy(xdl->dl_node, (const char *)iovp1[i].iov_base,
		    XEN_MAX_NODE_LEN);
		for (j = 0; j < iov2_cnt; j++) {
			error = xen_attach_device(sc, xdl,
			    (const char *)iovp1[i].iov_base,
			    (const char *)iovp2[j].iov_base);
			if (error) {
				printf("%s: failed to attach \"%s/%s\"\n",
				    sc->sc_dev.dv_xname, path,
				    (const char *)iovp2[j].iov_base);
				continue;
			}
		}
		/* Setup a watch for every device subtree */
		if (xs_watch(sc, "device", (char *)iovp1[i].iov_base,
		    &xdl->dl_task, xen_hotplug, xdl))
			printf("%s: failed to setup hotplug watch for \"%s\"\n",
			    sc->sc_dev.dv_xname, (char *)iovp1[i].iov_base);
		SLIST_INSERT_HEAD(&sc->sc_devlists, xdl, dl_entry);
		xs_resfree(&xst, iovp2, iov2_cnt);
		iovp2 = NULL;
		iov2_cnt = 0;
	}

 out:
	if (iovp2)
		xs_resfree(&xst, iovp2, iov2_cnt);
	xs_resfree(&xst, iovp1, iov1_cnt);
	return (error);
}

void
xen_hotplug(void *arg)
{
	struct xen_devlist *xdl = arg;
	struct xen_softc *sc = xdl->dl_xen;
	struct xen_device *xdv, *xvdn;
	struct xs_transaction xst;
	struct iovec *iovp = NULL;
	int error, i, keep, iov_cnt = 0;
	char path[256];
	int8_t *seen;

	memset(&xst, 0, sizeof(xst));
	xst.xst_id = 0;
	xst.xst_cookie = sc->sc_xs;

	snprintf(path, sizeof(path), "device/%s", xdl->dl_node);
	if ((error = xs_cmd(&xst, XS_LIST, path, &iovp, &iov_cnt)) != 0)
		return;

	seen = malloc(iov_cnt, M_TEMP, M_ZERO | M_WAITOK);

	/* Detect all removed and kept devices */
	LIST_FOREACH_SAFE(xdv, &xdl->dl_devs, dv_entry, xvdn) {
		for (i = 0, keep = 0; i < iov_cnt; i++) {
			if (!seen[i] &&
			    !strcmp(xdv->dv_unit, (char *)iovp[i].iov_base)) {
				seen[i]++;
				keep++;
				break;
			}
		}
		if (!keep) {
			DPRINTF("%s: removing \"%s/%s\"\n", sc->sc_dev.dv_xname,
			    xdl->dl_node, xdv->dv_unit);
			LIST_REMOVE(xdv, dv_entry);
			config_detach(xdv->dv_dev, 0);
			free(xdv, M_DEVBUF, sizeof(struct xen_device));
		}
	}

	/* Attach all new devices */
	for (i = 0; i < iov_cnt; i++) {
		if (seen[i])
			continue;
		DPRINTF("%s: attaching \"%s/%s\"\n", sc->sc_dev.dv_xname,
			    xdl->dl_node, (const char *)iovp[i].iov_base);
		error = xen_attach_device(sc, xdl, xdl->dl_node,
		    (const char *)iovp[i].iov_base);
		if (error) {
			printf("%s: failed to attach \"%s/%s\"\n",
			    sc->sc_dev.dv_xname, path,
			    (const char *)iovp[i].iov_base);
			continue;
		}
	}

	free(seen, M_TEMP, iov_cnt);

	xs_resfree(&xst, iovp, iov_cnt);
}

#include <machine/pio.h>

#define	XMI_PORT		0x10
#define XMI_MAGIC		0x49d2
#define XMI_UNPLUG_IDE		0x01
#define XMI_UNPLUG_NIC		0x02
#define XMI_UNPLUG_IDESEC	0x04

void
xen_disable_emulated_devices(struct xen_softc *sc)
{
#if defined(__i386__) || defined(__amd64__)
	ushort unplug = 0;

	if (inw(XMI_PORT) != XMI_MAGIC) {
		printf("%s: failed to disable emulated devices\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	if (sc->sc_unplug & XEN_UNPLUG_IDE)
		unplug |= XMI_UNPLUG_IDE;
	if (sc->sc_unplug & XEN_UNPLUG_IDESEC)
		unplug |= XMI_UNPLUG_IDESEC;
	if (sc->sc_unplug & XEN_UNPLUG_NIC)
		unplug |= XMI_UNPLUG_NIC;
	if (unplug)
		outw(XMI_PORT, unplug);
#endif	/* __i386__ || __amd64__ */
}

void
xen_unplug_emulated(void *xsc, int what)
{
	struct xen_softc *sc = xsc;

	sc->sc_unplug |= what;
}
