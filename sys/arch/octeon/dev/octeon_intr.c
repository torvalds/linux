/*	$OpenBSD: octeon_intr.c,v 1.25 2019/03/17 05:25:06 visa Exp $	*/

/*
 * Copyright (c) 2000-2004 Opsycon AB  (www.opsycon.se)
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

/*
 * Interrupt support for Octeon Processor.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <dev/ofw/openfirm.h>

#include <machine/autoconf.h>
#include <machine/intr.h>

struct intr_handle {
	struct intr_controller	*ih_ic;
	void			*ih_ih;
};

struct intr_controller *octeon_ic;

LIST_HEAD(, intr_controller) octeon_ic_list =
	LIST_HEAD_INITIALIZER(octeon_ic_list);

void
octeon_intr_register(struct intr_controller *ic)
{
	struct intr_controller *tmp;

	/* Assume the first controller to register is the root. */
	if (octeon_ic == NULL)
		octeon_ic = ic;

	ic->ic_phandle = OF_getpropint(ic->ic_node, "phandle", 0);
	if (ic->ic_phandle == 0)
		return;

	LIST_FOREACH(tmp, &octeon_ic_list, ic_list) {
		if (tmp->ic_phandle == ic->ic_phandle) {
			printf("%s: node %d: duplicate phandle %d\n",
			    __func__, ic->ic_node, ic->ic_phandle);
			return;
		}
	}

	LIST_INSERT_HEAD(&octeon_ic_list, ic, ic_list);
}

void
octeon_intr_init(void)
{
	octeon_ic->ic_init();
}

/*
 * Establish an interrupt handler called from the dispatcher.
 * The interrupt function established should return zero if there was nothing
 * to serve (no int) and non-zero when an interrupt was serviced.
 */
void *
octeon_intr_establish(int irq, int level,
    int (*ih_fun)(void *), void *ih_arg, const char *ih_what)
{
	struct intr_controller *ic = octeon_ic;
	struct intr_handle *ih;
	void *handler;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_NOWAIT);
	if (ih == NULL)
		return NULL;

	handler = ic->ic_establish(irq, level, ih_fun, ih_arg, ih_what);
	if (handler == NULL) {
		free(ih, M_DEVBUF, sizeof(*ih));
		return NULL;
	}

	ih->ih_ic = ic;
	ih->ih_ih = handler;

	return ih;
}

void *
octeon_intr_establish_fdt(int node, int level,
    int (*ih_fun)(void *), void *ih_arg, const char *ih_what)
{
	return octeon_intr_establish_fdt_idx(node, 0, level, ih_fun,
	    ih_arg, ih_what);
}

void *
octeon_intr_establish_fdt_idx(int node, int idx, int level,
    int (*ih_fun)(void *), void *ih_arg, const char *ih_what)
{
	struct intr_controller *ic = NULL;
	struct intr_handle *ih;
	void *handler;
	int phandle;

	phandle = OF_getpropint(node, "interrupt-parent", 1);
	if (phandle < 1)
		return NULL;

	LIST_FOREACH(ic, &octeon_ic_list, ic_list) {
		if (ic->ic_phandle == phandle)
			break;
	}
	if (ic == NULL)
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_NOWAIT);
	if (ih == NULL)
		return NULL;

	handler = ic->ic_establish_fdt_idx(ic->ic_cookie, node, idx, level,
	    ih_fun, ih_arg, ih_what);
	if (handler == NULL) {
		free(ih, M_DEVBUF, sizeof(*ih));
		return NULL;
	}

	ih->ih_ic = ic;
	ih->ih_ih = handler;

	return ih;
}

void
octeon_intr_disestablish(void *cookie)
{
	struct intr_handle *ih = cookie;
	struct intr_controller *ic = ih->ih_ic;

	ic->ic_disestablish(ih->ih_ih);
	free(ih, M_DEVBUF, sizeof(*ih));
}

void
octeon_intr_disestablish_fdt(void *cookie)
{
	octeon_intr_disestablish(cookie);
}

void
intr_barrier(void *cookie)
{
	struct intr_handle *ih = cookie;
	struct intr_controller *ic = ih->ih_ic;

	ic->ic_intr_barrier(ih->ih_ih);
}

#ifdef MULTIPROCESSOR
/*
 * Inter-processor interrupt control logic.
 */

int
hw_ipi_intr_establish(int (*func)(void *), u_long cpuid)
{
	return octeon_ic->ic_ipi_establish(func, cpuid);
}

void
hw_ipi_intr_set(u_long cpuid)
{
	octeon_ic->ic_ipi_set(cpuid);
}

void
hw_ipi_intr_clear(u_long cpuid)
{
	octeon_ic->ic_ipi_clear(cpuid);
}
#endif /* MULTIPROCESSOR */
