/*	$OpenBSD: i82365reg.h,v 1.7 1999/05/02 22:35:40 fgsch Exp $	*/
/*	$NetBSD: i82365reg.h,v 1.2 1997/10/16 23:18:18 thorpej Exp $	*/

/*
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
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
 *	This product includes software developed by Marc Horowitz.
 * 4. The name of the author may not be used to endorse or promote products
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
 */

/*
 * All information is from the intel 82365sl PC Card Interface Controller
 * (PCIC) data sheet, marked "preliminary".  Order number 290423-002, January
 * 1993.
 */

#define	PCIC_IOSIZE		2

#define	PCIC_REG_INDEX		0
#define	PCIC_REG_DATA		1

/*
 * The PCIC allows two chips to share the same address.  In order not to run
 * afoul of the netbsd device model, this driver will treat those chips as
 * the same device.
 */

#define	PCIC_CHIP0_BASE		0x00
#define	PCIC_CHIP1_BASE		0x80

/* Each PCIC chip can drive two sockets */

#define	PCIC_SOCKETA_INDEX	0x00
#define	PCIC_SOCKETB_INDEX	0x40

/* general setup registers */

#define	PCIC_IDENT				0x00	/* RO */
#define	PCIC_IDENT_IFTYPE_MASK			0xC0
#define	PCIC_IDENT_IFTYPE_IO_ONLY		0x00
#define	PCIC_IDENT_IFTYPE_MEM_ONLY		0x40
#define	PCIC_IDENT_IFTYPE_MEM_AND_IO		0x80
#define	PCIC_IDENT_IFTYPE_RESERVED		0xC0
#define	PCIC_IDENT_ZERO				0x30
#define	PCIC_IDENT_REV_MASK			0x0F
#define	PCIC_IDENT_REV_I82365SLR0		0x82
#define	PCIC_IDENT_REV_I82365SLR1		0x83
#define	PCIC_IDENT_REV_I82365SLR2		0x84

#define	PCIC_IF_STATUS				0x01	/* RO */
#define	PCIC_IF_STATUS_GPI			0x80 /* General Purpose Input */
#define	PCIC_IF_STATUS_POWERACTIVE		0x40
#define	PCIC_IF_STATUS_READY			0x20 /* really READY/!BUSY */
#define	PCIC_IF_STATUS_MEM_WP			0x10
#define	PCIC_IF_STATUS_CARDDETECT_MASK		0x0C
#define	PCIC_IF_STATUS_CARDDETECT_PRESENT	0x0C
#define	PCIC_IF_STATUS_BATTERY_MASK		0x03
#define	PCIC_IF_STATUS_BATTERY_DEAD1		0x00
#define	PCIC_IF_STATUS_BATTERY_DEAD2		0x01
#define	PCIC_IF_STATUS_BATTERY_WARNING		0x02
#define	PCIC_IF_STATUS_BATTERY_GOOD		0x03

#define	PCIC_PWRCTL				0x02	/* RW */
#define	PCIC_PWRCTL_OE				0x80	/* output enable */
#define	PCIC_PWRCTL_DISABLE_RESETDRV		0x40
#define	PCIC_PWRCTL_AUTOSWITCH_ENABLE		0x20
#define	PCIC_PWRCTL_PWR_ENABLE			0x10
#define	PCIC_PWRCTL_VPP2_MASK			0x0C
/* XXX these are a little unclear from the data sheet */
#define	PCIC_PWRCTL_VPP2_RESERVED		0x0C
#define	PCIC_PWRCTL_VPP2_EN1			0x08
#define	PCIC_PWRCTL_VPP2_EN0			0x04
#define	PCIC_PWRCTL_VPP2_ENX			0x00
#define	PCIC_PWRCTL_VPP1_MASK			0x03
/* XXX these are a little unclear from the data sheet */
#define	PCIC_PWRCTL_VPP1_RESERVED		0x03
#define	PCIC_PWRCTL_VPP1_EN1			0x02
#define	PCIC_PWRCTL_VPP1_EN0			0x01
#define	PCIC_PWRCTL_VPP1_ENX			0x00

#define	PCIC_CSC				0x04	/* RW */
#define	PCIC_CSC_ZERO				0xE0
#define	PCIC_CSC_GPI				0x10
#define	PCIC_CSC_CD				0x08 /* Card Detect Change */
#define	PCIC_CSC_READY				0x04
#define	PCIC_CSC_BATTWARN			0x02
#define	PCIC_CSC_BATTDEAD			0x01	/* for memory cards */
#define	PCIC_CSC_RI				0x01	/* for i/o cards */

