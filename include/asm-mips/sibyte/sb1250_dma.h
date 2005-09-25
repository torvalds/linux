/*  *********************************************************************
    *  SB1250 Board Support Package
    *
    *  DMA definitions				File: sb1250_dma.h
    *
    *  This module contains constants and macros useful for
    *  programming the SB1250's DMA controllers, both the data mover
    *  and the Ethernet DMA.
    *
    *  SB1250 specification level:  User's manual 1/02/02
    *
    *  Author:  Mitch Lichtenberg
    *
    *********************************************************************
    *
    *  Copyright 2000,2001,2002,2003
    *  Broadcom Corporation. All rights reserved.
    *
    *  This program is free software; you can redistribute it and/or
    *  modify it under the terms of the GNU General Public License as
    *  published by the Free Software Foundation; either version 2 of
    *  the License, or (at your option) any later version.
    *
    *  This program is distributed in the hope that it will be useful,
    *  but WITHOUT ANY WARRANTY; without even the implied warranty of
    *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    *  GNU General Public License for more details.
    *
    *  You should have received a copy of the GNU General Public License
    *  along with this program; if not, write to the Free Software
    *  Foundation, Inc., 59 Temple Place, Suite 330, Boston,
    *  MA 02111-1307 USA
    ********************************************************************* */


#ifndef _SB1250_DMA_H
#define _SB1250_DMA_H


#include "sb1250_defs.h"

/*  *********************************************************************
    *  DMA Registers
    ********************************************************************* */

/*
 * Ethernet and Serial DMA Configuration Register 0  (Table 7-4)
 * Registers: DMA_CONFIG0_MAC_x_RX_CH_0
 * Registers: DMA_CONFIG0_MAC_x_TX_CH_0
 * Registers: DMA_CONFIG0_SER_x_RX
 * Registers: DMA_CONFIG0_SER_x_TX
 */


#define M_DMA_DROP                  _SB_MAKEMASK1(0)

#define M_DMA_CHAIN_SEL             _SB_MAKEMASK1(1)
#define M_DMA_RESERVED1             _SB_MAKEMASK1(2)

#define S_DMA_DESC_TYPE		    _SB_MAKE64(1)
#define M_DMA_DESC_TYPE		    _SB_MAKE64(2,S_DMA_DESC_TYPE)
#define V_DMA_DESC_TYPE(x)          _SB_MAKEVALUE(x,S_DMA_DESC_TYPE)
#define G_DMA_DESC_TYPE(x)          _SB_GETVALUE(x,S_DMA_DESC_TYPE,M_DMA_DESC_TYPE)

#define K_DMA_DESC_TYPE_RING_AL		0
#define K_DMA_DESC_TYPE_CHAIN_AL	1

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define K_DMA_DESC_TYPE_RING_UAL_WI	2
#define K_DMA_DESC_TYPE_RING_UAL_RMW	3
#endif /* 1250 PASS3 || 112x PASS1 */

#define M_DMA_EOP_INT_EN            _SB_MAKEMASK1(3)
#define M_DMA_HWM_INT_EN            _SB_MAKEMASK1(4)
#define M_DMA_LWM_INT_EN            _SB_MAKEMASK1(5)
#define M_DMA_TBX_EN                _SB_MAKEMASK1(6)
#define M_DMA_TDX_EN                _SB_MAKEMASK1(7)

#define S_DMA_INT_PKTCNT            _SB_MAKE64(8)
#define M_DMA_INT_PKTCNT            _SB_MAKEMASK(8,S_DMA_INT_PKTCNT)
#define V_DMA_INT_PKTCNT(x)         _SB_MAKEVALUE(x,S_DMA_INT_PKTCNT)
#define G_DMA_INT_PKTCNT(x)         _SB_GETVALUE(x,S_DMA_INT_PKTCNT,M_DMA_INT_PKTCNT)

#define S_DMA_RINGSZ                _SB_MAKE64(16)
#define M_DMA_RINGSZ                _SB_MAKEMASK(16,S_DMA_RINGSZ)
#define V_DMA_RINGSZ(x)             _SB_MAKEVALUE(x,S_DMA_RINGSZ)
#define G_DMA_RINGSZ(x)             _SB_GETVALUE(x,S_DMA_RINGSZ,M_DMA_RINGSZ)

#define S_DMA_HIGH_WATERMARK        _SB_MAKE64(32)
#define M_DMA_HIGH_WATERMARK        _SB_MAKEMASK(16,S_DMA_HIGH_WATERMARK)
#define V_DMA_HIGH_WATERMARK(x)     _SB_MAKEVALUE(x,S_DMA_HIGH_WATERMARK)
#define G_DMA_HIGH_WATERMARK(x)     _SB_GETVALUE(x,S_DMA_HIGH_WATERMARK,M_DMA_HIGH_WATERMARK)

