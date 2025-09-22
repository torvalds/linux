/*	$OpenBSD: iha.h,v 1.24 2020/07/22 13:16:04 krw Exp $ */
/*-------------------------------------------------------------------------
 *
 * Device driver for the INI-9XXXU/UW or INIC-940/950  PCI SCSI Controller.
 *
 * Written for 386bsd and FreeBSD by
 *	Winston Hung		<winstonh@initio.com>
 *
 * Copyright (c) 1997-1999 Initio Corp
 * Copyright (c) 2000-2002 Ken Westerback
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 *-------------------------------------------------------------------------
 */

#define IHA_MAX_SG_ENTRIES	33
#define IHA_MAX_TARGETS		16
#define IHA_MAX_SCB		32
#define IHA_MAX_EXTENDED_MSG	 4 /* SDTR(3) and WDTR(4) only */

/*
 *   Scatter-Gather Element Structure
 */
struct iha_sg_element {
	u_int32_t SG_Addr; /* Physical address of segment */
	u_int32_t SG_Len;  /* Length of segment */
};

/*
 * iha_scb - SCSI Request structure used by the
 *	     Tulip (aka inic-940/950). Note that 32
 *	     bit pointers and ints are assumed!
 */

struct iha_scb {
	TAILQ_ENTRY(iha_scb) SCB_ScbList;

	bus_dmamap_t	SCB_DataDma;	/* maps xfer buffer	     */
	bus_dmamap_t	SCB_SGDma;	/* maps scatter-gather list  */

	int	  SCB_Status;		   /* Current status of the SCB	     */
#define		      STATUS_QUEUED   0	   /*	   SCB one of Free/Done/Pend */
#define		      STATUS_RENT     1	   /*	   SCB allocated, not queued */
#define		      STATUS_SELECT   2	   /*	   SCB being selected	     */
#define		      STATUS_BUSY     3	   /*	   SCB I/O is active	     */
	u_int8_t  SCB_NxtStat;		   /* Next state function to apply   */
	int	  SCB_Flags;		   /* SCB Flags (xs->flags + private)*/
#define		      FLAG_RSENS      0x00010000 /*	 Request Sense sent  */
#define		      FLAG_SG	      0x00020000 /*      Scatter/Gather used */
#define		      FLAG_DIR	      (SCSI_DATA_IN | SCSI_DATA_OUT)
	u_int8_t  SCB_Target;		   /* Target Id			     */
	u_int8_t  SCB_Lun;		   /* Lun			     */
	u_int32_t SCB_BufChars;		   /* size of data buf		     */
	u_int32_t SCB_BufCharsLeft;	   /* Chars left to xfer to/from buf */
	u_int8_t  SCB_HaStat;		   /* Status of Host Adapter	     */
#define		      HOST_OK	      0x00 /*	   OK - operation a success  */
#define		      HOST_TIMED_OUT  0x01 /*      Request timed out         */
#define		      HOST_SPERR      0x10 /*	   SCSI parity error	     */
#define		      HOST_SEL_TOUT   0x11 /*	   Selection Timeout	     */
#define		      HOST_DO_DU      0x12 /*	   Data Over/Underrun	     */
#define		      HOST_BAD_PHAS   0x14 /*	   Unexpected SCSI bus phase */
#define		      HOST_SCSI_RST   0x1B /*	   SCSI bus was reset	     */
#define		      HOST_DEV_RST    0x1C /*	   Device was reset	     */
	u_int8_t  SCB_TaStat;		   /* SCSI Status Byte		     */
	u_int8_t  SCB_Ident;		   /* Identity Message		     */
	u_int8_t  SCB_TagMsg;		   /* Tag Message		     */
	u_int8_t  SCB_TagId;		   /* Queue Tag			     */

	u_int8_t  SCB_CDB[12];		   /* SCSI command being executed    */
	u_int8_t  SCB_CDBLen;		   /* Length of SCSI command in CDB  */

	struct scsi_xfer *SCB_Xs;	   /* xs this SCB is executing	     */

	struct iha_sg_element SCB_SGList[IHA_MAX_SG_ENTRIES]; /* SG list     */
	u_int16_t SCB_SGCount;		   /* # segments in list             */
	u_int16_t SCB_SGIdx;		   /* index to current element       */

	struct scsi_sense_data	SCB_ScsiSenseData; /* DMA-able sense buffer  */
	struct tcs	       *SCB_Tcs;   	   /* tcs for SCB_Target     */
};

