/*	$OpenBSD: intr.c,v 1.13 2025/04/26 11:10:28 visa Exp $	*/

/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/systm.h>

#include <machine/atomic.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>

/* Dummy implementations. */
void	dummy_exi(struct trapframe *);
void	dummy_hvi(struct trapframe *);
void	*dummy_intr_establish(uint32_t, int, int, struct cpu_info *,
	    int (*)(void *), void *, const char *);
void	dummy_intr_send_ipi(void *);
void	dummy_setipl(int);

/*
 * The function pointers are overridden when the driver for the real
 * interrupt controller attaches.
 */
void	(*_exi)(struct trapframe *) = dummy_exi;
void	(*_hvi)(struct trapframe *) = dummy_hvi;
void	*(*_intr_establish)(uint32_t, int, int, struct cpu_info *,
	    int (*)(void *), void *, const char *) = dummy_intr_establish;
void	(*_intr_send_ipi)(void *) = dummy_intr_send_ipi;
void	(*_setipl)(int) = dummy_setipl;

void
exi_intr(struct trapframe *frame)
{
	(*_exi)(frame);
}

void
hvi_intr(struct trapframe *frame)
{
	(*_hvi)(frame);
}

void *
intr_establish(uint32_t girq, int type, int level, struct cpu_info *ci,
    int (*func)(void *), void *arg, const char *name)
{
	return (*_intr_establish)(girq, type, level, ci, func, arg, name);
}

#define SI_TO_IRQBIT(x) (1 << (x))
uint32_t intr_smask[NIPL];

void
intr_init(void)
{
	int i;

	for (i = IPL_NONE; i <= IPL_HIGH; i++)  {
		intr_smask[i] = 0;
		if (i < IPL_SOFTCLOCK)
			intr_smask[i] |= SI_TO_IRQBIT(SOFTINTR_CLOCK);
		if (i < IPL_SOFTNET)
			intr_smask[i] |= SI_TO_IRQBIT(SOFTINTR_NET);
		if (i < IPL_SOFTTTY)
			intr_smask[i] |= SI_TO_IRQBIT(SOFTINTR_TTY);
	}
}

void
intr_do_pending(int new)
{
	struct cpu_info *ci = curcpu();
	u_long msr;

	msr = intr_disable();

#define DO_SOFTINT(si, ipl) \
	if ((ci->ci_ipending & intr_smask[new]) & SI_TO_IRQBIT(si)) {	\
		ci->ci_ipending &= ~SI_TO_IRQBIT(si);			\
		_setipl(ipl);						\
		intr_restore(msr);					\
		softintr_dispatch(si);					\
		msr = intr_disable();					\
	}

	do {
		DO_SOFTINT(SOFTINTR_TTY, IPL_SOFTTTY);
		DO_SOFTINT(SOFTINTR_NET, IPL_SOFTNET);
		DO_SOFTINT(SOFTINTR_CLOCK, IPL_SOFTCLOCK);
	} while (ci->ci_ipending & intr_smask[new]);

	intr_restore(msr);
}

int
splraise(int new)
{
	struct cpu_info *ci = curcpu();
	int old = ci->ci_cpl;

	if (new > old)
		(*_setipl)(new);
	return old;
}

int
spllower(int new)
{
	struct cpu_info *ci = curcpu();
	int old = ci->ci_cpl;

	if (new < old)
		(*_setipl)(new);
	return old;
}

void
splx(int new)
{
	struct cpu_info *ci = curcpu();

	if (ci->ci_dec_deferred && new < IPL_CLOCK) {
		mtdec(0);
		mtdec(UINT32_MAX);	/* raise DEC exception */
	}

	if (ci->ci_ipending & intr_smask[new])
		intr_do_pending(new);

	if (ci->ci_cpl != new)
		(*_setipl)(new);
}

void
softintr(int si)
{
	curcpu()->ci_ipending |= SI_TO_IRQBIT(si);
}

