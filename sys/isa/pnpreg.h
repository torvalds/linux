/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996, Sujal M. Patel
 * All rights reserved.
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
 *      This product includes software developed by Sujal M. Patel
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *      from: pnp.h,v 1.7 1998/09/13 22:15:44 eivind Exp
 */

#ifndef _ISA_PNPREG_H_
#define _ISA_PNPREG_H_

/* Maximum Number of PnP Devices.  8 should be plenty */
#define PNP_MAX_CARDS 8

/* Static ports to access PnP state machine */
#define _PNP_ADDRESS		0x279
#define _PNP_WRITE_DATA		0xa79

/* PnP Registers.  Write to ADDRESS and then use WRITE/READ_DATA */
#define PNP_SET_RD_DATA		0x00
	/***
	Writing to this location modifies the address of the port used for
	reading from the Plug and Play ISA cards.   Bits[7:0] become I/O
	read port address bits[9:2].  Reads from this register are ignored.
	***/

#define PNP_SERIAL_ISOLATION	0x01
	/***
	A read to this register causes a Plug and Play cards in the Isolation
	state to compare one bit of the boards ID.
	This register is read only.
	***/

#define	PNP_CONFIG_CONTROL	0x02
#define PNP_CONFIG_CONTROL_RESET_CSN	0x04
#define PNP_CONFIG_CONTROL_WAIT_FOR_KEY	0x02
#define PNP_CONFIG_CONTROL_RESET	0x01
	/***
	Bit[2]  Reset CSN to 0
	Bit[1]  Return to the Wait for Key state
	Bit[0]  Reset all logical devices and restore configuration
		registers to their power-up values.

	A write to bit[0] of this register performs a reset function on
	all logical devices.  This resets the contents of configuration
	registers to  their default state.  All card's logical devices
	enter their default state and the CSN is preserved.
		      
	A write to bit[1] of this register causes all cards to enter the
	Wait for Key state but all CSNs are preserved and logical devices
	are not affected.
			    
	A write to bit[2] of this register causes all cards to reset their
	CSN to zero .
			  
	This register is write-only.  The values are not sticky, that is,
	hardware will automatically clear them and there is no need for
	software to clear the bits.
	***/

#define PNP_WAKE		0x03
	/***
	A write to this port will cause all cards that have a CSN that
	matches the write data[7:0] to go from the Sleep state to the either
	the Isolation state if the write data for this command is zero or
	the Config state if the write data is not zero.  Additionally, the
	pointer to the byte-serial device is reset.  This register is  
	writeonly.
	***/

#define	PNP_RESOURCE_DATA	0x04
	/***
	A read from this address reads the next byte of resource information.
	The Status register must be polled until bit[0] is set before this
	register may be read.  This register is read only.
	***/

#define PNP_STATUS		0x05
	/***
	Bit[0] when set indicates it is okay to read the next data byte  
	from the Resource Data register.  This register is readonly.
	***/

#define PNP_SET_CSN		0x06
	/***
	A write to this port sets a card's CSN.  The CSN is a value uniquely
	assigned to each ISA card after the serial identification process
	so that each card may be individually selected during a Wake[CSN]
	command. This register is read/write. 
	***/

#define PNP_SET_LDN		0x07
	/***
	Selects the current logical device.  All reads and writes of memory,
	I/O, interrupt and DMA configuration information access the registers
	of the logical device written here.  In addition, the I/O Range
	Check and Activate  commands operate only on the selected logical
	device.  This register is read/write. If a card has only 1 logical
	device, this location should be a read-only value of 0x00.
	***/

/*** addresses 0x08 - 0x1F Card Level Reserved for future use ***/
/*** addresses 0x20 - 0x2F Card Level, Vendor Defined ***/

#define PNP_ACTIVATE		0x30
	/***
	For each logical device there is one activate register that controls
	whether or not the logical device is active on the ISA bus.  Bit[0],
	if set, activates the logical device.  Bits[7:1] are reserved and
	must return 0 on reads.  This is a read/write register. Before a
	logical device is activated, I/O range check must be disabled.
	***/

#define PNP_IO_RANGE_CHECK	0x31
#define PNP_IO_RANGE_CHECK_ENABLE	0x02
#define PNP_IO_RANGE_CHECK_READ_AS_55	0x01
	/***
	This register is used to perform a conflict check on the I/O port
	range programmed for use by a logical device.

	Bit[7:2]  Reserved and must return 0 on reads
	Bit[1]    Enable I/O Range check, if set then I/O Range Check
	is enabled. I/O range check is only valid when the logical
	device is inactive.

	Bit[0], if set, forces the logical device to respond to I/O reads
	of the logical device's assigned I/O range with a 0x55 when I/O
	range check is in operation.  If clear, the logical device drives
	0xAA.  This register is read/write.
	***/

