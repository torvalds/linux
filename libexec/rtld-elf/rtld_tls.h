/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Doug Rabson
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
 *
 *	$FreeBSD$
 */

/*
 * Semi-public interface from thread libraries to rtld for managing
 * TLS.
 */

#ifndef _RTLD_TLS_H_
#define	_RTLD_TLS_H_

/*
 * Allocate a TLS block for a new thread. The memory allocated will
 * include 'tcbsize' bytes aligned to a 'tcbalign' boundary (in bytes)
 * for the thread library's private purposes. The location of the TCB
 * block is returned by this function. For architectures using
 * 'Variant I' TLS, the thread local storage follows the TCB, and for
 * 'Variant II', the thread local storage precedes it. For
 * architectures using the 'Variant II' model (e.g. i386, amd64,
 * sparc64), the TCB must begin with two pointer fields which are used
 * by rtld for its TLS implementation. For the 'Variant I' model, the
 * TCB must begin with a single pointer field for rtld's
 * implementation.
 *
 * If the value of 'oldtls' is non-NULL, the new TLS block will be
 * initialised using the values contained in 'oldtls' and 'oldtls'
 * will be freed. This is typically used when initialising a thread
 * library to migrate from using the initial bootstrap TLS block
 * created by rtld to one which contains suitable thread library
 * private data.
 *
 * The value returned from this function is suitable for installing
 * directly into the thread pointer register.
 */
void *_rtld_allocate_tls(void* oldtls, size_t tcbsize, size_t tcbalign)
    __exported;

/*
 * Free a TLS block allocated using _rtld_allocate_tls(). The tcbsize
 * and tcbalign parameters must be the same as those used to allocate
 * the block.
 */
void _rtld_free_tls(void *tcb, size_t tcbsize, size_t tcbalign) __exported;

#endif
