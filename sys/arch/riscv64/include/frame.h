/*	$OpenBSD: frame.h,v 1.3 2022/02/24 14:19:10 visa Exp $	*/

/*
 * Copyright (c) 2019 Brian Bamsch <bbamsch@google.com>
 * Copyright (c) 2016 Dale Rahn <drahn@dalerahn.com>
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
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

#ifndef _MACHINE_FRAME_H_
#define _MACHINE_FRAME_H_

#ifndef _LOCORE

#include <sys/signal.h>

/*
 * Exception/Trap Stack Frame
 */
#define clockframe trapframe
typedef struct trapframe {
	/* Standard Registers */
	register_t tf_ra;
	register_t tf_sp;
	register_t tf_gp;
	register_t tf_tp;
	register_t tf_t[7];
	register_t tf_s[12];
	register_t tf_a[8];
	/* Supervisor Trap CSRs */
	register_t tf_sepc;
	register_t tf_sstatus;
	register_t tf_stval;
	register_t tf_scause;
	register_t tf_pad;
} trapframe_t;

/*
 * pushed on stack for signal delivery
 */
struct sigframe {
	int sf_signum;
	struct sigcontext sf_sc;
	siginfo_t sf_si;
};

/*
 * System stack frames.
 */

/*
 * Stack frame inside cpu_switch()
 */
struct switchframe {
	register_t sf_s[12];
	register_t sf_ra;
	register_t sf_pad;
};

struct callframe {
	struct callframe *f_frame;
	register_t f_ra;
};

#endif /* !_LOCORE */

#endif /* !_MACHINE_FRAME_H_ */
