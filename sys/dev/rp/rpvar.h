/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) Comtrol Corporation <support@comtrol.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted prodived that the follwoing conditions
 * are met.
 * 1. Redistributions of source code must retain the above copyright 
 *    notive, this list of conditions and the following disclainer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials prodided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *       This product includes software developed by Comtrol Corporation.
 * 4. The name of Comtrol Corporation may not be used to endorse or 
 *    promote products derived from this software without specific 
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COMTROL CORPORATION ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL COMTROL CORPORATION BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, LIFE OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * rpvar.h --- RocketPort data structure includes for FreeBSD
 */

#define RP_UNIT(x) dv_unit(x)
#define RP_PORT(x) (dev2unit(x) & 0x3f)
#define MAX_RP_PORTS	128


struct rp_port {
	struct tty *		rp_tty; /* cross reference */
	struct callout		rp_timer;

	unsigned char		state;	/* state of dtr */

	int			rp_port;
	int			rp_flags;
	int			rp_unit:2;
	int			rp_aiop:2;
	int			rp_chan:3;
	int			rp_intmask;
	int			rp_imask; /* Input mask */
	int			rp_fifo_lw;
	int			rp_restart;
	int			rp_overflows;
	int			rp_rts_iflow:1;
	int			rp_disable_writes:1;
	int			rp_cts:1;
	int			rp_waiting:1;
	int			rp_xmit_stopped:1;
	CONTROLLER_t *		rp_ctlp;
	CHANNEL_t		rp_channel;
	unsigned char		TxBuf[TXFIFO_SIZE];
	unsigned char		RxBuf[RXFIFO_SIZE];
};

/* Actually not used */
#ifdef notdef
extern struct termios deftermios;
#endif /* notdef */
