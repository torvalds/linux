/*	$OpenBSD: trm.h,v 1.9 2024/10/22 21:50:02 jsg Exp $
 * ------------------------------------------------------------
 *   O.S       : OpenBSD
 *   File Name : trm.h
 *   Device Driver for Tekram DC395U/UW/F,DC315/U
 *   PCI SCSI Bus Master Host Adapter
 *   (SCSI chip set used Tekram ASIC TRM-S1040)
 *
 * (C)Copyright 1995-1999 Tekram Technology Co., Ltd.
 * (C)Copyright 2001-2002 Ashley R. Martens and Kenneth R Westerback
 * ------------------------------------------------------------
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
 * ------------------------------------------------------------
 */

#ifndef _TRM_H
#define _TRM_H

/*
 * ------------------------------------------------------------
 * Segment Entry
 * ------------------------------------------------------------
 */
struct SGentry {
       u_int32_t        address;
       u_int32_t        length;
};

/*
 * -----------------------------------------------------------------------
 *     feature of chip set MAX value
 * -----------------------------------------------------------------------
 */

#define TRM_MAX_ADAPTER_NUM         4
#define TRM_MAX_TARGETS             16
#define TRM_MAX_LUNS		    8
#define TRM_MAX_SG_LISTENTRY        32
#define TRM_MAX_CMD_PER_LUN         32
#define TRM_MAX_SRB_CNT             TRM_MAX_CMD_PER_LUN*4
#define TRM_MAX_PHYSG_BYTE          ((TRM_MAX_SG_LISTENTRY - 1) << PGSHIFT)
#define TRM_MAX_SYNC_OFFSET         15
#define TRM_SEL_TIMEOUT             153  /* 250 ms selection timeout (@40MHz) */

/*
 *-----------------------------------------------------------------------
 *               SCSI Request Block
 *-----------------------------------------------------------------------
 */
struct trm_scsi_req_q {
	TAILQ_ENTRY(trm_scsi_req_q)      link;
	bus_dmamap_t	dmamapxfer;
	u_int32_t	PhysSRB;
	u_int32_t	SRBTotalXferLength;
	u_int32_t	SRBSGPhyAddr;        /* a segment starting address     */

	u_int16_t	SRBState;            /* State machine                  */
#define TRM_FREE                    0x0000
#define TRM_WAIT                    0x0001
#define TRM_READY                   0x0002
#define TRM_MSGOUT                  0x0004   /*arbitration+msg_out 1st byte    */
#define TRM_EXTEND_MSGIN            0x0010
#define TRM_COMMAND                 0x0020
#define TRM_START                   0x0040   /*arbitration+msg_out+command_out */
#define TRM_DISCONNECTED            0x0080
#define TRM_DATA_XFER               0x0100
#define TRM_XFERPAD                 0x0200
#define TRM_STATUS                  0x0400
#define TRM_COMPLETED               0x0800
#define TRM_ABORT_SENT              0x1000
#define TRM_UNEXPECT_RESEL          0x8000

	u_int8_t	AdaptStatus;
#define TRM_STATUS_GOOD             0x00
#define TRM_SELECTION_TIMED_OUT     0x11
#define TRM_OVER_UNDER_RUN          0x12
#define TRM_UNEXP_BUS_FREE          0x13
#define TRM_TARGET_PHASE_F          0x14
#define TRM_INVALID_CCB_OP          0x16
#define TRM_LINK_CCB_BAD            0x17
#define TRM_BAD_TARGET_DIR          0x18
#define TRM_DUPLICATE_CCB           0x19
#define TRM_BAD_CCB_OR_SG           0x1A
#define TRM_ABORT                   0xFF

	u_int8_t	CmdBlock[16];

	u_int8_t	ScsiCmdLen;
	u_int8_t	ScsiPhase;

	u_int8_t	SRBFlag;
#define TRM_AUTO_REQSENSE           0x01
#define TRM_SCSI_TIMED_OUT	    0x02
#define TRM_PARITY_ERROR            0x04
#define TRM_ON_GOING_SRB	    0x08
#define TRM_ON_WAITING_SRB	    0x10

