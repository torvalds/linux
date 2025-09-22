/*	$OpenBSD: siopreg.h,v 1.12 2010/07/23 07:47:13 jsg Exp $ */
/*	$NetBSD: siopreg.h,v 1.16 2005/02/27 00:27:02 perry Exp $	*/

/*
 * Copyright (c) 2000 Manuel Bouyer.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Devices definitions for Symbios/NCR M53c8xx PCI-SCSI I/O Processors
 * Docs available from http://www.symbios.com/
 */

#define SIOP_SCNTL0 	0x00 /* SCSI control 0, R/W */
#define SCNTL0_ARB_MASK	0xc0
#define SCNTL0_SARB	0x00
#define SCNTL0_FARB	0xc0
#define SCNTL0_START	0x20
#define SCNTL0_WATM	0x10
#define SCNTL0_EPC	0x08
#define SCNTL0_AAP	0x02
#define SCNTL0_TRG	0x01

#define SIOP_SCNTL1 	0x01 /* SCSI control 1, R/W */
#define SCNTL1_EXC	0x80
#define SCNTL1_ADB	0x40
#define SCNTL1_DHP	0x20
#define SCNTL1_CON	0x10
#define SCNTL1_RST	0x08
#define SCNTL1_AESP	0x04
#define SCNTL1_IARB	0x02
#define SCNTL1_SST	0x01

#define SIOP_SCNTL2 	0x02 /* SCSI control 2, R/W */
#define SCNTL2_SDU	0x80
#define SCNTL2_CHM	0x40	/* 875 only */
#define SCNTL2_SLPMD	0x20	/* 875 only */
#define SCNTL2_SLPHBEN	0x10	/* 875 only */
#define SCNTL2_WSS	0x08	/* 875 only */
#define SCNTL2_VUE0	0x04	/* 875 only */
#define SCNTL2_VUE1	0x02	/* 875 only */
#define SCNTL2_WSR	0x01	/* 875 only */

#define SIOP_SCNTL3 	0x03 /* SCSI control 3, R/W */
#define SCNTL3_ULTRA	0x80	/* 875 only */
#define SCNTL3_SCF_SHIFT 4
#define SCNTL3_SCF_MASK	0x70
#define SCNTL3_EWS	0x08	/* 875 only */
#define SCNTL3_CCF_SHIFT 0
#define SCNTL3_CCF_MASK	0x07

/* periods for various SCF values, assume transfer period of 4 */
struct scf_period {
	int clock; /* clock period (ns * 10) */
	int period; /* scsi period, as set in the SDTR message */
	int scf; /* scf value to use */
};

#ifdef SIOP_NEEDS_PERIOD_TABLES
static const struct scf_period scf_period[] = {
	{250, 25, 1}, /* 10.0 MHz */
	{250, 37, 2}, /* 6.67 MHz */
	{250, 50, 3},  /* 5.00 MHz */
	{250, 75, 4},  /* 3.33 MHz */
	{125, 12, 1},  /* 20.0 MHz */
	{125, 18, 2},  /* 13.3 MHz */
	{125, 25, 3},  /* 10.0 MHz */
	{125, 37, 4},  /* 6.67 MHz */
	{125, 50, 5},  /* 5.0 MHz */
	{ 62, 10, 1},  /* 40.0 MHz */
	{ 62, 12, 3},  /* 20.0 MHz */
	{ 62, 18, 4},  /* 13.3 MHz */
	{ 62, 25, 5},  /* 10.0 MHz */
};

static const struct scf_period dt_scf_period[] = {
	{ 62,  9, 1},  /* 80.0 MHz */
	{ 62, 10, 3},  /* 40.0 MHz */
	{ 62, 12, 5},  /* 20.0 MHz */
	{ 62, 18, 6},  /* 13.3 MHz */
	{ 62, 25, 7},  /* 10.0 MHz */
};
#endif

#define SIOP_SCID	0x04 /* SCSI chip ID R/W */
#define SCID_RRE	0x40
#define SCID_SRE	0x20
#define SCID_ENCID_SHIFT 0
#define SCID_ENCID_MASK	0x07

