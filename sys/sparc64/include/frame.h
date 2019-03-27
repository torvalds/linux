/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
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
 * $FreeBSD$
 */

#ifndef	_MACHINE_FRAME_H_
#define	_MACHINE_FRAME_H_

#define	RW_SHIFT	7
#define	SPOFF		2047
#define	BIAS		SPOFF		/* XXX - open/netbsd compat */

#ifndef LOCORE

/*
 * NOTE: keep this structure in sync with struct reg and struct mcontext.
 */
struct trapframe {
	uint64_t tf_global[8];
	uint64_t tf_out[8];
	uint64_t tf_fprs;
	uint64_t tf_fsr;
	uint64_t tf_gsr;
	uint64_t tf_level;
	uint64_t tf_pil;
	uint64_t tf_sfar;
	uint64_t tf_sfsr;
	uint64_t tf_tar;
	uint64_t tf_tnpc;
	uint64_t tf_tpc;
	uint64_t tf_tstate;
	uint64_t tf_type;
	uint64_t tf_y;
	uint64_t tf_wstate;
	uint64_t tf_pad[2];
};
#define	tf_sp	tf_out[6]

#define	TF_DONE(tf) do { \
	tf->tf_tpc = tf->tf_tnpc; \
	tf->tf_tnpc += 4; \
} while (0)

struct frame {
	u_long	fr_local[8];
	u_long	fr_in[8];
	u_long	fr_pad[8];
};
#define	fr_arg	fr_in
#define	fr_fp	fr_in[6]
#define	fr_pc	fr_in[7]

#define	v9next_frame(fp)	((struct frame *)(fp->fr_fp + BIAS))

/*
 * Frame used for pcb_rw.
 */
struct rwindow {
	u_long	rw_local[8];
	u_long	rw_in[8];
};

struct thread;

int	rwindow_save(struct thread *td);
int	rwindow_load(struct thread *td, struct trapframe *tf, int n);

#endif /* !LOCORE */

#endif /* !_MACHINE_FRAME_H_ */