/*
 *   Target Device Control Structure
 */
struct tcs {
	u_int16_t TCS_Flags;
#define		      FLAG_SCSI_RATE	 0x0007 /* Index into tul_rate_tbl[] */
#define		      FLAG_EN_DISC	 0x0008 /* Enable disconnect	     */
#define		      FLAG_NO_SYNC	 0x0010 /* No sync data transfer     */
#define		      FLAG_NO_WIDE	 0x0020 /* No wide data transfer     */
#define		      FLAG_1GIGA	 0x0040 /* 255 hd/63 sec (64/32)     */
#define		      FLAG_SPINUP	 0x0080 /* Start disk drive	     */
#define		      FLAG_WIDE_DONE	 0x0100 /* WDTR msg has been sent    */
#define		      FLAG_SYNC_DONE	 0x0200 /* SDTR msg has been sent    */
#define		      FLAG_NO_NEG_SYNC   (FLAG_NO_SYNC | FLAG_SYNC_DONE)
#define		      FLAG_NO_NEG_WIDE   (FLAG_NO_WIDE | FLAG_WIDE_DONE)
	u_int8_t  TCS_JS_Period;
#define		      PERIOD_WIDE_SCSI	 0x80	/* Enable Wide SCSI	     */
#define		      PERIOD_SYXPD	 0x70	/* Synch. SCSI Xfer rate     */
#define		      PERIOD_SYOFS	 0x0f	/* Synch. SCSI Offset	     */
	u_int8_t  TCS_SConfig0;
	u_int8_t  TCS_TagCnt;

	struct iha_scb  *TCS_NonTagScb;
};

struct iha_softc {
	struct device	     sc_dev;

	bus_space_tag_t	     sc_iot;
	bus_space_handle_t   sc_ioh;

	bus_dma_tag_t	     sc_dmat;
	bus_dmamap_t	     sc_dmamap;

	u_int16_t	     sc_id;
	u_int16_t	     sc_maxtargets;

	void		    *sc_ih;

	/*
	 *   Initio specific fields
	 */
	u_int8_t  HCS_Flags;
#define		      FLAG_EXPECT_DISC	     0x01
#define		      FLAG_EXPECT_SELECT     0x02
#define		      FLAG_EXPECT_RESET	     0x10
#define		      FLAG_EXPECT_DONE_DISC  0x20
	u_int8_t  HCS_Semaph;
#define		      SEMAPH_IN_MAIN	     0x00   /* Already in tulip_main */
	u_int8_t  HCS_Phase;			    /* MSG  C/D	 I/O	     */
#define		      PHASE_DATA_OUT	     0x00   /*	0    0	  0	     */
#define		      PHASE_DATA_IN	     0x01   /*	0    0	  1	     */
#define		      PHASE_CMD_OUT	     0x02   /*	0    1	  0	     */
#define		      PHASE_STATUS_IN	     0x03   /*	0    1	  1	     */
#define		      PHASE_MSG_OUT	     0x06   /*	1    1	  0	     */
#define		      PHASE_MSG_IN	     0x07   /*	1    1	  1	     */
	u_int8_t  HCS_JSInt;
	u_int8_t  HCS_JSStatus0;
	u_int8_t  HCS_JSStatus1;
	u_int8_t  HCS_SConf1;
	u_int8_t  HCS_Msg[IHA_MAX_EXTENDED_MSG];    /* [0] len, [1] Msg Code */

	struct iha_scb *HCS_Scb;		    /* SCB array	     */
	struct iha_scb *HCS_ActScb;	    /* SCB using SCSI bus    */

	TAILQ_HEAD(, iha_scb) HCS_FreeScb, HCS_PendScb, HCS_DoneScb;

	struct tcs HCS_Tcs[IHA_MAX_TARGETS];

	struct mutex		sc_scb_mtx;	/* scb queue protection */
	struct scsi_iopool	sc_iopool;
};

/*
 *   EEPROM for one SCSI Channel
 *
 */
