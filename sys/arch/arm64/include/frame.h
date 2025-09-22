/* $OpenBSD: frame.h,v 1.3 2018/06/30 15:23:37 deraadt Exp $ */
/*
 * Copyright (c) 2016 Dale Rahn <drahn@dalerahn.com>
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

#ifndef _MACHINE_FRAME_H_
#define _MACHINE_FRAME_H_

#ifndef _LOCORE

#include <sys/signal.h>


/*
 * Exception/Trap Stack Frame
 */
#define clockframe trapframe
typedef struct trapframe {
	register_t tf_sp;
	register_t tf_lr;
	register_t tf_elr;
	register_t tf_spsr;
	register_t tf_x[30];
} trapframe_t;

/*
 * pushed on stack for signal delivery
 */
struct sigframe {
	int	sf_signum;
	struct	sigcontext sf_sc;
	siginfo_t sf_si;
};

/*
 * System stack frames.
 */

/*
 * Stack frame inside cpu_switch()
 */

struct switchframe {
	register_t sf_x19;
	register_t sf_x20;
	register_t sf_x21;
	register_t sf_x22;
	register_t sf_x23;
	register_t sf_x24;
	register_t sf_x25;
	register_t sf_x26;
	register_t sf_x27;
	register_t sf_x28;
	register_t sf_x29;
	register_t sf_lr;
};

struct callframe {
	struct callframe	*f_frame;
	register_t		 f_lr;
};

#endif /* !_LOCORE */

#endif /* _MACHINE_FRAME_H_ */
