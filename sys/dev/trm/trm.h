/*-
 *	File Name : trm.h	
 *				    
 *	Tekram DC395U/UW/F ,DC315/U 
 *   PCI SCSI Bus Master Host Adapter Device Driver	
 *   (SCSI chip set used Tekram ASIC TRM-S1040)	
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * (C)Copyright 1995-2001 Tekram Technology Co.,Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * $FreeBSD$
 */

#ifndef trm_H
#define trm_H

/* SCSI CAM */

#define TRM_TRANS_CUR		0x01	/* Modify current neogtiation status */
#define TRM_TRANS_ACTIVE	0x03	/* Assume this is the active target */
#define TRM_TRANS_GOAL		0x04	/* Modify negotiation goal */
#define TRM_TRANS_USER		0x08	/* Modify user negotiation settings */

struct trm_transinfo {
	u_int8_t width;
	u_int8_t period;
	u_int8_t offset;
};

struct trm_target_info {
	u_int8_t	 		disc_tag;   /* bits define..... */
#define TRM_CUR_DISCENB	0x01 /* current setting disconnect enable */
#define TRM_CUR_TAGENB 0x02  /* current setting tag command Q enable */
#define TRM_USR_DISCENB	0x04 /* user adapter device setting disconnect enable */
#define TRM_USR_TAGENB 0x08  /* user adapter device setting tag command Q enable*/
	struct trm_transinfo	current; /* info of current */
	struct trm_transinfo 	goal;    /* info of after negotiating */
	struct trm_transinfo 	user;    /* info of user adapter device setting  */
};
/*
 * SCSI CAM  **
 */

/*
 *	bus_dma_segment_t
 *
 *	Describes a single contiguous DMA transaction.  Values
 *	are suitable for programming into DMA registers.
 *
 *typedef struct bus_dma_segment 
 *{
 *	bus_addr_t	ds_addr;	// DMA address
 *	bus_size_t	ds_len;		// length of transfer
 *} bus_dma_segment_t;
 */

/*;----------------------Segment Entry------------------------------------*/
typedef  struct  _SGentry {
       u_int32_t	address;
       u_int32_t	length;
} SGentry, *PSEG;
/*
 *-----------------------------------------------------------------------
 *     feature of chip set MAX value
 *-----------------------------------------------------------------------
 */

#define TRM_MAX_ADAPTER_NUM    	4
#define TRM_MAX_DEVICES	      	16
#define TRM_MAX_SG_LISTENTRY   	32
#define TRM_MAX_TARGETS	       	16
#define TRM_MAX_TAGS_CMD_QUEUE  256 /* MAX_CMD_QUEUE	20*/
#define TRM_MAX_CMD_PER_LUN    	32
#define TRM_MAX_SRB_CNT	       	256
#define TRM_MAX_START_JOB       256
#define TRM_MAXPHYS		(128 * 1024)
#define TRM_NSEG	        (btoc(TRM_MAXPHYS) + 1)
#define TRM_MAXTRANSFER_SIZE    0xFFFFFF /* restricted by 24 bit counter */
#define PAGELEN 	       	4096

#define SEL_TIMEOUT	    	153	/* 250 ms selection timeout (@ 40MHz) */

/*
 *  CAM ccb
 * Union of all CCB types for kernel space allocation.  This union should
 * never be used for manipulating CCBs - its only use is for the allocation
 * and deallocation of raw CCB space and is the return type of xpt_ccb_alloc
 * and the argument to xpt_ccb_free.
 * 
 *union ccb {
 *	struct	ccb_hdr		   	    ccb_h;	// For convenience 
 *	struct	ccb_scsiio	        csio;
 *	struct	ccb_getdev	        cgd;
 *	struct	ccb_getdevlist  	cgdl;
 *	struct	ccb_pathinq		    cpi;
 *	struct	ccb_relsim	    	crs;
 *	struct	ccb_setasync		csa;
 *	struct	ccb_setdev	    	csd;
 *	struct	ccb_dev_match		cdm;
 *	struct	ccb_trans_settings	cts;
 *	struct	ccb_calc_geometry	ccg;	
 *	struct	ccb_abort	    	cab;
 *	struct	ccb_resetbus		crb;
 *	struct	ccb_resetdev		crd;
 *	struct	ccb_termio	    	tio;
 *	struct	ccb_accept_tio		atio;
 *	struct	ccb_scsiio	    	ctio;
 *	struct	ccb_en_lun	    	cel;
 *	struct	ccb_immed_notify	cin;
 *	struct	ccb_notify_ack		cna;
 *	struct	ccb_eng_inq	    	cei;
 *	struct	ccb_eng_exec		cee;
 *	struct 	ccb_rescan	    	crcn;
 *	struct  ccb_debug	    	cdbg;
 *  };
 */

/*
 *-----------------------------------------------------------------------
 *               SCSI Request Block
 *-----------------------------------------------------------------------
 */
struct	_SRB {
	u_int8_t	CmdBlock[12];
	u_long		Segment0[2];
	u_long		Segment1[2];
	struct _SRB	*pNextSRB;
	struct _DCB	*pSRBDCB;
	SGentry		SgSenseTemp;

	PSEG		pSRBSGL;	/* scatter gather list */

	u_int32_t	SRBSGPhyAddr;	/* a segment starting address */
	u_int32_t	SRBTotalXferLength;
	
	/*
	 *	          CAM ccb
	 */
	union  ccb      *pccb; 
	bus_dmamap_t	sg_dmamap;
	bus_dmamap_t	 dmamap;
	u_int16_t	SRBState;
	u_int8_t *	pMsgPtr;
	
