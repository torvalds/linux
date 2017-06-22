/******************************************************************************
 *
 * Name: acbuffer.h - Support for buffers returned by ACPI predefined names
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2017, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef __ACBUFFER_H__
#define __ACBUFFER_H__

/*
 * Contains buffer structures for these predefined names:
 * _FDE, _GRT, _GTM, _PLD, _SRT
 */

/*
 * Note: C bitfields are not used for this reason:
 *
 * "Bitfields are great and easy to read, but unfortunately the C language
 * does not specify the layout of bitfields in memory, which means they are
 * essentially useless for dealing with packed data in on-disk formats or
 * binary wire protocols." (Or ACPI tables and buffers.) "If you ask me,
 * this decision was a design error in C. Ritchie could have picked an order
 * and stuck with it." Norman Ramsey.
 * See http://stackoverflow.com/a/1053662/41661
 */

/* _FDE return value */

struct acpi_fde_info {
	u32 floppy0;
	u32 floppy1;
	u32 floppy2;
	u32 floppy3;
	u32 tape;
};

/*
 * _GRT return value
 * _SRT input value
 */
struct acpi_grt_info {
	u16 year;
	u8 month;
	u8 day;
	u8 hour;
	u8 minute;
	u8 second;
	u8 valid;
	u16 milliseconds;
	u16 timezone;
	u8 daylight;
	u8 reserved[3];
};

/* _GTM return value */

struct acpi_gtm_info {
	u32 pio_speed0;
	u32 dma_speed0;
	u32 pio_speed1;
	u32 dma_speed1;
	u32 flags;
};

/*
 * Formatted _PLD return value. The minimum size is a package containing
 * one buffer.
 * Revision 1: Buffer is 16 bytes (128 bits)
 * Revision 2: Buffer is 20 bytes (160 bits)
 *
 * Note: This structure is returned from the acpi_decode_pld_buffer
 * interface.
 */
struct acpi_pld_info {
	u8 revision;
	u8 ignore_color;
	u8 red;
	u8 green;
	u8 blue;
	u16 width;
	u16 height;
	u8 user_visible;
	u8 dock;
	u8 lid;
	u8 panel;
	u8 vertical_position;
	u8 horizontal_position;
	u8 shape;
	u8 group_orientation;
	u8 group_token;
	u8 group_position;
	u8 bay;
	u8 ejectable;
	u8 ospm_eject_required;
	u8 cabinet_number;
	u8 card_cage_number;
	u8 reference;
	u8 rotation;
	u8 order;
	u8 reserved;
	u16 vertical_offset;
	u16 horizontal_offset;
};

/*
 * Macros to:
 *     1) Convert a _PLD buffer to internal struct acpi_pld_info format - ACPI_PLD_GET*
 *        (Used by acpi_decode_pld_buffer)
 *     2) Construct a _PLD buffer - ACPI_PLD_SET*
 *        (Intended for BIOS use only)
 */
#define ACPI_PLD_REV1_BUFFER_SIZE               16	/* For Revision 1 of the buffer (From ACPI spec) */
#define ACPI_PLD_REV2_BUFFER_SIZE               20	/* For Revision 2 of the buffer (From ACPI spec) */
#define ACPI_PLD_BUFFER_SIZE                    20	/* For Revision 2 of the buffer (From ACPI spec) */

/* First 32-bit dword, bits 0:32 */

#define ACPI_PLD_GET_REVISION(dword)            ACPI_GET_BITS (dword, 0, ACPI_7BIT_MASK)
#define ACPI_PLD_SET_REVISION(dword,value)      ACPI_SET_BITS (dword, 0, ACPI_7BIT_MASK, value)	/* Offset 0, Len 7 */

#define ACPI_PLD_GET_IGNORE_COLOR(dword)        ACPI_GET_BITS (dword, 7, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_IGNORE_COLOR(dword,value)  ACPI_SET_BITS (dword, 7, ACPI_1BIT_MASK, value)	/* Offset 7, Len 1 */

