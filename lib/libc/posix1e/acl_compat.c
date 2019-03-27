/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Edward Tomasz Napierała <trasz@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/acl.h>

int __oldacl_get_perm_np(acl_permset_t, oldacl_perm_t);
int __oldacl_add_perm(acl_permset_t, oldacl_perm_t);
int __oldacl_delete_perm(acl_permset_t, oldacl_perm_t);

/*
 * Compatibility wrappers for applications compiled against libc from before
 * NFSv4 ACLs were added.
 */
int
__oldacl_get_perm_np(acl_permset_t permset_d, oldacl_perm_t perm)
{

	return (acl_get_perm_np(permset_d, perm));
}

int
__oldacl_add_perm(acl_permset_t permset_d, oldacl_perm_t perm)
{

	return (acl_add_perm(permset_d, perm));
}

int
__oldacl_delete_perm(acl_permset_t permset_d, oldacl_perm_t perm)
{

	return (acl_delete_perm(permset_d, perm));
}

__sym_compat(acl_get_perm_np, __oldacl_get_perm_np, FBSD_1.0);
__sym_compat(acl_add_perm, __oldacl_add_perm, FBSD_1.0);
__sym_compat(acl_delete_perm, __oldacl_delete_perm, FBSD_1.0);
