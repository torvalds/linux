/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
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

#define	DMA_CR		0x000	/* Control */
#define	DMA_ES		0x004	/* Error Status */
#define	DMA_ERQ		0x00C	/* Enable Request */
#define	DMA_EEI		0x014	/* Enable Error Interrupt */
#define	DMA_CEEI	0x018	/* Clear Enable Error Interrupt */
#define	DMA_SEEI	0x019	/* Set Enable Error Interrupt */
#define	DMA_CERQ	0x01A	/* Clear Enable Request */
#define	DMA_SERQ	0x01B	/* Set Enable Request */
#define	DMA_CDNE	0x01C	/* Clear DONE Status Bit */
#define	DMA_SSRT	0x01D	/* Set START Bit */
#define	DMA_CERR	0x01E	/* Clear Error */
#define	 CERR_CAEI	(1 << 6) /* Clear All Error Indicators */
#define	DMA_CINT	0x01F	/* Clear Interrupt Request */
#define	 CINT_CAIR	(1 << 6) /* Clear All Interrupt Requests */
#define	DMA_INT		0x024	/* Interrupt Request */
#define	DMA_ERR		0x02C	/* Error */
#define	DMA_HRS		0x034	/* Hardware Request Status */
#define	DMA_EARS	0x044	/* Enable Asynchronous Request in Stop */
#define	DMA_DCHPRI3	0x100	/* Channel n Priority */
#define	DMA_DCHPRI2	0x101	/* Channel n Priority */
#define	DMA_DCHPRI1	0x102	/* Channel n Priority */
#define	DMA_DCHPRI0	0x103	/* Channel n Priority */
#define	DMA_DCHPRI7	0x104	/* Channel n Priority */
#define	DMA_DCHPRI6	0x105	/* Channel n Priority */
#define	DMA_DCHPRI5	0x106	/* Channel n Priority */
#define	DMA_DCHPRI4	0x107	/* Channel n Priority */
#define	DMA_DCHPRI11	0x108	/* Channel n Priority */
#define	DMA_DCHPRI10	0x109	/* Channel n Priority */
#define	DMA_DCHPRI9	0x10A	/* Channel n Priority */
#define	DMA_DCHPRI8	0x10B	/* Channel n Priority */
#define	DMA_DCHPRI15	0x10C	/* Channel n Priority */
#define	DMA_DCHPRI14	0x10D	/* Channel n Priority */
#define	DMA_DCHPRI13	0x10E	/* Channel n Priority */
#define	DMA_DCHPRI12	0x10F	/* Channel n Priority */
#define	DMA_DCHPRI19	0x110	/* Channel n Priority */
#define	DMA_DCHPRI18	0x111	/* Channel n Priority */
#define	DMA_DCHPRI17	0x112	/* Channel n Priority */
#define	DMA_DCHPRI16	0x113	/* Channel n Priority */
#define	DMA_DCHPRI23	0x114	/* Channel n Priority */
#define	DMA_DCHPRI22	0x115	/* Channel n Priority */
#define	DMA_DCHPRI21	0x116	/* Channel n Priority */
#define	DMA_DCHPRI20	0x117	/* Channel n Priority */
#define	DMA_DCHPRI27	0x118	/* Channel n Priority */
#define	DMA_DCHPRI26	0x119	/* Channel n Priority */
#define	DMA_DCHPRI25	0x11A	/* Channel n Priority */
#define	DMA_DCHPRI24	0x11B	/* Channel n Priority */
#define	DMA_DCHPRI31	0x11C	/* Channel n Priority */
#define	DMA_DCHPRI30	0x11D	/* Channel n Priority */
#define	DMA_DCHPRI29	0x11E	/* Channel n Priority */
#define	DMA_DCHPRI28	0x11F	/* Channel n Priority */

#define	DMA_TCDn_SADDR(n)		(0x00 + 0x20 * n)	/* Source Address */
#define	DMA_TCDn_SOFF(n)		(0x04 + 0x20 * n)	/* Signed Source Address Offset */
#define	DMA_TCDn_ATTR(n)		(0x06 + 0x20 * n)	/* Transfer Attributes */
#define	DMA_TCDn_NBYTES_MLNO(n)		(0x08 + 0x20 * n)	/* Minor Byte Count */
#define	DMA_TCDn_NBYTES_MLOFFNO(n)	(0x08 + 0x20 * n)	/* Signed Minor Loop Offset */
#define	DMA_TCDn_NBYTES_MLOFFYES(n)	(0x08 + 0x20 * n)	/* Signed Minor Loop Offset */
#define	DMA_TCDn_SLAST(n)		(0x0C + 0x20 * n)	/* Last Source Address Adjustment */
#define	DMA_TCDn_DADDR(n)		(0x10 + 0x20 * n)	/* Destination Address */
#define	DMA_TCDn_DOFF(n)		(0x14 + 0x20 * n)	/* Signed Destination Address Offset */
#define	DMA_TCDn_CITER_ELINKYES(n)	(0x16 + 0x20 * n)	/* Current Minor Loop Link, Major Loop Count */
#define	DMA_TCDn_CITER_ELINKNO(n)	(0x16 + 0x20 * n)
#define	DMA_TCDn_DLASTSGA(n)		(0x18 + 0x20 * n)	/* Last Dst Addr Adjustment/Scatter Gather Address */
#define	DMA_TCDn_CSR(n)			(0x1C + 0x20 * n)	/* Control and Status */
#define	DMA_TCDn_BITER_ELINKYES(n)	(0x1E + 0x20 * n)	/* Beginning Minor Loop Link, Major Loop Count */
#define	DMA_TCDn_BITER_ELINKNO(n)	(0x1E + 0x20 * n)	/* Beginning Minor Loop Link, Major Loop Count */

