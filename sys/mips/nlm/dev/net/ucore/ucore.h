/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef __NLM_UCORE_H__
#define	__NLM_UCORE_H__

/* Microcode registers */
#define	UCORE_OUTBUF_DONE	0x8000
#define	UCORE_RX_PKT_RDY	0x8004
#define	UCORE_RX_PKT_INFO	0x8008
#define	UCORE_CAM0		0x800c
#define	UCORE_CAM1		0x8010
#define	UCORE_CAM2		0x8014
#define	UCORE_CAM3		0x8018
#define	UCORE_CAM_RESULT	0x801c
#define	UCORE_CSUMINFO		0x8020
#define	UCORE_CRCINFO		0x8024
#define	UCORE_CRCPOS		0x8028
#define	UCORE_FR_FIFOEMPTY	0x802c
#define	UCORE_PKT_DISTR		0x8030

#define	PACKET_MEMORY		0xFFE00
#define	PACKET_DATA_OFFSET	64
#define	SHARED_SCRATCH_MEM	0x18000

/* Distribution mode */
#define	VAL_PDM(x)		(((x) & 0x7) << 0)

/* Dest distribution or distribution list */
#define	VAL_DEST(x)		(((x) & 0x3ff) << 8)
#define	VAL_PDL(x)		(((x) & 0xf) << 4)

/*output buffer done*/
#define	VAL_FSV(x)		(x << 19)
#define	VAL_FFS(x)		(x << 14)

#define	FWD_DEST_ONLY		1
#define	FWD_ENQ_DIST_VEC	2
#define	FWD_ENQ_DEST		3
#define	FWD_DIST_VEC		4
#define	FWD_ENQ_DIST_VEC_SER	6
#define	FWD_ENQ_DEST_SER	7

#define	USE_HASH_DST		(1 << 20)

static __inline unsigned int
nlm_read_ucore_reg(int reg)
{
	volatile unsigned int *addr = (volatile void *)reg;

	return (*addr);
}

static __inline void
nlm_write_ucore_reg(int reg, unsigned int val)
{
	volatile unsigned int *addr = (volatile void *)reg;

	*addr = val;
}

#define	NLM_DEFINE_UCORE(name, reg)				\
static __inline unsigned int					\
nlm_read_ucore_##name(void)					\
{								\
	return nlm_read_ucore_reg(reg);				\
}								\
								\
static __inline void						\
nlm_write_ucore_##name(unsigned int v)				\
{								\
	nlm_write_ucore_reg(reg, v);				\
} struct __hack


NLM_DEFINE_UCORE(obufdone,		UCORE_OUTBUF_DONE);
NLM_DEFINE_UCORE(rxpktrdy,		UCORE_RX_PKT_RDY);
NLM_DEFINE_UCORE(rxpktinfo,		UCORE_RX_PKT_INFO);
NLM_DEFINE_UCORE(cam0,			UCORE_CAM0);
NLM_DEFINE_UCORE(cam1,			UCORE_CAM1);
NLM_DEFINE_UCORE(cam2,			UCORE_CAM2);
NLM_DEFINE_UCORE(cam3,			UCORE_CAM3);
NLM_DEFINE_UCORE(camresult,		UCORE_CAM_RESULT);
NLM_DEFINE_UCORE(csuminfo,		UCORE_CSUMINFO);
NLM_DEFINE_UCORE(crcinfo,		UCORE_CRCINFO);
NLM_DEFINE_UCORE(crcpos,		UCORE_CRCPOS);
NLM_DEFINE_UCORE(freefifo_empty,	UCORE_FR_FIFOEMPTY);
NLM_DEFINE_UCORE(pktdistr,		UCORE_PKT_DISTR);

