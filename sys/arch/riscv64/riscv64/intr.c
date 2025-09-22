/*	$OpenBSD: intr.c,v 1.16 2025/09/14 15:09:36 kettenis Exp $	*/

/*
 * Copyright (c) 2011 Dale Rahn <drahn@openbsd.org>
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
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/sbi.h>

#include <dev/ofw/openfirm.h>

int riscv_intr_get_parent(int);
uint32_t riscv_intr_map_msi(int, uint64_t *);

void *riscv_intr_prereg_establish_fdt(void *, int *, int, struct cpu_info *,
    int (*)(void *), void *, char *);
void riscv_intr_prereg_disestablish_fdt(void *);
void riscv_intr_prereg_barrier_fdt(void *);

int riscv_dflt_splraise(int);
int riscv_dflt_spllower(int);
void riscv_dflt_splx(int);
void riscv_dflt_setipl(int);

void riscv_dflt_intr(void *);
void riscv_cpu_intr(void *);

#define SI_TO_IRQBIT(x) (1 << (x))
uint32_t riscv_smask[NIPL];

struct riscv_intr_func riscv_intr_func = {
	riscv_dflt_splraise,
	riscv_dflt_spllower,
	riscv_dflt_splx,
	riscv_dflt_setipl
};

void
riscv_dflt_intr(void *frame)
{
	panic("%s", __func__);
}

void (*riscv_intr_dispatch)(void *) = riscv_dflt_intr;

void
riscv_cpu_intr(void *frame)
{
	struct cpu_info	*ci = curcpu();

	uvmexp.intrs++;
	ci->ci_idepth++;
	(*riscv_intr_dispatch)(frame);
	ci->ci_idepth--;
}

/*
 * Find the interrupt parent by walking up the tree.
 */
int
riscv_intr_get_parent(int node)
{
	uint32_t phandle;

	while (node) {
		phandle = OF_getpropint(node, "interrupt-parent", 0);
		if (phandle)
			return OF_getnodebyphandle(phandle);
		node = OF_parent(node);
		if (OF_getpropbool(node, "interrupt-controller"))
			return node;
	}

	return 0;
}

uint32_t
riscv_intr_map_msi(int node, uint64_t *data)
{
	uint64_t msi_base;
	uint32_t phandle = 0;
	uint32_t *cell;
	uint32_t *map;
	uint32_t mask, rid_base, rid;
	int i, len, length, mcells, ncells;

	len = OF_getproplen(node, "msi-map");
	if (len <= 0) {
		while (node && !phandle) {
			phandle = OF_getpropint(node, "msi-parent", 0);
			node = OF_parent(node);
		}

		return phandle;
	}

	map = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "msi-map", map, len);

	mask = OF_getpropint(node, "msi-map-mask", 0xffff);
	rid = *data & mask;

	cell = map;
	ncells = len / sizeof(uint32_t);
	while (ncells > 1) {
		node = OF_getnodebyphandle(cell[1]);
		if (node == 0)
			goto out;

		/*
		 * Some device trees (e.g. those for the Rockchip
		 * RK3399 boards) are missing a #msi-cells property.
		 * Assume the msi-specifier uses a single cell in that
		 * case.
		 */
		mcells = OF_getpropint(node, "#msi-cells", 1);
		if (ncells < mcells + 3)
			goto out;

		rid_base = cell[0];
		length = cell[2 + mcells];
		msi_base = cell[2];
		for (i = 1; i < mcells; i++) {
			msi_base <<= 32;
			msi_base |= cell[2 + i];
		}
		if (rid >= rid_base && rid < rid_base + length) {
			*data = msi_base + (rid - rid_base);
			phandle = cell[1];
			break;
		}

		cell += (3 + mcells);
		ncells -= (3 + mcells);
	}

out:
	free(map, M_TEMP, len);
	return phandle;
}

/*
 * Interrupt pre-registration.
 *
 * To allow device drivers to establish interrupt handlers before all
 * relevant interrupt controllers have been attached, we support
 * pre-registration of interrupt handlers.  For each node in the
 * device tree that has an "interrupt-controller" property, we
 * register a dummy interrupt controller that simply stashes away all
 * relevant details of the interrupt handler being established.
 * Later, when the real interrupt controller registers itself, we
 * establish those interrupt handlers based on that information.
 */

