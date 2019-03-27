/*
 * Copyright (c) 2014 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com> and was subsequently ported
 * to FreeBSD by Michael Gmelin <freebsd@grem.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/*
 * Intel fourth generation mobile cpus integrated I2C device.
 *
 * Datasheet reference:  Section 22.
 *
 * http://www.intel.com/content/www/us/en/processors/core/4th-gen-core-family-mobile-i-o-datasheet.html?wapkw=datasheets+4th+generation
 *
 * This is a from-scratch driver under the BSD license using the Intel data
 * sheet and the linux driver for reference.  All code is freshly written
 * without referencing the linux driver code.  However, during testing
 * I am also using the linux driver code as a reference to help resolve any
 * issues that come.  These will be specifically documented in the code.
 *
 * This controller is an I2C master only and cannot act as a slave.  The IO
 * voltage should be set by the BIOS.  Standard (100Kb/s) and Fast (400Kb/s)
 * and fast mode plus (1MB/s) is supported.  High speed mode (3.4 MB/s) is NOT
 * supported.
 */

#ifndef _ICHIIC_IG4_REG_H_
#define _ICHIIC_IG4_REG_H_

/*
 * 22.2 MMIO registers can be accessed through BAR0 in PCI mode or through
 *	BAR1 when in ACPI mode.
 *
 *	Register width is 32-bits
 *
 * 22.2 Default Values on device reset are 0 except as specified here:
 *	TAR_ADD		0x00000055
 *	SS_SCL_HCNT	0x00000264
 *	SS_SCL_LCNT	0x000002C2
 *	FS_SCL_HCNT	0x0000006E
 *	FS_SCL_LCNT	0x000000CF
 *	INTR_MASK	0x000008FF
 *	I2C_STA		0x00000006
 *	SDA_HOLD	0x00000001
 *	SDA_SETUP	0x00000064
 *	COMP_PARAM1	0x00FFFF6E
 */

