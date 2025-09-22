/*	$OpenBSD: cn30xxpko.c,v 1.10 2025/07/16 07:15:42 jsg Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/octeonvar.h>

#include <octeon/dev/cn30xxfaureg.h>
#include <octeon/dev/cn30xxfpavar.h>
#include <octeon/dev/cn30xxpkoreg.h>
#include <octeon/dev/cn30xxpkovar.h>

static inline void	cn30xxpko_op_store(uint64_t, uint64_t);

#define	_PKO_RD8(sc, off) \
	bus_space_read_8((sc)->sc_regt, (sc)->sc_regh, (off))
#define	_PKO_WR8(sc, off, v) \
	bus_space_write_8((sc)->sc_regt, (sc)->sc_regh, (off), (v))

/* ----- global functions */

/* XXX */
void
cn30xxpko_init(struct cn30xxpko_attach_args *aa,
    struct cn30xxpko_softc **rsc)
{
	struct cn30xxpko_softc *sc;
	int status;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);
	if (sc == NULL)
		panic("can't allocate memory: %s", __func__);

	sc->sc_port = aa->aa_port;
	sc->sc_regt = aa->aa_regt;
	sc->sc_cmdptr = aa->aa_cmdptr;
	sc->sc_cmd_buf_pool = aa->aa_cmd_buf_pool;
	sc->sc_cmd_buf_size = aa->aa_cmd_buf_size;

	status = bus_space_map(sc->sc_regt, PKO_BASE, PKO_SIZE, 0,
	    &sc->sc_regh);
	if (status != 0)
		panic("can't map %s space", "pko register");

	*rsc = sc;
}

int
cn30xxpko_enable(struct cn30xxpko_softc *sc)
{
	uint64_t reg_flags;

	reg_flags = _PKO_RD8(sc, PKO_REG_FLAGS_OFFSET);
	/* PKO_REG_FLAGS_RESET=0 */
	/* PKO_REG_FLAGS_STORE_BE=0 */
	SET(reg_flags, PKO_REG_FLAGS_ENA_DWB);
	SET(reg_flags, PKO_REG_FLAGS_ENA_PKO);
	/* XXX */
	mips_sync();
	_PKO_WR8(sc, PKO_REG_FLAGS_OFFSET, reg_flags);

	return 0;
}

#if 0
void
cn30xxpko_reset(cn30xxpko_softc *sc)
{
	uint64_t reg_flags;

	reg_flags = _PKO_RD8(sc, PKO_REG_FLAGS_OFFSET);
	SET(reg_flags, PKO_REG_FLAGS_RESET);
	_PKO_WR8(sc, PKO_REG_FLAGS_OFFSET, reg_flags);
}
#endif

void
cn30xxpko_config(struct cn30xxpko_softc *sc)
{
	uint64_t reg_cmd_buf = 0;

	SET(reg_cmd_buf, (sc->sc_cmd_buf_pool << 20) & PKO_REG_CMD_BUF_POOL);
	SET(reg_cmd_buf, sc->sc_cmd_buf_size & PKO_REG_CMD_BUF_SIZE);
	_PKO_WR8(sc, PKO_REG_CMD_BUF_OFFSET, reg_cmd_buf);
}

int
cn30xxpko_port_enable(struct cn30xxpko_softc *sc, int enable)
{
	uint64_t reg_read_idx;
	uint64_t mem_queue_qos;

	reg_read_idx = 0;
	SET(reg_read_idx, sc->sc_port & PKO_REG_READ_IDX_IDX);

	/* XXX assume one queue mapped one port */
	/* Enable packet output by enabling all queues for this port */
	mem_queue_qos = 0;
	SET(mem_queue_qos, ((uint64_t)sc->sc_port << 7) & PKO_MEM_QUEUE_QOS_PID);
	SET(mem_queue_qos, sc->sc_port & PKO_MEM_QUEUE_QOS_QID);
	SET(mem_queue_qos, ((enable ? 0xffULL : 0x00ULL) << 53) &
	    PKO_MEM_QUEUE_QOS_QOS_MASK);

	_PKO_WR8(sc, PKO_REG_READ_IDX_OFFSET, reg_read_idx);
	_PKO_WR8(sc, PKO_MEM_QUEUE_QOS_OFFSET, mem_queue_qos);

	return 0;
}

int pko_queue_map_init[32];

int
cn30xxpko_port_config(struct cn30xxpko_softc *sc)
{
	paddr_t buf_ptr = 0;
	uint64_t mem_queue_ptrs, val;
	int i;

	KASSERT(sc->sc_port < 32);

	buf_ptr = cn30xxfpa_load(OCTEON_POOL_NO_CMD);
	if (buf_ptr == 0)
		return 1;

	KASSERT(buf_ptr != 0);

	if (sc->sc_port == 24) {
		/* Set up PKO for AGL port. */
		_PKO_WR8(sc, PKO_REG_READ_IDX_OFFSET, 1ULL << 8);
		for (i = 0; i < 40; i++) {
			val = _PKO_RD8(sc, PKO_MEM_PORT_PTRS_OFFSET);
			if ((val & PKO_MEM_PORT_PTRS_PID_M) == 24) {
				CLR(val, PKO_MEM_PORT_PTRS_EID_M);
				SET(val, 10ULL << PKO_MEM_PORT_PTRS_EID_S);
				CLR(val, PKO_MEM_PORT_PTRS_BP_PORT_M);
				SET(val, 40ULL << PKO_MEM_PORT_PTRS_BP_PORT_S);
				_PKO_WR8(sc, PKO_MEM_PORT_PTRS_OFFSET, val);
				break;
			}
		}
	}

	/* assume one queue mapped one port */
	mem_queue_ptrs = 0;
	SET(mem_queue_ptrs, PKO_MEM_QUEUE_PTRS_TAIL);
	SET(mem_queue_ptrs, ((uint64_t)0 << 13) & PKO_MEM_QUEUE_PTRS_IDX);
	SET(mem_queue_ptrs, ((uint64_t)sc->sc_port << 7) & PKO_MEM_QUEUE_PTRS_PID);
	SET(mem_queue_ptrs, sc->sc_port & PKO_MEM_QUEUE_PTRS_QID);
	SET(mem_queue_ptrs, ((uint64_t)0xff << 53) & PKO_MEM_QUEUE_PTRS_QOS_MASK);
	SET(mem_queue_ptrs, ((uint64_t)buf_ptr << 17) & PKO_MEM_QUEUE_PTRS_BUF_PTR);
	mips_sync();
	_PKO_WR8(sc, PKO_MEM_QUEUE_PTRS_OFFSET, mem_queue_ptrs);

	/*
	 * Set initial command buffer address and index 
	 * for queue.
	 */
	sc->sc_cmdptr->cmdptr = (uint64_t)buf_ptr;
	sc->sc_cmdptr->cmdptr_idx = 0;

	pko_queue_map_init[sc->sc_port] = 1;

	return 0;
}