#define MAX_INTERRUPT_CELLS	4

struct intr_prereg {
	LIST_ENTRY(intr_prereg) ip_list;
	uint32_t ip_phandle;
	uint32_t ip_cell[MAX_INTERRUPT_CELLS];

	int ip_level;
	struct cpu_info *ip_ci;
	int (*ip_func)(void *);
	void *ip_arg;
	char *ip_name;

	struct interrupt_controller *ip_ic;
	void *ip_ih;
};

LIST_HEAD(, intr_prereg) prereg_interrupts =
	LIST_HEAD_INITIALIZER(prereg_interrupts);

void *
riscv_intr_prereg_establish_fdt(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct interrupt_controller *ic = cookie;
	struct intr_prereg *ip;
	int i;

	ip = malloc(sizeof(struct intr_prereg), M_DEVBUF, M_ZERO | M_WAITOK);
	ip->ip_phandle = ic->ic_phandle;
	for (i = 0; i < ic->ic_cells; i++)
		ip->ip_cell[i] = cell[i];
	ip->ip_level = level;
	ip->ip_ci = ci;
	ip->ip_func = func;
	ip->ip_arg = arg;
	ip->ip_name = name;
	LIST_INSERT_HEAD(&prereg_interrupts, ip, ip_list);

	return ip;
}

void
riscv_intr_prereg_disestablish_fdt(void *cookie)
{
	struct intr_prereg *ip = cookie;
	struct interrupt_controller *ic = ip->ip_ic;

	if (ic != NULL && ip->ip_ih != NULL)
		ic->ic_disestablish(ip->ip_ih);

	if (ic == NULL)
		LIST_REMOVE(ip, ip_list);

	free(ip, M_DEVBUF, sizeof(*ip));
}

void
riscv_intr_prereg_barrier_fdt(void *cookie)
{
	struct intr_prereg *ip = cookie;
	struct interrupt_controller *ic = ip->ip_ic;

	if (ip->ip_ic != NULL && ip->ip_ih != NULL)
		ic->ic_barrier(ip->ip_ih);
}

void
riscv_intr_init_fdt_recurse(int node)
{
	struct interrupt_controller *ic;

	if (OF_getproplen(node, "interrupt-controller") >= 0) {
		ic = malloc(sizeof(struct interrupt_controller),
		    M_DEVBUF, M_ZERO | M_WAITOK);
		ic->ic_node = node;
		ic->ic_cookie = ic;
		ic->ic_establish = riscv_intr_prereg_establish_fdt;
		ic->ic_disestablish = riscv_intr_prereg_disestablish_fdt;
		ic->ic_barrier = riscv_intr_prereg_barrier_fdt;
		riscv_intr_register_fdt(ic);
	}

	for (node = OF_child(node); node; node = OF_peer(node))
		riscv_intr_init_fdt_recurse(node);
}

void
riscv_intr_init_fdt(void)
{
	int node = OF_peer(0);

	if (node)
		riscv_intr_init_fdt_recurse(node);
}

LIST_HEAD(, interrupt_controller) interrupt_controllers =
	LIST_HEAD_INITIALIZER(interrupt_controllers);

void
riscv_intr_register_fdt(struct interrupt_controller *ic)
{
	struct intr_prereg *ip, *tip;

	ic->ic_cells = OF_getpropint(ic->ic_node, "#interrupt-cells", 0);
	ic->ic_phandle = OF_getpropint(ic->ic_node, "phandle", 0);
	KASSERT(ic->ic_cells <= MAX_INTERRUPT_CELLS);

	LIST_INSERT_HEAD(&interrupt_controllers, ic, ic_list);

	/* Establish pre-registered interrupt handlers. */
	LIST_FOREACH_SAFE(ip, &prereg_interrupts, ip_list, tip) {
		if (ip->ip_phandle != ic->ic_phandle)
			continue;

		ip->ip_ic = ic;
		if (ic->ic_establish)/* riscv_cpu_intc sets this to NULL */
		{
			ip->ip_ih = ic->ic_establish(ic->ic_cookie, ip->ip_cell,
			    ip->ip_level, ip->ip_ci, ip->ip_func, ip->ip_arg, ip->ip_name);
			if (ip->ip_ih == NULL)
				printf("can't establish interrupt %s\n", ip->ip_name);
		}

		LIST_REMOVE(ip, ip_list);
	}
}