    	u_int8_t	SRBSGCount;
	u_int8_t	SRBSGIndex;
	u_int8_t	MsgInBuf[6];
	u_int8_t	MsgOutBuf[6];
	
	u_int8_t	AdaptStatus;
	u_int8_t	TargetStatus;
	u_int8_t	MsgCnt;
	u_int8_t	TagNumber;
	
	u_int8_t	SRBStatus;
	u_int8_t	RetryCnt;
	u_int8_t	SRBFlag;   
	u_int8_t	ScsiCmdLen;
	u_int8_t	ScsiPhase;
	u_int8_t	Reserved[3]; /*;for dword alignment */
};
typedef struct _SRB	TRM_SRB, *PSRB;

/*
 *-----------------------------------------------------------------------
 *                   Device Control Block
 *-----------------------------------------------------------------------
 */
struct	_DCB
{
	PSRB		pWaitingSRB;
	PSRB		pWaitingLastSRB;
	
	PSRB		pGoingSRB;
	PSRB		pGoingLastSRB;
	
	PSRB		pActiveSRB;

	u_int16_t	GoingSRBCnt;
	u_int16_t	MaxActiveCommandCnt;

	u_int8_t	TargetID;	/*; SCSI Target ID  (SCSI Only) */
	u_int8_t	TargetLUN;      /*; SCSI Log.  Unit (SCSI Only) */
	u_int8_t	DCBFlag;
	u_int8_t	DevType;

	u_int8_t	SyncMode;   	/* mode ? (1 sync):(0 async)  */
	u_int8_t	MaxNegoPeriod; 	/* for nego. */
	u_int8_t	SyncPeriod; 	/* for reg. */
	u_int8_t	SyncOffset;   	/* for reg. and nego.(low nibble) */
	
	u_int8_t	DevMode;
	u_int8_t	AdpMode;

	u_int8_t	IdentifyMsg;
	u_int8_t	DCBstatus;	/* DCB status */
	/*u_int8_t	Reserved[3];	for dword alignment */
	struct		trm_target_info tinfo; /* 10 bytes */
	struct _DCB	*pNextDCB;
};
typedef struct _DCB	TRM_DCB, *PDCB;

/*
 *-----------------------------------------------------------------------
 *                  Adapter Control Block
 *-----------------------------------------------------------------------
 */
struct	_ACB
{
	device_t		dev;
	
	bus_space_tag_t		tag;
	bus_space_handle_t	bsh;
	bus_dma_tag_t		parent_dmat;
	bus_dma_tag_t		buffer_dmat;   /* dmat for buffer I/O */  
	bus_dma_tag_t		srb_dmat;
	bus_dma_tag_t		sense_dmat; /* dmat for sense buffer */
	bus_dma_tag_t		sg_dmat;
	bus_dmamap_t		sense_dmamap;
	bus_dmamap_t		srb_dmamap;
	bus_addr_t		sense_busaddr;
	struct scsi_sense_data	*sense_buffers;
	struct resource		*iores, *irq;
	void			*ih;
    /*
     *	          CAM SIM/XPT
     */
	struct	   	 	cam_sim  *psim;
	struct	    		cam_path *ppath;

	TRM_SRB			TmpSRB;
	TRM_DCB			DCBarray[16][8];

	u_int32_t		srb_physbase;
	
	PSRB	    		pFreeSRB;
	PDCB	    		pActiveDCB;

	PDCB	    		pLinkDCB;
	PDCB	    		pDCBRunRobin;

	u_int16_t    		max_id;
	u_int16_t   	 	max_lun;

	u_int8_t    		msgin123[4];

	u_int8_t    		scan_devices[16][8];

	u_int8_t    		AdaptSCSIID;	/*; Adapter SCSI Target ID */
	u_int8_t    		AdaptSCSILUN;	/*; Adapter SCSI LUN */
	u_int8_t    		DeviceCnt;
	u_int8_t    		ACBFlag;

	u_int8_t    		TagMaxNum;
	u_int8_t           	Config;
	u_int8_t		AdaptType;
	u_int8_t		AdapterUnit;	/* nth Adapter this driver */
};
typedef struct  _ACB		 TRM_ACB, *PACB;
/*
 *   ----SRB State machine definition
 */
#define SRB_FREE                  	0x0000
#define SRB_WAIT                  	0x0001
#define SRB_READY                	0x0002
#define SRB_MSGOUT                	0x0004	/*arbitration+msg_out 1st byte*/
#define SRB_MSGIN                 	0x0008
#define SRB_EXTEND_MSGIN         	0x0010
#define SRB_COMMAND             	0x0020
#define SRB_START_                	0x0040	/*arbitration+msg_out+command_out*/
#define SRB_DISCONNECT            	0x0080
#define SRB_DATA_XFER             	0x0100
#define SRB_XFERPAD              	0x0200
#define SRB_STATUS              	0x0400
#define SRB_COMPLETED             	0x0800
#define SRB_ABORT_SENT           	0x1000
#define SRB_DO_SYNC_NEGO           	0x2000
#define SRB_DO_WIDE_NEGO        	0x4000
#define SRB_UNEXPECT_RESEL         	0x8000
/*
 *
 *      ACB Config	
 *
 */
#define HCC_WIDE_CARD	        	0x20
#define HCC_SCSI_RESET	        	0x10
#define HCC_PARITY	            	0x08
#define HCC_AUTOTERM	        	0x04
#define HCC_LOW8TERM	        	0x02
#define HCC_UP8TERM		        0x01
/*
 *   ---ACB Flag
 */