	u_int8_t	SRBSGCount;
	u_int8_t	SRBSGIndex;
	u_int8_t	TagNumber;
#define TRM_NO_TAG                  0x00

	u_int8_t	TargetStatus;	    /* SCSI status codes + Tekram: */
#define TRM_SCSI_UNEXP_BUS_FREE     0xFD    /*    Unexpected Bus Free      */
#define TRM_SCSI_BUS_RST_DETECTED   0xFE    /*    Scsi Bus Reset detected  */
#define TRM_SCSI_SELECT_TIMEOUT     0xFF    /*    Selection Time out       */

	struct	trm_dcb *pSRBDCB;

	struct	SGentry SegmentX[TRM_MAX_SG_LISTENTRY];

	struct	scsi_xfer      *xs;

	struct	scsi_sense_data scsisense;
	u_int32_t scsisensePhyAddr;
};

TAILQ_HEAD(SRB_HEAD, trm_scsi_req_q);

/*
 *-----------------------------------------------------------------------
 *                   Device Control Block
 *-----------------------------------------------------------------------
 */
struct trm_dcb {
	u_int32_t	TagMask;

	u_int16_t	DCBFlag;
#define TRM_WIDE_NEGO_ENABLE    0x0001
#define TRM_DOING_WIDE_NEGO     0x0002
#define TRM_WIDE_NEGO_DONE      0x0004
#define TRM_SYNC_NEGO_ENABLE    0x0008
#define TRM_DOING_SYNC_NEGO     0x0010
#define TRM_USE_TAG_QUEUING     0x0020
#define TRM_QUEUE_FULL          0x0040
#define TRM_WIDE_NEGO_16BIT     0x0080
#define TRM_QUIRKS_VALID        0x0100
#define TRM_BAD_DCB             0x0200

	u_int8_t	DevMode;	/* trm_target_nvram.NvmTarCfg0    */

	u_int8_t	MaxNegoPeriod;  /* Maximum allow sync period      */
	u_int8_t	SyncPeriod;     /* Current sync period            */
	u_int8_t	SyncOffset;     /* Current sync offset            */

	u_int8_t	target;         /* SCSI Target ID                 */
	u_int8_t	lun;            /* SCSI Logical Unit Number       */

	u_int8_t	IdentifyMsg;

	struct	scsi_link	*sc_link;
	struct	trm_scsi_req_q	*pActiveSRB;
};

/*
 *-----------------------------------------------------------------------
 *                  Adapter Control Block
 *-----------------------------------------------------------------------
 */
struct trm_softc {
	struct	device	sc_device;

	bus_space_handle_t	sc_iohandle;
	bus_space_tag_t	sc_iotag;
	bus_dma_tag_t	sc_dmatag;
	bus_dmamap_t	sc_dmamap_control; /* map the control structures */

	u_int16_t	sc_AdapterUnit;       /* nth Adapter this driver */

	u_int8_t	sc_AdaptSCSIID;       /* Adapter SCSI Target ID */
	u_int8_t	sc_TagMaxNum;

	u_int8_t	sc_config;
#define HCC_WIDE_CARD		0x20
#define HCC_SCSI_RESET		0x10
#define HCC_PARITY		0x08
#define HCC_AUTOTERM		0x04
#define HCC_LOW8TERM		0x02
#define HCC_UP8TERM		0x01

	u_int8_t	sc_Flag;
#define RESET_DEV		0x01
#define RESET_DETECT		0x02
#define RESET_DONE		0x04

	u_int8_t	MsgCnt;
	u_int8_t	MsgBuf[6];

	struct	SRB_HEAD	freeSRB;
	struct	SRB_HEAD	goingSRB;
	struct	SRB_HEAD	waitingSRB;

	struct	mutex		sc_srb_mtx;
	struct	scsi_iopool	sc_iopool;