void *
riscv_intr_establish_fdt(int node, int level, int (*func)(void *),
    void *cookie, char *name)
{
	return riscv_intr_establish_fdt_idx(node, 0, level, func, cookie, name);
}

void *
riscv_intr_establish_fdt_cpu(int node, int level, struct cpu_info *ci,
    int (*func)(void *), void *cookie, char *name)
{
	return riscv_intr_establish_fdt_idx_cpu(node, 0, level, ci, func,
	    cookie, name);
}

void *
riscv_intr_establish_fdt_idx(int node, int idx, int level, int (*func)(void *),
    void *cookie, char *name)
{
	return riscv_intr_establish_fdt_idx_cpu(node, idx, level, NULL, func,
	    cookie, name);
}

void *
riscv_intr_establish_fdt_idx_cpu(int node, int idx, int level,
    struct cpu_info *ci, int (*func)(void *), void *cookie, char *name)
{
	struct interrupt_controller *ic;
	int i, len, ncells, parent;
	int extended = 1;
	uint32_t *cell, *cells, phandle;
	struct machine_intr_handle *ih;
	void *val = NULL;

	len = OF_getproplen(node, "interrupts-extended");
	if (len <= 0) {
		len = OF_getproplen(node, "interrupts");
		extended = 0;
	}
	if (len <= 0 || (len % sizeof(uint32_t) != 0))
		return NULL;

	/* Old style. */
	if (!extended) {
		parent = riscv_intr_get_parent(node);
		LIST_FOREACH(ic, &interrupt_controllers, ic_list) {
			if (ic->ic_node == parent)
				break;
		}

		if (ic == NULL)
			return NULL;
	}

	cell = cells = malloc(len, M_TEMP, M_WAITOK);
	if (extended)
		OF_getpropintarray(node, "interrupts-extended", cells, len);
	else
		OF_getpropintarray(node, "interrupts", cells, len);
	ncells = len / sizeof(uint32_t);

	for (i = 0; i <= idx && ncells > 0; i++) {
		if (extended) {
			phandle = cell[0];

			/* Handle "empty" phandle reference. */
			if (phandle == 0) {
				cell++;
				ncells--;
				continue;
			}

			LIST_FOREACH(ic, &interrupt_controllers, ic_list) {
				if (ic->ic_phandle == phandle)
					break;
			}

			if (ic == NULL)
				break;

			cell++;
			ncells--;
		}

		if (i == idx && ncells >= ic->ic_cells && ic->ic_establish) {
			val = ic->ic_establish(ic->ic_cookie, cell, level,
			    ci, func, cookie, name);
			break;
		}

		cell += ic->ic_cells;
		ncells -= ic->ic_cells;
	}

	free(cells, M_TEMP, len);

	if (val == NULL)
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_ic = ic;
	ih->ih_ih = val;

	return ih;
}

void *
riscv_intr_establish_fdt_imap_cpu(int node, int *reg, int nreg, int level,
    struct cpu_info *ci, int (*func)(void *), void *cookie, char *name)
{
	struct interrupt_controller *ic;
	struct machine_intr_handle *ih;
	uint32_t *cell;
	uint32_t map_mask[4], *map;
	int len, acells, ncells;
	void *val = NULL;

	if (nreg != sizeof(map_mask))
		return NULL;

	if (OF_getpropintarray(node, "interrupt-map-mask", map_mask,
	    sizeof(map_mask)) != sizeof(map_mask))
		return NULL;

	len = OF_getproplen(node, "interrupt-map");
	if (len <= 0)
		return NULL;

	map = malloc(len, M_DEVBUF, M_WAITOK);
	OF_getpropintarray(node, "interrupt-map", map, len);