#define RESET_DEV       		0x00000001
#define RESET_DETECT    		0x00000002
#define RESET_DONE      		0x00000004

/*
 *   ---DCB Flag
 */
#define ABORT_DEV_      		0x00000001

/*
 *   ---DCB status
 */
#define DS_IN_QUEUE			0x00000001

/*
 *   ---SRB status 
 */
#define SRB_OK	        		0x00000001
#define ABORTION        		0x00000002
#define OVER_RUN        		0x00000004
#define UNDER_RUN       		0x00000008
#define PARITY_ERROR    		0x00000010
#define SRB_ERROR    		   	0x00000020

/*
 *   ---SRB Flag 
 */
#define DATAOUT         		0x00000080
#define DATAIN	        		0x00000040
#define RESIDUAL_VALID   		0x00000020
#define ENABLE_TIMER    		0x00000010
#define RESET_DEV0      		0x00000004
#define ABORT_DEV       		0x00000002
#define AUTO_REQSENSE    		0x00000001

/*
 *   ---Adapter status
 */
#define H_STATUS_GOOD   		0x00
#define H_SEL_TIMEOUT   		0x11
#define H_OVER_UNDER_RUN    		0x12
#define H_UNEXP_BUS_FREE    		0x13
#define H_TARGET_PHASE_F		0x14
#define H_INVALID_CCB_OP		0x16
#define H_LINK_CCB_BAD			0x17
#define H_BAD_TARGET_DIR		0x18
#define H_DUPLICATE_CCB			0x19
#define H_BAD_CCB_OR_SG			0x1A
#define H_ABORT				0x0FF

/*
 *   ---SCSI Status byte codes
 */
#define SCSI_STAT_GOOD	        	0x00 	/*;  Good status */
#define SCSI_STAT_CHECKCOND     	0x02	/*;  SCSI Check Condition */
#define SCSI_STAT_CONDMET       	0x04	/*;  Condition Met */
#define SCSI_STAT_BUSY	        	0x08	/*;  Target busy status */
#define SCSI_STAT_INTER         	0x10	/*;  Intermediate status */
#define SCSI_STAT_INTERCONDMET   	0x14	/*;  Intermediate condition met */
#define SCSI_STAT_RESCONFLICT   	0x18	/*;  Reservation conflict */
#define SCSI_STAT_CMDTERM       	0x22	/*;  Command Terminated */
#define SCSI_STAT_QUEUEFULL      	0x28	/*;  Queue Full */
#define SCSI_STAT_UNEXP_BUS_F    	0xFD	/*;  Unexpect Bus Free */
#define SCSI_STAT_BUS_RST_DETECT	0xFE	/*;  Scsi Bus Reset detected */
#define SCSI_STAT_SEL_TIMEOUT   	0xFF	/*;  Selection Time out */

/*
 *   ---Sync_Mode
 */
#define SYNC_WIDE_TAG_ATNT_DISABLE 	0x00000000
#define SYNC_NEGO_ENABLE         	0x00000001
#define SYNC_NEGO_DONE           	0x00000002
#define WIDE_NEGO_ENABLE  	        0x00000004
#define WIDE_NEGO_DONE    	        0x00000008
#define EN_TAG_QUEUING          	0x00000010
#define EN_ATN_STOP             	0x00000020

#define SYNC_NEGO_OFFSET            	15
/*
 *    ---SCSI bus phase
 */
#define SCSI_DATA_OUT_  		0
#define SCSI_DATA_IN_   		1
#define SCSI_COMMAND    		2
#define SCSI_STATUS_    		3
#define SCSI_NOP0       		4
#define SCSI_NOP1       		5
#define SCSI_MSG_OUT     		6
#define SCSI_MSG_IN     		7
	
/*
 *     ----SCSI MSG u_int8_t
 */
#define MSG_COMPLETE	    		0x00
#define MSG_EXTENDED	    		0x01
#define MSG_SAVE_PTR	   	 	0x02
#define MSG_RESTORE_PTR     		0x03
#define MSG_DISCONNECT	    		0x04
#define MSG_INITIATOR_ERROR  		0x05
#define MSG_ABORT		        0x06
#define MSG_REJECT_	        	0x07
#define MSG_NOP 	        	0x08
#define MSG_PARITY_ERROR 	   	0x09
#define MSG_LINK_CMD_COMPL   		0x0A
#define MSG_LINK_CMD_COMPL_FLG		0x0B
#define MSG_BUS_RESET	    		0x0C
/* #define MSG_ABORT_TAG	    	0x0D */
#define MSG_SIMPLE_QTAG   	  	0x20
#define MSG_HEAD_QTAG	    		0x21
#define MSG_ORDER_QTAG	    		0x22
#define MSG_IGNOREWIDE	    		0x23
/* #define MSG_IDENTIFY	    		0x80 */
#define MSG_HOST_ID	        	0xC0
/*     bus wide length     */
#define MSG_EXT_WDTR_BUS_8_BIT		0x00
#define MSG_EXT_WDTR_BUS_16_BIT		0x01
#define MSG_EXT_WDTR_BUS_32_BIT		0x02 
/*
 *     ----SCSI STATUS u_int8_t
 */
#define STATUS_GOOD	        	0x00
#define CHECK_CONDITION_  	  	0x02
#define STATUS_BUSY	        	0x08
#define STATUS_INTERMEDIATE  		0x10
#define RESERVE_CONFLICT    		0x18

