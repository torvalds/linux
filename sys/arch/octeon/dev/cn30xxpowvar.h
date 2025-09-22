/*	$OpenBSD: cn30xxpowvar.h,v 1.7 2024/05/20 23:13:33 jsg Exp $	*/

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

#ifndef _CN30XXPOWVAR_H_
#define _CN30XXPOWVAR_H_

#define POW_TAG_TYPE_ORDERED	0
#define POW_TAG_TYPE_ATOMIC	1
#define POW_TAG_TYPE_NULL	2
#define POW_TAG_TYPE_NULL_NULL	3

#define POW_TAG_OP_SWTAG		0
#define POW_TAG_OP_SWTAG_FULL		1
#define POW_TAG_OP_SWTAG_DESCHED	2
#define POW_TAG_OP_DESCHED		3
#define POW_TAG_OP_ADDWQ		4
#define POW_TAG_OP_UPD_WQP_GRP		5
#define POW_TAG_OP_CLR_NSCHED		7
#define POW_TAG_OP_NOP			15

#define POW_WAIT	1
#define POW_NO_WAIT	0

#define POW_WORKQ_IRQ(group)		(group)

/* XXX */
struct cn30xxpow_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_regt;
	bus_space_handle_t	sc_regh;
	int			sc_port;
	int			sc_int_pc_base;
};

/* XXX */
struct cn30xxpow_attach_args {
	int			aa_port;
	bus_space_tag_t		aa_regt;
};

void			cn30xxpow_config(struct cn30xxpow_softc *, int);

#define	_POW_RD8(sc, off) \
	bus_space_read_8((sc)->sc_regt, (sc)->sc_regh, (off))
#define	_POW_WR8(sc, off, v) \
	bus_space_write_8((sc)->sc_regt, (sc)->sc_regh, (off), (v))
#define	_POW_GROUP_RD8(sc, pi, off) \
	bus_space_read_8((sc)->sc_regt, (sc)->sc_regh, \
	    (off) + sizeof(uint64_t) * (pi)->pi_group)
#define	_POW_GROUP_WR8(sc, pi, off, v) \
	bus_space_write_8((sc)->sc_regt, (sc)->sc_regh, \
	    (off) + sizeof(uint64_t) * (pi)->pi_group, (v))

extern struct cn30xxpow_softc	cn30xxpow_softc;

/* -------------------------------------------------------------------------- */

/* 5.11.1 Load Operations */

/* GET_WORK Loads */

static inline uint64_t 
cn30xxpow_ops_get_work_load(
	int wait)			/* 0-1 */
{
	uint64_t ptr =
	    POW_OPERATION_BASE_IO_BIT |
	    __BITS64_SET(POW_OPERATION_BASE_MAJOR_DID, 0x0c) |
	    __BITS64_SET(POW_OPERATION_BASE_SUB_DID, 0x00) |
	    __BITS64_SET(POW_GET_WORK_LOAD_WAIT, wait);

	return octeon_xkphys_read_8(ptr);
}

/* 5.11.2 IOBDMA Operations */

/*
 * XXXSEIL
 * ``subdid'' values are inverted between ``get_work_addr'' and ``null_read_id''
 * in CN30XX-HM-1.0 and CN31XX-HM-1.01.  (Corrected in CN50XX-HM-0.99A.)
 *
 * XXXSEIL
 * The ``scraddr'' part is index in 8 byte words, not address.  This is not
 * documented...
 */

/* GET_WORK IOBDMAs */

static inline void
cn30xxpow_ops_get_work_iobdma(
	int scraddr,			/* 0-2047 */
	int wait)			/* 0-1 */
{
 	/* ``scraddr'' part is index in 64-bit words, not address */
	const int scrindex = scraddr / sizeof(uint64_t);

        uint64_t args =
             __BITS64_SET(POW_IOBDMA_GET_WORK_WAIT, wait);
        uint64_t value =
            __BITS64_SET(POW_IOBDMA_BASE_SCRADDR, scrindex) |
            __BITS64_SET(POW_IOBDMA_BASE_LEN, 0x01) |
            __BITS64_SET(POW_IOBDMA_BASE_MAJOR_DID, 0x0c) |
            __BITS64_SET(POW_IOBDMA_BASE_SUB_DID, 0x00) |
            __BITS64_SET(POW_IOBDMA_BASE_39_0, args);

        octeon_iobdma_write_8(value);
}

/* NULL_RD IOBDMAs */

static inline void
cn30xxpow_ops_null_rd_iobdma(
	int scraddr)			/* 0-2047 */
{
 	/* ``scraddr'' part is index in 64-bit words, not address */
	const int scrindex = scraddr / sizeof(uint64_t);

        uint64_t value =
            __BITS64_SET(POW_IOBDMA_BASE_SCRADDR, scrindex) |
            __BITS64_SET(POW_IOBDMA_BASE_LEN, 0x01) |
            __BITS64_SET(POW_IOBDMA_BASE_MAJOR_DID, 0x0c) |
            __BITS64_SET(POW_IOBDMA_BASE_SUB_DID, 0x04) |
            __BITS64_SET(POW_IOBDMA_BASE_39_0, 0);

        octeon_iobdma_write_8(value);
}