	struct	trm_dcb	       *pActiveDCB;
	struct	trm_dcb        *pDCB[TRM_MAX_TARGETS][TRM_MAX_LUNS];

	struct	trm_scsi_req_q *SRB;
};

/*
 * The SEEPROM structure for TRM_S1040
 */
struct trm_target_nvram {
    u_int8_t    NvmTarCfg0;        	/* Target configuration byte 0    */
#define TRM_WIDE                0x20    /* Wide negotiate                 */
#define TRM_TAG_QUEUING         0x10    /* Enable SCSI tag queuing        */
#define TRM_SEND_START          0x08    /* Send start command SPINUP      */
#define TRM_DISCONNECT          0x04    /* Enable SCSI disconnect         */
#define TRM_SYNC                0x02    /* Sync negotiation               */
#define TRM_PARITY              0x01    /* (it should be defined at NAC ) */

    u_int8_t    NvmTarPeriod;      	/* Target period                  */
    u_int8_t    NvmTarCfg2;        	/* Target configuration byte 2    */
    u_int8_t    NvmTarCfg3;        	/* Target configuration byte 3    */
};

struct trm_adapter_nvram {
    u_int8_t         NvramSubVendorID[2];     /*0,1  Sub Vendor ID            */
    u_int8_t         NvramSubSysID[2];        /*2,3  Sub System ID            */
    u_int8_t         NvramSubClass;           /*4    Sub Class                */
    u_int8_t         NvramVendorID[2];        /*5,6  Vendor ID                */
    u_int8_t         NvramDeviceID[2];        /*7,8  Device ID                */
    u_int8_t         NvramReserved;           /*9    Reserved                 */
    struct trm_target_nvram  NvramTarget[TRM_MAX_TARGETS]; /* 10              */
    u_int8_t         NvramScsiId;             /*74 Host Adapter SCSI ID       */
    u_int8_t         NvramChannelCfg;         /*75 Channel configuration      */
#define NAC_SCANLUN                 0x20      /*   Include LUN as BIOS device */
#define NAC_POWERON_SCSI_RESET      0x04      /*   Power on reset enable      */
#define NAC_GREATER_1G              0x02      /*   > 1G support enable        */
#define NAC_GT2DRIVES               0x01      /*   Support more than 2 drives */
    u_int8_t         NvramDelayTime;          /*76 Power on delay time        */
    u_int8_t         NvramMaxTag;             /*77 Maximum tags               */
    u_int8_t         NvramReserved0;          /*78                            */
    u_int8_t         NvramBootTarget;         /*79                            */
    u_int8_t         NvramBootLun;            /*80                            */
    u_int8_t         NvramReserved1;          /*81                            */
    u_int16_t        Reserved[22];            /*82,..125                      */
    u_int16_t        NvramCheckSum;           /*126,127                       */
};

/*
 * The PCI configuration register offsets for the TRM_S1040, and
 * the associated bit definitions.
 */

#define TRM_S1040_ID            0x00    /* Vendor and Device ID               */
#define TRM_S1040_COMMAND       0x04    /* PCI command register               */
#define TRM_S1040_IOBASE        0x10    /* I/O Space base address             */
#define TRM_S1040_ROMBASE       0x30    /* Expansion ROM Base Address         */
#define TRM_S1040_INTLINE       0x3C    /* Interrupt line                     */