/*
 *     ---- cmd->result
 */
#define STATUS_MASK_			0xFF
#define MSG_MASK	    		0xFF00
#define RETURN_MASK	    		0xFF0000

/*
 *  Inquiry Data format
 */

typedef struct	_SCSIInqData { /* INQ */

	u_int8_t 	 DevType;	/* Periph Qualifier & Periph Dev Type */
	u_int8_t	 RMB_TypeMod;	/* rem media bit & Dev Type Modifier  */
	u_int8_t	 Vers;		/* ISO, ECMA, & ANSI versions	      */
	u_int8_t	 RDF;		/* AEN, TRMIOP, & response data format*/
	u_int8_t	 AddLen;	/* length of additional data	      */
	u_int8_t	 Res1;		/* reserved	                      */
	u_int8_t	 Res2;		/* reserved	                      */
	u_int8_t	 Flags; 	/* RelADr,Wbus32,Wbus16,Sync,etc.     */
	u_int8_t	 VendorID[8];	/* Vendor Identification	      */
	u_int8_t	 ProductID[16];	/* Product Identification          */
	u_int8_t	 ProductRev[4]; /* Product Revision              */
} SCSI_INQDATA, *PSCSI_INQDATA;


/*  
 *      Inquiry byte 0 masks 
 */
#define SCSI_DEVTYPE	    	  0x1F    /* Peripheral Device Type 	    */
#define SCSI_PERIPHQUAL		  0xE0      /* Peripheral Qualifier	    */
/* 
 *      Inquiry byte 1 mask
 */
#define SCSI_REMOVABLE_MEDIA  	  0x80    /* Removable Media bit (1=removable)  */
/* 
 *      Peripheral Device Type definitions
 */
#define SCSI_DASD	       	  0x00	   /* Direct-access Device	  */
#define SCSI_SEQACESS		  0x01	   /* Sequential-access device	  */
#define SCSI_PRINTER		  0x02	   /* Printer device		  */
#define SCSI_PROCESSOR		  0x03	   /* Processor device		  */
#define SCSI_WRITEONCE		  0x04	   /* Write-once device 	  */
#define SCSI_CDROM	    	  0x05	   /* CD-ROM device		  */
#define SCSI_SCANNER		  0x06	   /* Scanner device		  */
#define SCSI_OPTICAL		  0x07	   /* Optical memory device	  */
#define SCSI_MEDCHGR		  0x08	   /* Medium changer device	  */
#define SCSI_COMM		  0x09	   /* Communications device	  */
#define SCSI_NODEV		  0x1F	   /* Unknown or no device type   */
/*
 *      Inquiry flag definitions (Inq data byte 7)
 */
#define SCSI_INQ_RELADR       0x80    /* device supports relative addressing*/
#define SCSI_INQ_WBUS32       0x40    /* device supports 32 bit data xfers  */
#define SCSI_INQ_WBUS16       0x20    /* device supports 16 bit data xfers  */
#define SCSI_INQ_SYNC	      0x10    /* device supports synchronous xfer   */
#define SCSI_INQ_LINKED       0x08    /* device supports linked commands    */
#define SCSI_INQ_CMDQUEUE     0x02    /* device supports command queueing   */
#define SCSI_INQ_SFTRE	      0x01    /* device supports soft resets */
/*
 *==========================================================
 *                EEPROM byte offset
 *==========================================================
 */
typedef  struct  _EEprom {
	u_int8_t	EE_MODE1;
	u_int8_t	EE_SPEED;
	u_int8_t	xx1;
	u_int8_t	xx2;
} EEprom, *PEEprom;

#define EE_ADAPT_SCSI_ID	64
#define EE_MODE2        	65
#define EE_DELAY        	66
#define EE_TAG_CMD_NUM   	67

/*
 *    EE_MODE1 bits definition
 */
#define PARITY_CHK_     	0x00000001
#define SYNC_NEGO_      	0x00000002
#define EN_DISCONNECT_   	0x00000004
#define SEND_START_     	0x00000008
#define TAG_QUEUING_    	0x00000010

/*
 *    EE_MODE2 bits definition
 */
#define MORE2_DRV        	0x00000001
#define GREATER_1G      	0x00000002
#define RST_SCSI_BUS    	0x00000004
#define ACTIVE_NEGATION		0x00000008
#define NO_SEEK         	0x00000010
#define LUN_CHECK       	0x00000020

#define ENABLE_CE       	0x01
#define DISABLE_CE      	0x00
#define EEPROM_READ     	0x80

/*
 * The PCI configuration register offset for TRM_S1040	
 *                  Registers bit Definition		
 */
#define     TRMREG_ID	   	0x00	/* Vendor and Device ID	     	*/
#define     TRMREG_COMMAND  	0x04	/* PCI command register	       	*/
#define     TRMREG_IOBASE   	0x10	/* I/O Space base address     	*/
#define     TRMREG_ROMBASE  	0x30	/* Expansion ROM Base Address  	*/
#define     TRMREG_INTLINE  	0x3C	/* Interrupt line	       	*/

/*
 *
 * The SCSI register offset for TRM_S1040		
 *
 */
