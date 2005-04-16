/* *
 *  Copyright (C) 2001 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __UART00_H
#define __UART00_H

/*
 * Register definitions for the UART
 */

#define UART_TX_FIFO_SIZE      (15)

#define UART_RSR(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x00 ))
#define UART_RSR_RX_LEVEL_MSK (0x1f)
#define UART_RSR_RX_LEVEL_OFST (0)
#define UART_RSR_RE_MSK (0x80)
#define UART_RSR_RE_OFST (7)

#define UART_RDS(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x04 ))
#define UART_RDS_BI_MSK (0x8)
#define UART_RDS_BI_OFST (4)
#define UART_RDS_FE_MSK (0x4)
#define UART_RDS_FE_OFST (2)
#define UART_RDS_PE_MSK (0x2)
#define UART_RDS_PE_OFST (1)
#define UART_RDS_OE_MSK (0x1)
#define UART_RDS_OE_OFST (0)

#define UART_RD(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x08 ))
#define UART_RD_RX_DATA_MSK (0xff)
#define UART_RD_RX_DATA_OFST (0)

#define UART_TSR(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x0c ))
#define UART_TSR_TX_LEVEL_MSK (0x1f)
#define UART_TSR_TX_LEVEL_OFST (0)
#define UART_TSR_TXI_MSK (0x80)
#define UART_TSR_TXI_OFST (7)

#define UART_TD(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x10 ))
#define UART_TD_TX_DATA_MSK (0xff)
#define UART_TD_TX_DATA_OFST (0)

#define UART_FCR(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x14 ))
#define UART_FCR_RX_THR_MSK (0xd0)
#define UART_FCR_RX_THR_OFST (5)
#define UART_FCR_RX_THR_1 (0x00)
#define UART_FCR_RX_THR_2 (0x20)
#define UART_FCR_RX_THR_4 (0x40)
#define UART_FCR_RX_THR_6 (0x60)
#define UART_FCR_RX_THR_8 (0x80)
#define UART_FCR_RX_THR_10 (0xa0)
#define UART_FCR_RX_THR_12 (0xc0)
#define UART_FCR_RX_THR_14 (0xd0)
#define UART_FCR_TX_THR_MSK (0x1c)
#define UART_FCR_TX_THR_OFST (2)
#define UART_FCR_TX_THR_0 (0x00)
#define UART_FCR_TX_THR_2 (0x04)
#define UART_FCR_TX_THR_4 (0x08)
#define UART_FCR_TX_THR_8 (0x0c)
#define UART_FCR_TX_THR_10 (0x10)
#define UART_FCR_TX_THR_12 (0x14)
#define UART_FCR_TX_THR_14 (0x18)
#define UART_FCR_TX_THR_15 (0x1c)
#define UART_FCR_RC_MSK (0x02)
#define UART_FCR_RC_OFST (1)
#define UART_FCR_TC_MSK (0x01)
#define UART_FCR_TC_OFST (0)

#define UART_IES(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x18 ))
#define UART_IES_ME_MSK (0x8)
#define UART_IES_ME_OFST (3)
#define UART_IES_TIE_MSK (0x4)
#define UART_IES_TIE_OFST (2)
#define UART_IES_TE_MSK (0x2)
#define UART_IES_TE_OFST (1)
#define UART_IES_RE_MSK (0x1)
#define UART_IES_RE_OFST (0)

#define UART_IEC(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x1c ))
#define UART_IEC_ME_MSK (0x8)
#define UART_IEC_ME_OFST (3)
#define UART_IEC_TIE_MSK (0x4)
#define UART_IEC_TIE_OFST (2)
#define UART_IEC_TE_MSK (0x2)
#define UART_IEC_TE_OFST (1)
#define UART_IEC_RE_MSK (0x1)
#define UART_IEC_RE_OFST (0)

#define UART_ISR(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x20 ))
#define UART_ISR_MI_MSK (0x8)
#define UART_ISR_MI_OFST (3)
#define UART_ISR_TII_MSK (0x4)
#define UART_ISR_TII_OFST (2)
#define UART_ISR_TI_MSK (0x2)
#define UART_ISR_TI_OFST (1)
#define UART_ISR_RI_MSK (0x1)
#define UART_ISR_RI_OFST (0)

#define UART_IID(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x24 ))
#define UART_IID_IID_MSK (0x7)
#define UART_IID_IID_OFST (0)

#define UART_MC(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x28 ))
#define UART_MC_OE_MSK (0x40)
#define UART_MC_OE_OFST (6)
#define UART_MC_SP_MSK (0x20)
#define UART_MC_SP_OFST (5)
#define UART_MC_EP_MSK (0x10)
#define UART_MC_EP_OFST (4)
#define UART_MC_PE_MSK (0x08)
#define UART_MC_PE_OFST (3)
#define UART_MC_ST_MSK (0x04)
#define UART_MC_ST_ONE (0x0)
#define UART_MC_ST_TWO (0x04)
#define UART_MC_ST_OFST (2)
#define UART_MC_CLS_MSK (0x03)
#define UART_MC_CLS_OFST (0)
#define UART_MC_CLS_CHARLEN_5 (0)
#define UART_MC_CLS_CHARLEN_6 (1)
#define UART_MC_CLS_CHARLEN_7 (2)
#define UART_MC_CLS_CHARLEN_8 (3)

#define UART_MCR(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x2c ))
#define UART_MCR_AC_MSK (0x80)
#define UART_MCR_AC_OFST (7)
#define UART_MCR_AR_MSK (0x40)
#define UART_MCR_AR_OFST (6)
#define UART_MCR_BR_MSK (0x20)
#define UART_MCR_BR_OFST (5)
#define UART_MCR_LB_MSK (0x10)
#define UART_MCR_LB_OFST (4)
#define UART_MCR_DCD_MSK (0x08)
#define UART_MCR_DCD_OFST (3)
#define UART_MCR_RI_MSK (0x04)
#define UART_MCR_RI_OFST (2)
#define UART_MCR_DTR_MSK (0x02)
#define UART_MCR_DTR_OFST (1)
#define UART_MCR_RTS_MSK (0x01)
#define UART_MCR_RTS_OFST (0)

#define UART_MSR(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x30 ))
#define UART_MSR_DCD_MSK (0x80)
#define UART_MSR_DCD_OFST (7)
#define UART_MSR_RI_MSK (0x40)
#define UART_MSR_RI_OFST (6)
#define UART_MSR_DSR_MSK (0x20)
#define UART_MSR_DSR_OFST (5)
#define UART_MSR_CTS_MSK (0x10)
#define UART_MSR_CTS_OFST (4)
#define UART_MSR_DDCD_MSK (0x08)
#define UART_MSR_DDCD_OFST (3)
#define UART_MSR_TERI_MSK (0x04)
#define UART_MSR_TERI_OFST (2)
#define UART_MSR_DDSR_MSK (0x02)
#define UART_MSR_DDSR_OFST (1)
#define UART_MSR_DCTS_MSK (0x01)
#define UART_MSR_DCTS_OFST (0)

#define UART_DIV_LO(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x34 ))
#define UART_DIV_LO_DIV_MSK (0xff)
#define UART_DIV_LO_DIV_OFST (0)

#define UART_DIV_HI(BASE_ADDR) (UART00_TYPE (BASE_ADDR  + 0x38 ))
#define UART_DIV_HI_DIV_MSK (0xff)
#define UART_DIV_HI_DIV_OFST (0)

#endif /* __UART00_H */