#define TRM_S1040_SCSI_STATUS   0x80    /* SCSI Status (R)                    */
#define     COMMANDPHASEDONE    0x2000  /* SCSI command phase done            */
#define     SCSIXFERDONE        0x0800  /* SCSI SCSI transfer done            */
#define     SCSIXFERCNT_2_ZERO  0x0100  /* SCSI SCSI transfer count to zero   */
#define     SCSIINTERRUPT       0x0080  /* SCSI interrupt pending             */
#define     COMMANDABORT        0x0040  /* SCSI command abort                 */
#define     SEQUENCERACTIVE     0x0020  /* SCSI sequencer active              */
#define     PHASEMISMATCH       0x0010  /* SCSI phase mismatch                */
#define     PARITYERROR         0x0008  /* SCSI parity error                  */
#define     PHASEMASK           0x0007  /* Phase MSG/CD/IO                    */
#define     PH_DATA_OUT         0x00    /* Data out phase                     */
#define     PH_DATA_IN          0x01    /* Data in phase                      */
#define     PH_COMMAND          0x02    /* Command phase                      */
#define     PH_STATUS           0x03    /* Status phase                       */
#define     PH_BUS_FREE         0x05    /* Invalid phase used as bus free     */
#define     PH_MSG_OUT          0x06    /* Message out phase                  */
#define     PH_MSG_IN           0x07    /* Message in phase                   */
#define TRM_S1040_SCSI_CONTROL  0x80    /* SCSI Control (W)                   */
#define     DO_CLRATN           0x0400  /* Clear ATN                          */
#define     DO_SETATN           0x0200  /* Set ATN                            */
#define     DO_CMDABORT         0x0100  /* Abort SCSI command                 */
#define     DO_RSTMODULE        0x0010  /* Reset SCSI chip                    */
#define     DO_RSTSCSI          0x0008  /* Reset SCSI bus                     */
#define     DO_CLRFIFO          0x0004  /* Clear SCSI transfer FIFO           */
#define     DO_DATALATCH        0x0002  /* Enable SCSI bus data latch         */
#define     DO_HWRESELECT       0x0001  /* Enable hardware reselection        */
#define TRM_S1040_SCSI_FIFOCNT  0x82    /* SCSI FIFO Counter 5bits(R)         */
#define TRM_S1040_SCSI_SIGNAL   0x83    /* SCSI low level signal (R/W)        */
#define TRM_S1040_SCSI_INTSTATUS    0x84    /* SCSI Interrupt Status (R)      */
#define     INT_SCAM            0x80    /* SCAM selection interrupt           */
#define     INT_SELECT          0x40    /* Selection interrupt                */
#define     INT_SELTIMEOUT      0x20    /* Selection timeout interrupt        */
#define     INT_DISCONNECT      0x10    /* Bus disconnected interrupt         */
#define     INT_RESELECTED      0x08    /* Reselected interrupt               */
#define     INT_SCSIRESET       0x04    /* SCSI reset detected interrupt      */
#define     INT_BUSSERVICE      0x02    /* Bus service interrupt              */
#define     INT_CMDDONE         0x01    /* SCSI command done interrupt        */
#define TRM_S1040_SCSI_OFFSET   0x84    /* SCSI Offset Count (W)              */
/*
 *   Bit           Name            Definition
 *   07-05    0    RSVD            Reversed. Always 0.
 *   04       0    OFFSET4         Reversed for LVDS. Always 0.
 *   03-00    0    OFFSET[03:00]   Offset number from 0 to 15
 */
