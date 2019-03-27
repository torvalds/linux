/*-
 * SPDX-License-Identifier: (BSD-2-Clause-FreeBSD AND BSD-4-Clause)
 *
 * Copyright (c) 1999, 2000 Kenneth D. Merry.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 *	$FreeBSD$
 */
/*
 * The ti_stats structure below is from code with the following copyright, 
 * and originally comes from the Alteon firmware documentation.
 */
/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: if_tireg.h,v 1.8 1999/07/23 18:46:24 wpaul Exp $
 */

#ifndef _SYS_TIIO_H_
#define _SYS_TIIO_H_

#include <sys/ioccom.h>

/*
 * Tigon statistics counters.
 */
struct ti_stats {
	/*
	 * MAC stats, taken from RFC 1643, ethernet-like MIB
	 */
	volatile u_int32_t dot3StatsAlignmentErrors;		/* 0 */
	volatile u_int32_t dot3StatsFCSErrors;			/* 1 */
	volatile u_int32_t dot3StatsSingleCollisionFrames;	/* 2 */
	volatile u_int32_t dot3StatsMultipleCollisionFrames;	/* 3 */
	volatile u_int32_t dot3StatsSQETestErrors;		/* 4 */
	volatile u_int32_t dot3StatsDeferredTransmissions;	/* 5 */
	volatile u_int32_t dot3StatsLateCollisions;		/* 6 */
	volatile u_int32_t dot3StatsExcessiveCollisions;	/* 7 */
	volatile u_int32_t dot3StatsInternalMacTransmitErrors;	/* 8 */
	volatile u_int32_t dot3StatsCarrierSenseErrors;		/* 9 */
	volatile u_int32_t dot3StatsFrameTooLongs;		/* 10 */
	volatile u_int32_t dot3StatsInternalMacReceiveErrors;	/* 11 */
	/*
	 * interface stats, taken from RFC 1213, MIB-II, interfaces group
	 */
	volatile u_int32_t ifIndex;				/* 12 */
	volatile u_int32_t ifType;				/* 13 */
	volatile u_int32_t ifMtu;				/* 14 */
	volatile u_int32_t ifSpeed;				/* 15 */
	volatile u_int32_t ifAdminStatus;			/* 16 */
#define IF_ADMIN_STATUS_UP      1
#define IF_ADMIN_STATUS_DOWN    2
#define IF_ADMIN_STATUS_TESTING 3
	volatile u_int32_t ifOperStatus;			/* 17 */
#define IF_OPER_STATUS_UP       1
#define IF_OPER_STATUS_DOWN     2
#define IF_OPER_STATUS_TESTING  3
#define IF_OPER_STATUS_UNKNOWN  4
#define IF_OPER_STATUS_DORMANT  5
	volatile u_int32_t ifLastChange;			/* 18 */
	volatile u_int32_t ifInDiscards;			/* 19 */
	volatile u_int32_t ifInErrors;				/* 20 */
	volatile u_int32_t ifInUnknownProtos;			/* 21 */
	volatile u_int32_t ifOutDiscards;			/* 22 */
	volatile u_int32_t ifOutErrors;				/* 23 */
	volatile u_int32_t ifOutQLen;     /* deprecated */	/* 24 */
	volatile u_int8_t  ifPhysAddress[8]; /* 8 bytes */	/* 25 - 26 */
	volatile u_int8_t  ifDescr[32];				/* 27 - 34 */
	u_int32_t alignIt;      /* align to 64 bit for u_int64_ts following */
	/*
	 * more interface stats, taken from RFC 1573, MIB-IIupdate,
	 * interfaces group
	 */
	volatile u_int64_t ifHCInOctets;			/* 36 - 37 */
	volatile u_int64_t ifHCInUcastPkts;			/* 38 - 39 */
	volatile u_int64_t ifHCInMulticastPkts;			/* 40 - 41 */
	volatile u_int64_t ifHCInBroadcastPkts;			/* 42 - 43 */
	volatile u_int64_t ifHCOutOctets;			/* 44 - 45 */
	volatile u_int64_t ifHCOutUcastPkts;			/* 46 - 47 */
	volatile u_int64_t ifHCOutMulticastPkts;		/* 48 - 49 */
	volatile u_int64_t ifHCOutBroadcastPkts;		/* 50 - 51 */
	volatile u_int32_t ifLinkUpDownTrapEnable;		/* 52 */
	volatile u_int32_t ifHighSpeed;				/* 53 */
	volatile u_int32_t ifPromiscuousMode; 			/* 54 */
	volatile u_int32_t ifConnectorPresent; /* follow link state 55 */
	/*
	 * Host Commands
	 */
	volatile u_int32_t nicCmdsHostState;			/* 56 */
	volatile u_int32_t nicCmdsFDRFiltering;			/* 57 */
	volatile u_int32_t nicCmdsSetRecvProdIndex;		/* 58 */
	volatile u_int32_t nicCmdsUpdateGencommStats;		/* 59 */
	volatile u_int32_t nicCmdsResetJumboRing;		/* 60 */
	volatile u_int32_t nicCmdsAddMCastAddr;			/* 61 */
	volatile u_int32_t nicCmdsDelMCastAddr;			/* 62 */
	volatile u_int32_t nicCmdsSetPromiscMode;		/* 63 */
	volatile u_int32_t nicCmdsLinkNegotiate;		/* 64 */
	volatile u_int32_t nicCmdsSetMACAddr;			/* 65 */
	volatile u_int32_t nicCmdsClearProfile;			/* 66 */
	volatile u_int32_t nicCmdsSetMulticastMode;		/* 67 */
	volatile u_int32_t nicCmdsClearStats;			/* 68 */
	volatile u_int32_t nicCmdsSetRecvJumboProdIndex;	/* 69 */
	volatile u_int32_t nicCmdsSetRecvMiniProdIndex;		/* 70 */
	volatile u_int32_t nicCmdsRefreshStats;			/* 71 */
	volatile u_int32_t nicCmdsUnknown;			/* 72 */
	/*
	 * NIC Events
	 */
	volatile u_int32_t nicEventsNICFirmwareOperational;	/* 73 */
	volatile u_int32_t nicEventsStatsUpdated;		/* 74 */
	volatile u_int32_t nicEventsLinkStateChanged;		/* 75 */
	volatile u_int32_t nicEventsError;			/* 76 */
	volatile u_int32_t nicEventsMCastListUpdated;		/* 77 */
	volatile u_int32_t nicEventsResetJumboRing;		/* 78 */
	/*
	 * Ring manipulation
	 */
	volatile u_int32_t nicRingSetSendProdIndex;		/* 79 */
	volatile u_int32_t nicRingSetSendConsIndex;		/* 80 */
	volatile u_int32_t nicRingSetRecvReturnProdIndex;	/* 81 */
	/*
	 * Interrupts
	 */
	volatile u_int32_t nicInterrupts;			/* 82 */
	volatile u_int32_t nicAvoidedInterrupts;		/* 83 */
	/*
	 * BD Coalessing Thresholds
	 */
	volatile u_int32_t nicEventThresholdHit;		/* 84 */
	volatile u_int32_t nicSendThresholdHit;			/* 85 */
	volatile u_int32_t nicRecvThresholdHit;			/* 86 */
	/*
	 * DMA Attentions
	 */
	volatile u_int32_t nicDmaRdOverrun;			/* 87 */
	volatile u_int32_t nicDmaRdUnderrun;			/* 88 */
	volatile u_int32_t nicDmaWrOverrun;			/* 89 */
	volatile u_int32_t nicDmaWrUnderrun;			/* 90 */
	volatile u_int32_t nicDmaWrMasterAborts;		/* 91 */
	volatile u_int32_t nicDmaRdMasterAborts;		/* 92 */
	/*
	 * NIC Resources
	 */
	volatile u_int32_t nicDmaWriteRingFull;			/* 93 */
	volatile u_int32_t nicDmaReadRingFull;			/* 94 */
	volatile u_int32_t nicEventRingFull;			/* 95 */
	volatile u_int32_t nicEventProducerRingFull;		/* 96 */
	volatile u_int32_t nicTxMacDescrRingFull;		/* 97 */
	volatile u_int32_t nicOutOfTxBufSpaceFrameRetry;	/* 98 */
	volatile u_int32_t nicNoMoreWrDMADescriptors;		/* 99 */
	volatile u_int32_t nicNoMoreRxBDs;			/* 100 */
	volatile u_int32_t nicNoSpaceInReturnRing;		/* 101 */
	volatile u_int32_t nicSendBDs;            /* current count 102 */
	volatile u_int32_t nicRecvBDs;            /* current count 103 */
	volatile u_int32_t nicJumboRecvBDs;       /* current count 104 */
	volatile u_int32_t nicMiniRecvBDs;        /* current count 105 */
	volatile u_int32_t nicTotalRecvBDs;       /* current count 106 */
	volatile u_int32_t nicTotalSendBDs;       /* current count 107 */
	volatile u_int32_t nicJumboSpillOver;			/* 108 */
	volatile u_int32_t nicSbusHangCleared;			/* 109 */
	volatile u_int32_t nicEnqEventDelayed;			/* 110 */
	/*
	 * Stats from MAC rx completion
	 */
	volatile u_int32_t nicMacRxLateColls;			/* 111 */
	volatile u_int32_t nicMacRxLinkLostDuringPkt;		/* 112 */
	volatile u_int32_t nicMacRxPhyDecodeErr;		/* 113 */
	volatile u_int32_t nicMacRxMacAbort;			/* 114 */
	volatile u_int32_t nicMacRxTruncNoResources;		/* 115 */
	/*
	 * Stats from the mac_stats area
	 */
	volatile u_int32_t nicMacRxDropUla;			/* 116 */
	volatile u_int32_t nicMacRxDropMcast;			/* 117 */
	volatile u_int32_t nicMacRxFlowControl;			/* 118 */
	volatile u_int32_t nicMacRxDropSpace;			/* 119 */
	volatile u_int32_t nicMacRxColls;			/* 120 */
	/*
 	 * MAC RX Attentions
	 */
	volatile u_int32_t nicMacRxTotalAttns;			/* 121 */
	volatile u_int32_t nicMacRxLinkAttns;			/* 122 */
	volatile u_int32_t nicMacRxSyncAttns;			/* 123 */
	volatile u_int32_t nicMacRxConfigAttns;			/* 124 */
	volatile u_int32_t nicMacReset;				/* 125 */
	volatile u_int32_t nicMacRxBufDescrAttns;		/* 126 */
	volatile u_int32_t nicMacRxBufAttns;			/* 127 */
	volatile u_int32_t nicMacRxZeroFrameCleanup;		/* 128 */
	volatile u_int32_t nicMacRxOneFrameCleanup;		/* 129 */
	volatile u_int32_t nicMacRxMultipleFrameCleanup;	/* 130 */
	volatile u_int32_t nicMacRxTimerCleanup;		/* 131 */
	volatile u_int32_t nicMacRxDmaCleanup;			/* 132 */
	/*
	 * Stats from the mac_stats area
	 */
	volatile u_int32_t nicMacTxCollisionHistogram[15];	/* 133 */
	/*
	 * MAC TX Attentions
	 */
	volatile u_int32_t nicMacTxTotalAttns;			/* 134 */
	/*
	 * NIC Profile
	 */
	volatile u_int32_t nicProfile[32];			/* 135 */
	/*
	 * Pat to 1024 bytes.
	 */
	u_int32_t		pad[75];
};