/*** addr 0x32 - 0x37 Logical Device Control Reserved for future use ***/
/*** addr 0x38 - 0x3F Logical Device Control Vendor Define ***/

#define PNP_MEM_BASE_HIGH(i)	(0x40 + 8*(i))
#define PNP_MEM_BASE_LOW(i)	(0x41 + 8*(i))
#define PNP_MEM_CONTROL(i)	(0x42 * 8*(i))
#define PNP_MEM_CONTROL_16BIT	0x2
#define PNP_MEM_CONTROL_LIMIT	0x1
#define PNP_MEM_RANGE_HIGH(i)	(0x43 + 8*(i))
#define PNP_MEM_RANGE_LOW(i)	(0x44 + 8*(i))
	/***
	Four memory resource registers per range, four ranges.
	Fill with 0 if no ranges are enabled.

	Offset 0:	RW Memory base address bits[23:16]
	Offset 1:	RW Memory base address bits[15:8]
	Offset 2:	Memory control
	    Bit[1] specifies 8/16-bit control.  This bit is set to indicate
	    16-bit memory, and cleared to indicate 8-bit memory.
	    Bit[0], if cleared, indicates the next field can be used as a range
	    length for decode (implies range length and base alignment of memory
	    descriptor are equal).
	    Bit[0], if set, indicates the next field is the upper limit for
	    the address. -  - Bit[0] is read-only.
	Offset 3:	RW upper limit or range len, bits[23:16]
	Offset 4:	RW upper limit or range len, bits[15:8]
	Offset 5-Offset 7: filler, unused.
	***/

#define PNP_IO_BASE_HIGH(i)	(0x60 + 2*(i))
#define PNP_IO_BASE_LOW(i)	(0x61 + 2*(i))
	/***
	Eight ranges, two bytes per range.
	Offset 0:		I/O port base address bits[15:8]
	Offset 1:		I/O port base address bits[7:0]
	***/

#define PNP_IRQ_LEVEL(i)	(0x70 + 2*(i))
#define PNP_IRQ_TYPE(i)		(0x71 + 2*(i))
	/***
	Two entries, two bytes per entry.
	Offset 0:	RW interrupt level (1..15, 0=unused).
	Offset 1:	Bit[1]: level(1:hi, 0:low),
			Bit[0]: type (1:level, 0:edge)
		byte 1 can be readonly if 1 type of int is used.
	***/

#define PNP_DMA_CHANNEL(i)	(0x74 + 1*(i))
	/***
	Two entries, one byte per entry. Bits[2:0] select
	which DMA channel is in use for DMA 0.  Zero selects DMA channel
	0, seven selects DMA channel 7. DMA channel 4, the cascade channel
	is used to indicate no DMA channel is active.
	***/

/*** 32-bit memory accesses are at 0x76 ***/

/* Macros to parse Resource IDs */
#define PNP_RES_TYPE(a)		(a >> 7)
#define PNP_SRES_NUM(a)		(a >> 3)
#define PNP_SRES_LEN(a)		(a & 0x07)
#define PNP_LRES_NUM(a)		(a & 0x7f)

/* Small Resource Item names */
#define PNP_TAG_VERSION		0x1
#define PNP_TAG_LOGICAL_DEVICE	0x2
#define PNP_TAG_COMPAT_DEVICE	0x3
#define PNP_TAG_IRQ_FORMAT	0x4
#define PNP_TAG_DMA_FORMAT	0x5
#define PNP_TAG_START_DEPENDANT	0x6
#define PNP_TAG_END_DEPENDANT	0x7
#define PNP_TAG_IO_RANGE	0x8
#define PNP_TAG_IO_FIXED	0x9
#define PNP_TAG_RESERVED	0xa-0xd
#define PNP_TAG_VENDOR		0xe
#define PNP_TAG_END		0xf

/* Large Resource Item names */
#define PNP_TAG_MEMORY_RANGE	0x1
#define PNP_TAG_ID_ANSI		0x2
#define PNP_TAG_ID_UNICODE	0x3
#define PNP_TAG_LARGE_VENDOR	0x4
#define PNP_TAG_MEMORY32_RANGE	0x5
#define PNP_TAG_MEMORY32_FIXED	0x6
#define PNP_TAG_LARGE_RESERVED	0x7-0x7f

#endif /* !_ISA_PNPREG_H_ */