struct iha_nvram_scsi {
	u_int8_t  NVM_SCSI_Id;	    /* 0x00 Channel Adapter SCSI Id          */
	u_int8_t  NVM_SCSI_Cfg;	    /* 0x01 Channel configuration            */
#define		      CFG_SCSI_RESET 0x0001 /*     Reset bus at power up     */
#define		      CFG_EN_PAR     0x0002 /*     SCSI parity enable        */
#define		      CFG_ACT_TERM1  0x0004 /*     Enable active term 1      */
#define		      CFG_ACT_TERM2  0x0008 /*     Enable active term 2      */
#define		      CFG_AUTO_TERM  0x0010 /*     Enable auto terminator    */
#define		      CFG_EN_PWR     0x0080 /*     Enable power mgmt         */
	u_int8_t  NVM_SCSI_CfgByte2;        /* 0x02 Unused Channel Cfg byte 2*/
	u_int8_t  NVM_SCSI_Targets;	    /* 0x03 Number of SCSI targets   */
					    /* 0x04 Lower bytes of targ flags*/
	u_int8_t  NVM_SCSI_TargetFlags[IHA_MAX_TARGETS];
};

/*
 * Tulip (aka ini-940/950) Serial EEPROM Layout
 *
 */
struct iha_nvram {
	/* ---------- Header ------------------------------------------------*/
	u_int16_t  NVM_Signature;	       /* 0x00 NVRAM Signature	     */
#define		       SIGNATURE	0xC925
	u_int8_t   NVM_Size;		       /* 0x02 Size of data structure*/
	u_int8_t   NVM_Revision;	       /* 0x03 Rev. of data structure*/

	/* ---------- Host Adapter Structure --------------------------------*/
	u_int8_t   NVM_ModelByte0;	       /* 0x04 Model number (byte 0) */
	u_int8_t   NVM_ModelByte1;	       /* 0x05 Model number (byte 1) */
	u_int8_t   NVM_ModelInfo;	       /* 0x06 Model information     */
	u_int8_t   NVM_NumOfCh;		       /* 0x07 Number of SCSI channel*/
	u_int8_t   NVM_BIOSConfig1;	       /* 0x08 BIOS configuration 1  */
#define		       BIOSCFG_ENABLE	  0x01 /*      BIOS enable	     */
#define		       BIOSCFG_8DRIVE	  0x02 /*      Support > 2 drives    */
#define		       BIOSCFG_REMOVABLE  0x04 /*      Support removable drv */
#define		       BIOSCFG_INT19	  0x08 /*      Intercept int 19h     */
#define		       BIOSCFG_BIOSSCAN	  0x10 /*      Dynamic BIOS scan     */
#define		       BIOSCFG_LUNSUPPORT 0x40 /*      Support LUN	     */
#define		       BIOSCFG_DEFAULT	  (BIOSCFG_ENABLE)
	u_int8_t   NVM_BIOSConfig2;	       /* 0x09 BIOS configuration 2  */
	u_int8_t   NVM_HAConfig1;	       /* 0x0a Host adapter config 1 */
#define		       HACFG_BOOTIDMASK	  0x0F /*      Boot ID number	     */
#define		       HACFG_LUNMASK	  0x70 /*      Boot LUN number	     */
#define		       HACFG_CHANMASK	  0x80 /*      Boot Channel number   */
	u_int8_t   NVM_HAConfig2;	       /* 0x0b Host adapter config 2 */
	struct iha_nvram_scsi NVM_Scsi[2];     /* 0x0c		             */
	u_int8_t   NVM_Reserved[10];	       /* 0x34			     */

	/* --------- CheckSum -----------------------------------------------*/
	u_int16_t  NVM_CheckSum;	       /* 0x3E Checksum of NVRam     */
};

/*
 *  Tulip (aka inic-940/950) PCI Configuration Space Initio Specific Registers
 *
 *  Offsets 0x00 through 0x3f are the standard PCI Configuration Header
 *  registers.
 *
 *  Offsets 0x40 through 0x4f, 0x51, 0x53, 0x57, 0x5b, 0x5e and 0x5f are
 *  reserved registers.
 *
 *  Registers 0x50 and 0x52 always read as 0.
 *
 *  The register offset names and associated bit field names are taken
 *  from the Inic-950 Data Sheet, Version 2.1, March 1997
 */