#define TRMREG_SCSI_STATUS   	0x80	/* SCSI Status (R)	      	*/
/* ######### */
#define     COMMANDPHASEDONE	0x2000	/* SCSI command phase done     	*/
#define     SCSIXFERDONE	    0x0800  /* SCSI SCSI transfer done	*/
#define     SCSIXFERCNT_2_ZERO  0x0100	/* SCSI SCSI transfer count to zero*/
#define     SCSIINTERRUPT       0x0080	/* SCSI interrupt pending     	*/
#define     COMMANDABORT        0x0040	/* SCSI command abort	       	*/
#define     SEQUENCERACTIVE     0x0020	/* SCSI sequencer active       	*/
#define     PHASEMISMATCH       0x0010	/* SCSI phase mismatch	       	*/
#define     PARITYERROR	        0x0008	/* SCSI parity error	       	*/

#define     PHASEMASK	        0x0007	/* Phase MSG/CD/IO	       	*/
#define 	PH_DATA_OUT	        0x00	/* Data out phase      	*/
#define 	PH_DATA_IN	        0x01	/* Data in phase       	*/
#define 	PH_COMMAND	        0x02	/* Command phase       	*/
#define 	PH_STATUS	        0x03	/* Status phase	       	*/
#define 	PH_BUS_FREE	        0x05	/* Invalid phase used as bus free	*/
#define 	PH_MSG_OUT	        0x06	/* Message out phase   	*/
#define 	PH_MSG_IN	        0x07	/* Message in phase    	*/

#define TRMREG_SCSI_CONTROL  	0x80	/* SCSI Control (W)	       	*/
/* ######### */
#define     DO_CLRATN	        0x0400	/* Clear ATN	        	*/
#define     DO_SETATN	        0x0200	/* Set ATN		       	*/
#define     DO_CMDABORT	        0x0100	/* Abort SCSI command   	*/
#define     DO_RSTMODULE        0x0010	/* Reset SCSI chip      	*/
#define     DO_RSTSCSI	        0x0008	/* Reset SCSI bus	       	*/
#define     DO_CLRFIFO	        0x0004	/* Clear SCSI transfer FIFO    	*/
#define     DO_DATALATCH    	0x0002	/* Enable SCSI bus data latch 	*/
#define     DO_HWRESELECT       0x0001	/* Enable hardware reselection 	*/
#define TRMREG_SCSI_FIFOCNT  	0x82	/* SCSI FIFO Counter 5bits(R) 	*/
#define TRMREG_SCSI_SIGNAL   	0x83	/* SCSI low level signal (R/W) 	*/
#define TRMREG_SCSI_INTSTATUS	0x84    /* SCSI Interrupt Status (R)   	*/
/* ######### */
#define     INT_SCAM	        0x80	/* SCAM selection interrupt    	*/
#define     INT_SELECT	        0x40	/* Selection interrupt	       	*/
#define     INT_SELTIMEOUT      0x20	/* Selection timeout interrupt 	*/
#define     INT_DISCONNECT      0x10	/* Bus disconnected interrupt  	*/
#define     INT_RESELECTED      0x08	/* Reselected interrupt	       	*/
#define     INT_SCSIRESET       0x04	/* SCSI reset detected interrupt*/
#define     INT_BUSSERVICE      0x02	/* Bus service interrupt       	*/
#define     INT_CMDDONE	        0x01	/* SCSI command done interrupt 	*/
#define TRMREG_SCSI_OFFSET   	0x84	/* SCSI Offset Count (W)       	*/
/*
 *   Bit		Name	        Definition
 *   07-05	0	RSVD	        Reversed. Always 0.
 *   04 	0	OFFSET4	        Reversed for LVDS. Always 0.
 *   03-00	0	OFFSET[03:00]	Offset number from 0 to 15
 */
#define TRMREG_SCSI_SYNC        0x85	/* SCSI Synchronous Control (R/W)*/
/* ######### */
#define     LVDS_SYNC	        0x20	/* Enable LVDS synchronous       */
#define     WIDE_SYNC	        0x10	/* Enable WIDE synchronous       */
#define     ALT_SYNC	        0x08	/* Enable Fast-20 alternate synchronous */
/*
 * SYNCM	7    6	  5	   4	3   	2   	1   	0
 * Name 	RSVD RSVD LVDS WIDE	ALTPERD	PERIOD2	PERIOD1	PERIOD0
 * Default	0	 0	  0	   0	0	    0	    0	    0
 *
 *
 * Bit		    Name                	Definition
 * 07-06	0	RSVD                	Reversed. Always read 0
 * 05   	0	LVDS                	Reversed. Always read 0
 * 04   	0	WIDE/WSCSI          	Enable wide (16-bits) SCSI transfer.
 * 03   	0	ALTPERD/ALTPD	        Alternate (Sync./Period) mode. 
 *
 *                                      @@ When this bit is set,
 *                                         the synchronous period bits 2:0 
 *                                         in the Synchronous Mode register
 *                                         are used to transfer data 
 *                                         at the Fast-20 rate.
 *                                      @@ When this bit is reset,
 *                                         the synchronous period bits 2:0 
 *                                         in the Synchronous Mode Register
 *                                         are used to transfer data 
 *                                         at the Fast-40 rate.
 *
 * 02-00	0	PERIOD[2:0]/SXPD[02:00]	Synchronous SCSI Transfer Rate.
 *                                      These 3 bits specify 
 *                                      the Synchronous SCSI Transfer Rate
 *                                      for Fast-20 and Fast-10.
 *                                      These bits are also reset
 *                                      by a SCSI Bus reset.
 *
 * For Fast-10 bit ALTPD = 0 and LVDS = 0 
 *     and 0x00000004,0x00000002,0x00000001 is defined as follows :
 *
 *  	   000	100ns, 10.0 Mbytes/s
 *   	   001	150ns,  6.6 Mbytes/s
 *  	   010	200ns,  5.0 Mbytes/s
 *  	   011	250ns,  4.0 Mbytes/s
 *   	   100	300ns,  3.3 Mbytes/s
 *  	   101	350ns,  2.8 Mbytes/s
 *	       110	400ns,  2.5 Mbytes/s
 *	       111	450ns,  2.2 Mbytes/s
 *
 * For Fast-20 bit ALTPD = 1 and LVDS = 0 
 *     and 0x00000004,0x00000002,0x00000001 is defined as follows :
 *
 *	       000	 50ns, 20.0 Mbytes/s
 *	       001	 75ns, 13.3 Mbytes/s
 *	       010	100ns, 10.0 Mbytes/s
 *	       011	125ns,  8.0 Mbytes/s
 *	       100	150ns,  6.6 Mbytes/s
 *	       101	175ns,  5.7 Mbytes/s
 *	       110	200ns,  5.0 Mbytes/s
 *	       111	250ns,  4.0 Mbytes/s
 *
 * For Fast-40 bit ALTPD = 0 and LVDS = 1
 *     and 0x00000004,0x00000002,0x00000001 is defined as follows :
 *
 *	       000	 25ns, 40.0 Mbytes/s
 *	       001	 50ns, 20.0 Mbytes/s
 *	       010	 75ns, 13.3 Mbytes/s
 *	       011	100ns, 10.0 Mbytes/s
 *	       100	125ns,  8.0 Mbytes/s
 *	       101	150ns,  6.6 Mbytes/s
 *	       110	175ns,  5.7 Mbytes/s
 *	       111	200ns,  5.0 Mbytes/s
 */