#ifdef DIAGNOSTIC
void
splassert_check(int wantipl, const char *func)
{
	int oldipl = curcpu()->ci_cpl;

	if (oldipl < wantipl) {
		splassert_fail(wantipl, oldipl, func);
		/*
		 * If the splassert_ctl is set to not panic, raise the ipl
		 * in a feeble attempt to reduce damage.
		 */
		(*_setipl)(wantipl);
	}

	if (wantipl == IPL_NONE && curcpu()->ci_idepth != 0) {
		splassert_fail(-1, curcpu()->ci_idepth, func);
	}
}
#endif

void
dummy_exi(struct trapframe *frame)
{
	panic("Unhandled external interrupt");
}

void
dummy_hvi(struct trapframe *frame)
{
	panic("Unhandled hypervisor virtualization interrupt");
}

void *
dummy_intr_establish(uint32_t girq, int type, int level, struct cpu_info *ci,
	    int (*func)(void *), void *arg, const char *name)
{
	return NULL;
}

void
dummy_setipl(int new)
{
	struct cpu_info *ci = curcpu();
	ci->ci_cpl = new;
}

void
dummy_intr_send_ipi(void *cookie)
{
}

/*
 * FDT interrupt support.
 */

#define MAX_INTERRUPT_CELLS	4

struct fdt_intr_handle {
	struct interrupt_controller *ih_ic;
	void *ih_ih;
};

LIST_HEAD(, interrupt_controller) interrupt_controllers =
	LIST_HEAD_INITIALIZER(interrupt_controllers);

void
interrupt_controller_register(struct interrupt_controller *ic)
{
	ic->ic_cells = OF_getpropint(ic->ic_node, "#interrupt-cells", 0);
	ic->ic_phandle = OF_getpropint(ic->ic_node, "phandle", 0);
	if (ic->ic_phandle == 0)
		return;
	KASSERT(ic->ic_cells <= MAX_INTERRUPT_CELLS);

	LIST_INSERT_HEAD(&interrupt_controllers, ic, ic_list);
}

/*
 * Find the interrupt parent by walking up the tree.
 */
uint32_t
fdt_intr_get_parent(int node)
{
	uint32_t phandle = 0;

	while (node && !phandle) {
		phandle = OF_getpropint(node, "interrupt-parent", 0);
		node = OF_parent(node);
	}

	return phandle;
}

void *
fdt_intr_establish_idx_cpu(int node, int idx, int level, struct cpu_info *ci,
    int (*func)(void *), void *cookie, char *name)
{
	struct interrupt_controller *ic;
	int i, len, ncells, extended = 1;
	uint32_t *cell, *cells, phandle;
	struct fdt_intr_handle *ih;
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
		phandle = fdt_intr_get_parent(node);
		LIST_FOREACH(ic, &interrupt_controllers, ic_list) {
			if (ic->ic_phandle == phandle)
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
fdt_intr_establish_imap(int node, int *reg, int nreg, int level,
    int (*func)(void *), void *cookie, char *name)
{
	return fdt_intr_establish_imap_cpu(node, reg, nreg, level, NULL,
	    func, cookie, name);
}

void *
fdt_intr_establish_imap_cpu(int node, int *reg, int nreg, int level,
    struct cpu_info *ci, int (*func)(void *), void *cookie, char *name)
{
	struct interrupt_controller *ic;
	struct fdt_intr_handle *ih;
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

void
fdt_intr_disestablish(void *cookie)
{
	panic("%s: not implemented", __func__);
}

#ifdef MULTIPROCESSOR

void
intr_send_ipi(struct cpu_info *ci, int reason)
{
	struct fdt_intr_handle *ih = ci->ci_ipi;

	if (ci == curcpu() && reason == IPI_NOP)
		return;

	if (reason != IPI_NOP)
		atomic_setbits_int(&ci->ci_ipi_reason, reason);

	if (ih && ih->ih_ic)
		ih->ih_ic->ic_send_ipi(ih->ih_ih);
}

#endif
