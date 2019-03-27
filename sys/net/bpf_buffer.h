/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Seccuris Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract to
 * Seccuris Inc.
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
 *
 * $FreeBSD$
 */

#ifndef _NET_BPF_BUFFER_H_
#define	_NET_BPF_BUFFER_H_

#ifndef _KERNEL
#error "no user-serviceable parts inside"
#endif

void	bpf_buffer_append_bytes(struct bpf_d *d, caddr_t buf, u_int offset,
	    void *src, u_int len);
void	bpf_buffer_append_mbuf(struct bpf_d *d, caddr_t buf, u_int offset,
	    void *src, u_int len);
void	bpf_buffer_free(struct bpf_d *d);
void	bpf_buffer_init(struct bpf_d *d);
int	bpf_buffer_ioctl_sblen(struct bpf_d *d, u_int *i);
int	bpf_buffer_uiomove(struct bpf_d *d, caddr_t buf, u_int len,
	    struct uio *uio);

#endif /* !_NET_BPF_BUFFER_H_ */