#define TRM_S1040_SCSI_SYNC     0x85    /* SCSI Synchronous Control (R/W)     */
#define     LVDS_SYNC           0x20    /* Enable LVDS synchronous            */
#define     WIDE_SYNC           0x10    /* Enable WIDE synchronous            */
#define     ALT_SYNC            0x08    /* Enable Fast-20 alternate synchronous */
/*
 * SYNCM       7    6    5    4          3          2          1          0
 * Name     RSVD RSVD LVDS WIDE    ALTPERD    PERIOD2    PERIOD1    PERIOD0
 * Default     0    0    0    0          0          0          0          0
 *
 *
 * Bit           Name                   Definition
 * ---           ----                   ----------
 * 07-06    0    RSVD                   Reversed. Always read 0
 * 05       0    LVDS                   Reversed. Always read 0
 * 04       0    WIDE/WSCSI             Enable wide (16-bits) SCSI transfer.
 * 03       0    ALTPERD/ALTPD          Alternate (Sync./Period) mode.
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
 * 02-00    0    PERIOD[2:0]/SXPD[02:00]    Synchronous SCSI Transfer Rate.
 *                                      These 3 bits specify
 *                                      the Synchronous SCSI Transfer Rate
 *                                      for Fast-20 and Fast-10.
 *                                      These bits are also reset
 *                                      by a SCSI Bus reset.
 *
 * For Fast-10 bit ALTPD = 0 and LVDS = 0
 *     and bit2,bit1,bit0 is defined as follows :
 *
 *           000    100ns, 10.0 Mbytes/s
 *           001    150ns,  6.6 Mbytes/s
 *           010    200ns,  5.0 Mbytes/s
 *           011    250ns,  4.0 Mbytes/s
 *           100    300ns,  3.3 Mbytes/s
 *           101    350ns,  2.8 Mbytes/s
 *           110    400ns,  2.5 Mbytes/s
 *           111    450ns,  2.2 Mbytes/s
 *
 * For Fast-20 bit ALTPD = 1 and LVDS = 0
 *     and bit2,bit1,bit0 is defined as follows :
 *
 *           000     50ns, 20.0 Mbytes/s
 *           001     75ns, 13.3 Mbytes/s
 *           010    100ns, 10.0 Mbytes/s
 *           011    125ns,  8.0 Mbytes/s
 *           100    150ns,  6.6 Mbytes/s
 *           101    175ns,  5.7 Mbytes/s
 *           110    200ns,  5.0 Mbytes/s
 *           111    250ns,  4.0 Mbytes/s
 *
 * For Fast-40 bit ALTPD = 0 and LVDS = 1
 *     and bit2,bit1,bit0 is defined as follows :
 *
 *           000     25ns, 40.0 Mbytes/s
 *           001     50ns, 20.0 Mbytes/s
 *           010     75ns, 13.3 Mbytes/s
 *           011    100ns, 10.0 Mbytes/s
 *           100    125ns,  8.0 Mbytes/s
 *           101    150ns,  6.6 Mbytes/s
 *           110    175ns,  5.7 Mbytes/s
 *           111    200ns,  5.0 Mbytes/s
 */
