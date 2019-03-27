/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Sam Leffler, Errno Consulting
 * Copyright (c) 2003 Global Technology Associates, Inc.
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
#ifndef _SAFE_SAFEREG_H_
#define	_SAFE_SAFEREG_H_

/*
 * Register definitions for SafeNet SafeXcel-1141 crypto device.
 * Definitions from revision 1.3 (Nov 6 2002) of the User's Manual.
 */

#define BS_BAR			0x10	/* DMA base address register */
#define	BS_TRDY_TIMEOUT		0x40	/* TRDY timeout */
#define	BS_RETRY_TIMEOUT	0x41	/* DMA retry timeout */

#define	PCI_VENDOR_SAFENET	0x16ae		/* SafeNet, Inc. */

/* SafeNet */
#define	PCI_PRODUCT_SAFEXCEL	0x1141		/* 1141 */

#define	SAFE_PE_CSR		0x0000	/* Packet Enginge Ctrl/Status */
#define	SAFE_PE_SRC		0x0004	/* Packet Engine Source */
#define	SAFE_PE_DST		0x0008	/* Packet Engine Destination */
#define	SAFE_PE_SA		0x000c	/* Packet Engine SA */
#define	SAFE_PE_LEN		0x0010	/* Packet Engine Length */
#define	SAFE_PE_DMACFG		0x0040	/* Packet Engine DMA Configuration */
#define	SAFE_PE_DMASTAT		0x0044	/* Packet Engine DMA Status */
#define	SAFE_PE_PDRBASE		0x0048	/* Packet Engine Descriptor Ring Base */
#define	SAFE_PE_RDRBASE		0x004c	/* Packet Engine Result Ring Base */
#define	SAFE_PE_RINGCFG		0x0050	/* Packet Engine Ring Configuration */
#define	SAFE_PE_RINGPOLL	0x0054	/* Packet Engine Ring Poll */
#define	SAFE_PE_IRNGSTAT	0x0058	/* Packet Engine Internal Ring Status */
#define	SAFE_PE_ERNGSTAT	0x005c	/* Packet Engine External Ring Status */
#define	SAFE_PE_IOTHRESH	0x0060	/* Packet Engine I/O Threshold */
#define	SAFE_PE_GRNGBASE	0x0064	/* Packet Engine Gather Ring Base */
#define	SAFE_PE_SRNGBASE	0x0068	/* Packet Engine Scatter Ring Base */
#define	SAFE_PE_PARTSIZE	0x006c	/* Packet Engine Particlar Ring Size */
#define	SAFE_PE_PARTCFG		0x0070	/* Packet Engine Particle Ring Config */
#define	SAFE_CRYPTO_CTRL	0x0080	/* Crypto Control */
#define	SAFE_DEVID		0x0084	/* Device ID */
#define	SAFE_DEVINFO		0x0088	/* Device Info */
#define	SAFE_HU_STAT		0x00a0	/* Host Unmasked Status */
#define	SAFE_HM_STAT		0x00a4	/* Host Masked Status (read-only) */
#define	SAFE_HI_CLR		0x00a4	/* Host Clear Interrupt (write-only) */
#define	SAFE_HI_MASK		0x00a8	/* Host Mask Control */
#define	SAFE_HI_CFG		0x00ac	/* Interrupt Configuration */
#define	SAFE_HI_RD_DESCR	0x00b4	/* Force Descriptor Read */
#define	SAFE_HI_DESC_CNT	0x00b8	/* Host Descriptor Done Count */
#define	SAFE_DMA_ENDIAN		0x00c0	/* Master Endian Status */
#define	SAFE_DMA_SRCADDR	0x00c4	/* DMA Source Address Status */
#define	SAFE_DMA_DSTADDR	0x00c8	/* DMA Destination Address Status */
#define	SAFE_DMA_STAT		0x00cc	/* DMA Current Status */
#define	SAFE_DMA_CFG		0x00d4	/* DMA Configuration/Status */
#define	SAFE_ENDIAN		0x00e0	/* Endian Configuration */
#define	SAFE_PK_A_ADDR		0x0800	/* Public Key A Address */
#define	SAFE_PK_B_ADDR		0x0804	/* Public Key B Address */
#define	SAFE_PK_C_ADDR		0x0808	/* Public Key C Address */
#define	SAFE_PK_D_ADDR		0x080c	/* Public Key D Address */
#define	SAFE_PK_A_LEN		0x0810	/* Public Key A Length */
#define	SAFE_PK_B_LEN		0x0814	/* Public Key B Length */
#define	SAFE_PK_SHIFT		0x0818	/* Public Key Shift */
#define	SAFE_PK_FUNC		0x081c	/* Public Key Function */
#define	SAFE_RNG_OUT		0x0100	/* RNG Output */
#define	SAFE_RNG_STAT		0x0104	/* RNG Status */
#define	SAFE_RNG_CTRL		0x0108	/* RNG Control */
#define	SAFE_RNG_A		0x010c	/* RNG A */
#define	SAFE_RNG_B		0x0110	/* RNG B */
#define	SAFE_RNG_X_LO		0x0114	/* RNG X [31:0] */
#define	SAFE_RNG_X_MID		0x0118	/* RNG X [63:32] */
#define	SAFE_RNG_X_HI		0x011c	/* RNG X [80:64] */
#define	SAFE_RNG_X_CNTR		0x0120	/* RNG Counter */
#define	SAFE_RNG_ALM_CNT	0x0124	/* RNG Alarm Count */
#define	SAFE_RNG_CNFG		0x0128	/* RNG Configuration */
#define	SAFE_RNG_LFSR1_LO	0x012c	/* RNG LFSR1 [31:0] */
#define	SAFE_RNG_LFSR1_HI	0x0130	/* RNG LFSR1 [47:32] */
#define	SAFE_RNG_LFSR2_LO	0x0134	/* RNG LFSR1 [31:0] */
#define	SAFE_RNG_LFSR2_HI	0x0138	/* RNG LFSR1 [47:32] */