#define SIOP_SXFER	0x05 /* SCSI transfer, R/W */
#define SXFER_TP_SHIFT	 5
#define SXFER_TP_MASK	0xe0
#define SXFER_MO_SHIFT  0
#define SXFER_MO_MASK  0x3f

#define SIOP_SDID	0x06 /* SCSI destination ID, R/W */
#define SDID_ENCID_SHIFT 0
#define SDID_ENCID_MASK	0x07

#define SIOP_GPREG	0x07 /* General purpose, R/W */
#define GPREG_GPIO4	0x10	/* 875 only */
#define GPREG_GPIO3	0x08	/* 875 only */
#define GPREG_GPIO2	0x04	/* 875 only */
#define GPREG_GPIO1	0x02
#define GPREG_GPIO0	0x01

#define SIOP_SFBR	0x08 /* SCSI first byte received, R/W */

#define SIOP_SOCL	0x09 /* SCSI output control latch, RW */

#define SIOP_SSID	0x0A /* SCSI selector ID, RO */
#define SSID_VAL	0x80
#define SSID_ENCID_SHIFT 0
#define SSID_ENCID_MASK 0x0f

#define SIOP_SBCL	0x0B /* SCSI control line, RO */

#define SIOP_DSTAT	0x0C /* DMA status, RO */
#define DSTAT_DFE	0x80
#define DSTAT_MDPE	0x40
#define DSTAT_BF	0x20
#define DSTAT_ABRT	0x10
#define DSTAT_SSI	0x08
#define DSTAT_SIR	0x04
#define DSTAT_IID	0x01

#define SIOP_SSTAT0	0x0D /* STSI status 0, RO */
#define SSTAT0_ILF	0x80
#define SSTAT0_ORF	0x40
#define SSTAT0_OLF	0x20
#define SSTAT0_AIP	0x10
#define SSTAT0_LOA	0x08
#define SSTAT0_WOA	0x04
#define SSTAT0_RST	0x02
#define SSTAT0_SDP	0x01

#define SIOP_SSTAT1	0x0E /* STSI status 1, RO */
#define SSTAT1_FFO_SHIFT 4
#define SSTAT1_FFO_MASK 0x80
#define SSTAT1_SDPL	0x08
#define SSTAT1_MSG	0x04
#define SSTAT1_CD	0x02
#define SSTAT1_IO	0x01
#define SSTAT1_PHASE_MASK (SSTAT1_IO | SSTAT1_CD | SSTAT1_MSG)
#define SSTAT1_PHASE_DATAOUT	0
#define SSTAT1_PHASE_DATAIN	SSTAT1_IO
#define SSTAT1_PHASE_CMD	SSTAT1_CD
#define SSTAT1_PHASE_STATUS	(SSTAT1_CD | SSTAT1_IO)
#define SSTAT1_PHASE_MSGOUT	(SSTAT1_MSG | SSTAT1_CD)
#define SSTAT1_PHASE_MSGIN	(SSTAT1_MSG | SSTAT1_CD | SSTAT1_IO)

#define SIOP_SSTAT2	0x0F /* STSI status 2, RO */
#define SSTAT2_ILF1	0x80	/* 875 only */
#define SSTAT2_ORF1	0x40	/* 875 only */
#define SSTAT2_OLF1	0x20	/* 875 only */
#define SSTAT2_FF4	0x10	/* 875 only */
#define SSTAT2_SPL1	0x08	/* 875 only */
#define SSTAT2_DF	0x04	/* 875 only */
#define SSTAT2_LDSC	0x02
#define SSTAT2_SDP1	0x01	/* 875 only */

#define SIOP_DSA	0x10 /* data struct addr, R/W */

#define SIOP_ISTAT	0x14 /* IRQ status, R/W */
#define ISTAT_ABRT	0x80
#define ISTAT_SRST	0x40
#define ISTAT_SIGP	0x20
#define ISTAT_SEM	0x10
#define ISTAT_CON	0x08
#define ISTAT_INTF	0x04
#define ISTAT_SIP	0x02
#define ISTAT_DIP	0x01