/*
 ***************************************
 */
#define TRMREG_SCSI_TARGETID 	0x86	/* SCSI Target ID (R/W)  	*/
/*
 ***************************************
 */
#define TRMREG_SCSI_IDMSG    	0x87	/* SCSI Identify Message (R)   	*/
/*
 ***************************************
 */
#define TRMREG_SCSI_HOSTID   	0x87	/* SCSI Host ID (W)	       	*/
/*
 ***************************************
 */
#define TRMREG_SCSI_COUNTER  	0x88	/* SCSI Transfer Counter 24bits(R/W)*/
/*
 ***************************************
 */
#define TRMREG_SCSI_INTEN    	0x8C	/* SCSI Interrupt Enable (R/W)   */
/* ######### */
#define     EN_SCAM	        0x80	/* Enable SCAM selection interrupt*/
#define     EN_SELECT	        0x40	/* Enable selection interrupt     */
#define     EN_SELTIMEOUT       0x20	/* Enable selection timeout interrupt*/
#define     EN_DISCONNECT       0x10	/* Enable bus disconnected interrupt*/
#define     EN_RESELECTED       0x08	/* Enable reselected interrupt   */
#define     EN_SCSIRESET        0x04	/* Enable SCSI reset detected interrupt*/
#define     EN_BUSSERVICE       0x02	/* Enable bus service interrupt  */
#define     EN_CMDDONE	        0x01	/* Enable SCSI command done interrupt*/
/*
 ***************************************
 */
#define TRMREG_SCSI_CONFIG0  	0x8D   	/* SCSI Configuration 0 (R/W)  	*/
/* ######### */
#define     PHASELATCH	        0x40	/* Enable phase latch	       	*/
#define     INITIATOR	        0x20	/* Enable initiator mode       	*/
#define     PARITYCHECK	        0x10	/* Enable parity check	       	*/
#define     BLOCKRST	        0x01	/* Disable SCSI reset1	       	*/
/*
 ***************************************
 */
#define TRMREG_SCSI_CONFIG1  	0x8E   	/* SCSI Configuration 1 (R/W)  	*/
/* ######### */
#define     ACTIVE_NEGPLUS      0x10	/* Enhance active negation     	*/
#define     FILTER_DISABLE      0x08	/* Disable SCSI data filter    	*/
#define     ACTIVE_NEG	        0x02	/* Enable active negation      	*/
#define	    ACTIVE_HISLEW	0x01	/* Enable high slew rate (3/6 ns) */
/*
 ***************************************
 */
#define TRMREG_SCSI_CONFIG2  	0x8F   	/* SCSI Configuration 2 (R/W)  	*/
/*
 ***************************************
 */