#define	PCIC_ADDRWIN_ENABLE			0x06	/* RW */
#define	PCIC_ADDRWIN_ENABLE_IO1			0x80
#define	PCIC_ADDRWIN_ENABLE_IO0			0x40
#define	PCIC_ADDRWIN_ENABLE_MEMCS16		0x20	/* rtfds if you care */
#define	PCIC_ADDRWIN_ENABLE_MEM4		0x10
#define	PCIC_ADDRWIN_ENABLE_MEM3		0x08
#define	PCIC_ADDRWIN_ENABLE_MEM2		0x04
#define	PCIC_ADDRWIN_ENABLE_MEM1		0x02
#define	PCIC_ADDRWIN_ENABLE_MEM0		0x01

#define	PCIC_CARD_DETECT			0x16	/* RW */
#define	PCIC_CARD_DETECT_RESERVED		0xC0
#define	PCIC_CARD_DETECT_SW_INTR		0x20
#define	PCIC_CARD_DETECT_RESUME_ENABLE		0x10
#define	PCIC_CARD_DETECT_GPI_TRANSCTL		0x08
#define	PCIC_CARD_DETECT_GPI_ENABLE		0x04
#define	PCIC_CARD_DETECT_CFGRST_ENABLE		0x02
#define	PCIC_CARD_DETECT_MEMDLY_INHIBIT		0x01

/* interrupt registers */

#define	PCIC_INTR				0x03	/* RW */
#define	PCIC_INTR_RI_ENABLE			0x80
#define	PCIC_INTR_RESET				0x40	/* active low (zero) */
#define	PCIC_INTR_CARDTYPE_MASK			0x20
#define	PCIC_INTR_CARDTYPE_IO			0x20
#define	PCIC_INTR_CARDTYPE_MEM			0x00
#define	PCIC_INTR_ENABLE			0x10
#define	PCIC_INTR_IRQ_MASK			0x0F
#define	PCIC_INTR_IRQ_SHIFT			0
#define	PCIC_INTR_IRQ_NONE			0x00
#define	PCIC_INTR_IRQ_RESERVED1			0x01
#define	PCIC_INTR_IRQ_RESERVED2			0x02
#define	PCIC_INTR_IRQ3				0x03
#define	PCIC_INTR_IRQ4				0x04
#define	PCIC_INTR_IRQ5				0x05
#define	PCIC_INTR_IRQ_RESERVED6			0x06
#define	PCIC_INTR_IRQ7				0x07
#define	PCIC_INTR_IRQ_RESERVED8			0x08
#define	PCIC_INTR_IRQ9				0x09
#define	PCIC_INTR_IRQ10				0x0A
#define	PCIC_INTR_IRQ11				0x0B
#define	PCIC_INTR_IRQ12				0x0C
#define	PCIC_INTR_IRQ_RESERVED13		0x0D
#define	PCIC_INTR_IRQ14				0x0E
#define	PCIC_INTR_IRQ15				0x0F

#define	PCIC_INTR_IRQ_VALIDMASK			0xDEB8 /* 1101 1110 1011 1000 */

#define	PCIC_CSC_INTR				0x05	/* RW */
#define	PCIC_CSC_INTR_IRQ_MASK			0xF0
#define	PCIC_CSC_INTR_IRQ_SHIFT			4
#define	PCIC_CSC_INTR_IRQ_NONE			0x00
#define	PCIC_CSC_INTR_IRQ_RESERVED1		0x10
#define	PCIC_CSC_INTR_IRQ_RESERVED2		0x20
#define	PCIC_CSC_INTR_IRQ3			0x30
#define	PCIC_CSC_INTR_IRQ4			0x40
#define	PCIC_CSC_INTR_IRQ5			0x50
#define	PCIC_CSC_INTR_IRQ_RESERVED6		0x60
#define	PCIC_CSC_INTR_IRQ7			0x70
#define	PCIC_CSC_INTR_IRQ_RESERVED8		0x80
#define	PCIC_CSC_INTR_IRQ9			0x90
#define	PCIC_CSC_INTR_IRQ10			0xA0
#define	PCIC_CSC_INTR_IRQ11			0xB0
#define	PCIC_CSC_INTR_IRQ12			0xC0
#define	PCIC_CSC_INTR_IRQ_RESERVED13		0xD0
#define	PCIC_CSC_INTR_IRQ14			0xE0
#define	PCIC_CSC_INTR_IRQ15			0xF0
#define	PCIC_CSC_INTR_CD_ENABLE			0x08
#define	PCIC_CSC_INTR_READY_ENABLE		0x04
#define	PCIC_CSC_INTR_BATTWARN_ENABLE		0x02
#define	PCIC_CSC_INTR_BATTDEAD_ENABLE		0x01	/* for memory cards */
#define	PCIC_CSC_INTR_RI_ENABLE			0x01	/* for I/O cards */

#define	PCIC_CSC_INTR_IRQ_VALIDMASK		0xDEB8 /* 1101 1110 1011 1000 */