#define ACPI_PLD_GET_RED(dword)                 ACPI_GET_BITS (dword, 8, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_RED(dword,value)           ACPI_SET_BITS (dword, 8, ACPI_8BIT_MASK, value)	/* Offset 8, Len 8 */

#define ACPI_PLD_GET_GREEN(dword)               ACPI_GET_BITS (dword, 16, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_GREEN(dword,value)         ACPI_SET_BITS (dword, 16, ACPI_8BIT_MASK, value)	/* Offset 16, Len 8 */

#define ACPI_PLD_GET_BLUE(dword)                ACPI_GET_BITS (dword, 24, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_BLUE(dword,value)          ACPI_SET_BITS (dword, 24, ACPI_8BIT_MASK, value)	/* Offset 24, Len 8 */

/* Second 32-bit dword, bits 33:63 */

#define ACPI_PLD_GET_WIDTH(dword)               ACPI_GET_BITS (dword, 0, ACPI_16BIT_MASK)
#define ACPI_PLD_SET_WIDTH(dword,value)         ACPI_SET_BITS (dword, 0, ACPI_16BIT_MASK, value)	/* Offset 32+0=32, Len 16 */

#define ACPI_PLD_GET_HEIGHT(dword)              ACPI_GET_BITS (dword, 16, ACPI_16BIT_MASK)
#define ACPI_PLD_SET_HEIGHT(dword,value)        ACPI_SET_BITS (dword, 16, ACPI_16BIT_MASK, value)	/* Offset 32+16=48, Len 16 */

/* Third 32-bit dword, bits 64:95 */

#define ACPI_PLD_GET_USER_VISIBLE(dword)        ACPI_GET_BITS (dword, 0, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_USER_VISIBLE(dword,value)  ACPI_SET_BITS (dword, 0, ACPI_1BIT_MASK, value)	/* Offset 64+0=64, Len 1 */

#define ACPI_PLD_GET_DOCK(dword)                ACPI_GET_BITS (dword, 1, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_DOCK(dword,value)          ACPI_SET_BITS (dword, 1, ACPI_1BIT_MASK, value)	/* Offset 64+1=65, Len 1 */

#define ACPI_PLD_GET_LID(dword)                 ACPI_GET_BITS (dword, 2, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_LID(dword,value)           ACPI_SET_BITS (dword, 2, ACPI_1BIT_MASK, value)	/* Offset 64+2=66, Len 1 */

#define ACPI_PLD_GET_PANEL(dword)               ACPI_GET_BITS (dword, 3, ACPI_3BIT_MASK)
#define ACPI_PLD_SET_PANEL(dword,value)         ACPI_SET_BITS (dword, 3, ACPI_3BIT_MASK, value)	/* Offset 64+3=67, Len 3 */

#define ACPI_PLD_GET_VERTICAL(dword)            ACPI_GET_BITS (dword, 6, ACPI_2BIT_MASK)
#define ACPI_PLD_SET_VERTICAL(dword,value)      ACPI_SET_BITS (dword, 6, ACPI_2BIT_MASK, value)	/* Offset 64+6=70, Len 2 */

#define ACPI_PLD_GET_HORIZONTAL(dword)          ACPI_GET_BITS (dword, 8, ACPI_2BIT_MASK)
#define ACPI_PLD_SET_HORIZONTAL(dword,value)    ACPI_SET_BITS (dword, 8, ACPI_2BIT_MASK, value)	/* Offset 64+8=72, Len 2 */

#define ACPI_PLD_GET_SHAPE(dword)               ACPI_GET_BITS (dword, 10, ACPI_4BIT_MASK)
#define ACPI_PLD_SET_SHAPE(dword,value)         ACPI_SET_BITS (dword, 10, ACPI_4BIT_MASK, value)	/* Offset 64+10=74, Len 4 */

#define ACPI_PLD_GET_ORIENTATION(dword)         ACPI_GET_BITS (dword, 14, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_ORIENTATION(dword,value)   ACPI_SET_BITS (dword, 14, ACPI_1BIT_MASK, value)	/* Offset 64+14=78, Len 1 */