#define	SAFE_PE_CSR_READY	0x00000001	/* ready for processing */
#define	SAFE_PE_CSR_DONE	0x00000002	/* h/w completed processing */
#define	SAFE_PE_CSR_LOADSA	0x00000004	/* load SA digests */
#define	SAFE_PE_CSR_HASHFINAL	0x00000010	/* do hash pad & write result */
#define	SAFE_PE_CSR_SABUSID	0x000000c0	/* bus id for SA */
#define	SAFE_PE_CSR_SAPCI	0x00000040	/* PCI bus id for SA */
#define	SAFE_PE_CSR_NXTHDR	0x0000ff00	/* next hdr value for IPsec */
#define	SAFE_PE_CSR_FPAD	0x0000ff00	/* fixed pad for basic ops */
#define	SAFE_PE_CSR_STATUS	0x00ff0000	/* operation result status */
#define	SAFE_PE_CSR_AUTH_FAIL	0x00010000	/* ICV mismatch (inbound) */
#define	SAFE_PE_CSR_PAD_FAIL	0x00020000	/* pad verify fail (inbound) */
#define	SAFE_PE_CSR_SEQ_FAIL	0x00040000	/* sequence number (inbound) */
#define	SAFE_PE_CSR_XERROR	0x00080000	/* extended error follows */
#define	SAFE_PE_CSR_XECODE	0x00f00000	/* extended error code */
#define	SAFE_PE_CSR_XECODE_S	20
#define	SAFE_PE_CSR_XECODE_BADCMD	0	/* invalid command */
#define	SAFE_PE_CSR_XECODE_BADALG	1	/* invalid algorithm */
#define	SAFE_PE_CSR_XECODE_ALGDIS	2	/* algorithm disabled */
#define	SAFE_PE_CSR_XECODE_ZEROLEN	3	/* zero packet length */
#define	SAFE_PE_CSR_XECODE_DMAERR	4	/* bus DMA error */
#define	SAFE_PE_CSR_XECODE_PIPEABORT	5	/* secondary bus DMA error */
#define	SAFE_PE_CSR_XECODE_BADSPI	6	/* IPsec SPI mismatch */
#define	SAFE_PE_CSR_XECODE_TIMEOUT	10	/* failsafe timeout */
#define	SAFE_PE_CSR_PAD		0xff000000	/* ESP padding control/status */
#define	SAFE_PE_CSR_PAD_MIN	0x00000000	/* minimum IPsec padding */
#define	SAFE_PE_CSR_PAD_16	0x08000000	/* pad to 16-byte boundary */
#define	SAFE_PE_CSR_PAD_32	0x10000000	/* pad to 32-byte boundary */
#define	SAFE_PE_CSR_PAD_64	0x20000000	/* pad to 64-byte boundary */
#define	SAFE_PE_CSR_PAD_128	0x40000000	/* pad to 128-byte boundary */
#define	SAFE_PE_CSR_PAD_256	0x80000000	/* pad to 256-byte boundary */