#define SIOP_CTEST0	0x18 /* Chip test 0, R/W */
#define CTEST0_EHP	0x04    /* 720/770 */

#define SIOP_CTEST1	0x19 /* Chip test 1, R/W */

#define SIOP_CTEST2	0x1A /* Chip test 2, R/W */
#define CTEST2_SRTCH	0x04	/* 875 only */

#define SIOP_CTEST3	0x1B /* Chip test 3, R/W */
#define CTEST3_FLF	0x08
#define CTEST3_CLF	0x04
#define CTEST3_FM	0x02
#define CTEST3_WRIE	0x01

#define SIOP_TEMP	0x1C /* Temp register (used by CALL/RET), R/W */

#define SIOP_DFIFO	0x20 /* DMA FIFO */

#define SIOP_CTEST4	0x21 /* Chip test 4, R/W */
#define CTEST4_MUX	0x80    /* 720/770 */
#define CTEST4_BDIS	0x80
#define CTEST_ZMOD	0x40
#define CTEST_ZSD	0x20
#define CTEST_SRTM	0x10
#define CTEST_MPEE	0x08

#define SIOP_CTEST5	0x22 /* Chip test 5, R/W */
#define CTEST5_ADCK	0x80
#define CTEST5_BBCK	0x40
#define CTEST5_DFS	0x20
#define CTEST5_MASR	0x10
#define CTEST5_DDIR	0x08
#define CTEST5_BOMASK	0x03

#define SIOP_CTEST6	0x23 /* Chip test 6, R/W */

#define SIOP_DBC	0x24 /* DMA byte counter, R/W */

#define SIOP_DCMD	0x27 /* DMA command, R/W */

#define SIOP_DNAD	0x28 /* DMA next addr, R/W */

#define SIOP_DSP	0x2C /* DMA scripts pointer, R/W */

#define SIOP_DSPS	0x30 /* DMA scripts pointer save, R/W */

#define SIOP_SCRATCHA	0x34 /* scratch register A. R/W */

#define SIOP_DMODE	0x38 /* DMA mode, R/W */
#define DMODE_BL_SHIFT   6
#define DMODE_BL_MASK	0xC0
#define DMODE_SIOM	0x20
#define DMODE_DIOM	0x10
#define DMODE_ERL	0x08
#define DMODE_ERMP	0x04
#define DMODE_BOF	0x02
#define DMODE_MAN	0x01

#define SIOP_DIEN	0x39 /* DMA interrupt enable, R/W */
#define DIEN_MDPE	0x40
#define DIEN_BF		0x20
#define DIEN_AVRT	0x10
#define DIEN_SSI	0x08
#define DIEN_SIR	0x04
#define DIEN_IID	0x01

#define SIOP_SBR	0x3A /* scratch byte register, R/W */

#define SIOP_DCNTL	0x3B /* DMA control, R/W */
#define DCNTL_CLSE	0x80
#define DCNTL_PFF	0x40
#define DCNTL_EA	0x20    /* 720/770 */
#define DCNTL_PFEN	0x20    /* 8xx */
#define DCNTL_SSM	0x10
#define DCNTL_IRQM	0x08
#define DCNTL_STD	0x04
#define DCNTL_IRQD	0x02
#define DCNTL_COM	0x01

#define SIOP_ADDER	0x3C /* adder output sum, RO */

#define SIOP_SIEN0	0x40 /* SCSI interrupt enable 0, R/W */
#define SIEN0_MA	0x80
#define SIEN0_CMP	0x40
#define SIEN0_SEL	0x20
#define SIEN0_RSL	0x10
#define SIEN0_SGE	0x08
#define SIEN0_UDC	0x04
#define SIEN0_SRT	0x02
#define SIEN0_PAR	0x01

#define SIOP_SIEN1	0x41 /* SCSI interrupt enable 1, R/W */
#define SIEN1_SBMC	0x10 /* 895 only */
#define SIEN1_STO	0x04
#define SIEN1_GEN	0x02
#define SIEN1_HTH	0x01