	cell = map;
	ncells = len / sizeof(uint32_t);
	while (ncells > 5) {
		LIST_FOREACH(ic, &interrupt_controllers, ic_list) {
			if (ic->ic_phandle == cell[4])
				break;
		}

		if (ic == NULL)
			break;

		acells = OF_getpropint(ic->ic_node, "#address-cells", 0);
		if (ncells >= (5 + acells + ic->ic_cells) &&
		    (reg[0] & map_mask[0]) == cell[0] &&
		    (reg[1] & map_mask[1]) == cell[1] &&
		    (reg[2] & map_mask[2]) == cell[2] &&
		    (reg[3] & map_mask[3]) == cell[3] &&
		    ic->ic_establish) {
			val = ic->ic_establish(ic->ic_cookie, &cell[5 + acells],
			    level, ci, func, cookie, name);
			break;
		}

		cell += (5 + acells + ic->ic_cells);
		ncells -= (5 + acells + ic->ic_cells);
	}

	if (val == NULL) {
		free(map, M_DEVBUF, len);
		return NULL;
	}

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_ic = ic;
	ih->ih_ih = val;

	free(map, M_DEVBUF, len);
	return ih;
}

void *
riscv_intr_establish_fdt_msi_cpu(int node, uint64_t *addr, uint64_t *data,
    int level, struct cpu_info *ci, int (*func)(void *), void *cookie,
    char *name)
{
	struct interrupt_controller *ic;
	struct machine_intr_handle *ih;
	uint32_t phandle;
	void *val = NULL;

	phandle = riscv_intr_map_msi(node, data);
	LIST_FOREACH(ic, &interrupt_controllers, ic_list) {
		if (ic->ic_phandle == phandle)
			break;
	}

	if (ic == NULL || ic->ic_establish_msi == NULL)
		return NULL;

	val = ic->ic_establish_msi(ic->ic_cookie, addr, data,
	    level, ci, func, cookie, name);
	if (val == NULL)
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_ic = ic;
	ih->ih_ih = val;

	return ih;
}

void
riscv_intr_disestablish_fdt(void *cookie)
{
	struct machine_intr_handle *ih = cookie;
	struct interrupt_controller *ic = ih->ih_ic;

	ic->ic_disestablish(ih->ih_ih);
	free(ih, M_DEVBUF, sizeof(*ih));
}

void
riscv_intr_enable(void *cookie)
{
	struct machine_intr_handle *ih = cookie;
	struct interrupt_controller *ic = ih->ih_ic;

	KASSERT(ic->ic_enable != NULL);
	ic->ic_enable(ih->ih_ih);
}

void
riscv_intr_disable(void *cookie)
{
	struct machine_intr_handle *ih = cookie;
	struct interrupt_controller *ic = ih->ih_ic;

	KASSERT(ic->ic_disable != NULL);
	ic->ic_disable(ih->ih_ih);
}

void
riscv_intr_route(void *cookie, int enable, struct cpu_info *ci)
{
	struct machine_intr_handle *ih = cookie;
	struct interrupt_controller *ic = ih->ih_ic;

	if (ic->ic_route)
		ic->ic_route(ih->ih_ih, enable, ci);
}

void
riscv_intr_cpu_enable(void)
{
	struct interrupt_controller *ic;

	LIST_FOREACH(ic, &interrupt_controllers, ic_list)
		if (ic->ic_cpu_enable)
			ic->ic_cpu_enable();
}

int
riscv_dflt_splraise(int newcpl)
{
	struct cpu_info *ci = curcpu();
	int oldcpl;

	oldcpl = ci->ci_cpl;

	if (newcpl < oldcpl)
		newcpl = oldcpl;

	ci->ci_cpl = newcpl;

	return oldcpl;
}

int
riscv_dflt_spllower(int newcpl)
{
	struct cpu_info *ci = curcpu();
	int oldcpl;

	oldcpl = ci->ci_cpl;

	splx(newcpl);

	return oldcpl;
}

void
riscv_dflt_splx(int newcpl)
{
	struct cpu_info *ci = curcpu();

	if (ci->ci_ipending & riscv_smask[newcpl])
		riscv_do_pending_intr(newcpl);
	ci->ci_cpl = newcpl;
}

void
riscv_dflt_setipl(int newcpl)
{
	struct cpu_info *ci = curcpu();

	ci->ci_cpl = newcpl;
}