#define TRMREG_SCSI_COMMAND   	0x90   	/* SCSI Command (R/W)  		*/
/* ######### */
#define     SCMD_COMP	        0x12	/* Command complete            	*/
#define     SCMD_SEL_ATN        0x60	/* Selection with ATN  		*/
#define     SCMD_SEL_ATN3       0x64	/* Selection with ATN3 		*/
#define     SCMD_SEL_ATNSTOP    0xB8	/* Selection with ATN and Stop 	*/
#define     SCMD_FIFO_OUT       0xC0	/* SCSI FIFO transfer out      	*/
#define     SCMD_DMA_OUT        0xC1	/* SCSI DMA transfer out       	*/
#define     SCMD_FIFO_IN        0xC2	/* SCSI FIFO transfer in       	*/
#define     SCMD_DMA_IN	        0xC3	/* SCSI DMA transfer in	       	*/
#define     SCMD_MSGACCEPT      0xD8	/* Message accept	       	*/
/*
 *  Code	Command Description
 *
 *  02	    Enable reselection with FIFO
 *  40  	Select without ATN with FIFO
 *  60   	Select with ATN with FIFO
 *  64  	Select with ATN3 with FIFO
 *  A0  	Select with ATN and stop with FIFO
 *  C0  	Transfer information out with FIFO
 *  C1  	Transfer information out with DMA
 *  C2  	Transfer information in with FIFO
 *  C3  	Transfer information in with DMA
 *  12  	Initiator command complete with FIFO
 *  50  	Initiator transfer information out sequence without ATN with FIFO
 *  70  	Initiator transfer information out sequence with ATN with FIFO
 *  74  	Initiator transfer information out sequence with ATN3 with FIFO
 *  52  	Initiator transfer information in sequence without ATN with FIFO
 *  72   	Initiator transfer information in sequence with ATN with FIFO
 *  76	    Initiator transfer information in sequence with ATN3 with FIFO
 *  90  	Initiator transfer information out command complete with FIFO
 *  92  	Initiator transfer information in command complete with FIFO
 *  D2  	Enable selection
 *  08  	Reselection
 *  48  	Disconnect command with FIFO
 *  88  	Terminate command with FIFO
 *  C8  	Target command complete with FIFO
 *  18  	SCAM Arbitration/ Selection
 *  5A  	Enable reselection
 *  98  	Select without ATN with FIFO
 *  B8  	Select with ATN with FIFO
 *  D8  	Message Accepted
 *  58  	NOP
 */
/*
 ***************************************
 */
#define TRMREG_SCSI_TIMEOUT  	0x91	/* SCSI Time Out Value (R/W)   	*/
/*
 ***************************************
 */
#define TRMREG_SCSI_FIFO     	0x98	/* SCSI FIFO (R/W)	       	*/
/*
 ***************************************
 */
#define     TRMREG_SCSI_TCR00     	0x9C	/* SCSI Target Control 0 (R/W) 	*/
/* ######### */
#define     TCR0_DO_WIDE_NEGO     	0x80	/* Do wide NEGO		      	*/
#define     TCR0_DO_SYNC_NEGO      	0x40	/* Do sync NEGO	             	*/
#define     TCR0_DISCONNECT_EN	    	0x20	/* Disconnection enable     	*/
#define     TCR0_OFFSET_MASK	    	0x1F	/* Offset number	       	*/
/*
 ***************************************
 */
#define     TRMREG_SCSI_TCR01   	0x9D	/* SCSI Target Control 0 (R/W)  */
/* ######### */
#define     TCR0_ENABLE_LVDS    	0xF8	/* LVD   		   	*/
#define     TCR0_ENABLE_WIDE    	0xF9	/* SE       			*/
/*
****************************************
*/

/*
 ***************************************
 */
#define TRMREG_SCSI_TCR1     	0x9E   	/* SCSI Target Control 1 (R/W) 	*/
/* ######### */
#define     MAXTAG_MASK	        0x7F00	/* Maximum tags (127)	       	*/
#define     NON_TAG_BUSY        0x0080	/* Non tag command active      	*/
#define     ACTTAG_MASK	        0x007F	/* Active tags		      	*/
/*
 *
 * The DMA register offset for TRM_S1040				
 *
 */
#define TRMREG_DMA_COMMAND   	0xA0	/* DMA Command (R/W)	        	*/
/* ######### */
#define     XFERDATAIN	        0x0103 	/* Transfer data in	       	*/
#define     XFERDATAOUT	        0x0102	/* Transfer data out    	*/
/*
 ***************************************
 */
#define TRMREG_DMA_FIFOCNT   	0xA1	/* DMA FIFO Counter (R)	       	*/
/*
 ***************************************
 */
#define TRMREG_DMA_CONTROL   	0xA1	/* DMA Control (W)     		*/
/* ######### */
#define     STOPDMAXFER	        0x08	/* Stop  DMA transfer  		*/
#define     ABORTXFER	        0x04	/* Abort DMA transfer         	*/
#define     CLRXFIFO	        0x02	/* Clear DMA transfer FIFO     	*/
#define     STARTDMAXFER        0x01	/* Start DMA transfer     	*/
/*
 ***************************************
 */
#define TRMREG_DMA_STATUS    	0xA3	/* DMA Interrupt Status (R/W)  	*/
/* ######### */
#define     XFERPENDING	        0x80	/* Transfer pending	        */
#define     DMAXFERCOMP	        0x02    /* Bus Master XFER Complete status  */
#define     SCSICOMP	        0x01	/* SCSI complete interrupt     	*/
/*
 ***************************************
 */
#define TRMREG_DMA_INTEN  	    0xA4	/* DMA Interrupt Enable (R/W)*/
/* ######### */
#define     EN_SCSIINTR	        0x01	/* Enable SCSI complete interrupt   */
/*
 ***************************************
 */
#define TRMREG_DMA_CONFIG    	0xA6	/* DMA Configuration (R/W)     	*/
/* ######### */
#define     DMA_ENHANCE	        0x8000	/* Enable DMA enhance feature  	*/
/*
 ***************************************
 */
#define TRMREG_DMA_XCNT   	    0xA8	/* DMA Transfer Counter (R/W)*/
/*
 ***************************************
 */
#define TRMREG_DMA_CXCNT   	    0xAC	/* DMA Current Transfer Counter (R) */
/*
 ***************************************
 */
#define TRMREG_DMA_XLOWADDR  	0xB0	/* DMA Transfer Physical Low Address  */
/*
 ***************************************
 */
#define TRMREG_DMA_XHIGHADDR 	0xB4	/* DMA Transfer Physical High Address */

/*
 *
 * The general register offset for TRM_S1040	
 *
 */