/* 5.11.3 Store Operations */

static inline void
cn30xxpow_store(
	int subdid,			/* 0, 1, 3 */
	uint64_t addr,			/* 0-0x0000.000f.ffff.ffff */
	int no_sched,			/* 0, 1 */
	int index,			/* 0-8191 */
	int op,				/* 0-15 */
	int qos,			/* 0-7 */
	int grp,			/* 0-7 */
	int type,			/* 0-7 */
	uint32_t tag)			/* 0-0xffff.ffff */
{
	/* Physical Address to Store to POW */
	uint64_t ptr =
	    POW_OPERATION_BASE_IO_BIT |
	    __BITS64_SET(POW_OPERATION_BASE_MAJOR_DID, 0x0c) |
	    __BITS64_SET(POW_OPERATION_BASE_SUB_DID, subdid) |
	    __BITS64_SET(POW_PHY_ADDR_STORE_ADDR, addr); 

	/* Store Data on Store to POW */
	uint64_t args =
	    __BITS64_SET(POW_STORE_DATA_NO_SCHED, no_sched) |
	    __BITS64_SET(POW_STORE_DATA_INDEX, index) |
	    __BITS64_SET(POW_STORE_DATA_OP, op) |
	    __BITS64_SET(POW_STORE_DATA_QOS, qos) |
	    __BITS64_SET(POW_STORE_DATA_GRP, grp) |
	    __BITS64_SET(POW_STORE_DATA_TYPE, type) |
	    __BITS64_SET(POW_STORE_DATA_TAG, tag); 

	octeon_xkphys_write_8(ptr, args);
}

/* SWTAG */

static inline void
cn30xxpow_ops_swtag(int type, uint32_t tag)
{
	cn30xxpow_store(
		1,			/* subdid == 1 */
		0, 			/* addr (not used for SWTAG) */
		0,			/* no_sched (not used for SWTAG) */
		0,			/* index (not used for SWTAG) */
		POW_TAG_OP_SWTAG,	/* op == SWTAG */
		0,			/* qos (not used for SWTAG) */
		0,			/* grp (not used for SWTAG) */
		type,
		tag);
	/* switch to NULL completes immediately */
}

/* SWTAG_FULL */

static inline void
cn30xxpow_ops_swtag_full(paddr_t addr, int grp, int type, uint32_t tag)
{
	cn30xxpow_store(
		0,			/* subdid == 0 */
		addr,
		0,			/* no_sched (not used for SWTAG_FULL) */
		0,			/* index (not used for SWTAG_FULL) */
		POW_TAG_OP_SWTAG_FULL,	/* op == SWTAG_FULL */
		0,			/* qos (not used for SWTAG_FULL) */
		grp,
		type,
		tag);
}

/* SWTAG_DESCHED */

static inline void
cn30xxpow_ops_swtag_desched(int no_sched, int grp, int type, uint32_t tag)
{
	cn30xxpow_store(
		3,			/* subdid == 3 */
		0,			/* addr (not used for SWTAG_DESCHED) */
		no_sched,
		0,			/* index (not used for SWTAG_DESCHED) */
		POW_TAG_OP_SWTAG_DESCHED, /* op == SWTAG_DESCHED */
		0,			/* qos (not used for SWTAG_DESCHED) */
		grp,
		type,
		tag);
}

/* DESCHED */

static inline void
cn30xxpow_ops_desched(int no_sched)
{
	cn30xxpow_store(
		3,			/* subdid == 3 */
		0,			/* addr (not used for DESCHED) */
		no_sched,
		0,			/* index (not used for DESCHED) */
		POW_TAG_OP_DESCHED,	/* op == DESCHED */
		0,			/* qos (not used for DESCHED) */
		0,			/* grp (not used for DESCHED) */
		0,			/* type (not used for DESCHED) */
		0);			/* tag (not used for DESCHED) */
}

/* ADDWQ */

static inline void
cn30xxpow_ops_addwq(paddr_t addr, int qos, int grp, int type, uint32_t tag)
{
	cn30xxpow_store(
		1,			/* subdid == 1 */
		addr,
		0,			/* no_sched (not used for ADDWQ) */
		0,			/* index (not used for ADDWQ) */
		POW_TAG_OP_ADDWQ,	/* op == ADDWQ */
		qos,
		grp,
		type,
		tag);
}

/* UPD_WQP_GRP */