/* I/O registers */

#define	PCIC_IO_WINS				2

#define	PCIC_IOCTL				0x07	/* RW */
#define	PCIC_IOCTL_IO1_WAITSTATE		0x80
#define	PCIC_IOCTL_IO1_ZEROWAIT			0x40
#define	PCIC_IOCTL_IO1_IOCS16SRC_MASK		0x20
#define	PCIC_IOCTL_IO1_IOCS16SRC_CARD		0x20
#define	PCIC_IOCTL_IO1_IOCS16SRC_DATASIZE	0x00
#define	PCIC_IOCTL_IO1_DATASIZE_MASK		0x10
#define	PCIC_IOCTL_IO1_DATASIZE_16BIT		0x10
#define	PCIC_IOCTL_IO1_DATASIZE_8BIT		0x00
#define	PCIC_IOCTL_IO0_WAITSTATE		0x08
#define	PCIC_IOCTL_IO0_ZEROWAIT			0x04
#define	PCIC_IOCTL_IO0_IOCS16SRC_MASK		0x02
#define	PCIC_IOCTL_IO0_IOCS16SRC_CARD		0x02
#define	PCIC_IOCTL_IO0_IOCS16SRC_DATASIZE	0x00
#define	PCIC_IOCTL_IO0_DATASIZE_MASK		0x01
#define	PCIC_IOCTL_IO0_DATASIZE_16BIT		0x01
#define	PCIC_IOCTL_IO0_DATASIZE_8BIT		0x00

#define	PCIC_IOADDR0_START_LSB			0x08
#define	PCIC_IOADDR0_START_MSB			0x09
#define	PCIC_IOADDR0_STOP_LSB			0x0A
#define	PCIC_IOADDR0_STOP_MSB			0x0B
#define	PCIC_IOADDR1_START_LSB			0x0C
#define	PCIC_IOADDR1_START_MSB			0x0D
#define	PCIC_IOADDR1_STOP_LSB			0x0E
#define	PCIC_IOADDR1_STOP_MSB			0x0F

/* memory registers */

/*
 * memory window addresses refer to bits A23-A12 of the ISA system memory
 * address.  This is a shift of 12 bits.  The LSB contains A19-A12, and the
 * MSB contains A23-A20, plus some other bits.
 */

#define	PCIC_MEM_WINS				5

#define	PCIC_MEM_SHIFT				12
#define	PCIC_MEM_PAGESIZE			(1<<PCIC_MEM_SHIFT)

#define	PCIC_SYSMEM_ADDRX_SHIFT				PCIC_MEM_SHIFT
#define	PCIC_SYSMEM_ADDRX_START_MSB_DATASIZE_MASK	0x80
#define	PCIC_SYSMEM_ADDRX_START_MSB_DATASIZE_16BIT	0x80
#define	PCIC_SYSMEM_ADDRX_START_MSB_DATASIZE_8BIT	0x00
#define	PCIC_SYSMEM_ADDRX_START_MSB_ZEROWAIT		0x40
#define	PCIC_SYSMEM_ADDRX_START_MSB_SCRATCH_MASK	0x30
#define	PCIC_SYSMEM_ADDRX_START_MSB_ADDR_MASK		0x0F

#define	PCIC_SYSMEM_ADDRX_STOP_MSB_WAIT_MASK		0xC0
#define	PCIC_SYSMEM_ADDRX_STOP_MSB_WAIT0		0x00
#define	PCIC_SYSMEM_ADDRX_STOP_MSB_WAIT1		0x40
#define	PCIC_SYSMEM_ADDRX_STOP_MSB_WAIT2		0x80
#define	PCIC_SYSMEM_ADDRX_STOP_MSB_WAIT3		0xC0
#define	PCIC_SYSMEM_ADDRX_STOP_MSB_ADDR_MASK		0x0F

/*
 * The card side of a memory mapping consists of bits A19-A12 of the card
 * memory address in the LSB, and A25-A20 plus some other bits in the MSB.
 * Again, the shift is 12 bits.
 */

#define	PCIC_CARDMEM_ADDRX_SHIFT		PCIC_MEM_SHIFT
#define	PCIC_CARDMEM_ADDRX_MSB_WP		0x80
#define	PCIC_CARDMEM_ADDRX_MSB_REGACTIVE_MASK	0x40
#define	PCIC_CARDMEM_ADDRX_MSB_REGACTIVE_ATTR	0x40
#define	PCIC_CARDMEM_ADDRX_MSB_REGACTIVE_COMMON	0x00
#define	PCIC_CARDMEM_ADDRX_MSB_ADDR_MASK	0x3F

#define	PCIC_SYSMEM_ADDR0_START_LSB		0x10
#define	PCIC_SYSMEM_ADDR0_START_MSB		0x11
#define	PCIC_SYSMEM_ADDR0_STOP_LSB		0x12
#define	PCIC_SYSMEM_ADDR0_STOP_MSB		0x13