#define TRMREG_GEN_CONTROL   	0xD4	/* Global Control	       	*/
/* ######### */
#define     EN_EEPROM	        0x10	/* Enable EEPROM programming   	*/
#define     AUTOTERM	        0x04	/* Enable Auto SCSI terminator 	*/
#define     LOW8TERM	        0x02	/* Enable Lower 8 bit SCSI terminator */
#define     UP8TERM	            0x01	/* Enable Upper 8 bit SCSI terminator */
/*
 ***************************************
 */
#define TRMREG_GEN_STATUS    	0xD5	/* Global Status	       	*/
/* ######### */
#define     GTIMEOUT	        0x80	/* Global timer reach 0 	*/
#define     CON5068	        0x10	/* External 50/68 pin connected	*/
#define     CON68	        0x08	/* Internal 68 pin connected   	*/
#define     CON50	        0x04	/* Internal 50 pin connected   	*/
#define     WIDESCSI	        0x02	/* Wide SCSI card	       	*/
/*
 ***************************************
 */
#define TRMREG_GEN_NVRAM     	0xD6	/* Serial NON-VOLATILE RAM port	*/
/* ######### */
#define     NVR_BITOUT	        0x08	/* Serial data out	       	*/
#define     NVR_BITIN	        0x04	/* Serial data in	       	*/
#define     NVR_CLOCK	        0x02	/* Serial clock		       	*/
#define     NVR_SELECT	        0x01	/* Serial select	       	*/
/*
 ***************************************
 */
#define TRMREG_GEN_EDATA     	0xD7	/* Parallel EEPROM data port   	*/
/*
 ***************************************
 */
#define TRMREG_GEN_EADDRESS  	0xD8	/* Parallel EEPROM address     	*/
/*
 ***************************************
 */
#define TRMREG_GEN_TIMER       	0xDB	/* Global timer	       		*/

/*
 * The SEEPROM structure for TRM_S1040			
 */
typedef struct NVRAM_TARGET_STRUCT
{
	u_int8_t	NvmTarCfg0;	/* Target configuration byte 0	*/
	u_int8_t	NvmTarPeriod;	/* Target period	       	*/
	u_int8_t	NvmTarCfg2;	/* Target configuration byte 2  */
	u_int8_t	NvmTarCfg3;	/* Target configuration byte 3 	*/
} NVRAMTARGETTYPE;
/*   NvmTarCfg0: Target configuration byte 0 :..pDCB->DevMode */
#define NTC_DO_WIDE_NEGO	    0x20    /* Wide negotiate	    	*/
#define NTC_DO_TAG_QUEUING  	0x10	/* Enable SCSI tag queuing	*/
#define NTC_DO_SEND_START       0x08    /* Send start command SPINUP*/
#define NTC_DO_DISCONNECT   	0x04	/* Enable SCSI disconnect	*/
#define NTC_DO_SYNC_NEGO    	0x02    /* Sync negotiation	    	*/
#define	NTC_DO_PARITY_CHK   	0x01    /* (it should define at NAC )
                                           Parity check enable		*/

/*
 *
 *
 *
 */
typedef struct NVRAM_STRUC {
	u_int8_t       	NvramSubVendorID[2];	 /*0,1  Sub Vendor ID	 */
	u_int8_t       	NvramSubSysID[2];	     /*2,3  Sub System ID*/
	u_int8_t       	NvramSubClass;		     /*4    Sub Class  	*/	
	u_int8_t       	NvramVendorID[2];	     /*5,6  Vendor ID  	*/
	u_int8_t       	NvramDeviceID[2];	     /*7,8  Device ID  	*/
	u_int8_t       	NvramReserved;		     /*9    Reserved   	*/
	NVRAMTARGETTYPE	NvramTarget[TRM_MAX_TARGETS];/*										  *10,11,12,13
	                                          *14,15,16,17									  * ....
						  * ....
						  *70,71,72,73
	                                          */
	u_int8_t       	NvramScsiId;	   /*74 Host Adapter SCSI ID	*/
	u_int8_t       	NvramChannelCfg;   /*75 Channel configuration	*/
	u_int8_t       	NvramDelayTime;	   /*76 Power on delay time	*/
	u_int8_t       	NvramMaxTag;	   /*77 Maximum tags	    	*/
	u_int8_t       	NvramReserved0;    /*78  */
	u_int8_t       	NvramBootTarget;   /*79  */
	u_int8_t       	NvramBootLun;      /*80  */
	u_int8_t       	NvramReserved1;    /*81  */
	u_int16_t      	Reserved[22];      /*82,..125 */
	u_int16_t      	NvramCheckSum;     /*126,127*/
} NVRAMTYPE,*PNVRAMTYPE;
/* Nvram Initiater bits definition */
#define MORE2_DRV       	0x00000001
#define GREATER_1G      	0x00000002
#define RST_SCSI_BUS    	0x00000004
#define ACTIVE_NEGATION    	0x00000008
#define NO_SEEK         	0x00000010
#define LUN_CHECK       	0x00000020

/* Nvram Adapter NvramChannelCfg bits definition */
#define NAC_SCANLUN	    	        0x20    /* Include LUN as BIOS device*/
#define NAC_POWERON_SCSI_RESET		0x04	/* Power on reset enable     */
#define NAC_GREATER_1G	           	 0x02	/* > 1G support enable	     */
#define NAC_GT2DRIVES		        0x01	/* Support more than 2 drives*/
/*
 *#define NAC_DO_PARITY_CHK       	0x08    // Parity check enable	    
 */

#endif /* trm_H */