#define S_DMA_LOW_WATERMARK         _SB_MAKE64(48)
#define M_DMA_LOW_WATERMARK         _SB_MAKEMASK(16,S_DMA_LOW_WATERMARK)
#define V_DMA_LOW_WATERMARK(x)      _SB_MAKEVALUE(x,S_DMA_LOW_WATERMARK)
#define G_DMA_LOW_WATERMARK(x)      _SB_GETVALUE(x,S_DMA_LOW_WATERMARK,M_DMA_LOW_WATERMARK)

/*
 * Ethernet and Serial DMA Configuration Register 1 (Table 7-5)
 * Registers: DMA_CONFIG1_MAC_x_RX_CH_0
 * Registers: DMA_CONFIG1_DMA_x_TX_CH_0
 * Registers: DMA_CONFIG1_SER_x_RX
 * Registers: DMA_CONFIG1_SER_x_TX
 */

#define M_DMA_HDR_CF_EN             _SB_MAKEMASK1(0)
#define M_DMA_ASIC_XFR_EN           _SB_MAKEMASK1(1)
#define M_DMA_PRE_ADDR_EN           _SB_MAKEMASK1(2)
#define M_DMA_FLOW_CTL_EN           _SB_MAKEMASK1(3)
#define M_DMA_NO_DSCR_UPDT          _SB_MAKEMASK1(4)
#define M_DMA_L2CA		    _SB_MAKEMASK1(5)

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_DMA_RX_XTRA_STATUS	    _SB_MAKEMASK1(6)
#define M_DMA_TX_CPU_PAUSE	    _SB_MAKEMASK1(6)
#define M_DMA_TX_FC_PAUSE_EN	    _SB_MAKEMASK1(7)
#endif /* 1250 PASS3 || 112x PASS1 */

#define M_DMA_MBZ1                  _SB_MAKEMASK(6,15)

#define S_DMA_HDR_SIZE              _SB_MAKE64(21)
#define M_DMA_HDR_SIZE              _SB_MAKEMASK(9,S_DMA_HDR_SIZE)
#define V_DMA_HDR_SIZE(x)           _SB_MAKEVALUE(x,S_DMA_HDR_SIZE)
#define G_DMA_HDR_SIZE(x)           _SB_GETVALUE(x,S_DMA_HDR_SIZE,M_DMA_HDR_SIZE)

#define M_DMA_MBZ2                  _SB_MAKEMASK(5,32)

#define S_DMA_ASICXFR_SIZE          _SB_MAKE64(37)
#define M_DMA_ASICXFR_SIZE          _SB_MAKEMASK(9,S_DMA_ASICXFR_SIZE)
#define V_DMA_ASICXFR_SIZE(x)       _SB_MAKEVALUE(x,S_DMA_ASICXFR_SIZE)
#define G_DMA_ASICXFR_SIZE(x)       _SB_GETVALUE(x,S_DMA_ASICXFR_SIZE,M_DMA_ASICXFR_SIZE)

#define S_DMA_INT_TIMEOUT           _SB_MAKE64(48)
#define M_DMA_INT_TIMEOUT           _SB_MAKEMASK(16,S_DMA_INT_TIMEOUT)
#define V_DMA_INT_TIMEOUT(x)        _SB_MAKEVALUE(x,S_DMA_INT_TIMEOUT)
#define G_DMA_INT_TIMEOUT(x)        _SB_GETVALUE(x,S_DMA_INT_TIMEOUT,M_DMA_INT_TIMEOUT)

/*
 * Ethernet and Serial DMA Descriptor base address (Table 7-6)
 */

#define M_DMA_DSCRBASE_MBZ          _SB_MAKEMASK(4,0)


/*
 * ASIC Mode Base Address (Table 7-7)
 */

#define M_DMA_ASIC_BASE_MBZ         _SB_MAKEMASK(20,0)

/*
 * DMA Descriptor Count Registers (Table 7-8)
 */

/* No bitfields */


/*
 * Current Descriptor Address Register (Table 7-11)
 */

#define S_DMA_CURDSCR_ADDR          _SB_MAKE64(0)
#define M_DMA_CURDSCR_ADDR          _SB_MAKEMASK(40,S_DMA_CURDSCR_ADDR)
#define S_DMA_CURDSCR_COUNT         _SB_MAKE64(40)
#define M_DMA_CURDSCR_COUNT         _SB_MAKEMASK(16,S_DMA_CURDSCR_COUNT)

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_DMA_TX_CH_PAUSE_ON	    _SB_MAKEMASK1(56)
#endif /* 1250 PASS3 || 112x PASS1 */

/*
 * Receive Packet Drop Registers
 */
#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define S_DMA_OODLOST_RX           _SB_MAKE64(0)
#define M_DMA_OODLOST_RX           _SB_MAKEMASK(16,S_DMA_OODLOST_RX)
#define G_DMA_OODLOST_RX(x)        _SB_GETVALUE(x,S_DMA_OODLOST_RX,M_DMA_OODLOST_RX)