#define	PCIC_CARDMEM_ADDR0_LSB			0x14
#define	PCIC_CARDMEM_ADDR0_MSB			0x15

/* #define	PCIC_RESERVED			0x17 */

#define	PCIC_SYSMEM_ADDR1_START_LSB		0x18
#define	PCIC_SYSMEM_ADDR1_START_MSB		0x19
#define	PCIC_SYSMEM_ADDR1_STOP_LSB		0x1A
#define	PCIC_SYSMEM_ADDR1_STOP_MSB		0x1B

#define	PCIC_CARDMEM_ADDR1_LSB			0x1C
#define	PCIC_CARDMEM_ADDR1_MSB			0x1D

#define	PCIC_SYSMEM_ADDR2_START_LSB		0x20
#define	PCIC_SYSMEM_ADDR2_START_MSB		0x21
#define	PCIC_SYSMEM_ADDR2_STOP_LSB		0x22
#define	PCIC_SYSMEM_ADDR2_STOP_MSB		0x23

#define	PCIC_CARDMEM_ADDR2_LSB			0x24
#define	PCIC_CARDMEM_ADDR2_MSB			0x25

/* #define	PCIC_RESERVED			0x26 */
/* #define	PCIC_RESERVED			0x27 */

#define	PCIC_SYSMEM_ADDR3_START_LSB		0x28
#define	PCIC_SYSMEM_ADDR3_START_MSB		0x29
#define	PCIC_SYSMEM_ADDR3_STOP_LSB		0x2A
#define	PCIC_SYSMEM_ADDR3_STOP_MSB		0x2B

#define	PCIC_CARDMEM_ADDR3_LSB			0x2C
#define	PCIC_CARDMEM_ADDR3_MSB			0x2D

/* #define	PCIC_RESERVED			0x2E */
/* #define	PCIC_RESERVED			0x2F */

#define	PCIC_SYSMEM_ADDR4_START_LSB		0x30
#define	PCIC_SYSMEM_ADDR4_START_MSB		0x31
#define	PCIC_SYSMEM_ADDR4_STOP_LSB		0x32
#define	PCIC_SYSMEM_ADDR4_STOP_MSB		0x33

#define	PCIC_CARDMEM_ADDR4_LSB			0x34
#define	PCIC_CARDMEM_ADDR4_MSB			0x35

/* #define	PCIC_RESERVED			0x36 */
/* #define	PCIC_RESERVED			0x37 */
/* #define	PCIC_RESERVED			0x38 */
/* #define	PCIC_RESERVED			0x39 */
/* #define	PCIC_RESERVED			0x3A */
/* #define	PCIC_RESERVED			0x3B */
/* #define	PCIC_RESERVED			0x3C */
/* #define	PCIC_RESERVED			0x3D */
/* #define	PCIC_RESERVED			0x3E */
/* #define	PCIC_RESERVED			0x3F */

/* vendor-specific registers */

#define	PCIC_INTEL_GLOBAL_CTL			0x1E	/* RW */
#define	PCIC_INTEL_GLOBAL_CTL_RESERVED		0xF0
#define	PCIC_INTEL_GLOBAL_CTL_IRQ14PULSE_ENABLE	0x08
#define	PCIC_INTEL_GLOBAL_CTL_EXPLICIT_CSC_ACK	0x04
#define	PCIC_INTEL_GLOBAL_CTL_IRQLEVEL_ENABLE	0x02
#define	PCIC_INTEL_GLOBAL_CTL_POWERDOWN		0x01

#define	PCIC_CIRRUS_MISC_CTL_2			0x1E
#define	PCIC_CIRRUS_MISC_CTL_2_SUSPEND		0x04

#define	PCIC_CIRRUS_CHIP_INFO			0x1F
#define	PCIC_CIRRUS_CHIP_INFO_CHIP_ID		0xC0
#define	PCIC_CIRRUS_CHIP_INFO_SLOTS		0x20
#define	PCIC_CIRRUS_CHIP_INFO_REV		0x1F

#define	PCIC_CIRRUS_EXTENDED_INDEX		0x2E
#define	PCIC_CIRRUS_EXTENDED_DATA		0x2F
#define	PCIC_CIRRUS_EXT_CONTROL_1		0x03
#define	PCIC_CIRRUS_EXT_CONTROL_1_PCI_INTR_MASK	0x18

#define	PCIC_IDENT_VADEM_MASK			0x08

#define	PCIC_VG468_MISC				0x3A
#define	PCIC_VG468_MISC_VADEMREV		0x40

#define	PCIC_VG469_VSELECT			0x2f
#define	PCIC_VG469_VSELECT_VCC			0x03
