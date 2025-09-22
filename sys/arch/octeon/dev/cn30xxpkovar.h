/*	$OpenBSD: cn30xxpkovar.h,v 1.7 2024/05/20 23:13:33 jsg Exp $	*/

/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Cavium Networks OCTEON CN30XX Hardware Reference Manual
 * CN30XX-HM-1.0
 * 8.9 PKO Registers
 */

#ifndef _CN30XXPKOVAR_H_
#define _CN30XXPKOVAR_H_

#include <octeon/dev/cn30xxfauvar.h>
#include <octeon/dev/cn30xxpkoreg.h>

#define FAU_OP_SIZE_8	0
#define FAU_OP_SIZE_16	1
#define FAU_OP_SIZE_32	2   
#define FAU_OP_SIZE_64	3        

#define PKO_OUTPUT_PORTS_NUM		5
#define PKO_OUTPUT_PORTS_PKTIF_NUM	3
#define PKO_OUTPUT_PORTS_PCIIF_NUM	2
#define	PKO_MEM_QUEUE_PTRS_ILLEGAL_PID	63

/* XXX */
struct cn30xxpko_cmdptr_desc {
	uint64_t	cmdptr;
	uint64_t	cmdptr_idx;
};

/* XXX */
struct cn30xxpko_softc {
	int			sc_port;
	bus_space_tag_t		sc_regt;
	bus_space_handle_t	sc_regh;
	struct cn30xxpko_cmdptr_desc
				*sc_cmdptr;
	int			sc_cmd_buf_pool;
	size_t			sc_cmd_buf_size;
};

/* XXX */
struct cn30xxpko_attach_args {
	int				aa_port;
	bus_space_tag_t			aa_regt;
	struct cn30xxpko_cmdptr_desc	*aa_cmdptr;
	int				aa_cmd_buf_pool;
	size_t				aa_cmd_buf_size;
};

/* XXX */
void			cn30xxpko_init(struct cn30xxpko_attach_args *,
			    struct cn30xxpko_softc **);
int			cn30xxpko_enable(struct cn30xxpko_softc *);
int			cn30xxpko_reset(struct cn30xxpko_softc *);
void			cn30xxpko_config(struct cn30xxpko_softc *);
int			cn30xxpko_port_enable(struct cn30xxpko_softc *, int);
int			cn30xxpko_port_config(struct cn30xxpko_softc *);
static inline uint64_t	cn30xxpko_cmd_word0(int, int, int, int, int, int,
			    int, int, int, int, int, int, int, int, int, int);
static inline uint64_t	cn30xxpko_cmd_word1(int, int, int, int, paddr_t);


static inline uint64_t
cn30xxpko_cmd_word0(int sz1, int sz0, int s1, int reg1, int s0, int reg0,
    int le, int n2, int q, int r, int g, int ipoffp1, int ii, int df, int segs,
    int totalbytes)
{
	uint64_t cmd = 0;

	SET(cmd, (((uint64_t)sz1 << 62) & PKO_CMD_WORD0_SZ1)
 	    | (((uint64_t)sz0 << 60) & PKO_CMD_WORD0_SZ0)
	    | (((uint64_t)s1 << 59) & PKO_CMD_WORD0_S1)
	    | (((uint64_t)reg1 << 48) & PKO_CMD_WORD0_REG1)
	    | (((uint64_t)s0 << 47) & PKO_CMD_WORD0_S0)
	    | (((uint64_t)reg0 << 36) & PKO_CMD_WORD0_REG0)
	    | (((uint64_t)le << 35) & PKO_CMD_WORD0_LE)
	    | (((uint64_t)n2 << 34) & PKO_CMD_WORD0_N2)
	    | (((uint64_t)q << 33) & PKO_CMD_WORD0_Q)
	    | (((uint64_t)r << 32) & PKO_CMD_WORD0_R)
	    | (((uint64_t)g << 31) & PKO_CMD_WORD0_G)
	    | (((uint64_t)ipoffp1 << 24) & PKO_CMD_WORD0_IPOFFP1)
	    | (((uint64_t)ii << 23) & PKO_CMD_WORD0_II)
	    | (((uint64_t)df << 22) & PKO_CMD_WORD0_DF)
	    | (((uint64_t)segs << 16) & PKO_CMD_WORD0_SEGS)
	    | (((uint64_t)totalbytes << 0) & PKO_CMD_WORD0_TOTALBYTES));
	return cmd;
}

static inline uint64_t
cn30xxpko_cmd_word1(int i, int back, int pool, int size, paddr_t addr)
{
	uint64_t cmd = 0;

	SET(cmd, (((uint64_t)i << 63) & PKO_CMD_WORD1_I)
	    | (((uint64_t)back << 59) & PKO_CMD_WORD1_BACK)
	    | (((uint64_t)pool << 56) & PKO_CMD_WORD1_POOL)
	    | (((uint64_t)size << 40) & PKO_CMD_WORD1_SIZE)
	    | (((uint64_t)addr << 0) & PKO_CMD_WORD1_ADDR));
	return cmd;
}

/* ---- operation primitives */

/* 8.8.1 Store Operations */

static inline void
cn30xxpko_op_store(uint64_t args, uint64_t value)
{
	paddr_t addr;

	addr =
	    ((uint64_t)1 << 48) |
	    ((uint64_t)(CN30XXPKO_MAJORDID & 0x1f) << 43) |
	    ((uint64_t)(CN30XXPKO_SUBDID & 0x7) << 40) |
	    ((uint64_t)args);
	/* XXX */
	mips_sync();
	octeon_xkphys_write_8(addr, value);
}

static inline void
cn30xxpko_op_doorbell_write(int pid, int qid, int wdc)
{
	uint64_t args, value;

	args =
	    ((uint64_t)(pid & 0x3f) << 12) |
	    ((uint64_t)(qid & 0x1ff) << 3);
	value = wdc & 0xfffff;
	cn30xxpko_op_store(args, value);
}

#endif