#define S_DMA_EOP_COUNT_RX         _SB_MAKE64(16)
#define M_DMA_EOP_COUNT_RX         _SB_MAKEMASK(8,S_DMA_EOP_COUNT_RX)
#define G_DMA_EOP_COUNT_RX(x)      _SB_GETVALUE(x,S_DMA_EOP_COUNT_RX,M_DMA_EOP_COUNT_RX)
#endif /* 1250 PASS3 || 112x PASS1 */

/*  *********************************************************************
    *  DMA Descriptors
    ********************************************************************* */

/*
 * Descriptor doubleword "A"  (Table 7-12)
 */

#define S_DMA_DSCRA_OFFSET          _SB_MAKE64(0)
#define M_DMA_DSCRA_OFFSET          _SB_MAKEMASK(5,S_DMA_DSCRA_OFFSET)
#define V_DMA_DSCRA_OFFSET(x)       _SB_MAKEVALUE(x,S_DMA_DSCRA_OFFSET)
#define G_DMA_DSCRA_OFFSET(x)       _SB_GETVALUE(x,S_DMA_DSCRA_OFFSET,M_DMA_DSCRA_OFFSET)

/* Note: Don't shift the address over, just mask it with the mask below */
#define S_DMA_DSCRA_A_ADDR          _SB_MAKE64(5)
#define M_DMA_DSCRA_A_ADDR          _SB_MAKEMASK(35,S_DMA_DSCRA_A_ADDR)

#define M_DMA_DSCRA_A_ADDR_OFFSET   (M_DMA_DSCRA_OFFSET | M_DMA_DSCRA_A_ADDR)

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define S_DMA_DSCRA_A_ADDR_UA        _SB_MAKE64(0)
#define M_DMA_DSCRA_A_ADDR_UA        _SB_MAKEMASK(40,S_DMA_DSCRA_A_ADDR_UA)
#endif /* 1250 PASS3 || 112x PASS1 */

#define S_DMA_DSCRA_A_SIZE          _SB_MAKE64(40)
#define M_DMA_DSCRA_A_SIZE          _SB_MAKEMASK(9,S_DMA_DSCRA_A_SIZE)
#define V_DMA_DSCRA_A_SIZE(x)       _SB_MAKEVALUE(x,S_DMA_DSCRA_A_SIZE)
#define G_DMA_DSCRA_A_SIZE(x)       _SB_GETVALUE(x,S_DMA_DSCRA_A_SIZE,M_DMA_DSCRA_A_SIZE)

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define S_DMA_DSCRA_DSCR_CNT	    _SB_MAKE64(40)
#define M_DMA_DSCRA_DSCR_CNT	    _SB_MAKEMASK(8,S_DMA_DSCRA_DSCR_CNT)
#define G_DMA_DSCRA_DSCR_CNT(x)	    _SB_GETVALUE(x,S_DMA_DSCRA_DSCR_CNT,M_DMA_DSCRA_DSCR_CNT)
#endif /* 1250 PASS3 || 112x PASS1 */

#define M_DMA_DSCRA_INTERRUPT       _SB_MAKEMASK1(49)
#define M_DMA_DSCRA_OFFSETB	    _SB_MAKEMASK1(50)

#define S_DMA_DSCRA_STATUS          _SB_MAKE64(51)
#define M_DMA_DSCRA_STATUS          _SB_MAKEMASK(13,S_DMA_DSCRA_STATUS)
#define V_DMA_DSCRA_STATUS(x)       _SB_MAKEVALUE(x,S_DMA_DSCRA_STATUS)
#define G_DMA_DSCRA_STATUS(x)       _SB_GETVALUE(x,S_DMA_DSCRA_STATUS,M_DMA_DSCRA_STATUS)

/*
 * Descriptor doubleword "B"  (Table 7-13)
 */


#define S_DMA_DSCRB_OPTIONS         _SB_MAKE64(0)
#define M_DMA_DSCRB_OPTIONS         _SB_MAKEMASK(4,S_DMA_DSCRB_OPTIONS)
#define V_DMA_DSCRB_OPTIONS(x)      _SB_MAKEVALUE(x,S_DMA_DSCRB_OPTIONS)
#define G_DMA_DSCRB_OPTIONS(x)      _SB_GETVALUE(x,S_DMA_DSCRB_OPTIONS,M_DMA_DSCRB_OPTIONS)

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define S_DMA_DSCRB_A_SIZE        _SB_MAKE64(8)
#define M_DMA_DSCRB_A_SIZE        _SB_MAKEMASK(14,S_DMA_DSCRB_A_SIZE)
#define V_DMA_DSCRB_A_SIZE(x)     _SB_MAKEVALUE(x,S_DMA_DSCRB_A_SIZE)
#define G_DMA_DSCRB_A_SIZE(x)     _SB_GETVALUE(x,S_DMA_DSCRB_A_SIZE,M_DMA_DSCRB_A_SIZE)
#endif /* 1250 PASS3 || 112x PASS1 */

#define R_DMA_DSCRB_ADDR            _SB_MAKE64(0x10)