#define ACPI_PLD_GET_TOKEN(dword)               ACPI_GET_BITS (dword, 15, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_TOKEN(dword,value)         ACPI_SET_BITS (dword, 15, ACPI_8BIT_MASK, value)	/* Offset 64+15=79, Len 8 */

#define ACPI_PLD_GET_POSITION(dword)            ACPI_GET_BITS (dword, 23, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_POSITION(dword,value)      ACPI_SET_BITS (dword, 23, ACPI_8BIT_MASK, value)	/* Offset 64+23=87, Len 8 */

#define ACPI_PLD_GET_BAY(dword)                 ACPI_GET_BITS (dword, 31, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_BAY(dword,value)           ACPI_SET_BITS (dword, 31, ACPI_1BIT_MASK, value)	/* Offset 64+31=95, Len 1 */

/* Fourth 32-bit dword, bits 96:127 */

#define ACPI_PLD_GET_EJECTABLE(dword)           ACPI_GET_BITS (dword, 0, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_EJECTABLE(dword,value)     ACPI_SET_BITS (dword, 0, ACPI_1BIT_MASK, value)	/* Offset 96+0=96, Len 1 */

#define ACPI_PLD_GET_OSPM_EJECT(dword)          ACPI_GET_BITS (dword, 1, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_OSPM_EJECT(dword,value)    ACPI_SET_BITS (dword, 1, ACPI_1BIT_MASK, value)	/* Offset 96+1=97, Len 1 */

#define ACPI_PLD_GET_CABINET(dword)             ACPI_GET_BITS (dword, 2, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_CABINET(dword,value)       ACPI_SET_BITS (dword, 2, ACPI_8BIT_MASK, value)	/* Offset 96+2=98, Len 8 */

#define ACPI_PLD_GET_CARD_CAGE(dword)           ACPI_GET_BITS (dword, 10, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_CARD_CAGE(dword,value)     ACPI_SET_BITS (dword, 10, ACPI_8BIT_MASK, value)	/* Offset 96+10=106, Len 8 */

#define ACPI_PLD_GET_REFERENCE(dword)           ACPI_GET_BITS (dword, 18, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_REFERENCE(dword,value)     ACPI_SET_BITS (dword, 18, ACPI_1BIT_MASK, value)	/* Offset 96+18=114, Len 1 */

#define ACPI_PLD_GET_ROTATION(dword)            ACPI_GET_BITS (dword, 19, ACPI_4BIT_MASK)
#define ACPI_PLD_SET_ROTATION(dword,value)      ACPI_SET_BITS (dword, 19, ACPI_4BIT_MASK, value)	/* Offset 96+19=115, Len 4 */

#define ACPI_PLD_GET_ORDER(dword)               ACPI_GET_BITS (dword, 23, ACPI_5BIT_MASK)
#define ACPI_PLD_SET_ORDER(dword,value)         ACPI_SET_BITS (dword, 23, ACPI_5BIT_MASK, value)	/* Offset 96+23=119, Len 5 */

/* Fifth 32-bit dword, bits 128:159 (Revision 2 of _PLD only) */

#define ACPI_PLD_GET_VERT_OFFSET(dword)         ACPI_GET_BITS (dword, 0, ACPI_16BIT_MASK)
#define ACPI_PLD_SET_VERT_OFFSET(dword,value)   ACPI_SET_BITS (dword, 0, ACPI_16BIT_MASK, value)	/* Offset 128+0=128, Len 16 */

#define ACPI_PLD_GET_HORIZ_OFFSET(dword)        ACPI_GET_BITS (dword, 16, ACPI_16BIT_MASK)
#define ACPI_PLD_SET_HORIZ_OFFSET(dword,value)  ACPI_SET_BITS (dword, 16, ACPI_16BIT_MASK, value)	/* Offset 128+16=144, Len 16 */

#endif				/* ACBUFFER_H */
