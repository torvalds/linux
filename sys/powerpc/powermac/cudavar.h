/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2006 Michael Lorenz
 * Copyright (c) 2008 Nathan Whitehorn
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
 * 3. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef	_POWERPC_CUDAVAR_H_
#define	_POWERPC_CUDAVAR_H_

#define CUDA_DEVSTR	"Apple CUDA I/O Controller"
#define	CUDA_MAXPACKETS	10

/* Cuda addresses */
#define CUDA_ADB	0
#define CUDA_PSEUDO	1
#define CUDA_ERROR	2	/* error codes? */
#define CUDA_TIMER	3
#define CUDA_POWER	4
#define CUDA_IIC	5	/* XXX ??? */
#define CUDA_PMU	6
#define CUDA_ADB_QUERY	7

/* Cuda commands */
#define CMD_AUTOPOLL	1
#define CMD_READ_RTC	3
#define CMD_WRITE_RTC	9
#define CMD_POWEROFF	10
#define CMD_RESET	17
#define CMD_IIC		34

/* Cuda state codes */
#define CUDA_NOTREADY	0x1	/* has not been initialized yet */
#define CUDA_IDLE	0x2	/* the bus is currently idle */
#define CUDA_OUT	0x3	/* sending out a command */
#define CUDA_IN		0x4	/* receiving data */
#define CUDA_POLLING	0x5	/* polling - II only */

struct cuda_packet {
	uint8_t len;
	uint8_t type;

	uint8_t data[253];
	STAILQ_ENTRY(cuda_packet) pkt_q;
};

STAILQ_HEAD(cuda_pktq, cuda_packet);

struct cuda_softc {
	device_t	sc_dev;
	int		sc_memrid;
	struct resource	*sc_memr;
	int     	sc_irqrid;
        struct resource *sc_irq;
        void    	*sc_ih;

	struct mtx	sc_mutex;

	device_t	adb_bus;

	int		sc_node;
	volatile int	sc_state;
	int		sc_waiting;
	int		sc_polling;
	int		sc_iic_done;
	volatile int	sc_autopoll;
	uint32_t	sc_rtc;

	int sc_i2c_read_len;

	/* internal buffers */
	uint8_t		sc_in[256];
	uint8_t		sc_out[256];
	int		sc_sent;
	int		sc_out_length;
	int		sc_received;

	struct cuda_packet sc_pkts[CUDA_MAXPACKETS];
	struct cuda_pktq sc_inq;
	struct cuda_pktq sc_outq;
	struct cuda_pktq sc_freeq;
};

#endif /* _POWERPC_CUDAVAR_H_ */
