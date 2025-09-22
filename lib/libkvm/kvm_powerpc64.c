/*	$OpenBSD: kvm_powerpc64.c,v 1.3 2021/12/01 21:45:19 deraadt Exp $	*/

/*
 * Copyright (c) 2007 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/kcore.h>

#include <unistd.h>
#include <stdlib.h>
#include <nlist.h>
#include <kvm.h>

#include <db.h>

#include "kvm_private.h"

#include <machine/cpu.h>
#include <machine/kcore.h>
#include <machine/pte.h>
#include <machine/vmparam.h>

void
_kvm_freevtop(kvm_t *kd)
{
}

int
_kvm_initvtop(kvm_t *kd)
{
	return (0);
}

/*
 * Translate a kernel virtual address to a physical address by walking
 * the kernel page tables.
 */
int
_kvm_kvatop(kvm_t *kd, u_long va, paddr_t *pa)
{
	if (ISALIVE(kd)) {
		_kvm_err(kd, 0, "vatop called in live kernel!");
		return (0);
	}

	_kvm_err(kd, 0, "%s not yet implemented", __func__);
	*pa = (paddr_t)-1;
	return (0);
}

/*
 * Translate a physical address to a file offset in the crash dump.
 */
off_t
_kvm_pa2off(kvm_t *kd, paddr_t pa)
{
	_kvm_err(kd, 0, "%s not yet implemented", __func__);
	return (0);
}