/* Note: Don't shift the address over, just mask it with the mask below */
#define S_DMA_DSCRB_B_ADDR          _SB_MAKE64(5)
#define M_DMA_DSCRB_B_ADDR          _SB_MAKEMASK(35,S_DMA_DSCRB_B_ADDR)

#define S_DMA_DSCRB_B_SIZE          _SB_MAKE64(40)
#define M_DMA_DSCRB_B_SIZE          _SB_MAKEMASK(9,S_DMA_DSCRB_B_SIZE)
#define V_DMA_DSCRB_B_SIZE(x)       _SB_MAKEVALUE(x,S_DMA_DSCRB_B_SIZE)
#define G_DMA_DSCRB_B_SIZE(x)       _SB_GETVALUE(x,S_DMA_DSCRB_B_SIZE,M_DMA_DSCRB_B_SIZE)

#define M_DMA_DSCRB_B_VALID         _SB_MAKEMASK1(49)

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define S_DMA_DSCRB_PKT_SIZE_MSB    _SB_MAKE64(48)
#define M_DMA_DSCRB_PKT_SIZE_MSB    _SB_MAKEMASK(2,S_DMA_DSCRB_PKT_SIZE_MSB)
#define V_DMA_DSCRB_PKT_SIZE_MSB(x) _SB_MAKEVALUE(x,S_DMA_DSCRB_PKT_SIZE_MSB)
#define G_DMA_DSCRB_PKT_SIZE_MSB(x) _SB_GETVALUE(x,S_DMA_DSCRB_PKT_SIZE_MSB,M_DMA_DSCRB_PKT_SIZE_MSB)
#endif /* 1250 PASS3 || 112x PASS1 */

#define S_DMA_DSCRB_PKT_SIZE        _SB_MAKE64(50)
#define M_DMA_DSCRB_PKT_SIZE        _SB_MAKEMASK(14,S_DMA_DSCRB_PKT_SIZE)
#define V_DMA_DSCRB_PKT_SIZE(x)     _SB_MAKEVALUE(x,S_DMA_DSCRB_PKT_SIZE)
#define G_DMA_DSCRB_PKT_SIZE(x)     _SB_GETVALUE(x,S_DMA_DSCRB_PKT_SIZE,M_DMA_DSCRB_PKT_SIZE)

/*
 * from pass2 some bits in dscr_b are also used for rx status
 */
#define S_DMA_DSCRB_STATUS          _SB_MAKE64(0)
#define M_DMA_DSCRB_STATUS          _SB_MAKEMASK(1,S_DMA_DSCRB_STATUS)
#define V_DMA_DSCRB_STATUS(x)       _SB_MAKEVALUE(x,S_DMA_DSCRB_STATUS)
#define G_DMA_DSCRB_STATUS(x)       _SB_GETVALUE(x,S_DMA_DSCRB_STATUS,M_DMA_DSCRB_STATUS)

/*
 * Ethernet Descriptor Status Bits (Table 7-15)
 */

#define M_DMA_ETHRX_BADIP4CS        _SB_MAKEMASK1(51)
#define M_DMA_ETHRX_DSCRERR	    _SB_MAKEMASK1(52)

#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
/* Note: BADTCPCS is actually in DSCR_B options field */
#define M_DMA_ETHRX_BADTCPCS	_SB_MAKEMASK1(0)
#endif /* 1250 PASS2 || 112x PASS1 */

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_DMA_ETH_VLAN_FLAG	_SB_MAKEMASK1(1)
#define M_DMA_ETH_CRC_FLAG	_SB_MAKEMASK1(2)
#endif /* 1250 PASS3 || 112x PASS1 */

#define S_DMA_ETHRX_RXCH            53
#define M_DMA_ETHRX_RXCH            _SB_MAKEMASK(2,S_DMA_ETHRX_RXCH)
#define V_DMA_ETHRX_RXCH(x)         _SB_MAKEVALUE(x,S_DMA_ETHRX_RXCH)
#define G_DMA_ETHRX_RXCH(x)         _SB_GETVALUE(x,S_DMA_ETHRX_RXCH,M_DMA_ETHRX_RXCH)

#define S_DMA_ETHRX_PKTTYPE         55
#define M_DMA_ETHRX_PKTTYPE         _SB_MAKEMASK(3,S_DMA_ETHRX_PKTTYPE)
#define V_DMA_ETHRX_PKTTYPE(x)      _SB_MAKEVALUE(x,S_DMA_ETHRX_PKTTYPE)
#define G_DMA_ETHRX_PKTTYPE(x)      _SB_GETVALUE(x,S_DMA_ETHRX_PKTTYPE,M_DMA_ETHRX_PKTTYPE)

#define K_DMA_ETHRX_PKTTYPE_IPV4    0
#define K_DMA_ETHRX_PKTTYPE_ARPV4   1
#define K_DMA_ETHRX_PKTTYPE_802     2
#define K_DMA_ETHRX_PKTTYPE_OTHER   3
#define K_DMA_ETHRX_PKTTYPE_USER0   4
#define K_DMA_ETHRX_PKTTYPE_USER1   5
#define K_DMA_ETHRX_PKTTYPE_USER2   6
#define K_DMA_ETHRX_PKTTYPE_USER3   7