static inline void
cn30xxpow_ops_upd_wqp_grp(paddr_t addr, int grp)
{
	cn30xxpow_store(
		1,			/* subdid == 1 */
		addr,
		0,			/* no_sched (not used for UPD_WQP_GRP) */
		0,			/* index (not used for UPD_WQP_GRP) */
		POW_TAG_OP_UPD_WQP_GRP,	/* op == UPD_WQP_GRP */
		0,			/* qos (not used for UPD_WQP_GRP) */
		grp,
		0,			/* type (not used for UPD_WQP_GRP) */
		0);			/* tag (not used for UPD_WQP_GRP) */
}

/* CLR_NSCHED */

static inline void
cn30xxpow_ops_clr_nsched(paddr_t addr, int index)
{
	cn30xxpow_store(
		1,			/* subdid == 1 */
		addr,
		0,			/* no_sched (not used for CLR_NSCHED) */
		index,
		POW_TAG_OP_CLR_NSCHED,	/* op == CLR_NSCHED */
		0,			/* qos (not used for CLR_NSCHED) */
		0,			/* grp (not used for CLR_NSCHED) */
		0,			/* type (not used for CLR_NSCHED) */
		0);			/* tag (not used for CLR_NSCHED) */
}

/* NOP */

static inline void
cn30xxpow_ops_nop(void)
{
	cn30xxpow_store(
		1,			/* subdid == 1 */
		0,			/* addr (not used for NOP) */
		0,			/* no_sched (not used for NOP) */
		0,			/* index (not used for NOP) */
		POW_TAG_OP_NOP,		/* op == NOP */
		0,			/* qos (not used for NOP) */
		0,			/* grp (not used for NOP) */
		0,			/* type (not used for NOP) */
		0);			/* tag (not used for NOP) */
}

/*
 * Check if there is a pending POW tag switch.
 */
static inline int
cn30xxpow_tag_sw_pending(void)
{
	int result;

	/*
	 * "RDHWR rt, $30" returns:
	 *	0 => pending bit is set
	 *	1 => pending bit is clear
	 */

	__asm volatile (
		"	.set	push\n"
		"	.set	noreorder\n"
		"	.set	arch=mips64r2\n"
		"	rdhwr	%0, $30\n"
		"	.set	pop\n"
		: "=r" (result));
	return result == 0;
}

/*
 * Wait until there is no pending POW tag switch.
 */
static inline void
cn30xxpow_tag_sw_wait(void)
{
	while (cn30xxpow_tag_sw_pending())
		continue;
}

/* -------------------------------------------------------------------------- */

/*
 * global functions
 */
void		cn30xxpow_work_request_async(uint64_t, uint64_t);
uint64_t	*cn30xxpow_work_response_async(uint64_t);
void		cn30xxpow_ops_swtag(int, uint32_t);

static inline void
cn30xxpow_config_int_pc(struct cn30xxpow_softc *sc, int unit)
{
	uint64_t wq_int_pc;
	uint64_t pc_thr;
	static uint64_t cpu_clock_hz;

	if (cpu_clock_hz == 0)
		cpu_clock_hz  = curcpu()->ci_hw.clock;
	
#if 0
	/* from Documents */
	/*
 	 * => counter value is POW_WQ_INT_PC[PC_THR] * 256 + 255
	 * => counter is decremented every CPU clock
	 * => counter is reset when interrupt occurs
	 *
	 *      cpu_clock_per_sec
	 *              = cpu_clock_mhz * 1000000
	 *      cpu_clock_per_msec
	 *              = cpu_clock_mhz * 1000
	 *      cpu_clock_per_usec
	 *              = cpu_clock_mhz
 	 *
	 *      pc_thr_for_1sec * 256 + 255
	 *              = cpu_clock_mhz * 1000000
	 *      pc_thr_for_1sec
	 *              = ((cpu_clock_mhz * 1000000) - 255) / 256
	 *
	 *      pc_thr_for_1msec * 256 + 255
	 *              = cpu_clock_mhz * 1000
	 *      pc_thr_for_1msec
	 *              = ((cpu_clock_mhz * 1000) - 255) / 256
	 *
	 *      pc_thr_for_1usec * 256 + 255
	 *              = cpu_clock_mhz
	 *      pc_thr_for_1usec
	 *              = (cpu_clock_mhz - 255) / 256
	 */
	pc_thr = (((cpu_clock_hz / 1000000) * (unit)) - 255) / 256;
#else
	pc_thr = (cpu_clock_hz) / (unit * 16 * 256);
#endif
	wq_int_pc = pc_thr << POW_WQ_INT_PC_PC_THR_SHIFT;
	_POW_WR8(sc, POW_WQ_INT_PC_OFFSET, wq_int_pc);
}

static inline void
cn30xxpow_config_int_pc_rate(struct cn30xxpow_softc *sc, int rate)
{
	cn30xxpow_config_int_pc(sc, sc->sc_int_pc_base / rate);
}

#endif /* _CN30XXPOWVAR_H_ */