void
riscv_do_pending_intr(int pcpl)
{
	struct cpu_info *ci = curcpu();
	u_long sie;

	sie = intr_disable();

#define DO_SOFTINT(si, ipl) \
	if ((ci->ci_ipending & riscv_smask[pcpl]) &	\
	    SI_TO_IRQBIT(si)) {				\
		ci->ci_ipending &= ~SI_TO_IRQBIT(si);	\
		riscv_intr_func.setipl(ipl);		\
		intr_restore(sie);			\
		softintr_dispatch(si);			\
		sie = intr_disable();			\
	}

	do {
		DO_SOFTINT(SOFTINTR_TTY, IPL_SOFTTTY);
		DO_SOFTINT(SOFTINTR_NET, IPL_SOFTNET);
		DO_SOFTINT(SOFTINTR_CLOCK, IPL_SOFTCLOCK);
	} while (ci->ci_ipending & riscv_smask[pcpl]);

	/* Don't use splx... we are here already! */
	riscv_intr_func.setipl(pcpl);
	intr_restore(sie);
}

void
riscv_set_intr_func(int (*raise)(int), int (*lower)(int), void (*x)(int),
    void (*setipl)(int))
{
	riscv_intr_func.raise		= raise;
	riscv_intr_func.lower		= lower;
	riscv_intr_func.x		= x;
	riscv_intr_func.setipl		= setipl;
}

void
riscv_set_intr_handler(void (*intr_handle)(void *))
{
	riscv_intr_dispatch		= intr_handle;
}

void
riscv_init_smask(void)
{
	static int inited = 0;
	int i;

	if (inited)
		return;
	inited = 1;

	for (i = IPL_NONE; i <= IPL_HIGH; i++)  {
		riscv_smask[i] = 0;
		if (i < IPL_SOFTCLOCK)
			riscv_smask[i] |= SI_TO_IRQBIT(SOFTINTR_CLOCK);
		if (i < IPL_SOFTNET)
			riscv_smask[i] |= SI_TO_IRQBIT(SOFTINTR_NET);
		if (i < IPL_SOFTTTY)
			riscv_smask[i] |= SI_TO_IRQBIT(SOFTINTR_TTY);
	}
}

/* provide functions for asm */
#undef splraise
#undef spllower
#undef splx

int
splraise(int ipl)
{
	return riscv_intr_func.raise(ipl);
}

int _spllower(int ipl); /* XXX - called from asm? */
int
_spllower(int ipl)
{
	return riscv_intr_func.lower(ipl);
}
int
spllower(int ipl)
{
	return riscv_intr_func.lower(ipl);
}

void
splx(int ipl)
{
	riscv_intr_func.x(ipl);
}

void
softintr(int si)
{
	curcpu()->ci_ipending |= SI_TO_IRQBIT(si);
}

#ifdef DIAGNOSTIC
void
riscv_splassert_check(int wantipl, const char *func)
{
	int oldipl = curcpu()->ci_cpl;

	if (oldipl < wantipl) {
		splassert_fail(wantipl, oldipl, func);
		/*
		 * If the splassert_ctl is set to not panic, raise the ipl
		 * in a feeble attempt to reduce damage.
		 */
		riscv_intr_func.setipl(wantipl);
	}

	if (wantipl == IPL_NONE && curcpu()->ci_idepth != 0) {
		splassert_fail(-1, curcpu()->ci_idepth, func);
	}
}
#endif

void
intr_barrier(void *cookie)
{
	struct machine_intr_handle *ih = cookie;
	struct interrupt_controller *ic = ih->ih_ic;

	ic->ic_barrier(ih->ih_ih);
}

/*
 * IPI implementation
 */

#ifdef MULTIPROCESSOR

void
intr_send_ipi(struct cpu_info *ci, int reason)
{
	unsigned long hart_mask;

	if (ci == curcpu() && reason == IPI_NOP)
		return;

	if (reason != IPI_NOP)
		atomic_setbits_int(&ci->ci_ipi_reason, reason);

	hart_mask = (1UL << ci->ci_hartid);
	sbi_send_ipi(&hart_mask);
}

int
ipi_intr(void *frame)
{
	struct cpu_info *ci = curcpu();
	int pending;

	csr_clear(sip, SIP_SSIP);
	pending = atomic_swap_uint(&ci->ci_ipi_reason, IPI_NOP);

	if (pending & IPI_DDB)
		db_enter();

	return 1;
}

#endif