/*
 * l3cachelines - number of cache lines to allocate into l3
 * fsv - 0 : use interface-id for selecting the free fifo pool
 *       1 : use free fifo pool selected by FFS field
 * ffs - selects which free fifo pool to use to take a free fifo
 * prepad_en - If this field is set to 1, part or all of the
 *             64 byte prepad seen by micro engines, is written
 *             infront of every packet.
 * prepad_ovride - If this field is 1, the ucore system uses
 *                 prepad configuration defined in this register,
 *                 0 means that it uses the configuration defined
 *                 in NAE RX_CONFIG register
 * prepad_size - number of 16 byte words in the 64-byte prepad
 *               seen by micro engines and dma'ed to memory as
 *               pkt prepad. This field is meaningful only if
 *               prepad_en and prepad_ovride is set.
 *               0 : 1 word
 *               1 : 2 words
 *               2 : 3 words
 *               3 : 4 words
 * prepad[0-3]: writing 0 to this means that the 1st 16 byte offset
 *              of prepad in micro engine, gets setup as prepad0/1/2/3.
 *              prepad word.
 *              1 : means 2nd 16 byte chunk in prepad0/1/2/3
 *              2 : means 3rd 16 byte chunk in prepad0/1/2/3
 *              3 : means 4rth 16 byte chunk in prepad0/1/2/3
 * pkt_discard - packet will be discarded if this is set to 1
 * rd5 - value (single bit) to be inserted in bit 5, the unclassified
 *       pkt bit of receive descriptor. If this bit is set, HPRE bit
 *       should also be set in ucore_rxpktready register
 */
static __inline__ void
nlm_ucore_pkt_done(int l3cachelines, int fsv, int ffs, int prepad_en,
    int prepad_ovride, int prepad_size, int prepad0, int prepad1,
    int prepad2, int prepad3, int pkt_discard, int rd5)
{
	unsigned int val = 0;

	val |= ((l3cachelines & 0xfff) << 20);
	val |= ((fsv & 0x1) << 19);
	val |= ((ffs & 0x1f) << 14);
	val |= ((prepad_en & 0x1) << 3);
	val |= ((prepad_ovride & 0x1) << 2);
	val |= ((prepad_size & 0x3) << 12);
	val |= ((prepad0 & 0x3) << 4);
	val |= ((prepad1 & 0x3) << 6);
	val |= ((prepad2 & 0x3) << 8);
	val |= ((prepad3 & 0x3) << 10);
	val |= ((pkt_discard & 0x1) << 1);
	val |= ((rd5 & 0x1) << 0);

	nlm_write_ucore_obufdone(val);
}

/* Get the class full vector field from POE.
 * The POE maintains a threshold for each class.
 * A bit in this field will be set corresponding to the class approaching
 * class full status.
 */
static __inline__ int
nlm_ucore_get_rxpkt_poeclassfullvec(unsigned int pktrdy)
{
	return ((pktrdy >> 24) & 0xff);
}

/* This function returns 1 if the hardware parser extraction process
 * resulted in an error. Else, returns 0.
 */
static __inline__ int
nlm_ucore_get_rxpkt_hwparsererr(unsigned int pktrdy)
{
	return ((pktrdy >> 23) & 0x1);
}

/* This function returns the context number assigned to incoming
 * packet
 */
static __inline__ int
nlm_ucore_get_rxpkt_context(unsigned int pktrdy)
{
	return ((pktrdy >> 13) & 0x3ff);
}

/* this function returns the channel number of incoming packet,
 * and applies only to interlaken.
 */
static __inline__ int
nlm_ucore_get_rxpkt_channel(unsigned int pktrdy)
{
	return ((pktrdy >> 5) & 0xff);
}

/* This function returns the interface number on which the pkt
 * was received
 */
static __inline__ int
nlm_ucore_get_rxpkt_interface(unsigned int pktrdy)
{
	return (pktrdy & 0x1f);
}

/* This function returns 1 if end of packet (EOP) is set in
 * packet data.
 */
static __inline__ int
nlm_ucore_get_rxpkt_eop(unsigned int rxpkt_info)
{
	return ((rxpkt_info >> 9) & 0x1);
}

/* This function returns packet length of received pkt */
static __inline__ int
nlm_ucore_get_rxpktlen(unsigned int rxpkt_info)
{
	return (rxpkt_info & 0x1ff);
}

/* this function sets up the ucore TCAM keys. */
static __inline__ void
nlm_ucore_setup_camkey(unsigned int cam_key0, unsigned int cam_key1,
    unsigned int cam_key2, unsigned int cam_key3)
{
	nlm_write_ucore_cam0(cam_key0);
	nlm_write_ucore_cam1(cam_key1);
	nlm_write_ucore_cam2(cam_key2);
	nlm_write_ucore_cam3(cam_key3);
}