#define TUL_GCTRL0	0x54	       /* R/W Global Control 0		     */
#define	    EEPRG	    0x04       /*     Enable EEPROM Programming	     */
#define TUL_GCTRL1	0x55	       /* R/W Global Control 1		     */
#define	    ATDEN	    0x01       /*     Auto Termination Detect Enable */
#define TUL_GSTAT	0x56	       /* R/W Global Status - connector type */
#define TUL_EPAD0	0x58	       /* R/W External EEPROM Addr (lo byte) */
#define TUL_EPAD1	0x59	       /* R/W External EEPROM Addr (hi byte) */
#define TUL_PNVPG	0x5A	       /* R/W Data port to external BIOS     */
#define TUL_EPDATA	0x5C	       /* R/W EEPROM Data port		     */
#define TUL_NVRAM	0x5D	       /* R/W Non-volatile RAM port	     */
#define     NVREAD	    0x80       /*     Read from given NVRAM addr     */
#define     NVWRITE         0x40       /*     Write to given NVRAM addr	     */
#define     NVENABLE_ERASE  0x30       /*     Enable NVRAM Erase/Write       */
#define	    NVRCS	    0x08       /*     Select external NVRAM	     */
#define	    NVRCK	    0x04       /*     NVRAM Clock		     */
#define	    NVRDO	    0x02       /*     NVRAM Write Data		     */
#define	    NVRDI	    0x01       /*     NVRAM Read  Data		     */

/*
 *   Tulip (aka inic-940/950) SCSI Registers
 */