/*
 * Check the CSR to see if the PE has returned ownership to
 * the host.  Note that before processing a descriptor this
 * must be done followed by a check of the SAFE_PE_LEN register
 * status bits to avoid premature processing of a descriptor
 * on its way back to the host.
 */
#define	SAFE_PE_CSR_IS_DONE(_csr) \
    (((_csr) & (SAFE_PE_CSR_READY | SAFE_PE_CSR_DONE)) == SAFE_PE_CSR_DONE)

#define	SAFE_PE_LEN_LENGTH	0x000fffff	/* total length (bytes) */
#define	SAFE_PE_LEN_READY	0x00400000	/* ready for processing */
#define	SAFE_PE_LEN_DONE	0x00800000	/* h/w completed processing */
#define	SAFE_PE_LEN_BYPASS	0xff000000	/* bypass offset (bytes) */
#define	SAFE_PE_LEN_BYPASS_S	24

#define	SAFE_PE_LEN_IS_DONE(_len) \
    (((_len) & (SAFE_PE_LEN_READY | SAFE_PE_LEN_DONE)) == SAFE_PE_LEN_DONE)

/* NB: these apply to HU_STAT, HM_STAT, HI_CLR, and HI_MASK */
#define	SAFE_INT_PE_CDONE	0x00000002	/* PE context done */
#define	SAFE_INT_PE_DDONE	0x00000008	/* PE descriptor done */
#define	SAFE_INT_PE_ERROR	0x00000010	/* PE error */
#define	SAFE_INT_PE_ODONE	0x00000020	/* PE operation done */

#define	SAFE_HI_CFG_PULSE	0x00000001	/* use pulse interrupt */
#define	SAFE_HI_CFG_LEVEL	0x00000000	/* use level interrupt */
#define	SAFE_HI_CFG_AUTOCLR	0x00000002	/* auto-clear pulse interrupt */

#define	SAFE_ENDIAN_PASS	0x000000e4	/* straight pass-thru */
#define	SAFE_ENDIAN_SWAB	0x0000001b	/* swap bytes in 32-bit word */

#define	SAFE_PE_DMACFG_PERESET	0x00000001	/* reset packet engine */
#define	SAFE_PE_DMACFG_PDRRESET	0x00000002	/* reset PDR counters/ptrs */
#define	SAFE_PE_DMACFG_SGRESET	0x00000004	/* reset scatter/gather cache */
#define	SAFE_PE_DMACFG_FSENA	0x00000008	/* enable failsafe reset */
#define	SAFE_PE_DMACFG_PEMODE	0x00000100	/* packet engine mode */
#define	SAFE_PE_DMACFG_SAPREC	0x00000200	/* SA precedes packet */
#define	SAFE_PE_DMACFG_PKFOLL	0x00000400	/* packet follows descriptor */
#define	SAFE_PE_DMACFG_GPRBID	0x00003000	/* gather particle ring busid */
#define	SAFE_PE_DMACFG_GPRPCI	0x00001000	/* PCI gather particle ring */
#define	SAFE_PE_DMACFG_SPRBID	0x0000c000	/* scatter part. ring busid */
#define	SAFE_PE_DMACFG_SPRPCI	0x00004000	/* PCI scatter part. ring */
#define	SAFE_PE_DMACFG_ESDESC	0x00010000	/* endian swap descriptors */
#define	SAFE_PE_DMACFG_ESSA	0x00020000	/* endian swap SA data */
#define	SAFE_PE_DMACFG_ESPACKET	0x00040000	/* endian swap packet data */
#define	SAFE_PE_DMACFG_ESPDESC	0x00080000	/* endian swap particle desc. */
#define	SAFE_PE_DMACFG_NOPDRUP	0x00100000	/* supp. PDR ownership update */
#define	SAFE_PD_EDMACFG_PCIMODE	0x01000000	/* PCI target mode */