#define M_DMA_ETHRX_MATCH_HASH      _SB_MAKEMASK1(58)
#define M_DMA_ETHRX_MATCH_EXACT     _SB_MAKEMASK1(59)
#define M_DMA_ETHRX_BCAST           _SB_MAKEMASK1(60)
#define M_DMA_ETHRX_MCAST           _SB_MAKEMASK1(61)
#define M_DMA_ETHRX_BAD	            _SB_MAKEMASK1(62)
#define M_DMA_ETHRX_SOP             _SB_MAKEMASK1(63)

/*
 * Ethernet Transmit Status Bits (Table 7-16)
 */

#define M_DMA_ETHTX_SOP	    	    _SB_MAKEMASK1(63)

/*
 * Ethernet Transmit Options (Table 7-17)
 */

#define K_DMA_ETHTX_NOTSOP          _SB_MAKE64(0x00)
#define K_DMA_ETHTX_APPENDCRC       _SB_MAKE64(0x01)
#define K_DMA_ETHTX_REPLACECRC      _SB_MAKE64(0x02)
#define K_DMA_ETHTX_APPENDCRC_APPENDPAD _SB_MAKE64(0x03)
#define K_DMA_ETHTX_APPENDVLAN_REPLACECRC _SB_MAKE64(0x04)
#define K_DMA_ETHTX_REMOVEVLAN_REPLACECRC _SB_MAKE64(0x05)
#define K_DMA_ETHTX_REPLACEVLAN_REPLACECRC _SB_MAKE64(0x6)
#define K_DMA_ETHTX_NOMODS          _SB_MAKE64(0x07)
#define K_DMA_ETHTX_RESERVED1       _SB_MAKE64(0x08)
#define K_DMA_ETHTX_REPLACESADDR_APPENDCRC _SB_MAKE64(0x09)
#define K_DMA_ETHTX_REPLACESADDR_REPLACECRC _SB_MAKE64(0x0A)
#define K_DMA_ETHTX_REPLACESADDR_APPENDCRC_APPENDPAD _SB_MAKE64(0x0B)
#define K_DMA_ETHTX_REPLACESADDR_APPENDVLAN_REPLACECRC _SB_MAKE64(0x0C)
#define K_DMA_ETHTX_REPLACESADDR_REMOVEVLAN_REPLACECRC _SB_MAKE64(0x0D)
#define K_DMA_ETHTX_REPLACESADDR_REPLACEVLAN_REPLACECRC _SB_MAKE64(0x0E)
#define K_DMA_ETHTX_RESERVED2       _SB_MAKE64(0x0F)

/*
 * Serial Receive Options (Table 7-18)
 */
#define M_DMA_SERRX_CRC_ERROR       _SB_MAKEMASK1(56)
#define M_DMA_SERRX_ABORT           _SB_MAKEMASK1(57)
#define M_DMA_SERRX_OCTET_ERROR     _SB_MAKEMASK1(58)
#define M_DMA_SERRX_LONGFRAME_ERROR _SB_MAKEMASK1(59)
#define M_DMA_SERRX_SHORTFRAME_ERROR _SB_MAKEMASK1(60)
#define M_DMA_SERRX_OVERRUN_ERROR   _SB_MAKEMASK1(61)
#define M_DMA_SERRX_GOOD            _SB_MAKEMASK1(62)
#define M_DMA_SERRX_SOP             _SB_MAKEMASK1(63)

/*
 * Serial Transmit Status Bits (Table 7-20)
 */

#define M_DMA_SERTX_FLAG	    _SB_MAKEMASK1(63)

/*
 * Serial Transmit Options (Table 7-21)
 */

#define K_DMA_SERTX_RESERVED        _SB_MAKEMASK1(0)
#define K_DMA_SERTX_APPENDCRC       _SB_MAKEMASK1(1)
#define K_DMA_SERTX_APPENDPAD       _SB_MAKEMASK1(2)
#define K_DMA_SERTX_ABORT           _SB_MAKEMASK1(3)


/*  *********************************************************************
    *  Data Mover Registers
    ********************************************************************* */

/*
 * Data Mover Descriptor Base Address Register (Table 7-22)
 * Register: DM_DSCR_BASE_0
 * Register: DM_DSCR_BASE_1
 * Register: DM_DSCR_BASE_2
 * Register: DM_DSCR_BASE_3
 */

#define M_DM_DSCR_BASE_MBZ          _SB_MAKEMASK(4,0)

/*  Note: Just mask the base address and then OR it in. */
#define S_DM_DSCR_BASE_ADDR         _SB_MAKE64(4)
#define M_DM_DSCR_BASE_ADDR         _SB_MAKEMASK(36,S_DM_DSCR_BASE_ADDR)