#define TUL_STCNT0	0x80	       /* R/W 24 bit SCSI Xfer Count	     */
#define	    TCNT	    0x00ffffff /*     SCSI Xfer Transfer Count	     */
#define TUL_SFIFOCNT	0x83	       /* R/W  5 bit FIFO counter	     */
#define	    FIFOC	    0x1f       /*     SCSI Offset Fifo Count	     */
#define TUL_SISTAT	0x84	       /* R   Interrupt Register	     */
#define	    RSELED	    0x80       /*     Reselected		     */
#define	    STIMEO	    0x40       /*     Selected/Reselected Timeout    */
#define	    SBSRV	    0x20       /*     SCSI Bus Service		     */
#define	    SRSTD	    0x10       /*     SCSI Reset Detected	     */
#define	    DISCD	    0x08       /*     Disconnected Status	     */
#define	    SELED	    0x04       /*     Select Interrupt		     */
#define	    SCAMSCT	    0x02       /*     SCAM selected		     */
#define	    SCMDN	    0x01       /*     Command Complete		     */
#define TUL_SIEN	0x84	       /* W   Interrupt enable		     */
#define	    ALL_INTERRUPTS  0xff
#define TUL_STAT0	0x85	       /* R   Status 0			     */
#define	    INTPD	    0x80       /*     Interrupt pending		     */
#define	    SQACT	    0x40       /*     Sequencer active		     */
#define	    XFCZ	    0x20       /*     Xfer counter zero		     */
#define	    SFEMP	    0x10       /*     FIFO empty		     */
#define	    SPERR	    0x08       /*     SCSI parity error		     */
#define	    PH_MASK	    0x07       /*     SCSI phase mask		     */
#define TUL_SCTRL0	0x85	       /* W   Control 0			     */
#define	    RSSQC	    0x20       /*     Reset sequence counter	     */
#define	    RSFIFO	    0x10       /*     Flush FIFO		     */
#define	    CMDAB	    0x04       /*     Abort command (sequence)	     */
#define	    RSMOD	    0x02       /*     Reset SCSI Chip		     */
#define	    RSCSI	    0x01       /*     Reset SCSI Bus		     */
#define TUL_STAT1	0x86	       /* R   Status 1			     */
#define	    STRCV	    0x80       /*     Status received		     */
#define	    MSGST	    0x40       /*     Message sent		     */
#define	    CPDNE	    0x20       /*     Data phase done		     */
#define	    DPHDN	    0x10       /*     Data phase done		     */
#define	    STSNT	    0x08       /*     Status sent		     */
#define	    SXCMP	    0x04       /*     Xfer completed		     */
#define	    SLCMP	    0x02       /*     Selection completed	     */
#define	    ARBCMP	    0x01       /*     Arbitration completed	     */
#define TUL_SCTRL1	0x86	       /* W   Control 1			     */
#define	    ENSCAM	    0x80       /*     Enable SCAM		     */
#define	    NIDARB	    0x40       /*     No ID for Arbitration	     */
#define	    ENLRS	    0x20       /*     Low Level Reselect	     */
#define	    PWDN	    0x10       /*     Power down mode		     */
#define	    WCPU	    0x08       /*     Wide CPU			     */
#define	    EHRSL	    0x04       /*     Enable HW reselect	     */
#define	    ESBUSOUT	    0x02       /*     Enable SCSI data bus out latch */
#define	    ESBUSIN	    0x01       /*     Enable SCSI data bus in latch  */
#define TUL_SSTATUS2	0x87	       /* R   Status 2			     */
#define	    SABRT	    0x80       /*     Command aborted		     */
#define	    OSCZ	    0x40       /*     Offset counter zero	     */
#define	    SFFUL	    0x20       /*     FIFO full			     */
#define	    TMCZ	    0x10       /*     Timeout counter zero	     */
#define	    BSYGN	    0x08       /*     Busy release		     */
#define	    PHMIS	    0x04       /*     Phase mismatch		     */
#define	    SBEN	    0x02       /*     SCSI data bus enable	     */
#define	    SRST	    0x01       /*     SCSI bus reset in progress     */
#define TUL_SCONFIG0	0x87	       /* W   Configuration		     */
#define	    PHLAT	    0x80       /*     Enable phase latch	     */
#define	    ITMOD	    0x40       /*     Initiator mode		     */
#define	    SPCHK	    0x20       /*     Enable SCSI parity	     */
#define	    ADMA8	    0x10       /*     Alternate dma 8-bits mode	     */
#define	    ADMAW	    0x08       /*     Alternate dma 16-bits mode     */
#define	    EDACK	    0x04       /*     Enable DACK in wide SCSI xfer  */
#define	    ALTPD	    0x02       /*     Alternate sync period mode     */
#define	    DSRST	    0x01       /*     Disable SCSI Reset signal	     */
#define	    SCONFIG0DEFAULT (PHLAT | ITMOD | ALTPD | DSRST)
#define TUL_SOFSC	0x88	       /* R   Offset			     */
#define TUL_SYNCM	0x88	       /* W   Sync. Xfer Period & Offset     */
#define TUL_SBID	0x89	       /* R   SCSI BUS ID		     */
#define TUL_SID		0x89	       /* W   SCSI ID			     */
#define TUL_SALVC	0x8A	       /* R   FIFO Avail Cnt/Identify Msg    */
#define TUL_STIMO	0x8A	       /* W   Sel/Resel Time Out Register    */
#define TUL_SDATI	0x8B	       /* R   SCSI Bus contents		     */
#define TUL_SDAT0	0x8B	       /* W   SCSI Data Out		     */
#define TUL_SFIFO	0x8C	       /* R/W FIFO			     */
#define TUL_SSIGI	0x90	       /* R   SCSI signal in		     */
#define	    REQ		    0x80       /*     REQ signal		     */
#define	    ACK		    0x40       /*     ACK signal		     */
#define	    BSY		    0x20       /*     BSY signal		     */
#define	    SEL		    0x10       /*     SEL signal		     */
#define	    ATN		    0x08       /*     ATN signal		     */
#define	    MSG		    0x04       /*     MSG signal		     */
#define	    CD		    0x02       /*     C/D signal		     */
#define	    IO		    0x01       /*     I/O signal		     */
#define TUL_SSIGO	0x90	       /* W   SCSI signal out		     */
#define TUL_SCMD	0x91	       /* R/W SCSI Command		     */
#define	    NO_OP	    0x00       /*     Place Holder for tulip_wait()  */
#define	    SEL_NOATN	    0x01       /*     Select w/o ATN Sequence	     */
#define	    XF_FIFO_OUT	    0x03       /*     FIFO Xfer Information out	     */
#define	    MSG_ACCEPT	    0x0F       /*     Message Accept		     */
#define	    SEL_ATN	    0x11       /*     Select w ATN Sequence	     */
#define	    SEL_ATNSTOP	    0x12       /*     Select w ATN & Stop Sequence   */
#define	    SELATNSTOP	    0x1E       /*     Select w ATN & Stop Sequence   */
#define	    SEL_ATN3	    0x31       /*     Select w ATN3 Sequence	     */
#define	    XF_DMA_OUT	    0x43       /*     DMA Xfer Information out	     */
#define	    EN_RESEL	    0x80       /*     Enable Reselection	     */
#define	    XF_FIFO_IN	    0x83       /*     FIFO Xfer Information in	     */
#define	    CMD_COMP	    0x84       /*     Command Complete Sequence	     */
#define	    XF_DMA_IN	    0xC3       /*     DMA Xfer Information in	     */
#define TUL_STEST0	0x92	       /* R/W Test0			     */
#define TUL_STEST1	0x93	       /* R/W Test1			     */

/*
 *   Tulip (aka inic-940/950) DMA Registers
 */