#define	SAFE_PE_DMASTAT_PEIDONE	0x00000001	/* PE core input done */
#define	SAFE_PE_DMASTAT_PEODONE	0x00000002	/* PE core output done */
#define	SAFE_PE_DMASTAT_ENCDONE	0x00000004	/* encryption done */
#define	SAFE_PE_DMASTAT_IHDONE	0x00000008	/* inner hash done */
#define	SAFE_PE_DMASTAT_OHDONE	0x00000010	/* outer hash (HMAC) done */
#define	SAFE_PE_DMASTAT_PADFLT	0x00000020	/* crypto pad fault */
#define	SAFE_PE_DMASTAT_ICVFLT	0x00000040	/* ICV fault */
#define	SAFE_PE_DMASTAT_SPIMIS	0x00000080	/* SPI mismatch */
#define	SAFE_PE_DMASTAT_CRYPTO	0x00000100	/* crypto engine timeout */
#define	SAFE_PE_DMASTAT_CQACT	0x00000200	/* command queue active */
#define	SAFE_PE_DMASTAT_IRACT	0x00000400	/* input request active */
#define	SAFE_PE_DMASTAT_ORACT	0x00000800	/* output request active */
#define	SAFE_PE_DMASTAT_PEISIZE	0x003ff000	/* PE input size:32-bit words */
#define	SAFE_PE_DMASTAT_PEOSIZE	0xffc00000	/* PE out. size:32-bit words */

#define	SAFE_PE_RINGCFG_SIZE	0x000003ff	/* ring size (descriptors) */
#define	SAFE_PE_RINGCFG_OFFSET	0xffff0000	/* offset btw desc's (dwords) */
#define	SAFE_PE_RINGCFG_OFFSET_S	16

#define	SAFE_PE_RINGPOLL_POLL	0x00000fff	/* polling frequency/divisor */
#define	SAFE_PE_RINGPOLL_RETRY	0x03ff0000	/* polling frequency/divisor */
#define	SAFE_PE_RINGPOLL_CONT	0x80000000	/* continuously poll */

#define	SAFE_PE_IRNGSTAT_CQAVAIL 0x00000001	/* command queue available */

#define	SAFE_PE_ERNGSTAT_NEXT	0x03ff0000	/* index of next packet desc. */
#define	SAFE_PE_ERNGSTAT_NEXT_S	16

#define	SAFE_PE_IOTHRESH_INPUT	0x000003ff	/* input threshold (dwords) */
#define	SAFE_PE_IOTHRESH_OUTPUT	0x03ff0000	/* output threshold (dwords) */

#define	SAFE_PE_PARTCFG_SIZE	0x0000ffff	/* scatter particle size */
#define	SAFE_PE_PARTCFG_GBURST	0x00030000	/* gather particle burst */
#define	SAFE_PE_PARTCFG_GBURST_2	0x00000000
#define	SAFE_PE_PARTCFG_GBURST_4	0x00010000
#define	SAFE_PE_PARTCFG_GBURST_8	0x00020000
#define	SAFE_PE_PARTCFG_GBURST_16	0x00030000
#define	SAFE_PE_PARTCFG_SBURST	0x000c0000	/* scatter particle burst */
#define	SAFE_PE_PARTCFG_SBURST_2	0x00000000
#define	SAFE_PE_PARTCFG_SBURST_4	0x00040000
#define	SAFE_PE_PARTCFG_SBURST_8	0x00080000
#define	SAFE_PE_PARTCFG_SBURST_16	0x000c0000

#define	SAFE_PE_PARTSIZE_SCAT	0xffff0000	/* scatter particle ring size */
#define	SAFE_PE_PARTSIZE_GATH	0x0000ffff	/* gather particle ring size */

#define	SAFE_CRYPTO_CTRL_3DES	0x00000001	/* enable 3DES support */
#define	SAFE_CRYPTO_CTRL_PKEY	0x00010000	/* enable public key support */
#define	SAFE_CRYPTO_CTRL_RNG	0x00020000	/* enable RNG support */