#define IG4_REG_CTL		0x0000	/* RW	Control Register */
#define IG4_REG_TAR_ADD		0x0004	/* RW	Target Address */
#define IG4_REG_HS_MADDR	0x000C	/* RW	High Speed Master Mode Code Address*/
#define IG4_REG_DATA_CMD	0x0010	/* RW	Data Buffer and Command */
#define IG4_REG_SS_SCL_HCNT	0x0014	/* RW	Std Speed clock High Count */
#define IG4_REG_SS_SCL_LCNT	0x0018	/* RW	Std Speed clock Low Count */
#define IG4_REG_FS_SCL_HCNT	0x001C	/* RW	Fast Speed clock High Count */
#define IG4_REG_FS_SCL_LCNT	0x0020	/* RW	Fast Speed clock Low Count */
#define IG4_REG_INTR_STAT	0x002C	/* RO	Interrupt Status */
#define IG4_REG_INTR_MASK	0x0030	/* RW	Interrupt Mask */
#define IG4_REG_RAW_INTR_STAT	0x0034	/* RO	Raw Interrupt Status */
#define IG4_REG_RX_TL		0x0038	/* RW	Receive FIFO Threshold */
#define IG4_REG_TX_TL		0x003C	/* RW	Transmit FIFO Threshold */
#define IG4_REG_CLR_INTR	0x0040	/* RO	Clear Interrupt */
#define IG4_REG_CLR_RX_UNDER	0x0044	/* RO	Clear RX_Under Interrupt */
#define IG4_REG_CLR_RX_OVER	0x0048	/* RO	Clear RX_Over Interrupt */
#define IG4_REG_CLR_TX_OVER	0x004C	/* RO	Clear TX_Over Interrupt */
#define IG4_REG_CLR_RD_REQ	0x0050	/* RO	Clear RD_Req Interrupt */
#define IG4_REG_CLR_TX_ABORT	0x0054	/* RO	Clear TX_Abort Interrupt */
#define IG4_REG_CLR_RX_DONE	0x0058	/* RO	Clear RX_Done Interrupt */
#define IG4_REG_CLR_ACTIVITY	0x005C	/* RO	Clear Activity Interrupt */
#define IG4_REG_CLR_STOP_DET	0x0060	/* RO	Clear STOP Detection Int */
#define IG4_REG_CLR_START_DET	0x0064	/* RO	Clear START Detection Int */
#define IG4_REG_CLR_GEN_CALL	0x0068	/* RO	Clear General Call Interrupt */
#define IG4_REG_I2C_EN		0x006C	/* RW	I2C Enable */
#define IG4_REG_I2C_STA		0x0070	/* RO	I2C Status */
#define IG4_REG_TXFLR		0x0074	/* RO	Transmit FIFO Level */
#define IG4_REG_RXFLR		0x0078	/* RO	Receive FIFO Level */
#define IG4_REG_SDA_HOLD	0x007C	/* RW	SDA Hold Time Length */
#define IG4_REG_TX_ABRT_SOURCE	0x0080	/* RO	Transmit Abort Source */
#define IG4_REG_SLV_DATA_NACK	0x0084	/* RW	General Slave Data NACK */
#define IG4_REG_DMA_CTRL	0x0088	/* RW	DMA Control */
#define IG4_REG_DMA_TDLR	0x008C	/* RW	DMA Transmit Data Level */
#define IG4_REG_DMA_RDLR	0x0090	/* RW	DMA Receive Data Level */
#define IG4_REG_SDA_SETUP	0x0094	/* RW	SDA Setup */
#define IG4_REG_ACK_GENERAL_CALL 0x0098	/* RW	I2C ACK General Call */
#define IG4_REG_ENABLE_STATUS	0x009C	/* RO	Enable Status */
/* Available at least on Atom SoCs and Haswell mobile. */
#define IG4_REG_COMP_PARAM1	0x00F4	/* RO	Component Parameter */
#define IG4_REG_COMP_VER	0x00F8	/* RO	Component Version */
/* Available at least on Atom SoCs */
#define IG4_REG_COMP_TYPE	0x00FC	/* RO	Probe width/endian? (linux) */
/* Available on Skylake-U/Y and Kaby Lake-U/Y */
#define IG4_REG_RESETS_SKL	0x0204	/* RW	Reset Register */
#define IG4_REG_ACTIVE_LTR_VALUE 0x0210	/* RW	Active LTR Value */
#define IG4_REG_IDLE_LTR_VALUE	0x0214	/* RW	Idle LTR Value */
#define IG4_REG_TX_ACK_COUNT	0x0218	/* RO	TX ACK Count */
#define IG4_REG_RX_BYTE_COUNT	0x021C	/* RO	RX ACK Count */
#define IG4_REG_DEVIDLE_CTRL	0x024C	/* RW	Device Control */
/* Available at least on Atom SoCs */
#define IG4_REG_CLK_PARMS	0x0800	/* RW	Clock Parameters */
/* Available at least on Atom SoCs and Haswell mobile */
#define IG4_REG_RESETS_HSW	0x0804	/* RW	Reset Register */
#define IG4_REG_GENERAL		0x0808	/* RW	General Register */
/* These LTR config registers are at least available on Haswell mobile. */
#define IG4_REG_SW_LTR_VALUE	0x0810	/* RW	SW LTR Value */
#define IG4_REG_AUTO_LTR_VALUE	0x0814	/* RW	Auto LTR Value */

/*
 * CTL - Control Register 		22.2.1
 *	 Default Value: 0x0000007F.
 *
 *	RESTARTEN	- RW Restart Enable
 *	10BIT		- RW Controller operates in 10-bit mode, else 7-bit
 *
 * NOTE: When restart is disabled the controller is incapable of
 *	 performing the following functions:
 *
 *		 Sending a START Byte
 *		 Performing any high-speed mode op
 *		 Performing direction changes in combined format mode
 *		 Performing a read operation with a 10-bit address
 *
 *	 Attempting to perform the above operations will result in the
 *	 TX_ABORT bit being set in RAW_INTR_STAT.
 */