#define S_DM_DSCR_BASE_RINGSZ       _SB_MAKE64(40)
#define M_DM_DSCR_BASE_RINGSZ       _SB_MAKEMASK(16,S_DM_DSCR_BASE_RINGSZ)
#define V_DM_DSCR_BASE_RINGSZ(x)    _SB_MAKEVALUE(x,S_DM_DSCR_BASE_RINGSZ)
#define G_DM_DSCR_BASE_RINGSZ(x)    _SB_GETVALUE(x,S_DM_DSCR_BASE_RINGSZ,M_DM_DSCR_BASE_RINGSZ)

#define S_DM_DSCR_BASE_PRIORITY     _SB_MAKE64(56)
#define M_DM_DSCR_BASE_PRIORITY     _SB_MAKEMASK(3,S_DM_DSCR_BASE_PRIORITY)
#define V_DM_DSCR_BASE_PRIORITY(x)  _SB_MAKEVALUE(x,S_DM_DSCR_BASE_PRIORITY)
#define G_DM_DSCR_BASE_PRIORITY(x)  _SB_GETVALUE(x,S_DM_DSCR_BASE_PRIORITY,M_DM_DSCR_BASE_PRIORITY)

#define K_DM_DSCR_BASE_PRIORITY_1   0
#define K_DM_DSCR_BASE_PRIORITY_2   1
#define K_DM_DSCR_BASE_PRIORITY_4   2
#define K_DM_DSCR_BASE_PRIORITY_8   3
#define K_DM_DSCR_BASE_PRIORITY_16  4

#define M_DM_DSCR_BASE_ACTIVE       _SB_MAKEMASK1(59)
#define M_DM_DSCR_BASE_INTERRUPT    _SB_MAKEMASK1(60)
#define M_DM_DSCR_BASE_RESET        _SB_MAKEMASK1(61)	/* write register */
#define M_DM_DSCR_BASE_ERROR        _SB_MAKEMASK1(61)	/* read register */
#define M_DM_DSCR_BASE_ABORT        _SB_MAKEMASK1(62)
#define M_DM_DSCR_BASE_ENABL        _SB_MAKEMASK1(63)

/*
 * Data Mover Descriptor Count Register (Table 7-25)
 */

/* no bitfields */

/*
 * Data Mover Current Descriptor Address (Table 7-24)
 * Register: DM_CUR_DSCR_ADDR_0
 * Register: DM_CUR_DSCR_ADDR_1
 * Register: DM_CUR_DSCR_ADDR_2
 * Register: DM_CUR_DSCR_ADDR_3
 */

#define S_DM_CUR_DSCR_DSCR_ADDR     _SB_MAKE64(0)
#define M_DM_CUR_DSCR_DSCR_ADDR     _SB_MAKEMASK(40,S_DM_CUR_DSCR_DSCR_ADDR)

#define S_DM_CUR_DSCR_DSCR_COUNT    _SB_MAKE64(48)
#define M_DM_CUR_DSCR_DSCR_COUNT    _SB_MAKEMASK(16,S_DM_CUR_DSCR_DSCR_COUNT)
#define V_DM_CUR_DSCR_DSCR_COUNT(r) _SB_MAKEVALUE(r,S_DM_CUR_DSCR_DSCR_COUNT)
#define G_DM_CUR_DSCR_DSCR_COUNT(r) _SB_GETVALUE(r,S_DM_CUR_DSCR_DSCR_COUNT,\
                                     M_DM_CUR_DSCR_DSCR_COUNT)


#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
/*
 * Data Mover Channel Partial Result Registers
 * Register: DM_PARTIAL_0
 * Register: DM_PARTIAL_1
 * Register: DM_PARTIAL_2
 * Register: DM_PARTIAL_3
 */
#define S_DM_PARTIAL_CRC_PARTIAL      _SB_MAKE64(0)
#define M_DM_PARTIAL_CRC_PARTIAL      _SB_MAKEMASK(32,S_DM_PARTIAL_CRC_PARTIAL)
#define V_DM_PARTIAL_CRC_PARTIAL(r)   _SB_MAKEVALUE(r,S_DM_PARTIAL_CRC_PARTIAL)
#define G_DM_PARTIAL_CRC_PARTIAL(r)   _SB_GETVALUE(r,S_DM_PARTIAL_CRC_PARTIAL,\
                                       M_DM_PARTIAL_CRC_PARTIAL)

#define S_DM_PARTIAL_TCPCS_PARTIAL    _SB_MAKE64(32)
#define M_DM_PARTIAL_TCPCS_PARTIAL    _SB_MAKEMASK(16,S_DM_PARTIAL_TCPCS_PARTIAL)
#define V_DM_PARTIAL_TCPCS_PARTIAL(r) _SB_MAKEVALUE(r,S_DM_PARTIAL_TCPCS_PARTIAL)
#define G_DM_PARTIAL_TCPCS_PARTIAL(r) _SB_GETVALUE(r,S_DM_PARTIAL_TCPCS_PARTIAL,\
                                       M_DM_PARTIAL_TCPCS_PARTIAL)

