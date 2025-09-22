/*	$OpenBSD: kern_xcall.c,v 1.1 2025/07/13 05:45:21 dlg Exp $ */

/*
 * Copyright (c) 2025 David Gwynne <dlg@openbsd.org>
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
#include <sys/proc.h>
#include <sys/atomic.h>

#include <sys/xcall.h>

#include <machine/intr.h>

#define IPL_XCALL	IPL_SOFTCLOCK

void
cpu_xcall_set(struct xcall *xc, void (*func)(void *), void *arg)
{
	xc->xc_func = func;
	xc->xc_arg = arg;
}

static inline void
cpu_xcall_self(struct xcall *xc)
{
	int s;

	s = splraise(IPL_XCALL);
	xc->xc_func(xc->xc_arg);
	splx(s);
}

#ifdef MULTIPROCESSOR

void
cpu_xcall(struct cpu_info *ci, struct xcall *xc)
{
	struct xcall_cpu *xci = &ci->ci_xcall;
	size_t i;

	if (ci == curcpu()) {
		/* execute the task immediately on the local cpu */
		cpu_xcall_self(xc);
		return;
	}

	for (;;) {
		for (i = 0; i < nitems(xci->xci_xcalls); i++) {
			if (atomic_cas_ptr(&xci->xci_xcalls[i],
			    NULL, xc) != NULL)
				continue;

			cpu_xcall_ipi(ci);
			return;
		}

		CPU_BUSY_CYCLE();
	}
}

struct xcall_sync {
	struct xcall		xcs_xc;
	struct cond		xcs_c;
};

static void
cpu_xcall_done(void *arg)
{
	struct xcall_sync *xcs = arg;
	struct xcall *xc = &xcs->xcs_xc;

	xc->xc_func(xc->xc_arg);

	cond_signal(&xcs->xcs_c);
}

void
cpu_xcall_sync(struct cpu_info *ci, void (*func)(void *), void *arg,
    const char *wmesg)
{
	struct xcall_sync xcs = {
		.xcs_xc = XCALL_INITIALIZER(func, arg),
		.xcs_c = COND_INITIALIZER(),
	};
	struct xcall xc = XCALL_INITIALIZER(cpu_xcall_done, &xcs);

	cpu_xcall(ci, &xc);

	cond_wait(&xcs.xcs_c, wmesg);
}

/*
 * This is the glue between the MD IPI code and the calling of xcalls
 */
static void
cpu_xcall_handler(void *arg)
{
	struct xcall_cpu *xci = arg;
	struct xcall *xc;
	size_t i;

	for (i = 0; i < nitems(xci->xci_xcalls); i++) {
		xc = xci->xci_xcalls[i];
		if (xc != NULL) {
			xci->xci_xcalls[i] = NULL;
			(*xc->xc_func)(xc->xc_arg);
		}
	}
}

void
cpu_xcall_establish(struct cpu_info *ci)
{
	struct xcall_cpu *xci = &ci->ci_xcall;
	size_t i;

	for (i = 0; i < nitems(xci->xci_xcalls); i++)
		xci->xci_xcalls[i] = NULL;

	xci->xci_softintr = softintr_establish(IPL_XCALL | IPL_MPSAFE,
	    cpu_xcall_handler, xci);
}

void
cpu_xcall_dispatch(struct cpu_info *ci)
{
	struct xcall_cpu *xci = &ci->ci_xcall;

	softintr_schedule(xci->xci_softintr);
}

#else /* MULTIPROCESSOR */

/*
 * uniprocessor implementation
 */

void
cpu_xcall(struct cpu_info *ci, struct xcall *xc)
{
	cpu_xcall_self(xc);
}

void
cpu_xcall_sync(struct cpu_info *ci, void (*func)(void *), void *arg,
    const char *wmesg)
{
	int s;

	s = splraise(IPL_XCALL);
	func(arg);
	splx(s);
}
#endif /* MULTIPROCESSOR */