#define IG4_CTL_SLAVE_DISABLE	0x0040	/* snarfed from linux */
#define IG4_CTL_RESTARTEN	0x0020	/* Allow Restart when master */
#define IG4_CTL_10BIT		0x0010	/* ctlr accepts 10-bit addresses */
#define IG4_CTL_SPEED_FAST	0x0004	/* snarfed from linux */
#define IG4_CTL_SPEED_STD	0x0002	/* snarfed from linux */
#define IG4_CTL_MASTER		0x0001	/* snarfed from linux */

/*
 * TAR_ADD - Target Address Register	22.2.2
 *	     Default Value: 0x00000055F
 *
 *	10BIT		- RW controller starts its transfers in 10-bit
 *			  address mode, else 7-bit.
 *
 *	SPECIAL		- RW Indicates whether software performs a General Call
 *			  or START BYTE command.
 *
 *		0	  Ignore GC_OR_START and use TAR address.
 *
 *		1	  Perform special I2C Command based on GC_OR_START.
 *
 *	GC_OR_START	- RW (only if SPECIAL is set)
 *
 *		0	  General Call Address.  After issuing a General Call,
 *			  only writes may be performed.  Attempting to issue
 *			  a read command results in IX_ABRT in RAW_INTR_STAT.
 *			  The controller remains in General Call mode until
 *			  bit 11 (SPECIAL) is cleared.
 *
 *		1	  START BYTE.
 *
 *
 * 	IC_TAR		- RW when transmitting a general call, these bits are
 *			  ignored.  To generate a START BYTE, the address
 *			  needs to be written into these bits once.
 *
 * This register should only be updated when the IIC is disabled (I2C_ENABLE=0)
 */
#define IG4_TAR_10BIT		0x1000	/* start xfer in 10-bit mode */
#define IG4_TAR_SPECIAL		0x0800	/* Perform special command */
#define IG4_TAR_GC_OR_START	0x0400	/* General Call or Start */
#define IG4_TAR_ADDR_MASK	0x03FF	/* Target address */

/*
 * TAR_DATA_CMD - Data Buffer and Command Register	22.2.3
 *
 *	RESTART		- RW This bit controls whether a forced RESTART is
 *			  issued before the byte is sent or received.
 *
 *		0	  If not set a RESTART is only issued if the transfer
 *			  direction is changing from the previous command.
 *
 *		1	  A RESTART is issued before the byte is sent or
 *			  received, regardless of whether or not the transfer
 *			  direction is changing from the previous command.
 *
 *	STOP		- RW This bit controls whether a STOP is issued after
 *			  the byte is sent or received.
 *
 *		0	  STOP is not issued after this byte, regardless
 *			  of whether or not the Tx FIFO is empty.
 *
 *		1	  STOP is issued after this byte, regardless of
 *			  whether or not the Tx FIFO is empty.  If the
 *			  Tx FIFO is not empty the master immediately tries
 *			  to start a new transfer by issuing a START and
 *			  arbitrating for the bus.
 *
 *			  i.e. the STOP is issued along with this byte,
 *			  within the write stream.
 *
 *	COMMAND		- RW Control whether a read or write is performed.
 *
 *		0	  WRITE
 *
 *		1	  READ
 *
 *	DATA (7:0)	- RW Contains the data to be transmitted or received
 *			  on the I2C bus.
 *
 *	NOTE: Writing to this register causes a START + slave + RW to be
 *	      issued if the direction has changed or the last data byte was
 *	      sent with a STOP.
 *
 *	NOTE: We control termination?  so this register must be written
 *	      for each byte we wish to receive.  We can then drain the
 *	      receive FIFO.
 */