/* This function checks if the cam result is valid or not.
 * If valid, it returns the result, else it returns 0.
 */
static __inline__ int
nlm_ucore_get_cam_result(unsigned int cam_result)
{
	if (((cam_result >> 15) & 0x1) == 1) /* valid result */
	    return (cam_result & 0x3fff);

	return 0;
}

/* This function sets up the csum in ucore.
 * iphdr_start - defines the start of ip header (to check - is this byte
 * position???)
 * iphdr_len - This field is auto filled by h/w parser if zero, else
 * the value defined will be used.
 */
static __inline__ void
nlm_ucore_csum_setup(int iphdr_start, int iphdr_len)
{
	unsigned int val = 0;

	val |= ((iphdr_len & 0xff) << 8);
	val |= (iphdr_len & 0xff);
	nlm_write_ucore_csuminfo(val);
}

/* crcpos - position of crc in pkt. If crc position is within startcrc and
 * endcrc, zero out these bytes in the packet before computing crc. This
 * field is not needed for FCoE.
 * cps - If 1, uses the polynomial in RX_CRC_POLY1 of NAE register.
 *       if 0, uses the polynomial in RX_CRC_POLY0 of NAE register.
 * fcoe - If this is 1, crc calculation starts from 'startCRC' and the CRC
 * engine ends calculation before the last byte.
 * cbm - if 1, enables crc byte mirroring, where bits within a byte will get
 * reversed (mirrored) during calculation of crc.
 * cfi - If 1, performs a final inversion of crc before comarison is done during
 * pkt reception.
 * startcrc - This field is always required for both FCoE and SCTP crc.
 * endcrc - This information needs to be setup only for SCTP. For FCoE this
 * information is provided by hardware.
 * valid - if set to 1, CRC status is placed into bit 2 of rx descriptor
 *         if set to 0, TCP checksum status is placed into bit 2 of rx descriptor
 * keysize - defines the number of bytes in the pre-pad that contains the key
 */
static __inline__ void
nlm_ucore_crc_setup(int crcpos, int cps, int cfi, int cbm, int fcoe,
    int keysize, int valid, int startcrc, int endcrc)
{
	unsigned int val = 0;

	val |= ((cfi & 0x1) << 20);
	val |= ((cbm & 0x1) << 19);
	val |= ((fcoe & 0x1) << 18);
	val |= ((cps & 0x1) << 16);
	val |= (crcpos & 0xffff);

	nlm_write_ucore_crcpos(val);

	val = 0;
	val |= ((keysize & 0x3f) << 25);
	val |= ((valid & 0x1) << 24);
	val |= ((endcrc & 0xffff) << 8);
	val |= (startcrc & 0xff);

	nlm_write_ucore_crcinfo(val);
}

/* This function returns a fifo empty vector, where each bit provides
 * the status of a fifo pool, where if the pool is empty the bit gets
 * set to 1.
 */
static __inline__ int
nlm_ucore_get_fifoempty(unsigned int fifoempty)
{
	return (fifoempty & 0xfffff);
}

/* This function controls how POE will distribute the packet.
 * pdm - is the packet distribution mode, where
 *       0x0 - means packet distribution mode is not used
 *       0x1 - means forwarding based on destination only (no enqueue)
 *       0x2 - means forwarding based on FID and distr vector (enqueue)
 *       0x3 - means forwarding based on dest and FID (enqueue)
 *       0x4 - means forwarding based on distr vec (no enqueue)
 *       0x6 - means forward based on FID (enqueue), distr vec and serial mode
 *       0x7 - means forward based on FID (enqueue), dest and serial mode
 * mc3 - If 1, then the 3 most significant bits of distribution list are taken
 * from context->class_table
 * pdl - poe distribution list
 * dest - fixed destination setup
 * hash - if 1, use hash based destination
 */
static __inline__ void
nlm_ucore_setup_poepktdistr(int pdm, int mc3, int pdl, int dest, int hash)
{
	unsigned int val = 0;

	val |= ((hash & 0x1) << 20);
	val |= ((dest & 0xfff) << 8);
	val |= ((pdl & 0xf) << 4);
	val |= ((mc3 & 0x1) << 3);
	val |= (pdm & 0x7);

	nlm_write_ucore_pktdistr(val);
}

#endif