#define TUL_DXPA	0xC0	       /* R/W DMA      Xfer Physcl Addr	 0-31*/
#define TUL_DXPAE	0xC4	       /* R/W DMA      Xfer Physcl Addr 32-63*/
#define TUL_DCXA	0xC8	       /* R   DMA Curr Xfer Physcl Addr	 0-31*/
#define TUL_DCXAE	0xCC	       /* R   DMA Curr Xfer Physcl Addr 32-63*/
#define TUL_DXC		0xD0	       /* R/W DMA Xfer Counter		     */
#define TUL_DCXC	0xD4	       /* R   DMA Current Xfer Counter	     */
#define TUL_DCMD	0xD8	       /* R/W DMA Command Register	     */
#define	    SGXFR	    0x80       /*     Scatter/Gather Xfer	     */
#define	    RSVD	    0x40       /*     Reserved - always reads as 0   */
#define	    XDIR	    0x20       /*     Xfer Direction 0/1 = out/in    */
#define	    BMTST	    0x10       /*     Bus Master Test		     */
#define	    CLFIFO	    0x08       /*     Clear FIFO		     */
#define	    ABTXFR	    0x04       /*     Abort Xfer		     */
#define	    FRXFR	    0x02       /*     Force Xfer		     */
#define	    STRXFR	    0x01       /*     Start Xfer		     */
#define	    ST_X_IN	    (XDIR | STRXFR)
#define	    ST_X_OUT	    (	    STRXFR)
#define	    ST_SG_IN	    (SGXFR | ST_X_IN)
#define	    ST_SG_OUT	    (SGXFR | ST_X_OUT)
#define TUL_ISTUS0	0xDC	       /* R/W Interrupt Status Register	     */
#define	    DGINT	    0x80       /*     DMA Global Interrupt	     */
#define	    RSVRD0	    0x40       /*     Reserved			     */
#define	    RSVRD1	    0x20       /*     Reserved			     */
#define	    SCMP	    0x10       /*     SCSI Complete		     */
#define	    PXERR	    0x08       /*     PCI Xfer Error		     */
#define	    DABT	    0x04       /*     DMA Xfer Aborted		     */
#define	    FXCMP	    0x02       /*     Forced Xfer Complete	     */
#define	    XCMP	    0x01       /*     Bus Master Xfer Complete	     */
#define TUL_ISTUS1	0xDD	       /* R   DMA status Register	     */
#define	    SCBSY	    0x08       /*     SCSI Busy			     */
#define	    FFULL	    0x04       /*     FIFO Full			     */
#define	    FEMPT	    0x02       /*     FIFO Empty		     */
#define	    XPEND	    0x01       /*     Xfer pending		     */
#define TUL_IMSK	0xE0	       /* R/W Interrupt Mask Register	     */
#define	    MSCMP	    0x10       /*     Mask SCSI Complete	     */
#define	    MPXFER	    0x08       /*     Mask PCI Xfer Error	     */
#define	    MDABT	    0x04       /*     Mask Bus Master Abort	     */
#define	    MFCMP	    0x02       /*     Mask Force Xfer Complete	     */
#define	    MXCMP	    0x01       /*     Mask Bus Master Xfer Complete  */
#define	    MASK_ALL	    (MXCMP | MFCMP | MDABT | MPXFER | MSCMP)
#define TUL_DCTRL0	0xE4	       /* R/W DMA Control Register	     */
#define	    SXSTP	    0x80       /*     SCSI Xfer Stop		     */
#define	    RPMOD	    0x40       /*     Reset PCI Module		     */
#define	    RSVRD2	    0x20       /*     SCSI Xfer Stop		     */
#define	    PWDWN	    0x10       /*     Power Down		     */
#define	    ENTM	    0x08       /*     Enable SCSI Terminator Low     */
#define	    ENTMW	    0x04       /*     Enable SCSI Terminator High    */
#define	    DISAFC	    0x02       /*     Disable Auto Clear	     */
#define	    LEDCTL	    0x01       /*     LED Control		     */
#define TUL_DCTRL1	0xE5	       /* R/W DMA Control Register 1	     */
#define	    SDWS	    0x01       /*     SCSI DMA Wait State	     */
#define TUL_DFIFO	0xE8	       /* R/W DMA FIFO			     */

#define TUL_WCTRL	0xF7	       /* ?/? Bus master wait state control  */
#define TUL_DCTRL	0xFB	       /* ?/? DMA delay control		     */

/* Functions used by higher SCSI layers, the kernel, or iha.c and iha_pci.c  */

void iha_scsi_cmd(struct scsi_xfer *);
int  iha_intr(void *);
int  iha_init_tulip(struct iha_softc *);