#define IG4_DATA_RESTART	0x0400	/* Force RESTART */
#define IG4_DATA_STOP		0x0200	/* Force STOP[+START] */
#define IG4_DATA_COMMAND_RD	0x0100	/* bus direction 0=write 1=read */
#define IG4_DATA_MASK		0x00FF

/*
 * SS_SCL_HCNT - Standard Speed Clock High Count Register	22.2.4
 * SS_SCL_LCNT - Standard Speed Clock Low Count Register	22.2.5
 * FS_SCL_HCNT - Fast Speed Clock High Count Register		22.2.6
 * FS_SCL_LCNT - Fast Speed Clock Low Count Register		22.2.7
 *
 *	COUNT (15:0)	- Set the period count to a value between 6 and
 *			  65525.
 */
#define IG4_SCL_CLOCK_MASK	0xFFFFU	/* count bits in register */

/*
 * INTR_STAT	- (RO) Interrupt Status Register		22.2.8
 * INTR_MASK	- (RW) Interrupt Mask Register			22.2.9
 * RAW_INTR_STAT- (RO) Raw Interrupt Status Register		22.2.10
 *
 *	GEN_CALL	Set only when a general call (broadcast) address
 *			is received and acknowleged, stays set until
 *			cleared by reading CLR_GEN_CALL.
 *
 *	START_DET	Set when a START or RESTART condition has occurred
 *			on the interface.
 *
 *	STOP_DET	Set when a STOP condition has occurred on the
 *			interface.
 *
 *	ACTIVITY	Set by any activity on the interface.  Cleared
 *			by reading CLR_ACTIVITY or CLR_INTR.
 *
 *	TX_ABRT		Indicates the controller as a transmitter is
 *			unable to complete the intended action.  When set,
 *			the controller will hold the TX FIFO in a reset
 *			state (flushed) until CLR_TX_ABORT is read to
 *			clear the condition.  Once cleared, the TX FIFO
 *			will be available again.
 *
 *	TX_EMPTY	Indicates that the transmitter is at or below
 *			the specified TX_TL threshold.  Automatically
 *			cleared by HW when the buffer level goes above
 *			the threshold.
 *
 *	TX_OVER		Indicates that the processor attempted to write
 *			to the TX FIFO while the TX FIFO was full.  Cleared
 *			by reading CLR_TX_OVER.
 *
 *	RX_FULL		Indicates that the receive FIFO has reached or
 *			exceeded the specified RX_TL threshold.  Cleared
 *			by HW when the cpu drains the FIFO to below the
 *			threshold.
 *
 *	RX_OVER		Indicates that the receive FIFO was unable to
 *			accept new data and data was lost.  Cleared by
 *			reading CLR_RX_OVER.
 *
 *	RX_UNDER	Indicates that the cpu attempted to read data
 *			from the receive buffer while the RX FIFO was
 *			empty.  Cleared by reading CLR_RX_UNDER.
 *
 * NOTES ON RAW_INTR_STAT:
 *
 *	This register can be used to monitor the GEN_CALL, START_DET,
 *	STOP_DET, ACTIVITY, TX_ABRT, TX_EMPTY, TX_OVER, RX_FULL, RX_OVER,
 *	and RX_UNDER bits.  The documentation is a bit unclear but presumably
 *	this is the unlatched version.
 *
 *	Code should test FIFO conditions using the I2C_STA (status) register,
 *	not the interrupt status registers.
 */

#define IG4_INTR_GEN_CALL	0x0800
#define IG4_INTR_START_DET	0x0400
#define IG4_INTR_STOP_DET	0x0200
#define IG4_INTR_ACTIVITY	0x0100
#define IG4_INTR_TX_ABRT	0x0040
#define IG4_INTR_TX_EMPTY	0x0010
#define IG4_INTR_TX_OVER	0x0008
#define IG4_INTR_RX_FULL	0x0004
#define IG4_INTR_RX_OVER	0x0002
#define IG4_INTR_RX_UNDER	0x0001