#define M_DM_PARTIAL_ODD_BYTE         _SB_MAKEMASK1(48)
#endif /* 1250 PASS3 || 112x PASS1 */


#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
/*
 * Data Mover CRC Definition Registers
 * Register: CRC_DEF_0
 * Register: CRC_DEF_1
 */
#define S_CRC_DEF_CRC_INIT            _SB_MAKE64(0)
#define M_CRC_DEF_CRC_INIT            _SB_MAKEMASK(32,S_CRC_DEF_CRC_INIT)
#define V_CRC_DEF_CRC_INIT(r)         _SB_MAKEVALUE(r,S_CRC_DEF_CRC_INIT)
#define G_CRC_DEF_CRC_INIT(r)         _SB_GETVALUE(r,S_CRC_DEF_CRC_INIT,\
                                       M_CRC_DEF_CRC_INIT)

#define S_CRC_DEF_CRC_POLY            _SB_MAKE64(32)
#define M_CRC_DEF_CRC_POLY            _SB_MAKEMASK(32,S_CRC_DEF_CRC_POLY)
#define V_CRC_DEF_CRC_POLY(r)         _SB_MAKEVALUE(r,S_CRC_DEF_CRC_POLY)
#define G_CRC_DEF_CRC_POLY(r)         _SB_GETVALUE(r,S_CRC_DEF_CRC_POLY,\
                                       M_CRC_DEF_CRC_POLY)
#endif /* 1250 PASS3 || 112x PASS1 */


#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
/*
 * Data Mover CRC/Checksum Definition Registers
 * Register: CTCP_DEF_0
 * Register: CTCP_DEF_1
 */
#define S_CTCP_DEF_CRC_TXOR           _SB_MAKE64(0)
#define M_CTCP_DEF_CRC_TXOR           _SB_MAKEMASK(32,S_CTCP_DEF_CRC_TXOR)
#define V_CTCP_DEF_CRC_TXOR(r)        _SB_MAKEVALUE(r,S_CTCP_DEF_CRC_TXOR)
#define G_CTCP_DEF_CRC_TXOR(r)        _SB_GETVALUE(r,S_CTCP_DEF_CRC_TXOR,\
                                       M_CTCP_DEF_CRC_TXOR)

#define S_CTCP_DEF_TCPCS_INIT         _SB_MAKE64(32)
#define M_CTCP_DEF_TCPCS_INIT         _SB_MAKEMASK(16,S_CTCP_DEF_TCPCS_INIT)
#define V_CTCP_DEF_TCPCS_INIT(r)      _SB_MAKEVALUE(r,S_CTCP_DEF_TCPCS_INIT)
#define G_CTCP_DEF_TCPCS_INIT(r)      _SB_GETVALUE(r,S_CTCP_DEF_TCPCS_INIT,\
                                       M_CTCP_DEF_TCPCS_INIT)

#define S_CTCP_DEF_CRC_WIDTH          _SB_MAKE64(48)
#define M_CTCP_DEF_CRC_WIDTH          _SB_MAKEMASK(2,S_CTCP_DEF_CRC_WIDTH)
#define V_CTCP_DEF_CRC_WIDTH(r)       _SB_MAKEVALUE(r,S_CTCP_DEF_CRC_WIDTH)
#define G_CTCP_DEF_CRC_WIDTH(r)       _SB_GETVALUE(r,S_CTCP_DEF_CRC_WIDTH,\
                                       M_CTCP_DEF_CRC_WIDTH)

#define K_CTCP_DEF_CRC_WIDTH_4        0
#define K_CTCP_DEF_CRC_WIDTH_2        1
#define K_CTCP_DEF_CRC_WIDTH_1        2

#define M_CTCP_DEF_CRC_BIT_ORDER      _SB_MAKEMASK1(50)
#endif /* 1250 PASS3 || 112x PASS1 */


/*
 * Data Mover Descriptor Doubleword "A"  (Table 7-26)
 */

#define S_DM_DSCRA_DST_ADDR         _SB_MAKE64(0)
#define M_DM_DSCRA_DST_ADDR         _SB_MAKEMASK(40,S_DM_DSCRA_DST_ADDR)

#define M_DM_DSCRA_UN_DEST          _SB_MAKEMASK1(40)
#define M_DM_DSCRA_UN_SRC           _SB_MAKEMASK1(41)
#define M_DM_DSCRA_INTERRUPT        _SB_MAKEMASK1(42)
#if SIBYTE_HDR_FEATURE_UP_TO(1250, PASS1)
#define M_DM_DSCRA_THROTTLE         _SB_MAKEMASK1(43)
#endif /* up to 1250 PASS1 */

#define S_DM_DSCRA_DIR_DEST         _SB_MAKE64(44)
#define M_DM_DSCRA_DIR_DEST         _SB_MAKEMASK(2,S_DM_DSCRA_DIR_DEST)
#define V_DM_DSCRA_DIR_DEST(x)      _SB_MAKEVALUE(x,S_DM_DSCRA_DIR_DEST)
#define G_DM_DSCRA_DIR_DEST(x)      _SB_GETVALUE(x,S_DM_DSCRA_DIR_DEST,M_DM_DSCRA_DIR_DEST)