#define SIOP_SIST0	0x42 /* SCSI interrupt status 0, RO */
#define SIST0_MA	0x80
#define SIST0_CMP	0x40
#define SIST0_SEL	0x20
#define SIST0_RSL	0x10
#define SIST0_SGE	0x08
#define SIST0_UDC	0x04
#define SIST0_RST	0x02
#define SIST0_PAR	0x01

#define SIOP_SIST1	0x43 /* SCSI interrupt status 1, RO */
#define SIST1_SBMC	0x10 /* 895 only */
#define SIST1_STO	0x04
#define SIST1_GEN	0x02
#define SIST1_HTH	0x01

#define SIOP_SLPAR	0x44 /* scsi longitudinal parity, R/W */

#define SIOP_SWIDE	0x45 /* scsi wide residue, RW, 875 only */

#define SIOP_MACNTL	0x46 /* memory access control, R/W */

#define SIOP_GPCNTL	0x47 /* General Purpose Pin control, R/W */
#define GPCNTL_ME	0x80	/* 875 only */
#define GPCNTL_FE	0x40	/* 875 only */
#define GPCNTL_IN4	0x10	/* 875 only */
#define GPCNTL_IN3	0x08	/* 875 only */
#define GPCNTL_IN2	0x04	/* 875 only */
#define GPCNTL_IN1	0x02
#define GPCNTL_IN0	0x01

#define SIOP_STIME0	0x48 /* SCSI timer 0, R/W */
#define STIME0_HTH_SHIFT 4
#define STIME0_HTH_MASK	0xf0
#define STIME0_SEL_SHIFT 0
#define STIME0_SEL_MASK	0x0f

#define SIOP_STIME1	0x49 /* SCSI timer 1, R/W */
#define STIME1_HTHBA	0x40	/* 875 only */
#define STIME1_GENSF	0x20	/* 875 only */
#define STIME1_HTHSF	0x10	/* 875 only */
#define STIME1_GEN_SHIFT 0
#define STIME1_GEN_MASK	0x0f

#define SIOP_RESPID0	0x4A /* response ID, R/W */

#define SIOP_RESPID1	0x4B /* response ID, R/W, 875-only */

#define SIOP_STEST0	0x4C /* SCSI test 0, RO */

#define SIOP_STEST1	0x4D /* SCSI test 1, RO, RW on 875 */
#define STEST1_DOGE	0x20	/* 1010 only */
#define STEST1_DIGE	0x10	/* 1010 only */
#define STEST1_DBLEN	0x08	/* 875-only */
#define STEST1_DBLSEL	0x04	/* 875-only */

#define SIOP_STEST2	0x4E /* SCSI test 2, RO, R/W on 875 */
#define STEST2_DIF	0x20	/* 875 only */
#define STEST2_EXT	0x02

#define SIOP_STEST3	0x4F /* SCSI test 3, RO, RW on 875 */
#define STEST3_TE	0x80
#define STEST3_HSC	0x20

#define SIOP_STEST4	0x52 /* SCSI test 4, 895 only */
#define STEST4_MODE_MASK 0xc0
#define STEST4_MODE_DIF	0x40
#define STEST4_MODE_SE	0x80
#define STEST4_MODE_LVD	0xc0
#define STEST4_LOCK	0x20
#define STEST4_

#define SIOP_SIDL	0x50 /* SCSI input data latch, RO */

#define SIOP_SODL	0x54 /* SCSI output data latch, R/W */

#define SIOP_SBDL	0x58 /* SCSI bus data lines, RO */

#define SIOP_SCRATCHB	0x5C /* Scratch register B, R/W */

#define SIOP_SCRATCHC	0x60 /* Scratch register C, R/W, 875 only */

#define SIOP_SCRATCHD	0x64 /* Scratch register D, R/W, 875-only */

#define SIOP_SCRATCHE	0x68 /* Scratch register E, R/W, 875-only */

#define SIOP_SCRATCHF	0x6c /* Scratch register F, R/W, 875-only */