/*
 * RX_TL	- (RW) Receive FIFO Threshold Register		22.2.11
 * TX_TL	- (RW) Transmit FIFO Threshold Register		22.2.12
 *
 * 	Specify the receive and transmit FIFO threshold register.  The
 *	FIFOs have 16 elements.  The valid range is 0-15.  Setting a
 *	value greater than 15 causes the actual value to be the maximum
 *	depth of the FIFO.
 *
 *	Generally speaking since everything is messaged, we can use a
 *	mid-level setting for both parameters and (e.g.) fully drain the
 *	receive FIFO on the STOP_DET condition to handle loose ends.
 */
#define IG4_FIFO_MASK		0x00FF
#define IG4_FIFO_LIMIT		16

/*
 * CLR_INTR	- (RO) Clear Interrupt Register			22.2.13
 * CLR_RX_UNDER	- (RO) Clear Interrupt Register (specific)	22.2.14
 * CLR_RX_OVER	- (RO) Clear Interrupt Register (specific)	22.2.15
 * CLR_TX_OVER	- (RO) Clear Interrupt Register (specific)	22.2.16
 * CLR_TX_ABORT	- (RO) Clear Interrupt Register (specific)	22.2.17
 * CLR_ACTIVITY	- (RO) Clear Interrupt Register (specific)	22.2.18
 * CLR_STOP_DET	- (RO) Clear Interrupt Register (specific)	22.2.19
 * CLR_START_DET- (RO) Clear Interrupt Register (specific)	22.2.20
 * CLR_GEN_CALL	- (RO) Clear Interrupt Register (specific)	22.2.21
 *
 *	CLR_* specific operations clear the appropriate bit in the
 *	RAW_INTR_STAT register.  Intel does not really document whether
 *	these operations clear the normal interrupt status register.
 *
 *	CLR_INTR clears bits in the normal interrupt status register and
 *	presumably also the raw(?) register?  Intel is again unclear.
 *
 * NOTE: CLR_INTR only clears software-clearable interrupts.  Hardware
 *	 clearable interrupts are controlled entirely by the hardware.
 *	 CLR_INTR also clears the TX_ABRT_SOURCE register.
 *
 * NOTE: CLR_TX_ABORT also clears the TX_ABRT_SOURCE register and releases
 *	 the TX FIFO from its flushed/reset state, allowing more writes
 *	 to the TX FIFO.
 *
 * NOTE: CLR_ACTIVITY has no effect if the I2C bus is still active.
 *	 Intel documents that the bit is automatically cleared when
 *	 there is no further activity on the bus.
 */
#define IG4_CLR_BIT		0x0001		/* Reflects source */

/*
 * I2C_EN	- (RW) I2C Enable Register			22.2.22
 *
 *	ABORT		Software can abort an I2C transfer by setting this
 *			bit.  Hardware will clear the bit once the STOP has
 *			been detected.  This bit can only be set while the
 *			I2C interface is enabled.
 *
 *	I2C_ENABLE	Enable the controller, else disable it.
 *			(Use I2C_ENABLE_STATUS to poll enable status
 *			& wait for changes)
 */
#define IG4_I2C_ABORT		0x0002
#define IG4_I2C_ENABLE		0x0001

/*
 * I2C_STA	- (RO) I2C Status Register			22.2.23
 */
#define IG4_STATUS_ACTIVITY	0x0020	/* Controller is active */
#define IG4_STATUS_RX_FULL	0x0010	/* RX FIFO completely full */
#define IG4_STATUS_RX_NOTEMPTY	0x0008	/* RX FIFO not empty */
#define IG4_STATUS_TX_EMPTY	0x0004	/* TX FIFO completely empty */
#define IG4_STATUS_TX_NOTFULL	0x0002	/* TX FIFO not full */
#define IG4_STATUS_I2C_ACTIVE	0x0001	/* I2C bus is active */