struct tg_reg {
	u_int32_t	data;
	u_int32_t	addr;
};      

struct tg_mem {
	u_int32_t	tgAddr;
	caddr_t		userAddr;
	int		len;
}; 


typedef enum {
	TI_PARAM_NONE		= 0x00,
	TI_PARAM_STAT_TICKS	= 0x01,
	TI_PARAM_RX_COAL_TICKS	= 0x02,
	TI_PARAM_TX_COAL_TICKS	= 0x04,
	TI_PARAM_RX_COAL_BDS	= 0x08,
	TI_PARAM_TX_COAL_BDS	= 0x10,
	TI_PARAM_TX_BUF_RATIO	= 0x20,
	TI_PARAM_ALL		= 0x2f
} ti_param_mask;

struct ti_params {
	u_int32_t	ti_stat_ticks;
	u_int32_t	ti_rx_coal_ticks;
	u_int32_t	ti_tx_coal_ticks;
	u_int32_t	ti_rx_max_coal_bds;
	u_int32_t	ti_tx_max_coal_bds;
	u_int32_t	ti_tx_buf_ratio;
	ti_param_mask	param_mask;
};

typedef enum {
	TI_TRACE_TYPE_NONE	= 0x00000000,
	TI_TRACE_TYPE_SEND	= 0x00000001,
	TI_TRACE_TYPE_RECV	= 0x00000002,
	TI_TRACE_TYPE_DMA	= 0x00000004,
	TI_TRACE_TYPE_EVENT	= 0x00000008,
	TI_TRACE_TYPE_COMMAND	= 0x00000010,
	TI_TRACE_TYPE_MAC	= 0x00000020,
	TI_TRACE_TYPE_STATS	= 0x00000040,
	TI_TRACE_TYPE_TIMER	= 0x00000080,
	TI_TRACE_TYPE_DISP	= 0x00000100,
	TI_TRACE_TYPE_MAILBOX	= 0x00000200,
	TI_TRACE_TYPE_RECV_BD	= 0x00000400,
	TI_TRACE_TYPE_LNK_PHY	= 0x00000800,
	TI_TRACE_TYPE_LNK_NEG	= 0x00001000,
	TI_TRACE_LEVEL_1	= 0x10000000,
	TI_TRACE_LEVEL_2	= 0x20000000
} ti_trace_type;

struct ti_trace_buf {
	u_long	*buf;
	int	buf_len;
	int	fill_len;
	u_long	cur_trace_ptr;
};

#define	TIIOCGETSTATS	_IOR('T', 1, struct ti_stats)
#define	TIIOCGETPARAMS	_IOR('T', 2, struct ti_params)
#define	TIIOCSETPARAMS	_IOW('T', 3, struct ti_params)
#define TIIOCSETTRACE	_IOW('T', 11, ti_trace_type)
#define TIIOCGETTRACE	_IOWR('T', 12, struct ti_trace_buf)

/*
 * Taken from Alteon's altioctl.h.  Alteon's ioctl numbers 1-6 aren't
 * used by the FreeBSD driver.
 */
#define ALT_ATTACH		_IO('a', 7)
#define ALT_READ_TG_MEM		_IOWR('a', 10, struct tg_mem)
#define ALT_WRITE_TG_MEM	_IOWR('a', 11, struct tg_mem)
#define ALT_READ_TG_REG		_IOWR('a', 12, struct tg_reg)
#define ALT_WRITE_TG_REG	_IOWR('a', 13, struct tg_reg)

#endif /* _SYS_TIIO_H_  */