#define	SAFE_DEVINFO_REV_MIN	0x0000000f	/* minor rev for chip */
#define	SAFE_DEVINFO_REV_MAJ	0x000000f0	/* major rev for chip */
#define	SAFE_DEVINFO_REV_MAJ_S	4
#define	SAFE_DEVINFO_DES	0x00000100	/* DES/3DES support present */
#define	SAFE_DEVINFO_ARC4	0x00000200	/* ARC4 support present */
#define	SAFE_DEVINFO_AES	0x00000400	/* AES support present */
#define	SAFE_DEVINFO_MD5	0x00001000	/* MD5 support present */
#define	SAFE_DEVINFO_SHA1	0x00002000	/* SHA-1 support present */
#define	SAFE_DEVINFO_RIPEMD	0x00004000	/* RIPEMD support present */
#define	SAFE_DEVINFO_DEFLATE	0x00010000	/* Deflate support present */
#define	SAFE_DEVINFO_SARAM	0x00100000	/* on-chip SA RAM present */
#define	SAFE_DEVINFO_EMIBUS	0x00200000	/* EMI bus present */
#define	SAFE_DEVINFO_PKEY	0x00400000	/* public key support present */
#define	SAFE_DEVINFO_RNG	0x00800000	/* RNG present */

#define	SAFE_REV(_maj, _min)	(((_maj) << SAFE_DEVINFO_REV_MAJ_S) | (_min))
#define	SAFE_REV_MAJ(_chiprev) \
	(((_chiprev) & SAFE_DEVINFO_REV_MAJ) >> SAFE_DEVINFO_REV_MAJ_S)
#define	SAFE_REV_MIN(_chiprev)	((_chiprev) & SAFE_DEVINFO_REV_MIN)

#define	SAFE_PK_FUNC_MULT	0x00000001	/* Multiply function */
#define	SAFE_PK_FUNC_SQUARE	0x00000004	/* Square function */
#define	SAFE_PK_FUNC_ADD	0x00000010	/* Add function */
#define	SAFE_PK_FUNC_SUB	0x00000020	/* Subtract function */
#define	SAFE_PK_FUNC_LSHIFT	0x00000040	/* Left-shift function */
#define	SAFE_PK_FUNC_RSHIFT	0x00000080	/* Right-shift function */
#define	SAFE_PK_FUNC_DIV	0x00000100	/* Divide function */
#define	SAFE_PK_FUNC_CMP	0x00000400	/* Compare function */
#define	SAFE_PK_FUNC_COPY	0x00000800	/* Copy function */
#define	SAFE_PK_FUNC_EXP16	0x00002000	/* Exponentiate (4-bit ACT) */
#define	SAFE_PK_FUNC_EXP4	0x00004000	/* Exponentiate (2-bit ACT) */

#define	SAFE_RNG_STAT_BUSY	0x00000001	/* busy, data not valid */

#define	SAFE_RNG_CTRL_PRE_LFSR	0x00000001	/* enable output pre-LFSR */
#define	SAFE_RNG_CTRL_TST_MODE	0x00000002	/* enable test mode */
#define	SAFE_RNG_CTRL_TST_RUN	0x00000004	/* start test state machine */
#define	SAFE_RNG_CTRL_ENA_RING1	0x00000008	/* test entropy oscillator #1 */
#define	SAFE_RNG_CTRL_ENA_RING2	0x00000010	/* test entropy oscillator #2 */
#define	SAFE_RNG_CTRL_DIS_ALARM	0x00000020	/* disable RNG alarm reports */
#define	SAFE_RNG_CTRL_TST_CLOCK	0x00000040	/* enable test clock */
#define	SAFE_RNG_CTRL_SHORTEN	0x00000080	/* shorten state timers */
#define	SAFE_RNG_CTRL_TST_ALARM	0x00000100	/* simulate alarm state */
#define	SAFE_RNG_CTRL_RST_LFSR	0x00000200	/* reset LFSR */

/*
 * Packet engine descriptor.  Note that d_csr is a copy of the
 * SAFE_PE_CSR register and all definitions apply, and d_len
 * is a copy of the SAFE_PE_LEN register and all definitions apply.
 * d_src and d_len may point directly to contiguous data or to a
 * list of ``particle descriptors'' when using scatter/gather i/o.
 */
struct safe_desc {
	u_int32_t	d_csr;			/* per-packet control/status */
	u_int32_t	d_src;			/* source address */
	u_int32_t	d_dst;			/* destination address */
	u_int32_t	d_sa;			/* SA address */
	u_int32_t	d_len;			/* length, bypass, status */
};

/*
 * Scatter/Gather particle descriptor.
 *
 * NB: scatter descriptors do not specify a size; this is fixed
 *     by the setting of the SAFE_PE_PARTCFG register.
 */