#define TRM_S1040_SCSI_TARGETID 0x86    /* SCSI Target ID (R/W)               */
#define TRM_S1040_SCSI_IDMSG    0x87    /* SCSI Identify Message (R)          */
#define TRM_S1040_SCSI_HOSTID   0x87    /* SCSI Host ID (W)                   */
#define TRM_S1040_SCSI_COUNTER  0x88    /* SCSI Transfer Counter 24bits(R/W)  */
#define TRM_S1040_SCSI_INTEN    0x8C    /* SCSI Interrupt Enable (R/W)        */
#define     EN_SCAM             0x80    /* Enable SCAM selection interrupt    */
#define     EN_SELECT           0x40    /* Enable selection interrupt         */
#define     EN_SELTIMEOUT       0x20    /* Enable selection timeout interrupt */
#define     EN_DISCONNECT       0x10    /* Enable bus disconnected interrupt  */
#define     EN_RESELECTED       0x08    /* Enable reselected interrupt        */
#define     EN_SCSIRESET        0x04    /* Enable SCSI reset detected interrupt*/
#define     EN_BUSSERVICE       0x02    /* Enable bus service interrupt       */
#define     EN_CMDDONE          0x01    /* Enable SCSI command done interrupt */
#define TRM_S1040_SCSI_CONFIG0  0x8D    /* SCSI Configuration 0 (R/W)         */
#define     PHASELATCH          0x40    /* Enable phase latch                 */
#define     INITIATOR           0x20    /* Enable initiator mode              */
#define     PARITYCHECK         0x10    /* Enable parity check                */
#define     BLOCKRST            0x01    /* Disable SCSI reset1                */
#define TRM_S1040_SCSI_CONFIG1  0x8E    /* SCSI Configuration 1 (R/W)         */
#define     ACTIVE_NEGPLUS      0x10    /* Enhance active negation            */
#define     FILTER_DISABLE      0x08    /* Disable SCSI data filter           */
#define     ACTIVE_NEG          0x02    /* Enable active negation             */
#define TRM_S1040_SCSI_CONFIG2  0x8F    /* SCSI Configuration 2 (R/W)         */
#define TRM_S1040_SCSI_COMMAND  0x90    /* SCSI Command (R/W)                 */
#define     SCMD_COMP           0x12    /* Command complete                   */
#define     SCMD_SEL_ATN        0x60    /* Selection with ATN                 */
#define     SCMD_SEL_ATN3       0x64    /* Selection with ATN3                */
#define     SCMD_SEL_ATNSTOP    0xB8    /* Selection with ATN and Stop        */
#define     SCMD_FIFO_OUT       0xC0    /* SCSI FIFO transfer out             */
#define     SCMD_DMA_OUT        0xC1    /* SCSI DMA transfer out              */
#define     SCMD_FIFO_IN        0xC2    /* SCSI FIFO transfer in              */
#define     SCMD_DMA_IN         0xC3    /* SCSI DMA transfer in               */
#define     SCMD_MSGACCEPT      0xD8    /* Message accept                     */
/*
 *  Code    Command Description
 *
 *  02      Enable reselection with FIFO
 *  40      Select without ATN with FIFO
 *  60      Select with ATN with FIFO
 *  64      Select with ATN3 with FIFO
 *  A0      Select with ATN and stop with FIFO
 *  C0      Transfer information out with FIFO
 *  C1      Transfer information out with DMA
 *  C2      Transfer information in with FIFO
 *  C3      Transfer information in with DMA
 *  12      Initiator command complete with FIFO
 *  50      Initiator transfer information out sequence without ATN  with FIFO
 *  70      Initiator transfer information out sequence with    ATN  with FIFO
 *  74      Initiator transfer information out sequence with    ATN3 with FIFO
 *  52      Initiator transfer information in  sequence without ATN  with FIFO
 *  72      Initiator transfer information in  sequence with    ATN  with FIFO
 *  76      Initiator transfer information in  sequence with    ATN3 with FIFO
 *  90      Initiator transfer information out command complete with FIFO
 *  92      Initiator transfer information in  command complete with FIFO
 *  D2      Enable selection
 *  08      Reselection
 *  48      Disconnect command with FIFO
 *  88      Terminate command with FIFO
 *  C8      Target command complete with FIFO
 *  18      SCAM Arbitration/ Selection
 *  5A      Enable reselection
 *  98      Select without ATN with FIFO
 *  B8      Select with ATN with FIFO
 *  D8      Message Accepted
 *  58      NOP
 */
#define TRM_S1040_SCSI_TIMEOUT  0x91    /* SCSI Time Out Value (R/W)          */
#define TRM_S1040_SCSI_FIFO     0x98    /* SCSI FIFO (R/W)                    */
#define TRM_S1040_SCSI_TCR0     0x9C    /* SCSI Target Control 0 (R/W)        */
#define     TCR0_WIDE_NEGO_DONE 0x8000  /* Wide        nego done              */
#define     TCR0_SYNC_NEGO_DONE 0x4000  /* Synchronous nego done              */
#define     TCR0_ENABLE_LVDS    0x2000  /* Enable LVDS synchronous            */
#define     TCR0_ENABLE_WIDE    0x1000  /* Enable WIDE synchronous            */
#define     TCR0_ENABLE_ALT     0x0800  /* Enable alternate synchronous       */
#define     TCR0_PERIOD_MASK    0x0700  /* Transfer rate                      */
#define     TCR0_DO_WIDE_NEGO   0x0080  /* Do wide NEGO                       */
#define     TCR0_DO_SYNC_NEGO   0x0040  /* Do sync NEGO                       */
#define     TCR0_DISCONNECT_EN  0x0020  /* Disconnection enable               */
#define     TCR0_OFFSET_MASK    0x001F  /* Offset number                      */
#define TRM_S1040_SCSI_TCR1     0x9E    /* SCSI Target Control 1 (R/W)        */
#define     MAXTAG_MASK         0x7F00  /* Maximum tags (127)                 */
#define     NON_TAG_BUSY        0x0080  /* Non tag command active             */
#define     ACTTAG_MASK         0x007F  /* Active tags                        */