#define SIOP_SCRATCHG	0x70 /* Scratch register G, R/W, 875-only */

#define SIOP_SCRATCHH	0x74 /* Scratch register H, R/W, 875-only */

#define SIOP_SCRATCHI	0x78 /* Scratch register I, R/W, 875-only */

#define SIOP_SCRATCHJ	0x7c /* Scratch register J, R/W, 875-only */

#define SIOP_SCNTL4	0xBC /* SCSI control 4, R/W, 1010-only */
#define SCNTL4_XCLKS_ST	0x01
#define SCNTL4_XCLKS_DT	0x02
#define SCNTL4_XCLKH_ST	0x04
#define SCNTL4_XCLKH_DT	0x08
#define SCNTL4_AIPEN	0x40
#define SCNTL4_U3EN	0x80

#define SIOP_DFBC	0xf0 /* DMA fifo byte count, RO */

#define SIOP_AIPCNTL0	0xbe	/* AIP Control 0, 1010-only */
#define AIPCNTL0_ERRLIVE 0x04	/* AIP error status, live */
#define AIPCNTL0_ERR	0x02	/* AIP error status, latched */
#define AIPCNTL0_PARITYERRs 0x01 /* Parity error */

#define SIOP_AIPCNTL1	0xbf	/* AIP Control 1, 1010-only */
#define AIPCNTL1_DIS	0x08	/* disable AIP generation, 1010-66 only */
#define AIPCNTL1_RSETERR 0x04	/* reset AIP error 1010-66 only */
#define AIPCNTL1_FB	0x02	/* force bad AIP value 1010-66 only */
#define AIPCNTL1_RSET	0x01	/* reset AIP sequence value 1010-66 only */

/*
 * Non-volatile configuration settings stored in the EEPROM.  There
 * are at least two known formats: Symbios Logic format and Tekram format.
 */

#define	SIOP_NVRAM_SYM_SIZE		368
#define	SIOP_NVRAM_SYM_ADDRESS		0x100

struct nvram_symbios {
	/* Header (6 bytes) */
	u_int16_t	type;		/* 0x0000 */
	u_int16_t	byte_count;	/* excluding header/trailer */
	u_int16_t	checksum;

	/* Adapter configuration (20 bytes) */
	u_int8_t	v_major;
	u_int8_t	v_minor;
	u_int32_t	boot_crc;
	u_int16_t	flags;
#define	NVRAM_SYM_F_SCAM_ENABLE		0x0001
#define	NVRAM_SYM_F_PARITY_ENABLE	0x0002
#define	NVRAM_SYM_F_VERBOSE_MESSAGES	0x0004
#define	NVRAM_SYM_F_CHS_MAPPING		0x0008
	u_int16_t	flags1;
#define	NVRAM_SYM_F1_SCAN_HI_LO		0x0001
	u_int16_t	term_state;
#define	NVRAM_SYM_TERM_CANT_PROGRAM	0
#define	NVRAM_SYM_TERM_ENABLED		1
#define	NVRAM_SYM_TERM_DISABLED		2
	u_int16_t	rmvbl_flags;
#define	NVRAM_SYM_RMVBL_NO_SUPPORT	0
#define	NVRAM_SYM_RMVBL_BOOT_DEVICE	1
#define	NVRAM_SYM_RMVBL_MEDIA_INSTALLED	2
	u_int8_t	host_id;
	u_int8_t	num_hba;
	u_int8_t	num_devices;
	u_int8_t	max_scam_devices;
	u_int8_t	num_valid_scam_devices;
	u_int8_t	rsvd;

	/* Boot order (14 bytes x 4) */
	struct nvram_symbios_host {
		u_int16_t	type;		/* 4 - 8xx */
		u_int16_t	device_id;	/* PCI device ID */
		u_int16_t	vendor_id;	/* PCI vendor ID */
		u_int8_t	bus_nr;		/* PCI bus number */
		u_int8_t	device_fn;	/* PCI device/func # << 3 */
		u_int16_t	word8;
		u_int16_t	flags;
#define	NVRAM_SYM_HOST_F_SCAN_AT_BOOT	0x0001
		u_int16_t	io_port;	/* PCI I/O address */
	} __packed host[4];