/*
 * TXFLR	- (RO) Transmit FIFO Level Register		22.2.24
 * RXFLR	- (RO) Receive FIFO Level Register		22.2.25
 *
 *	Read the number of entries currently in the Transmit or Receive
 *	FIFOs.  Note that for some reason the mask is 9 bits instead of
 *	the 8 bits the fill level controls.
 */
#define IG4_FIFOLVL_MASK	0x001F

/*
 * SDA_HOLD	- (RW) SDA Hold Time Length Register		22.2.26
 *
 *	Set the SDA hold time length register in I2C clocks.
 */
#define IG4_SDA_HOLD_MASK	0x00FF

/*
 * TX_ABRT_SOURCE- (RO) Transmit Abort Source Register		22.2.27
 *
 *	Indicates the cause of a transmit abort.  This can indicate a
 *	software programming error or a device expected address width
 *	mismatch or other issues.  The NORESTART conditions and GENCALL_NOACK
 *	can only occur if a programming error was made in the driver software.
 *
 *	In particular, it should be possible to detect whether any devices
 *	are on the bus by observing the GENCALL_READ status, and it might
 *	be possible to detect ADDR7 vs ADDR10 mismatches.
 */
#define IG4_ABRTSRC_TRANSFER		0x00010000 /* Abort initiated by user */
#define IG4_ABRTSRC_ARBLOST		0x00001000 /* Arbitration lost */
#define IG4_ABRTSRC_NORESTART_10	0x00000400 /* RESTART disabled */
#define IG4_ABRTSRC_NORESTART_START	0x00000200 /* RESTART disabled */
#define IG4_ABRTSRC_ACKED_START		0x00000080 /* Improper acked START */
#define IG4_ABRTSRC_GENCALL_NOACK	0x00000020 /* Improper GENCALL */
#define IG4_ABRTSRC_GENCALL_READ	0x00000010 /* Nobody acked GENCALL */
#define IG4_ABRTSRC_TXNOACK_DATA	0x00000008 /* data phase no ACK */
#define IG4_ABRTSRC_TXNOACK_ADDR10_2	0x00000004 /* addr10/1 phase no ACK */
#define IG4_ABRTSRC_TXNOACK_ADDR10_1	0x00000002 /* addr10/2 phase no ACK */
#define IG4_ABRTSRC_TXNOACK_ADDR7	0x00000001 /* addr7 phase no ACK */

/*
 * SLV_DATA_NACK - (RW) Generate Slave DATA NACK Register	22.2.28
 *
 *	When the controller is a receiver a NACK can be generated on
 *	receipt of data.
 *
 *	NACK_GENERATE		Set to 0 for normal NACK/ACK generation.
 *				Set to 1 to generate a NACK after next data
 *				byte received.
 *
 */
#define IG4_NACK_GENERATE	0x0001

/*
 * DMA_CTRL	- (RW) DMA Control Register			22.2.29
 *
 *	Enables DMA on the transmit and/or receive DMA channel.
 */
#define IG4_TX_DMA_ENABLE	0x0002
#define IG4_RX_DMA_ENABLE	0x0001

/*
 * DMA_TDLR	- (RW) DMA Transmit Data Level Register		22.2.30
 * DMA_RDLR	- (RW) DMA Receive Data Level Register		22.2.31
 *
 *	Similar to RX_TL and TX_TL but controls when a DMA burst occurs
 *	to empty or fill the FIFOs.  Use the same IG4_FIFO_MASK and
 *	IG4_FIFO_LIMIT defines for RX_RL and TX_TL.
 */
/* empty */

/*
 * SDA_SETUP	- (RW) SDA Setup Time Length Register		22.2.32
 *
 *	Set the SDA setup time length register in I2C clocks.
 *	The register must be programmed with a value >=2.
 *	(Defaults to 0x64).
 */
#define IG4_SDA_SETUP_MASK	0x00FF

