/*-
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef __AMD64_INCLUDE_EFI_H_
#define __AMD64_INCLUDE_EFI_H_

/*
 * XXX: from gcc 6.2 manual:
 * Note, the ms_abi attribute for Microsoft Windows 64-bit targets
 * currently requires the -maccumulate-outgoing-args option.
 *
 * Avoid EFIABI_ATTR declarations for compilers that don't support it.
 * GCC support began in version 4.4.
 */
#if defined(__clang__) || defined(__GNUC__) && \
    (__GNUC__ > 4 || __GNUC__ == 4 && __GNUC_MINOR__ >= 4)
#define	EFIABI_ATTR	__attribute__((ms_abi))
#endif

#ifdef _KERNEL
#include <isa/rtc.h>

#define	EFI_TIME_LOCK()		mtx_lock(&atrtc_time_lock)
#define	EFI_TIME_UNLOCK()	mtx_unlock(&atrtc_time_lock)
#define	EFI_TIME_OWNED()	mtx_assert(&atrtc_time_lock, MA_OWNED)

#define	EFI_RT_HANDLE_FAULTS_DEFAULT	1
#endif

struct efirt_callinfo {
	const char	*ec_name;
	register_t	ec_efi_status;
	register_t	ec_fptr;
	register_t	ec_argcnt;
	register_t	ec_arg1;
	register_t	ec_arg2;
	register_t	ec_arg3;
	register_t	ec_arg4;
	register_t	ec_arg5;
	register_t	ec_rbx;
	register_t	ec_rsp;
	register_t	ec_rbp;
	register_t	ec_r12;
	register_t	ec_r13;
	register_t	ec_r14;
	register_t	ec_r15;
};

#endif /* __AMD64_INCLUDE_EFI_H_ */
