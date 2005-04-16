/*
 *
 * BRIEF MODULE DESCRIPTION
 *	IT8172 system controller defines.
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __IT8172__H__
#define __IT8172__H__

#include <asm/addrspace.h>

#define IT8172_BASE			0x18000000
#define IT8172_PCI_IO_BASE		0x14000000
#define IT8172_PCI_MEM_BASE		0x10000000

// System registers offsets from IT8172_BASE
#define IT_CMFPCR			0x0
#define IT_DSRR				0x2
#define IT_PCDCR			0x4
#define IT_SPLLCR			0x6
#define IT_CIDR				0x10
#define IT_CRNR				0x12
#define IT_CPUTR			0x14
#define IT_CTCR				0x16
#define IT_SDPR				0xF0

// Power management register offset from IT8172_PCI_IO_BASE
// Power Management Device Standby Register
#define IT_PM_DSR		0x15800

#define IT_PM_DSR_TMR0SB	0x0001
#define IT_PM_DSR_TMR1SB	0x0002
#define IT_PM_DSR_CIR0SB	0x0004
#define IT_PM_DSR_CIR1SB	0x0008
#define IT_PM_DSR_SCR0SB	0x0010
#define IT_PM_DSR_SCR1SB	0x0020
#define IT_PM_DSR_PPSB		0x0040
#define IT_PM_DSR_I2CSB		0x0080
#define IT_PM_DSR_UARTSB	0x0100
#define IT_PM_DSR_IDESB		0x0200
#define IT_PM_DSR_ACSB		0x0400
#define IT_PM_DSR_M68KSB	0x0800

// Power Management PCI Device Software Reset Register
#define IT_PM_PCISR             0x15802

#define IT_PM_PCISR_IDESR       0x0001
#define IT_PM_PCISR_CDMASR      0x0002
#define IT_PM_PCISR_USBSR       0x0004
#define IT_PM_PCISR_DMASR       0x0008
#define IT_PM_PCISR_ACSR        0x0010
#define IT_PM_PCISR_MEMSR       0x0020
#define IT_PM_PCISR_68KSR       0x0040


// PCI Configuration address and data register offsets
// from IT8172_BASE
#define IT_CONFADDR			0x4000
#define IT_BUSNUM_SHF                   16
#define IT_DEVNUM_SHF                   11
#define IT_FUNCNUM_SHF                  8
#define IT_REGNUM_SHF                   2

#define IT_CONFDATA			0x4004

// PCI configuration header common register offsets
#define IT_VID				0x00
#define IT_DID				0x02
#define IT_PCICMD			0x04
#define IT_PCISTS			0x06
#define IT_RID				0x08
#define IT_CLASSC			0x09
#define IT_HEADT			0x0E
#define IT_SERIRQC			0x49

// PCI to Internal/LPC Bus Bridge configuration header register offset
#define IT_P2I_BCR				0x4C
#define IT_P2I_D0IOSC				0x50
#define IT_P2I_D1IOSC				0x54
#define IT_P2I_D2IOSC				0x58
#define IT_P2I_D3IOSC				0x5C
#define IT_P2I_D4IOSC				0x60
#define IT_P2I_D5IOSC				0x64
#define IT_P2I_D6IOSC				0x68
#define IT_P2I_D7IOSC				0x6C
#define IT_P2I_D8IOSC				0x70
#define IT_P2I_D9IOSC				0x74
#define IT_P2I_D10IOSC				0x78
#define IT_P2I_D11IOSC				0x7C

// Memory controller register offsets from IT8172_BASE
#define IT_MC_SDRMR					0x1000
#define IT_MC_SDRTR					0x1004
#define IT_MC_MCR					0x1008
#define IT_MC_SDTYPE					0x100C
#define IT_MC_WPBA					0x1010
#define IT_MC_WPTA					0x1014
#define IT_MC_HATR					0x1018
#define IT_MC_PCICR					0x101C

// Flash/ROM control register offsets from IT8172_BASE
#define IT_FC_BRCR					0x2000
#define IT_FC_FCR					0x2004
#define IT_FC_DCR					0x2008

// M68K interface bridge configuration header register offset
#define IT_M68K_MBCSR					0x54
#define IT_M68K_TMR					0x58
#define IT_M68K_BCR					0x5C
#define IT_M68K_BSR					0x5D
#define IT_M68K_DTR					0x5F

// Register offset from IT8172_PCI_IO_BASE
// These registers are accessible through 8172 PCI IO window.

// INTC
#define IT_INTC_BASE				0x10000
#define IT_INTC_LBDNIRR				0x10000
#define IT_INTC_LBDNIMR				0x10002
#define IT_INTC_LBDNITR				0x10004
#define IT_INTC_LBDNIAR				0x10006
#define IT_INTC_LPCNIRR				0x10010
#define IT_INTC_LPCNIMR				0x10012
#define IT_INTC_LPCNITR				0x10014
#define IT_INTC_LPCNIAR				0x10016
#define IT_INTC_PDNIRR				0x10020
#define IT_INTC_PDNIMR				0x10022
#define IT_INTC_PDNITR				0x10024
#define IT_INTC_PDNIAR				0x10026
#define IT_INTC_UMNIRR				0x10030
#define IT_INTC_UMNITR				0x10034
#define IT_INTC_UMNIAR				0x10036
#define IT_INTC_TYPER				0x107FE

// IT8172 PCI device number
#define IT_C2P_DEVICE				0
#define IT_AUDIO_DEVICE				1
#define IT_DMAC_DEVICE				1
#define IT_CDMAC_DEVICE				1
#define IT_USB_DEVICE				1
#define IT_P2I_DEVICE				1
#define IT_IDE_DEVICE				1
#define IT_M68K_DEVICE				1

// IT8172 PCI function number
#define IT_C2P_FUNCION				0
#define IT_AUDIO_FUNCTION			0
#define IT_DMAC_FUNCTION			1
#define IT_CDMAC_FUNCTION			2
#define IT_USB_FUNCTION				3
#define IT_P2I_FUNCTION				4
#define IT_IDE_FUNCTION				5
#define IT_M68K_FUNCTION			6

// IT8172 GPIO
#define IT_GPADR				0x13800
#define IT_GPBDR				0x13808
#define IT_GPCDR				0x13810
#define IT_GPACR				0x13802
#define IT_GPBCR				0x1380A
#define IT_GPCCR				0x13812
#define IT_GPAICR				0x13804
#define IT_GPBICR				0x1380C
#define IT_GPCICR				0x13814
#define IT_GPAISR				0x13806
#define IT_GPBISR				0x1380E
#define IT_GPCISR				0x13816
#define IT_GCR					0x13818

// IT8172 RTC
#define IT_RTC_BASE				0x14800
#define IT_RTC_CENTURY				0x14808

#define IT_RTC_RIR0				0x00
#define IT_RTC_RTR0				0x01
#define IT_RTC_RIR1				0x02
#define IT_RTC_RTR1				0x03
#define IT_RTC_RIR2				0x04
#define IT_RTC_RTR2				0x05
#define IT_RTC_RCTR				0x08
#define IT_RTC_RA				0x0A
#define IT_RTC_RB				0x0B
#define IT_RTC_RC				0x0C
#define IT_RTC_RD				0x0D

#define RTC_SEC_INDEX				0x00
#define RTC_MIN_INDEX				0x02
#define RTC_HOUR_INDEX				0x04
#define RTC_DAY_INDEX				0x06
#define RTC_DATE_INDEX				0x07
#define RTC_MONTH_INDEX				0x08
#define RTC_YEAR_INDEX				0x09

// IT8172 internal device registers
#define IT_TIMER_BASE				0x10800
#define IT_CIR0_BASE				0x11000
#define IT_UART_BASE				0x11800
#define IT_SCR0_BASE				0x12000
#define IT_SCR1_BASE				0x12800
#define IT_PP_BASE				0x13000
#define IT_I2C_BASE				0x14000
#define IT_CIR1_BASE				0x15000

// IT8172 Smart Card Reader offsets from IT_SCR*_BASE
#define IT_SCR_SFR				0x08
#define IT_SCR_SCDR				0x09

// IT8172 IT_SCR_SFR bit definition & mask
#define	IT_SCR_SFR_GATE_UART			0x40
#define	IT_SCR_SFR_GATE_UART_BIT		6
#define IT_SCR_SFR_GATE_UART_OFF		0
#define	IT_SCR_SFR_GATE_UART_ON			1
#define	IT_SCR_SFR_FET_CHARGE			0x30
#define	IT_SCR_SFR_FET_CHARGE_BIT		4
#define IT_SCR_SFR_FET_CHARGE_3_3_US		3
#define IT_SCR_SFR_FET_CHARGE_13_US		2
#define IT_SCR_SFR_FET_CHARGE_53_US		1
#define IT_SCR_SFR_FET_CHARGE_213_US		0
#define	IT_SCR_SFR_CARD_FREQ			0x0C
#define	IT_SCR_SFR_CARD_FREQ_BIT		2
#define IT_SCR_SFR_CARD_FREQ_STOP		3
#define IT_SCR_SFR_CARD_FREQ_3_5_MHZ		0
#define IT_SCR_SFR_CARD_FREQ_7_1_MHZ		2
#define IT_SCR_SFR_CARD_FREQ_96_DIV_MHZ		1
#define	IT_SCR_SFR_FET_ACTIVE			0x02
#define	IT_SCR_SFR_FET_ACTIVE_BIT		1
#define IT_SCR_SFR_FET_ACTIVE_INVERT		0
#define IT_SCR_SFR_FET_ACTIVE_NONINVERT		1
#define	IT_SCR_SFR_ENABLE			0x01
#define	IT_SCR_SFR_ENABLE_BIT			0
#define IT_SCR_SFR_ENABLE_OFF			0
#define IT_SCR_SFR_ENABLE_ON			1

// IT8172 IT_SCR_SCDR bit definition & mask
#define	IT_SCR_SCDR_RESET_MODE			0x80
#define	IT_SCR_SCDR_RESET_MODE_BIT		7
#define	IT_SCR_SCDR_RESET_MODE_ASYNC		0
#define IT_SCR_SCDR_RESET_MODE_SYNC		1
#define	IT_SCR_SCDR_DIVISOR			0x7F
#define	IT_SCR_SCDR_DIVISOR_BIT			0
#define	IT_SCR_SCDR_DIVISOR_STOP_VAL_1		0x00
#define	IT_SCR_SCDR_DIVISOR_STOP_VAL_2		0x01
#define	IT_SCR_SCDR_DIVISOR_STOP_VAL_3		0x7F

// IT8172 DMA
#define IT_DMAC_BASE				0x16000
#define IT_DMAC_BCAR0				0x00
#define IT_DMAC_BCAR1				0x04
#define IT_DMAC_BCAR2				0x08
#define IT_DMAC_BCAR3				0x0C
#define IT_DMAC_BCCR0				0x02
#define IT_DMAC_BCCR1				0x06
#define IT_DMAC_BCCR2				0x0a
#define IT_DMAC_BCCR3				0x0e
#define IT_DMAC_CR				0x10
#define IT_DMAC_SR				0x12
#define IT_DMAC_ESR				0x13
#define IT_DMAC_RQR				0x14
#define IT_DMAC_MR				0x16
#define IT_DMAC_EMR				0x17
#define IT_DMAC_MKR				0x18
#define IT_DMAC_PAR0				0x20
#define IT_DMAC_PAR1				0x22
#define IT_DMAC_PAR2				0x24
#define IT_DMAC_PAR3				0x26

// IT8172 IDE
#define IT_IDE_BASE				0x17800
#define IT_IDE_STATUS				0x1F7

// IT8172 Audio Controller
#define IT_AC_BASE				0x17000
#define	IT_AC_PCMOV				0x00
#define IT_AC_FMOV				0x02
#define	IT_AC_I2SV				0x04
#define IT_AC_DRSS				0x06
#define IT_AC_PCC				0x08
#define IT_AC_PCDL				0x0A
#define IT_AC_PCB1STA				0x0C
#define IT_AC_PCB2STA				0x10
#define IT_AC_CAPCC				0x14
#define IT_AC_CAPCDL				0x16
#define IT_AC_CAPB1STA				0x18
#define IT_AC_CAPB2STA				0x1C
#define IT_AC_CODECC				0x22
#define IT_AC_I2SMC				0x24
#define IT_AC_VS				0x26
#define IT_AC_SRCS				0x28
#define IT_AC_CIRCP				0x2A
#define IT_AC_CIRDP				0x2C
#define IT_AC_TM				0x4A
#define IT_AC_PFDP				0x4C
#define IT_AC_GC				0x54
#define IT_AC_IMC				0x56
#define IT_AC_ISC				0x5B
#define IT_AC_OPL3SR				0x68
#define IT_AC_OPL3DWDR				0x69
#define IT_AC_OPL3AB1W				0x6A
#define IT_AC_OPL3DW				0x6B
#define IT_AC_BPDC				0x70


// IT8172 Timer
#define IT_TIMER_BASE				0x10800
#define	TIMER_TCVR0				0x00
#define TIMER_TRVR0				0x02
#define	TIMER_TCR0				0x04
#define TIMER_TIRR				0x06
#define	TIMER_TCVR1				0x08
#define TIMER_TRVR1				0x0A
#define	TIMER_TCR1				0x0C
#define TIMER_TIDR				0x0E


#define IT_WRITE(ofs, data) *(volatile u32 *)KSEG1ADDR((IT8172_BASE+ofs)) = data
#define IT_READ(ofs, data)  data = *(volatile u32 *)KSEG1ADDR((IT8172_BASE+ofs))

#define IT_IO_WRITE(ofs, data) *(volatile u32 *)KSEG1ADDR((IT8172_PCI_IO_BASE+ofs)) = data
#define IT_IO_READ(ofs, data)  data = *(volatile u32 *)KSEG1ADDR((IT8172_PCI_IO_BASE+ofs))

#define IT_IO_WRITE16(ofs, data) *(volatile u16 *)KSEG1ADDR((IT8172_PCI_IO_BASE+ofs)) = data
#define IT_IO_READ16(ofs, data)  data = *(volatile u16 *)KSEG1ADDR((IT8172_PCI_IO_BASE+ofs))

#endif
