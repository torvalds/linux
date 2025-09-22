/* $OpenBSD: mp_setperf.c,v 1.6 2015/03/14 03:38:46 jsg Exp $ */
/*
 * Copyright (c) 2007 Gordon Willem Klok <gwk@openbsd.org>
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
#include <sys/sysctl.h>
#include <sys/mutex.h>

#include <machine/intr.h>

struct mutex setperf_mp_mutex = MUTEX_INITIALIZER(IPL_HIGH);

/* underlying setperf mechanism e.g. k8_powernow_setperf() */
void (*ul_setperf)(int);

/* protected by setperf_mp_mutex */
volatile int mp_perflevel;

void mp_setperf(int);

void
mp_setperf(int level)
{
	mtx_enter(&setperf_mp_mutex);
	mp_perflevel = level;

	ul_setperf(mp_perflevel);
	x86_broadcast_ipi(X86_IPI_SETPERF);

	mtx_leave(&setperf_mp_mutex);
}

void
x86_setperf_ipi(struct cpu_info *ci)
{
	ul_setperf(mp_perflevel);
}

void
mp_setperf_init(void)
{
	if (!cpu_setperf)
		return;

	ul_setperf = cpu_setperf;
	cpu_setperf = mp_setperf;
	mtx_init(&setperf_mp_mutex, IPL_HIGH);
}
