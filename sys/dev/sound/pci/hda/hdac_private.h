/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Stephane E. Potvin <sepotvin@videotron.ca>
 * Copyright (c) 2008-2012 Alexander Motin <mav@FreeBSD.org>
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

#ifndef _HDAC_PRIVATE_H_
#define _HDAC_PRIVATE_H_

/****************************************************************************
 * Miscellaneous defines
 ****************************************************************************/
#define HDAC_CODEC_MAX		16

/****************************************************************************
 * Helper Macros
 ****************************************************************************/
#define HDAC_READ_1(mem, offset)					\
	bus_space_read_1((mem)->mem_tag, (mem)->mem_handle, (offset))
#define HDAC_READ_2(mem, offset)					\
	bus_space_read_2((mem)->mem_tag, (mem)->mem_handle, (offset))
#define HDAC_READ_4(mem, offset)					\
	bus_space_read_4((mem)->mem_tag, (mem)->mem_handle, (offset))
#define HDAC_WRITE_1(mem, offset, value)				\
	bus_space_write_1((mem)->mem_tag, (mem)->mem_handle, (offset), (value))
#define HDAC_WRITE_2(mem, offset, value)				\
	bus_space_write_2((mem)->mem_tag, (mem)->mem_handle, (offset), (value))
#define HDAC_WRITE_4(mem, offset, value)				\
	bus_space_write_4((mem)->mem_tag, (mem)->mem_handle, (offset), (value))

#define HDAC_ISDCTL(sc, n)	(_HDAC_ISDCTL((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_ISDSTS(sc, n)	(_HDAC_ISDSTS((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_ISDPICB(sc, n)	(_HDAC_ISDPICB((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_ISDCBL(sc, n)	(_HDAC_ISDCBL((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_ISDLVI(sc, n)	(_HDAC_ISDLVI((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_ISDFIFOD(sc, n)	(_HDAC_ISDFIFOD((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_ISDFMT(sc, n)	(_HDAC_ISDFMT((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_ISDBDPL(sc, n)	(_HDAC_ISDBDPL((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_ISDBDPU(sc, n)	(_HDAC_ISDBDPU((n), (sc)->num_iss, (sc)->num_oss))

#define HDAC_OSDCTL(sc, n)	(_HDAC_OSDCTL((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_OSDSTS(sc, n)	(_HDAC_OSDSTS((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_OSDPICB(sc, n)	(_HDAC_OSDPICB((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_OSDCBL(sc, n)	(_HDAC_OSDCBL((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_OSDLVI(sc, n)	(_HDAC_OSDLVI((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_OSDFIFOD(sc, n)	(_HDAC_OSDFIFOD((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_OSDBDPL(sc, n)	(_HDAC_OSDBDPL((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_OSDBDPU(sc, n)	(_HDAC_OSDBDPU((n), (sc)->num_iss, (sc)->num_oss))

#define HDAC_BSDCTL(sc, n)	(_HDAC_BSDCTL((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_BSDSTS(sc, n)	(_HDAC_BSDSTS((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_BSDPICB(sc, n)	(_HDAC_BSDPICB((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_BSDCBL(sc, n)	(_HDAC_BSDCBL((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_BSDLVI(sc, n)	(_HDAC_BSDLVI((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_BSDFIFOD(sc, n)	(_HDAC_BSDFIFOD((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_BSDBDPL(sc, n)	(_HDAC_BSDBDPL((n), (sc)->num_iss, (sc)->num_oss))
#define HDAC_BSDBDPU(sc, n)	(_HDAC_BSDBDPU((n), (sc)->num_iss, (sc)->num_oss))

/****************************************************************************
 * Custom hdac malloc type
 ****************************************************************************/
MALLOC_DECLARE(M_HDAC);

/****************************************************************************
 * struct hdac_mem
 *
 * Holds the resources necessary to describe the physical memory associated
 * with the device.
 ****************************************************************************/
struct hdac_mem {
	struct resource		*mem_res;
	int			mem_rid;
	bus_space_tag_t		mem_tag;
	bus_space_handle_t	mem_handle;
};

/****************************************************************************
 * struct hdac_irq
 *
 * Holds the resources necessary to describe the irq associated with the
 * device.
 ****************************************************************************/
struct hdac_irq {
	struct resource		*irq_res;
	int			irq_rid;
	void			*irq_handle;
};

/****************************************************************************
 * struct hdac_dma
 *
 * This structure is used to hold all the information to manage the dma
 * states.
 ****************************************************************************/
struct hdac_dma {
	bus_dma_tag_t	dma_tag;
	bus_dmamap_t	dma_map;
	bus_addr_t	dma_paddr;
	bus_size_t	dma_size;
	caddr_t		dma_vaddr;
};

/****************************************************************************
 * struct hdac_rirb
 *
 * Hold a response from a verb sent to a codec received via the rirb.
 ****************************************************************************/
struct hdac_rirb {
	uint32_t	response;
	uint32_t	response_ex;
};

#define HDAC_RIRB_RESPONSE_EX_SDATA_IN_MASK	0x0000000f
#define HDAC_RIRB_RESPONSE_EX_SDATA_IN_OFFSET	0
#define HDAC_RIRB_RESPONSE_EX_UNSOLICITED	0x00000010

#define HDAC_RIRB_RESPONSE_EX_SDATA_IN(response_ex)			\
    (((response_ex) & HDAC_RIRB_RESPONSE_EX_SDATA_IN_MASK) >>		\
    HDAC_RIRB_RESPONSE_EX_SDATA_IN_OFFSET)

struct hdac_bdle {
	volatile uint32_t addrl;
	volatile uint32_t addrh;
	volatile uint32_t len;
	volatile uint32_t ioc;
} __packed;

struct hdac_stream {
	device_t	dev;
	struct hdac_dma	bdl;
	int		dir;
	int		stream;
	int		blksz;
	int		running;
	int		bw;
	int		stripe;
	uint16_t	format;
};

struct hdac_softc {
	device_t	dev;
	struct mtx	*lock;

	struct intr_config_hook intrhook;

	struct hdac_mem	mem;
	struct hdac_irq	irq;

	uint32_t	quirks_on;
	uint32_t	quirks_off;
	uint32_t	flags;
#define HDAC_F_DMA_NOCACHE	0x00000001

	int		num_iss;
	int		num_oss;
	int		num_bss;
	int		num_ss;
	int		num_sdo;
	int		support_64bit;

	int		corb_size;
	struct hdac_dma corb_dma;
	int		corb_wp;

	int		rirb_size;
	struct hdac_dma	rirb_dma;
	int		rirb_rp;

	struct hdac_dma	pos_dma;

	bus_dma_tag_t		chan_dmat;

	/* Polling */
	int			polling;
	int			poll_ival;
	struct callout		poll_callout;

	int			unsol_registered;
	struct task		unsolq_task;
#define HDAC_UNSOLQ_MAX		64
#define HDAC_UNSOLQ_READY	0
#define HDAC_UNSOLQ_BUSY	1
	int		unsolq_rp;
	int		unsolq_wp;
	int		unsolq_st;
	uint32_t	unsolq[HDAC_UNSOLQ_MAX];

	int			sdo_bw_used;

	struct hdac_stream	*streams;

	struct {
		device_t	dev;
		uint16_t	vendor_id;
		uint16_t	device_id;
		uint8_t		revision_id;
		uint8_t		stepping_id;
		int		pending;
		uint32_t	response;
		int		sdi_bw_used;
	} codecs[HDAC_CODEC_MAX];
};

#endif