#define K_DM_DSCRA_DIR_DEST_INCR    0
#define K_DM_DSCRA_DIR_DEST_DECR    1
#define K_DM_DSCRA_DIR_DEST_CONST   2

#define V_DM_DSCRA_DIR_DEST_INCR    _SB_MAKEVALUE(K_DM_DSCRA_DIR_DEST_INCR,S_DM_DSCRA_DIR_DEST)
#define V_DM_DSCRA_DIR_DEST_DECR    _SB_MAKEVALUE(K_DM_DSCRA_DIR_DEST_DECR,S_DM_DSCRA_DIR_DEST)
#define V_DM_DSCRA_DIR_DEST_CONST   _SB_MAKEVALUE(K_DM_DSCRA_DIR_DEST_CONST,S_DM_DSCRA_DIR_DEST)

#define S_DM_DSCRA_DIR_SRC          _SB_MAKE64(46)
#define M_DM_DSCRA_DIR_SRC          _SB_MAKEMASK(2,S_DM_DSCRA_DIR_SRC)
#define V_DM_DSCRA_DIR_SRC(x)       _SB_MAKEVALUE(x,S_DM_DSCRA_DIR_SRC)
#define G_DM_DSCRA_DIR_SRC(x)       _SB_GETVALUE(x,S_DM_DSCRA_DIR_SRC,M_DM_DSCRA_DIR_SRC)

#define K_DM_DSCRA_DIR_SRC_INCR     0
#define K_DM_DSCRA_DIR_SRC_DECR     1
#define K_DM_DSCRA_DIR_SRC_CONST    2

#define V_DM_DSCRA_DIR_SRC_INCR     _SB_MAKEVALUE(K_DM_DSCRA_DIR_SRC_INCR,S_DM_DSCRA_DIR_SRC)
#define V_DM_DSCRA_DIR_SRC_DECR     _SB_MAKEVALUE(K_DM_DSCRA_DIR_SRC_DECR,S_DM_DSCRA_DIR_SRC)
#define V_DM_DSCRA_DIR_SRC_CONST    _SB_MAKEVALUE(K_DM_DSCRA_DIR_SRC_CONST,S_DM_DSCRA_DIR_SRC)


#define M_DM_DSCRA_ZERO_MEM         _SB_MAKEMASK1(48)
#define M_DM_DSCRA_PREFETCH         _SB_MAKEMASK1(49)
#define M_DM_DSCRA_L2C_DEST         _SB_MAKEMASK1(50)
#define M_DM_DSCRA_L2C_SRC          _SB_MAKEMASK1(51)

#if SIBYTE_HDR_FEATURE(1250, PASS2) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_DM_DSCRA_RD_BKOFF	    _SB_MAKEMASK1(52)
#define M_DM_DSCRA_WR_BKOFF	    _SB_MAKEMASK1(53)
#endif /* 1250 PASS2 || 112x PASS1 */

#if SIBYTE_HDR_FEATURE(1250, PASS3) || SIBYTE_HDR_FEATURE(112x, PASS1)
#define M_DM_DSCRA_TCPCS_EN         _SB_MAKEMASK1(54)
#define M_DM_DSCRA_TCPCS_RES        _SB_MAKEMASK1(55)
#define M_DM_DSCRA_TCPCS_AP         _SB_MAKEMASK1(56)
#define M_DM_DSCRA_CRC_EN           _SB_MAKEMASK1(57)
#define M_DM_DSCRA_CRC_RES          _SB_MAKEMASK1(58)
#define M_DM_DSCRA_CRC_AP           _SB_MAKEMASK1(59)
#define M_DM_DSCRA_CRC_DFN          _SB_MAKEMASK1(60)
#define M_DM_DSCRA_CRC_XBIT         _SB_MAKEMASK1(61)
#endif /* 1250 PASS3 || 112x PASS1 */

#define M_DM_DSCRA_RESERVED2        _SB_MAKEMASK(3,61)

/*
 * Data Mover Descriptor Doubleword "B"  (Table 7-25)
 */

#define S_DM_DSCRB_SRC_ADDR         _SB_MAKE64(0)
#define M_DM_DSCRB_SRC_ADDR         _SB_MAKEMASK(40,S_DM_DSCRB_SRC_ADDR)

#define S_DM_DSCRB_SRC_LENGTH       _SB_MAKE64(40)
#define M_DM_DSCRB_SRC_LENGTH       _SB_MAKEMASK(20,S_DM_DSCRB_SRC_LENGTH)
#define V_DM_DSCRB_SRC_LENGTH(x)    _SB_MAKEVALUE(x,S_DM_DSCRB_SRC_LENGTH)
#define G_DM_DSCRB_SRC_LENGTH(x)    _SB_GETVALUE(x,S_DM_DSCRB_SRC_LENGTH,M_DM_DSCRB_SRC_LENGTH)


#endif