#define TCD_CSR_START			(1 << 0)
#define	TCD_CSR_INTMAJOR		(1 << 1)
#define	TCD_CSR_INTHALF			(1 << 2)
#define	TCD_CSR_DREQ			(1 << 3)
#define	TCD_CSR_ESG			(1 << 4)
#define	TCD_CSR_MAJORELINK		(1 << 5)
#define	TCD_CSR_ACTIVE			(1 << 6)
#define	TCD_CSR_DONE			(1 << 7)
#define	TCD_CSR_MAJORELINKCH_SHIFT	8

#define	TCD_ATTR_SMOD_SHIFT		11	/* Source Address Modulo */
#define	TCD_ATTR_SSIZE_SHIFT		8	/* Source Data Transfer Size */
#define	TCD_ATTR_DMOD_SHIFT		3	/* Dst Address Modulo */
#define	TCD_ATTR_DSIZE_SHIFT		0	/* Dst Data Transfer Size */

#define	TCD_READ4(_sc, _reg)		\
	bus_space_read_4(_sc->bst_tcd, _sc->bsh_tcd, _reg)
#define	TCD_WRITE4(_sc, _reg, _val)	\
	bus_space_write_4(_sc->bst_tcd, _sc->bsh_tcd, _reg, _val)
#define	TCD_READ2(_sc, _reg)		\
	bus_space_read_2(_sc->bst_tcd, _sc->bsh_tcd, _reg)
#define	TCD_WRITE2(_sc, _reg, _val)	\
	bus_space_write_2(_sc->bst_tcd, _sc->bsh_tcd, _reg, _val)
#define	TCD_READ1(_sc, _reg)		\
	bus_space_read_1(_sc->bst_tcd, _sc->bsh_tcd, _reg)
#define	TCD_WRITE1(_sc, _reg, _val)	\
	bus_space_write_1(_sc->bst_tcd, _sc->bsh_tcd, _reg, _val)

#define	EDMA_NUM_DEVICES	2
#define	EDMA_NUM_CHANNELS	32
#define	NCHAN_PER_MUX		16

struct tcd_conf {
	bus_addr_t	saddr;
	bus_addr_t	daddr;
	uint32_t	nbytes;
	uint32_t	nmajor;
	uint32_t	majorelink;
	uint32_t	majorelinkch;
	uint32_t	esg;
	uint32_t	smod;
	uint32_t	dmod;
	uint32_t	soff;
	uint32_t	doff;
	uint32_t	ssize;
	uint32_t	dsize;
	uint32_t	slast;
	uint32_t	dlast_sga;
	uint32_t	channel;
	uint32_t	(*ih)(void *, int);
	void		*ih_user;
};

/*
 * TCD struct is described at
 * Vybrid Reference Manual, Rev. 5, 07/2013
 *
 * Should be used for Scatter/Gathering feature.
 */

struct TCD {
	uint32_t	saddr;
	uint16_t	attr;
	uint16_t	soff;
	uint32_t	nbytes;
	uint32_t	slast;
	uint32_t	daddr;
	uint16_t	citer;
	uint16_t	doff;
	uint32_t	dlast_sga;
	uint16_t	biter;
	uint16_t	csr;
} __packed;

struct edma_softc {
	device_t		dev;
	struct resource		*res[4];
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	bus_space_tag_t		bst_tcd;
	bus_space_handle_t	bsh_tcd;
	void			*tc_ih;
	void			*err_ih;
	uint32_t		device_id;

	int	(*channel_configure) (struct edma_softc *, int, int);
	int	(*channel_free) (struct edma_softc *, int);
	int	(*dma_request) (struct edma_softc *, int);
	int	(*dma_setup) (struct edma_softc *, struct tcd_conf *);
	int	(*dma_stop) (struct edma_softc *, int);
};