#define TRM_S1040_DMA_COMMAND   0xA0    /* DMA Command (R/W)                  */
#define     XFERDATAIN          0x0103  /* Transfer data in                   */
#define     XFERDATAOUT         0x0102  /* Transfer data out                  */
#define TRM_S1040_DMA_FIFOCNT   0xA1    /* DMA FIFO Counter (R)               */
#define TRM_S1040_DMA_CONTROL   0xA1    /* DMA Control (W)                    */
#define     STOPDMAXFER         0x08    /* Stop  DMA transfer                 */
#define     ABORTXFER           0x04    /* Abort DMA transfer                 */
#define     CLRXFIFO            0x02    /* Clear DMA transfer FIFO            */
#define     STARTDMAXFER        0x01    /* Start DMA transfer                 */
#define TRM_S1040_DMA_STATUS    0xA3    /* DMA Interrupt Status (R/W)         */
#define     XFERPENDING         0x80    /* Transfer pending                   */
#define     DMAXFERCOMP         0x02    /* Bus Master XFER Complete status    */
#define     SCSICOMP            0x01    /* SCSI complete interrupt            */
#define TRM_S1040_DMA_INTEN     0xA4    /* DMA Interrupt Enable (R/W)         */
#define     EN_SCSIINTR         0x01    /* Enable SCSI complete interrupt     */
#define TRM_S1040_DMA_CONFIG    0xA6    /* DMA Configuration (R/W)            */
#define     DMA_ENHANCE         0x8000  /* Enable DMA enhance feature         */
#define TRM_S1040_DMA_XCNT      0xA8    /* DMA Transfer Counter (R/W)         */
#define TRM_S1040_DMA_CXCNT     0xAC    /* DMA Current Transfer Counter (R)   */
#define TRM_S1040_DMA_XLOWADDR  0xB0    /* DMA Transfer Physical Low Address  */
#define TRM_S1040_DMA_XHIGHADDR 0xB4    /* DMA Transfer Physical High Address */

#define TRM_S1040_GEN_CONTROL   0xD4    /* Global Control                     */
#define     EN_EEPROM           0x10    /* Enable EEPROM programming          */
#define     AUTOTERM            0x04    /* Enable Auto SCSI terminator        */
#define     LOW8TERM            0x02    /* Enable Lower 8 bit SCSI terminator */
#define     UP8TERM             0x01    /* Enable Upper 8 bit SCSI terminator */
#define TRM_S1040_GEN_STATUS    0xD5    /* Global Status                      */
#define     GTIMEOUT            0x80    /* Global timer reach 0               */
#define     CON5068             0x10    /* External 50/68 pin connected       */
#define     CON68               0x08    /* Internal 68 pin connected          */
#define     CON50               0x04    /* Internal 50 pin connected          */
#define     WIDESCSI            0x02    /* Wide SCSI card                     */
#define TRM_S1040_GEN_NVRAM     0xD6    /* Serial NON-VOLATILE RAM port       */
#define     NVR_BITOUT          0x08    /* Serial data out                    */
#define     NVR_BITIN           0x04    /* Serial data in                     */
#define     NVR_CLOCK           0x02    /* Serial clock                       */
#define     NVR_SELECT          0x01    /* Serial select                      */
#define TRM_S1040_GEN_EDATA     0xD7    /* Parallel EEPROM data port          */
#define TRM_S1040_GEN_EADDRESS  0xD8    /* Parallel EEPROM address            */
#define TRM_S1040_GEN_TIMER     0xDB    /* Global timer                       */

int   trm_Interrupt(void *);
int   trm_init(struct trm_softc *, int);
void  trm_scsi_cmd(struct scsi_xfer *);


#endif /* trm_h */
