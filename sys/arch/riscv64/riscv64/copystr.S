/*	$OpenBSD: copystr.S,v 1.5 2023/01/06 19:10:18 miod Exp $	*/

/*
 * Copyright (c) 2015 Dale Rahn <drahn@dalerahn.com>
 * Copyright (c) 2014 Patrick Wildt <patrick@blueri.se>
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

#include "assym.h"
#include <machine/asm.h>
#include <machine/param.h>
#include <machine/vmparam.h>
#include <machine/riscvreg.h>
#include <sys/errno.h>

	.text

/*
 * a0 - user space address
 * a1 - kernel space address
 * a2 - maxlens
 * a3 - lencopied
 *
 * Copy string from user space to kernel space
 */
ENTRY(copyinstr)
	RETGUARD_SETUP(copyiostr, a6)
	beqz	a2, .Lcopyiostrtoolong_early
	la	a4, .Lcopyiostrfault_user
	SWAP_FAULT_HANDLER(a4, t0, t1)
	ENTER_USER_ACCESS(t0)

	mv	a5, x0

	li	t1, VM_MAXUSER_ADDRESS
1:	bgtu	a0, t1, .Lcopyiostrfault_user
	lb	t0, 0(a0)
	addi	a0, a0, 1
	sb	t0, 0(a1)
	addi	a1, a1, 1
	addi	a2, a2, -1
	addi	a5, a5, 1
	beqz	t0, .Lcopyiostrsuccess_user
	bnez	a2, 1b

.Lcopyiostrtoolong_user:
	EXIT_USER_ACCESS(t0)
.Lcopyiostrtoolong:
	SET_FAULT_HANDLER(a4, t0)
.Lcopyiostrtoolong_early:
	li	a0, ENAMETOOLONG
	j	.Lcopyiostrcleanup

.Lcopyiostrfault_user:
	EXIT_USER_ACCESS(t0)
.Lcopyiostrfault:
	SET_FAULT_HANDLER(a4, t0)
	li	a0, EFAULT
	j	.Lcopyiostrcleanup

.Lcopyiostrsuccess_user:
	EXIT_USER_ACCESS(t0)
.Lcopyiostrsuccess:
	SET_FAULT_HANDLER(a4, t0)
	mv	a0, x0

.Lcopyiostrcleanup:
	beqz	a3, 2f
	sd	a5, 0(a3)
2:
	RETGUARD_CHECK(copyiostr, a6)
	ret

END(copyinstr)

/*
 * a0 - kernel space address
 * a1 - user space address
 * a2 - maxlens
 * a3 - lencopied
 *
 * Copy string from kernel space to user space
 */
ENTRY(copyoutstr)
	RETGUARD_SETUP(copyiostr, a6)
	beqz	a2, .Lcopyiostrtoolong_early
	la	a4, .Lcopyiostrfault_user
	SWAP_FAULT_HANDLER(a4, t0, t1)
	ENTER_USER_ACCESS(t0)

	mv	a5, x0

	li	t1, VM_MAXUSER_ADDRESS
1:	bgtu	a1, t1, .Lcopyiostrfault_user
	lb	t0, 0(a0)
	addi	a0, a0, 1
	sb	t0, 0(a1)
	addi	a1, a1, 1
	addi	a2, a2, -1
	addi	a5, a5, 1
	beqz	t0, .Lcopyiostrsuccess_user
	bnez	a2, 1b

	j	.Lcopyiostrtoolong_user
END(copyoutstr)