struct safe_pdesc {
	u_int32_t	pd_addr;		/* particle address */
	u_int16_t	pd_flags;		/* control word */
	u_int16_t	pd_size;		/* particle size (bytes) */
};

#define	SAFE_PD_READY	0x0001			/* ready for processing */
#define	SAFE_PD_DONE	0x0002			/* h/w completed processing */

/*
 * Security Association (SA) Record (Rev 1).  One of these is
 * required for each operation processed by the packet engine.
 */
struct safe_sarec {
	u_int32_t	sa_cmd0;
	u_int32_t	sa_cmd1;
	u_int32_t	sa_resv0;
	u_int32_t	sa_resv1;
	u_int32_t	sa_key[8];		/* DES/3DES/AES key */
	u_int32_t	sa_indigest[5];		/* inner digest */
	u_int32_t	sa_outdigest[5];	/* outer digest */
	u_int32_t	sa_spi;			/* SPI */
	u_int32_t	sa_seqnum;		/* sequence number */
	u_int32_t	sa_seqmask[2];		/* sequence number mask */
	u_int32_t	sa_resv2;
	u_int32_t	sa_staterec;		/* address of state record */
	u_int32_t	sa_resv3[2];
	u_int32_t	sa_samgmt0;		/* SA management field 0 */
	u_int32_t	sa_samgmt1;		/* SA management field 0 */
};

#define	SAFE_SA_CMD0_OP		0x00000007	/* operation code */
#define	SAFE_SA_CMD0_OP_CRYPT	0x00000000	/* encrypt/decrypt (basic) */
#define	SAFE_SA_CMD0_OP_BOTH	0x00000001	/* encrypt-hash/hash-decrypto */
#define	SAFE_SA_CMD0_OP_HASH	0x00000003	/* hash (outbound-only) */
#define	SAFE_SA_CMD0_OP_ESP	0x00000000	/* ESP in/out (proto) */
#define	SAFE_SA_CMD0_OP_AH	0x00000001	/* AH in/out (proto) */
#define	SAFE_SA_CMD0_INBOUND	0x00000008	/* inbound operation */
#define	SAFE_SA_CMD0_OUTBOUND	0x00000000	/* outbound operation */
#define	SAFE_SA_CMD0_GROUP	0x00000030	/* operation group */
#define	SAFE_SA_CMD0_BASIC	0x00000000	/* basic operation */
#define	SAFE_SA_CMD0_PROTO	0x00000010	/* protocol/packet operation */
#define	SAFE_SA_CMD0_BUNDLE	0x00000020	/* bundled operation (resvd) */
#define	SAFE_SA_CMD0_PAD	0x000000c0	/* crypto pad method */
#define	SAFE_SA_CMD0_PAD_IPSEC	0x00000000	/* IPsec padding */
#define	SAFE_SA_CMD0_PAD_PKCS7	0x00000040	/* PKCS#7 padding */
#define	SAFE_SA_CMD0_PAD_CONS	0x00000080	/* constant padding */
#define	SAFE_SA_CMD0_PAD_ZERO	0x000000c0	/* zero padding */
#define	SAFE_SA_CMD0_CRYPT_ALG	0x00000f00	/* symmetric crypto algorithm */
#define	SAFE_SA_CMD0_DES	0x00000000	/* DES crypto algorithm */
#define	SAFE_SA_CMD0_3DES	0x00000100	/* 3DES crypto algorithm */
#define	SAFE_SA_CMD0_AES	0x00000300	/* AES crypto algorithm */
#define	SAFE_SA_CMD0_CRYPT_NULL	0x00000f00	/* null crypto algorithm */
#define	SAFE_SA_CMD0_HASH_ALG	0x0000f000	/* hash algorithm */
#define	SAFE_SA_CMD0_MD5	0x00000000	/* MD5 hash algorithm */
#define	SAFE_SA_CMD0_SHA1	0x00001000	/* SHA-1 hash algorithm */
#define	SAFE_SA_CMD0_HASH_NULL	0x0000f000	/* null hash algorithm */
#define	SAFE_SA_CMD0_HDR_PROC	0x00080000	/* header processing */
#define	SAFE_SA_CMD0_IBUSID	0x00300000	/* input bus id */
#define	SAFE_SA_CMD0_IPCI	0x00100000	/* PCI input bus id */
#define	SAFE_SA_CMD0_OBUSID	0x00c00000	/* output bus id */
#define	SAFE_SA_CMD0_OPCI	0x00400000	/* PCI output bus id */
#define	SAFE_SA_CMD0_IVLD	0x03000000	/* IV loading */
#define	SAFE_SA_CMD0_IVLD_NONE	0x00000000	/* IV no load (reuse) */
#define	SAFE_SA_CMD0_IVLD_IBUF	0x01000000	/* IV load from input buffer */
#define	SAFE_SA_CMD0_IVLD_STATE	0x02000000	/* IV load from state */
#define	SAFE_SA_CMD0_HSLD	0x0c000000	/* hash state loading */
#define	SAFE_SA_CMD0_HSLD_SA	0x00000000	/* hash state load from SA */
#define	SAFE_SA_CMD0_HSLD_STATE	0x08000000	/* hash state load from state */
#define	SAFE_SA_CMD0_HSLD_NONE	0x0c000000	/* hash state no load */
#define	SAFE_SA_CMD0_SAVEIV	0x10000000	/* save IV */
#define	SAFE_SA_CMD0_SAVEHASH	0x20000000	/* save hash state */
#define	SAFE_SA_CMD0_IGATHER	0x40000000	/* input gather */
#define	SAFE_SA_CMD0_OSCATTER	0x80000000	/* output scatter */