	/* Targets (8 bytes x 16) */
	struct nvram_symbios_target {
		u_int8_t	flags;
#define	NVRAM_SYM_TARG_F_DISCONNECT_EN	0x0001
#define	NVRAM_SYM_TARG_F_SCAN_AT_BOOT	0x0002
#define	NVRAM_SYM_TARG_F_SCAN_LUNS	0x0004
#define	NVRAM_SYM_TARG_F_TQ_EN		0x0008
		u_int8_t	rsvd;
		u_int8_t	bus_width;
		u_int8_t	sync_offset;	/* 8, 16, etc. */
		u_int16_t	sync_period;	/* 4 * factor */
		u_int16_t	timeout;
	} __packed target[16];

	/* SCAM table (8 bytes x 4) */
	struct nvram_symbios_scam {
		u_int16_t	id;
		u_int16_t	method;
#define	NVRAM_SYM_SCAM_DEFAULT_METHOD	0
#define	NVRAM_SYM_SCAM_DONT_ASSIGN	1
#define	NVRAM_SYM_SCAM_SET_SPECIFIC_ID	2
#define	NVRAM_SYM_SCAM_USE_ORDER_GIVEN	3
		u_int16_t	status;
#define	NVRAM_SYM_SCAM_UNKNOWN		0
#define	NVRAM_SYM_SCAM_DEVICE_NOT_FOUND	1
#define	NVRAM_SYM_SCAM_ID_NOT_SET	2
#define	NVRAM_SYM_SCAM_ID_VALID		3
		u_int8_t		target_id;
		u_int8_t		rsvd;
	} __packed scam[4];

	u_int8_t	spare_devices[15 * 8];
	u_int8_t	trailer[6];	/* 0xfe 0xfe 0x00 0x00 0x00 0x00 */
} __packed;

#define	SIOP_NVRAM_TEK_SIZE		64
#define	SIOP_NVRAM_TEK_93c46_ADDRESS	0
#define	SIOP_NVRAM_TEK_24c16_ADDRESS	0x40

#if 0
static const u_int8_t tekram_sync_table[16] __attribute__((__unused__)) = {
	25, 31, 37,  43,
	50, 62, 75, 125,
	12, 15, 18,  21,
	 6,  7,  9,  10,
};

struct nvram_tekram {
	struct nvram_tekram_target {
		u_int8_t	flags;
#define	NVRAM_TEK_TARG_F_PARITY_CHECK	0x01
#define	NVRAM_TEK_TARG_F_SYNC_NEGO	0x02
#define	NVRAM_TEK_TARG_F_DISCONNECT_EN	0x04
#define	NVRAM_TEK_TARG_F_START_CMD	0x08
#define	NVRAM_TEK_TARG_F_TQ_EN		0x10
#define	NVRAM_TEK_TARG_F_WIDE_NEGO	0x20
		u_int8_t	sync_index;
		u_int16_t	word2;
	} __packed target[16];
	u_int8_t	host_id;
	u_int8_t	flags;
#define	NVRAM_TEK_F_MORE_THAN_2_DRIVES	0x01
#define	NVRAM_TEK_F_DRIVES_SUP_1G	0x02
#define	NVRAM_TEK_F_RESET_ON_POWER_ON	0x04
#define	NVRAM_TEK_F_ACTIVE_NEGATION	0x08
#define	NVRAM_TEK_F_IMMEDIATE_SEEK	0x10
#define	NVRAM_TEK_F_SCAN_LUNS		0x20
#define	NVRAM_TEK_F_REMOVABLE_FLAGS	0xc0	/* 0 dis, 1 boot, 2 all */
	u_int8_t	boot_delay_index;
	u_int8_t	max_tags_index;
	u_int16_t	flags1;
#define	NVRAM_TEK_F_F2_F6_ENABLED	0x0001
	u_int16_t	spare[29];
} __packed;
#endif
