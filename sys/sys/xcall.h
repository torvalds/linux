/*	$OpenBSD: xcall.h,v 1.1 2025/07/13 05:45:21 dlg Exp $ */

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

/*
 * CPU crosscall API
 *
 * Work to execute on a CPU is wrapped in a struct xcall, which is
 * like a task or timeout. Each CPU (in an MP kernel) has an array
 * of pointers to xcall structs. The local CPU uses CAS ops to try
 * and swap their xcall onto one of the slots on the remote CPU,
 * and will spin until space becomes available. Once the xcall has
 * been added, an IPI is sent to the remote CPU to kick of processing
 * of the array. The xcall IPI handler defers processing of the
 * xcall array to a low IPL level using a softintr handler.
 *
 * To implement this API on an architecture requires the following:
 *
 * 1. A device has to depend on the xcall attribute to have the
 * xcall code included in the kernel build. e.g., on amd64 the cpu
 * device depends on the xcall attribute, as defined in
 * src/sys/arch/amd64/conf/files.amd64:
 *
 * device cpu: xcall
 *
 * The rest of the changes are only necessary for MULTIPROCESSOR builds:
 *
 * 2. struct cpu_info has to have a `struct xcall_cpu ci_xcall` member.
 *
 * 3. cpu_xcall_establish has to be called against each cpu_info struct.
 *
 * 4. cpu_xcall_ipi has to be provided by machine/intr.h.
 *
 * 5. The MD xcall IPI handler has to call cpu_xcall_dispatch.
 */

#ifndef _SYS_XCALL_H
#define _SYS_XCALL_H

struct xcall {
	void (*xc_func)(void *);
	void		*xc_arg;
};

/* MD code adds this to struct cpu_info as ci_xcall */
struct xcall_cpu {
	struct xcall	*xci_xcalls[4];
	void		*xci_softintr;
};

#ifdef _KERNEL
#define XCALL_INITIALIZER(_f, _a) {				\
	.xc_func = _f,						\
	.xc_arg = _a,						\
}

void	cpu_xcall_set(struct xcall *, void (*)(void *), void *);
void	cpu_xcall(struct cpu_info *, struct xcall *);

void	cpu_xcall_sync(struct cpu_info *, void (*)(void *), void *,
	    const char *);

/* MD cpu setup calls this */
void	cpu_xcall_establish(struct cpu_info *);
/* MD ipi handler calls this */
void	cpu_xcall_dispatch(struct cpu_info *);

#endif /* _KERNEL */

#endif /* _SYS_XCALL_H */
