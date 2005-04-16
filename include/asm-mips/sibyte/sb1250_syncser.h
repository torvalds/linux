/*  *********************************************************************
    *  SB1250 Board Support Package
    *
    *  Synchronous Serial Constants              File: sb1250_syncser.h
    *
    *  This module contains constants and macros useful for
    *  manipulating the SB1250's Synchronous Serial
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


#ifndef _SB1250_SYNCSER_H
#define _SB1250_SYNCSER_H

#include "sb1250_defs.h"

/*
 * Serial Mode Configuration Register
 */

#define M_SYNCSER_CRC_MODE                 _SB_MAKEMASK1(0)
#define M_SYNCSER_MSB_FIRST                _SB_MAKEMASK1(1)

#define S_SYNCSER_FLAG_NUM                 2
#define M_SYNCSER_FLAG_NUM                 _SB_MAKEMASK(4,S_SYNCSER_FLAG_NUM)
#define V_SYNCSER_FLAG_NUM                 _SB_MAKEVALUE(x,S_SYNCSER_FLAG_NUM)

#define M_SYNCSER_FLAG_EN                  _SB_MAKEMASK1(6)
#define M_SYNCSER_HDLC_EN                  _SB_MAKEMASK1(7)
#define M_SYNCSER_LOOP_MODE                _SB_MAKEMASK1(8)
#define M_SYNCSER_LOOPBACK                 _SB_MAKEMASK1(9)

/*
 * Serial Clock Source and Line Interface Mode Register
 */

#define M_SYNCSER_RXCLK_INV                _SB_MAKEMASK1(0)
#define M_SYNCSER_RXCLK_EXT                _SB_MAKEMASK1(1)

#define S_SYNCSER_RXSYNC_DLY               2
#define M_SYNCSER_RXSYNC_DLY               _SB_MAKEMASK(2,S_SYNCSER_RXSYNC_DLY)
#define V_SYNCSER_RXSYNC_DLY(x)            _SB_MAKEVALUE(x,S_SYNCSER_RXSYNC_DLY)

#define M_SYNCSER_RXSYNC_LOW               _SB_MAKEMASK1(4)
#define M_SYNCSER_RXSTRB_LOW               _SB_MAKEMASK1(5)

#define M_SYNCSER_RXSYNC_EDGE              _SB_MAKEMASK1(6)
#define M_SYNCSER_RXSYNC_INT               _SB_MAKEMASK1(7)

#define M_SYNCSER_TXCLK_INV                _SB_MAKEMASK1(8)
#define M_SYNCSER_TXCLK_EXT                _SB_MAKEMASK1(9)

#define S_SYNCSER_TXSYNC_DLY               10
#define M_SYNCSER_TXSYNC_DLY               _SB_MAKEMASK(2,S_SYNCSER_TXSYNC_DLY)
#define V_SYNCSER_TXSYNC_DLY(x)            _SB_MAKEVALUE(x,S_SYNCSER_TXSYNC_DLY)

#define M_SYNCSER_TXSYNC_LOW               _SB_MAKEMASK1(12)
#define M_SYNCSER_TXSTRB_LOW               _SB_MAKEMASK1(13)

#define M_SYNCSER_TXSYNC_EDGE              _SB_MAKEMASK1(14)
#define M_SYNCSER_TXSYNC_INT               _SB_MAKEMASK1(15)

/*
 * Serial Command Register
 */

#define M_SYNCSER_CMD_RX_EN                _SB_MAKEMASK1(0)
#define M_SYNCSER_CMD_TX_EN                _SB_MAKEMASK1(1)
#define M_SYNCSER_CMD_RX_RESET             _SB_MAKEMASK1(2)
#define M_SYNCSER_CMD_TX_RESET             _SB_MAKEMASK1(3)
#define M_SYNCSER_CMD_TX_PAUSE             _SB_MAKEMASK1(5)

/*
 * Serial DMA Enable Register
 */

#define M_SYNCSER_DMA_RX_EN                _SB_MAKEMASK1(0)
#define M_SYNCSER_DMA_TX_EN                _SB_MAKEMASK1(4)

/*
 * Serial Status Register
 */

#define M_SYNCSER_RX_CRCERR                _SB_MAKEMASK1(0)
#define M_SYNCSER_RX_ABORT                 _SB_MAKEMASK1(1)
#define M_SYNCSER_RX_OCTET                 _SB_MAKEMASK1(2)
#define M_SYNCSER_RX_LONGFRM               _SB_MAKEMASK1(3)
#define M_SYNCSER_RX_SHORTFRM              _SB_MAKEMASK1(4)
#define M_SYNCSER_RX_OVERRUN               _SB_MAKEMASK1(5)
#define M_SYNCSER_RX_SYNC_ERR              _SB_MAKEMASK1(6)
#define M_SYNCSER_TX_CRCERR                _SB_MAKEMASK1(8)
#define M_SYNCSER_TX_UNDERRUN              _SB_MAKEMASK1(9)
#define M_SYNCSER_TX_SYNC_ERR              _SB_MAKEMASK1(10)
#define M_SYNCSER_TX_PAUSE_COMPLETE        _SB_MAKEMASK1(11)
#define M_SYNCSER_RX_EOP_COUNT             _SB_MAKEMASK1(16)
#define M_SYNCSER_RX_EOP_TIMER             _SB_MAKEMASK1(17)
#define M_SYNCSER_RX_EOP_SEEN              _SB_MAKEMASK1(18)
#define M_SYNCSER_RX_HWM                   _SB_MAKEMASK1(19)
#define M_SYNCSER_RX_LWM                   _SB_MAKEMASK1(20)
#define M_SYNCSER_RX_DSCR                  _SB_MAKEMASK1(21)
#define M_SYNCSER_RX_DERR                  _SB_MAKEMASK1(22)
#define M_SYNCSER_TX_EOP_COUNT             _SB_MAKEMASK1(24)
#define M_SYNCSER_TX_EOP_TIMER             _SB_MAKEMASK1(25)
#define M_SYNCSER_TX_EOP_SEEN              _SB_MAKEMASK1(26)
#define M_SYNCSER_TX_HWM                   _SB_MAKEMASK1(27)
#define M_SYNCSER_TX_LWM                   _SB_MAKEMASK1(28)
#define M_SYNCSER_TX_DSCR                  _SB_MAKEMASK1(29)
#define M_SYNCSER_TX_DERR                  _SB_MAKEMASK1(30)
#define M_SYNCSER_TX_DZERO                 _SB_MAKEMASK1(31)

/*
 * Sequencer Table Entry format
 */

#define M_SYNCSER_SEQ_LAST                 _SB_MAKEMASK1(0)
#define M_SYNCSER_SEQ_BYTE                 _SB_MAKEMASK1(1)

#define S_SYNCSER_SEQ_COUNT                2
#define M_SYNCSER_SEQ_COUNT                _SB_MAKEMASK(4,S_SYNCSER_SEQ_COUNT)
#define V_SYNCSER_SEQ_COUNT(x)             _SB_MAKEVALUE(x,S_SYNCSER_SEQ_COUNT)

#define M_SYNCSER_SEQ_ENABLE               _SB_MAKEMASK1(6)
#define M_SYNCSER_SEQ_STROBE               _SB_MAKEMASK1(7)

#endif