#define	SAFE_SA_CMD1_HDRCOPY	0x00000002	/* copy header to output */
#define	SAFE_SA_CMD1_PAYCOPY	0x00000004	/* copy payload to output */
#define	SAFE_SA_CMD1_PADCOPY	0x00000008	/* copy pad to output */
#define	SAFE_SA_CMD1_IPV4	0x00000000	/* IPv4 protocol */
#define	SAFE_SA_CMD1_IPV6	0x00000010	/* IPv6 protocol */
#define	SAFE_SA_CMD1_MUTABLE	0x00000020	/* mutable bit processing */
#define	SAFE_SA_CMD1_SRBUSID	0x000000c0	/* state record bus id */
#define	SAFE_SA_CMD1_SRPCI	0x00000040	/* state record from PCI */
#define	SAFE_SA_CMD1_CRMODE	0x00000300	/* crypto mode */
#define	SAFE_SA_CMD1_ECB	0x00000000	/* ECB crypto mode */
#define	SAFE_SA_CMD1_CBC	0x00000100	/* CBC crypto mode */
#define	SAFE_SA_CMD1_OFB	0x00000200	/* OFB crypto mode */
#define	SAFE_SA_CMD1_CFB	0x00000300	/* CFB crypto mode */
#define	SAFE_SA_CMD1_CRFEEDBACK	0x00000c00	/* crypto feedback mode */
#define	SAFE_SA_CMD1_64BIT	0x00000000	/* 64-bit crypto feedback */
#define	SAFE_SA_CMD1_8BIT	0x00000400	/* 8-bit crypto feedback */
#define	SAFE_SA_CMD1_1BIT	0x00000800	/* 1-bit crypto feedback */
#define	SAFE_SA_CMD1_128BIT	0x00000c00	/* 128-bit crypto feedback */
#define	SAFE_SA_CMD1_OPTIONS	0x00001000	/* HMAC/options mutable bit */
#define	SAFE_SA_CMD1_HMAC	SAFE_SA_CMD1_OPTIONS
#define	SAFE_SA_CMD1_SAREV1	0x00008000	/* SA Revision 1 */
#define	SAFE_SA_CMD1_OFFSET	0x00ff0000	/* hash/crypto offset(dwords) */
#define	SAFE_SA_CMD1_OFFSET_S	16
#define	SAFE_SA_CMD1_AESKEYLEN	0x0f000000	/* AES key length */
#define	SAFE_SA_CMD1_AES128	0x02000000	/* 128-bit AES key */
#define	SAFE_SA_CMD1_AES192	0x03000000	/* 192-bit AES key */
#define	SAFE_SA_CMD1_AES256	0x04000000	/* 256-bit AES key */

/* 
 * Security Associate State Record (Rev 1).
 */
struct safe_sastate {
	u_int32_t	sa_saved_iv[4];		/* saved IV (DES/3DES/AES) */
	u_int32_t	sa_saved_hashbc;	/* saved hash byte count */
	u_int32_t	sa_saved_indigest[5];	/* saved inner digest */
};
#endif /* _SAFE_SAFEREG_H_ */