/*
 * ACK_GEN_CALL	- (RW) ACK General Call Register		22.2.33
 *
 *	Control whether the controller responds with a ACK or NACK when
 *	it receives an I2C General Call address.
 *
 *	If set to 0 a NACK is generated and a General Call interrupt is
 *	NOT generated.  Otherwise an ACK + interrupt is generated.
 */
#define IG4_ACKGC_ACK		0x0001

/*
 * ENABLE_STATUS - (RO) Enable Status Registger			22.2.34
 *
 *	DATA_LOST	- Indicates that a slave receiver operation has
 *			  been aborted with at least one data byte received
 *			  from a transfer due to the I2C controller being
 *			  disabled (IG4_I2C_ENABLE -> 0)
 *
 *	ENABLED		- Intel documentation is lacking but I assume this
 *			  is a reflection of the IG4_I2C_ENABLE bit in the
 *			  I2C_EN register.
 *
 */
#define IG4_ENASTAT_DATA_LOST	0x0004
#define IG4_ENASTAT_ENABLED	0x0001

/*
 * COMP_PARAM1 - (RO) Component Parameter Register		22.2.35
 *		      Default Value 0x00FFFF6E
 *
 *	VALID		- Intel documentation is unclear but I believe this
 *			  must be read as a 1 to indicate that the rest of
 *			  the bits in the register are valid.
 *
 *	HASDMA		- Indicates that the chip is DMA-capable.  Presumably
 *			  in certain virtualization cases the chip might be
 *			  set to not be DMA-capable.
 *
 *	INTR_IO		- Indicates that all interrupts are combined to
 *			  generate one interrupt.  If not set, interrupts
 *			  are individual (more virtualization stuff?)
 *
 *	HCCNT_RO	- Indicates that the clock timing registers are
 *			  RW.  If not set, the registers are RO.
 *			  (more virtualization stuff).
 *
 *	MAXSPEED	- Indicates the maximum speed supported.
 *
 *	DATAW		- Indicates the internal bus width in bits.
 */
#define IG4_PARAM1_TXFIFO_DEPTH(v)	(((v) >> 16) & 0xFF)
#define IG4_PARAM1_RXFIFO_DEPTH(v)	(((v) >> 8) & 0xFF)
#define IG4_PARAM1_CONFIG_VALID		0x00000080
#define IG4_PARAM1_CONFIG_HASDMA	0x00000040
#define IG4_PARAM1_CONFIG_INTR_IO	0x00000020
#define IG4_PARAM1_CONFIG_HCCNT_RO	0x00000010
#define IG4_PARAM1_CONFIG_MAXSPEED_MASK	0x0000000C
#define IG4_PARAM1_CONFIG_DATAW_MASK	0x00000003

#define IG4_CONFIG_MAXSPEED_RESERVED00	0x00000000
#define IG4_CONFIG_MAXSPEED_STANDARD	0x00000004
#define IG4_CONFIG_MAXSPEED_FAST	0x00000008
#define IG4_CONFIG_MAXSPEED_HIGH	0x0000000C

#define IG4_CONFIG_DATAW_8		0x00000000
#define IG4_CONFIG_DATAW_16		0x00000001
#define IG4_CONFIG_DATAW_32		0x00000002
#define IG4_CONFIG_DATAW_RESERVED11	0x00000003

/*
 * COMP_VER - (RO) Component Version Register			22.2.36
 *
 *	Contains the chip version number.  All 32 bits.
 */
#define IG4_COMP_MIN_VER		0x3131352A

/*
 * COMP_TYPE - (RO) (linux) Endian and bus width probe
 *
 * 	Read32 from this register and test against IG4_COMP_TYPE
 *	to determine the bus width.  e.g. 01404457 = endian-reversed,
 *	and 00000140 or 00004457 means internal 16-bit bus (?).
 *
 *	This register is not in the intel documentation, I pulled it
 *	from the linux driver i2c-designware-core.c.
 */
#define IG4_COMP_TYPE		0x44570140

/*
 * RESETS - (RW) Resets Register				22.2.37
 *
 *	Used to reset the I2C host controller by SW.  There is no timing
 *	requirement, software can assert and de-assert in back-to-back
 *	transactions.
 *
 *	00	I2C host controller is NOT in reset.
 *	01	(reserved)
 *	10	(reserved)
 *	11	I2C host controller is in reset.
 */
#define IG4_RESETS_ASSERT_HSW	0x0003
#define IG4_RESETS_DEASSERT_HSW	0x0000

/* Skylake-U/Y and Kaby Lake-U/Y have the reset bits inverted */
#define IG4_RESETS_DEASSERT_SKL	0x0003
#define IG4_RESETS_ASSERT_SKL	0x0000

/* Newer versions of the I2C controller allow to check whether
 * the above ASSERT/DEASSERT is necessary by querying the DEVIDLE_CONTROL
 * register.
 * 
 * the RESTORE_REQUIRED bit can be cleared by writing 1
 * the DEVICE_IDLE status can be set to put the controller in an idle state
 *
 */
#define IG4_RESTORE_REQUIRED	0x0008
#define IG4_DEVICE_IDLE		0x0004

/*
 * GENERAL - (RW) General Reigster				22.2.38
 *
 *	IOVOLT	0=1.8V 1=3.3V
 *
 *	LTR	0=Auto 1=SW
 *
 *	    In Auto mode the BIOS will write to the host controller's
 *	    AUTO LTR Value register (offset 0x0814) with the active
 *	    state LTR value, and will write to the SW LTR Value register
 *	    (offset 0x0810) with the idle state LTR value.
 *
 *	    In SW mode the SW will write to the host controller SW LTR
 *	    value (offset 0x0810).  It is the SW responsibility to update
 *	    the LTR with the appropriate value.
 */
#define IG4_GENERAL_IOVOLT3_3	0x0008
#define IG4_GENERAL_SWMODE	0x0004

/*
 * SW_LTR_VALUE - (RW) SW LTR Value Register			22.2.39
 * AUTO_LTR_VALUE - (RW) SW LTR Value Register			22.2.40
 *
 *	Default value is 0x00000800 which means the best possible
 *	service/response time.
 *
 *	It isn't quite clear how the snooping works.  There are two scale
 *	bits for both sets but two of the four codes are reserved.  The
 *	*SNOOP_VALUE() is specified as a 10-bit latency value.  If 0, it
 *	indicates that the device cannot tolerate any delay and needs the
 *	best possible service/response time.
 *
 *	I think this is for snooping (testing) the I2C bus.  The lowest
 *	delay (0) probably runs the controller polling at a high, power hungry
 *	rate.  But I dunno.
 */
#define IG4_SWLTR_NSNOOP_REQ		0x80000000	/* (ro) */
#define IG4_SWLTR_NSNOOP_SCALE_MASK	0x1C000000	/* (ro) */
#define IG4_SWLTR_NSNOOP_SCALE_1US	0x08000000	/* (ro) */
#define IG4_SWLTR_NSNOOP_SCALE_32US	0x0C000000	/* (ro) */
#define IG4_SWLTR_NSNOOP_VALUE_DECODE(v) (((v) >> 16) & 0x3F)
#define IG4_SWLTR_NSNOOP_VALUE_ENCODE(v) (((v) & 0x3F) << 16)

#define IG4_SWLTR_SNOOP_REQ		0x00008000	/* (rw) */
#define IG4_SWLTR_SNOOP_SCALE_MASK	0x00001C00	/* (rw) */
#define IG4_SWLTR_SNOOP_SCALE_1US	0x00000800	/* (rw) */
#define IG4_SWLTR_SNOOP_SCALE_32US	0x00000C00	/* (rw) */
#define IG4_SWLTR_SNOOP_VALUE_DECODE(v)	 ((v) & 0x3F)
#define IG4_SWLTR_SNOOP_VALUE_ENCODE(v)	 ((v) & 0x3F)

#endif /* _ICHIIC_IG4_REG_H_ */
