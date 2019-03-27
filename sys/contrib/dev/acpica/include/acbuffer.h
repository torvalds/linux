/******************************************************************************
 *
 * Name: acbuffer.h - Support for buffers returned by ACPI predefined names
 *
 *****************************************************************************/

/******************************************************************************
 *
 * 1. Copyright Notice
 *
 * Some or all of this work - Copyright (c) 1999 - 2019, Intel Corp.
 * All rights reserved.
 *
 * 2. License
 *
 * 2.1. This is your license from Intel Corp. under its intellectual property
 * rights. You may have additional license terms from the party that provided
 * you this software, covering your right to use that party's intellectual
 * property rights.
 *
 * 2.2. Intel grants, free of charge, to any person ("Licensee") obtaining a
 * copy of the source code appearing in this file ("Covered Code") an
 * irrevocable, perpetual, worldwide license under Intel's copyrights in the
 * base code distributed originally by Intel ("Original Intel Code") to copy,
 * make derivatives, distribute, use and display any portion of the Covered
 * Code in any form, with the right to sublicense such rights; and
 *
 * 2.3. Intel grants Licensee a non-exclusive and non-transferable patent
 * license (with the right to sublicense), under only those claims of Intel
 * patents that are infringed by the Original Intel Code, to make, use, sell,
 * offer to sell, and import the Covered Code and derivative works thereof
 * solely to the minimum extent necessary to exercise the above copyright
 * license, and in no event shall the patent license extend to any additions
 * to or modifications of the Original Intel Code. No other license or right
 * is granted directly or by implication, estoppel or otherwise;
 *
 * The above copyright and patent license is granted only if the following
 * conditions are met:
 *
 * 3. Conditions
 *
 * 3.1. Redistribution of Source with Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification with rights to further distribute source must include
 * the above Copyright Notice, the above License, this list of Conditions,
 * and the following Disclaimer and Export Compliance provision. In addition,
 * Licensee must cause all Covered Code to which Licensee contributes to
 * contain a file documenting the changes Licensee made to create that Covered
 * Code and the date of any change. Licensee must include in that file the
 * documentation of any changes made by any predecessor Licensee. Licensee
 * must include a prominent statement that the modification is derived,
 * directly or indirectly, from Original Intel Code.
 *
 * 3.2. Redistribution of Source with no Rights to Further Distribute Source.
 * Redistribution of source code of any substantial portion of the Covered
 * Code or modification without rights to further distribute source must
 * include the following Disclaimer and Export Compliance provision in the
 * documentation and/or other materials provided with distribution. In
 * addition, Licensee may not authorize further sublicense of source of any
 * portion of the Covered Code, and must include terms to the effect that the
 * license from Licensee to its licensee is limited to the intellectual
 * property embodied in the software Licensee provides to its licensee, and
 * not to intellectual property embodied in modifications its licensee may
 * make.
 *
 * 3.3. Redistribution of Executable. Redistribution in executable form of any
 * substantial portion of the Covered Code or modification must reproduce the
 * above Copyright Notice, and the following Disclaimer and Export Compliance
 * provision in the documentation and/or other materials provided with the
 * distribution.
 *
 * 3.4. Intel retains all right, title, and interest in and to the Original
 * Intel Code.
 *
 * 3.5. Neither the name Intel nor any other trademark owned or controlled by
 * Intel shall be used in advertising or otherwise to promote the sale, use or
 * other dealings in products derived from or relating to the Covered Code
 * without prior written authorization from Intel.
 *
 * 4. Disclaimer and Export Compliance
 *
 * 4.1. INTEL MAKES NO WARRANTY OF ANY KIND REGARDING ANY SOFTWARE PROVIDED
 * HERE. ANY SOFTWARE ORIGINATING FROM INTEL OR DERIVED FROM INTEL SOFTWARE
 * IS PROVIDED "AS IS," AND INTEL WILL NOT PROVIDE ANY SUPPORT, ASSISTANCE,
 * INSTALLATION, TRAINING OR OTHER SERVICES. INTEL WILL NOT PROVIDE ANY
 * UPDATES, ENHANCEMENTS OR EXTENSIONS. INTEL SPECIFICALLY DISCLAIMS ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGEMENT AND FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * 4.2. IN NO EVENT SHALL INTEL HAVE ANY LIABILITY TO LICENSEE, ITS LICENSEES
 * OR ANY OTHER THIRD PARTY, FOR ANY LOST PROFITS, LOST DATA, LOSS OF USE OR
 * COSTS OF PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES, OR FOR ANY INDIRECT,
 * SPECIAL OR CONSEQUENTIAL DAMAGES ARISING OUT OF THIS AGREEMENT, UNDER ANY
 * CAUSE OF ACTION OR THEORY OF LIABILITY, AND IRRESPECTIVE OF WHETHER INTEL
 * HAS ADVANCE NOTICE OF THE POSSIBILITY OF SUCH DAMAGES. THESE LIMITATIONS
 * SHALL APPLY NOTWITHSTANDING THE FAILURE OF THE ESSENTIAL PURPOSE OF ANY
 * LIMITED REMEDY.
 *
 * 4.3. Licensee shall not export, either directly or indirectly, any of this
 * software or system incorporating such software without first obtaining any
 * required license or other approval from the U. S. Department of Commerce or
 * any other agency or department of the United States Government. In the
 * event Licensee exports any such software from the United States or
 * re-exports any such software from a foreign destination, Licensee shall
 * ensure that the distribution and export/re-export of the software is in
 * compliance with all laws, regulations, orders, or other restrictions of the
 * U.S. Export Administration Regulations. Licensee agrees that neither it nor
 * any of its subsidiaries will export/re-export any technical data, process,
 * software, or service, directly or indirectly, to any country for which the
 * United States government or any agency thereof requires an export license,
 * other governmental approval, or letter of assurance, without first obtaining
 * such license, approval or letter.
 *
 *****************************************************************************
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * following license:
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Alternatively, you may choose to be licensed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 *****************************************************************************/

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

typedef struct acpi_fde_info
{
    UINT32              Floppy0;
    UINT32              Floppy1;
    UINT32              Floppy2;
    UINT32              Floppy3;
    UINT32              Tape;

} ACPI_FDE_INFO;

/*
 * _GRT return value
 * _SRT input value
 */
typedef struct acpi_grt_info
{
    UINT16              Year;
    UINT8               Month;
    UINT8               Day;
    UINT8               Hour;
    UINT8               Minute;
    UINT8               Second;
    UINT8               Valid;
    UINT16              Milliseconds;
    UINT16              Timezone;
    UINT8               Daylight;
    UINT8               Reserved[3];

} ACPI_GRT_INFO;

/* _GTM return value */

typedef struct acpi_gtm_info
{
    UINT32              PioSpeed0;
    UINT32              DmaSpeed0;
    UINT32              PioSpeed1;
    UINT32              DmaSpeed1;
    UINT32              Flags;

} ACPI_GTM_INFO;

/*
 * Formatted _PLD return value. The minimum size is a package containing
 * one buffer.
 * Revision 1: Buffer is 16 bytes (128 bits)
 * Revision 2: Buffer is 20 bytes (160 bits)
 *
 * Note: This structure is returned from the AcpiDecodePldBuffer
 * interface.
 */
typedef struct acpi_pld_info
{
    UINT8               Revision;
    UINT8               IgnoreColor;
    UINT8               Red;
    UINT8               Green;
    UINT8               Blue;
    UINT16              Width;
    UINT16              Height;
    UINT8               UserVisible;
    UINT8               Dock;
    UINT8               Lid;
    UINT8               Panel;
    UINT8               VerticalPosition;
    UINT8               HorizontalPosition;
    UINT8               Shape;
    UINT8               GroupOrientation;
    UINT8               GroupToken;
    UINT8               GroupPosition;
    UINT8               Bay;
    UINT8               Ejectable;
    UINT8               OspmEjectRequired;
    UINT8               CabinetNumber;
    UINT8               CardCageNumber;
    UINT8               Reference;
    UINT8               Rotation;
    UINT8               Order;
    UINT8               Reserved;
    UINT16              VerticalOffset;
    UINT16              HorizontalOffset;

} ACPI_PLD_INFO;


/*
 * Macros to:
 *     1) Convert a _PLD buffer to internal ACPI_PLD_INFO format - ACPI_PLD_GET*
 *        (Used by AcpiDecodePldBuffer)
 *     2) Construct a _PLD buffer - ACPI_PLD_SET*
 *        (Intended for BIOS use only)
 */
#define ACPI_PLD_REV1_BUFFER_SIZE               16 /* For Revision 1 of the buffer (From ACPI spec) */
#define ACPI_PLD_REV2_BUFFER_SIZE               20 /* For Revision 2 of the buffer (From ACPI spec) */
#define ACPI_PLD_BUFFER_SIZE                    20 /* For Revision 2 of the buffer (From ACPI spec) */

/* First 32-bit dword, bits 0:32 */

#define ACPI_PLD_GET_REVISION(dword)            ACPI_GET_BITS (dword, 0, ACPI_7BIT_MASK)
#define ACPI_PLD_SET_REVISION(dword,value)      ACPI_SET_BITS (dword, 0, ACPI_7BIT_MASK, value)     /* Offset 0, Len 7 */

#define ACPI_PLD_GET_IGNORE_COLOR(dword)        ACPI_GET_BITS (dword, 7, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_IGNORE_COLOR(dword,value)  ACPI_SET_BITS (dword, 7, ACPI_1BIT_MASK, value)     /* Offset 7, Len 1 */

#define ACPI_PLD_GET_RED(dword)                 ACPI_GET_BITS (dword, 8, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_RED(dword,value)           ACPI_SET_BITS (dword, 8, ACPI_8BIT_MASK, value)    /* Offset 8, Len 8 */

#define ACPI_PLD_GET_GREEN(dword)               ACPI_GET_BITS (dword, 16, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_GREEN(dword,value)         ACPI_SET_BITS (dword, 16, ACPI_8BIT_MASK, value)    /* Offset 16, Len 8 */

#define ACPI_PLD_GET_BLUE(dword)                ACPI_GET_BITS (dword, 24, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_BLUE(dword,value)          ACPI_SET_BITS (dword, 24, ACPI_8BIT_MASK, value)    /* Offset 24, Len 8 */

/* Second 32-bit dword, bits 33:63 */

#define ACPI_PLD_GET_WIDTH(dword)               ACPI_GET_BITS (dword, 0, ACPI_16BIT_MASK)
#define ACPI_PLD_SET_WIDTH(dword,value)         ACPI_SET_BITS (dword, 0, ACPI_16BIT_MASK, value)    /* Offset 32+0=32, Len 16 */

#define ACPI_PLD_GET_HEIGHT(dword)              ACPI_GET_BITS (dword, 16, ACPI_16BIT_MASK)
#define ACPI_PLD_SET_HEIGHT(dword,value)        ACPI_SET_BITS (dword, 16, ACPI_16BIT_MASK, value)   /* Offset 32+16=48, Len 16 */

/* Third 32-bit dword, bits 64:95 */

#define ACPI_PLD_GET_USER_VISIBLE(dword)        ACPI_GET_BITS (dword, 0, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_USER_VISIBLE(dword,value)  ACPI_SET_BITS (dword, 0, ACPI_1BIT_MASK, value)     /* Offset 64+0=64, Len 1 */

#define ACPI_PLD_GET_DOCK(dword)                ACPI_GET_BITS (dword, 1, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_DOCK(dword,value)          ACPI_SET_BITS (dword, 1, ACPI_1BIT_MASK, value)     /* Offset 64+1=65, Len 1 */

#define ACPI_PLD_GET_LID(dword)                 ACPI_GET_BITS (dword, 2, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_LID(dword,value)           ACPI_SET_BITS (dword, 2, ACPI_1BIT_MASK, value)     /* Offset 64+2=66, Len 1 */

#define ACPI_PLD_GET_PANEL(dword)               ACPI_GET_BITS (dword, 3, ACPI_3BIT_MASK)
#define ACPI_PLD_SET_PANEL(dword,value)         ACPI_SET_BITS (dword, 3, ACPI_3BIT_MASK, value)     /* Offset 64+3=67, Len 3 */

#define ACPI_PLD_GET_VERTICAL(dword)            ACPI_GET_BITS (dword, 6, ACPI_2BIT_MASK)
#define ACPI_PLD_SET_VERTICAL(dword,value)      ACPI_SET_BITS (dword, 6, ACPI_2BIT_MASK, value)     /* Offset 64+6=70, Len 2 */

#define ACPI_PLD_GET_HORIZONTAL(dword)          ACPI_GET_BITS (dword, 8, ACPI_2BIT_MASK)
#define ACPI_PLD_SET_HORIZONTAL(dword,value)    ACPI_SET_BITS (dword, 8, ACPI_2BIT_MASK, value)     /* Offset 64+8=72, Len 2 */

#define ACPI_PLD_GET_SHAPE(dword)               ACPI_GET_BITS (dword, 10, ACPI_4BIT_MASK)
#define ACPI_PLD_SET_SHAPE(dword,value)         ACPI_SET_BITS (dword, 10, ACPI_4BIT_MASK, value)    /* Offset 64+10=74, Len 4 */

#define ACPI_PLD_GET_ORIENTATION(dword)         ACPI_GET_BITS (dword, 14, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_ORIENTATION(dword,value)   ACPI_SET_BITS (dword, 14, ACPI_1BIT_MASK, value)    /* Offset 64+14=78, Len 1 */

#define ACPI_PLD_GET_TOKEN(dword)               ACPI_GET_BITS (dword, 15, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_TOKEN(dword,value)         ACPI_SET_BITS (dword, 15, ACPI_8BIT_MASK, value)    /* Offset 64+15=79, Len 8 */

#define ACPI_PLD_GET_POSITION(dword)            ACPI_GET_BITS (dword, 23, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_POSITION(dword,value)      ACPI_SET_BITS (dword, 23, ACPI_8BIT_MASK, value)    /* Offset 64+23=87, Len 8 */

#define ACPI_PLD_GET_BAY(dword)                 ACPI_GET_BITS (dword, 31, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_BAY(dword,value)           ACPI_SET_BITS (dword, 31, ACPI_1BIT_MASK, value)    /* Offset 64+31=95, Len 1 */

/* Fourth 32-bit dword, bits 96:127 */

#define ACPI_PLD_GET_EJECTABLE(dword)           ACPI_GET_BITS (dword, 0, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_EJECTABLE(dword,value)     ACPI_SET_BITS (dword, 0, ACPI_1BIT_MASK, value)     /* Offset 96+0=96, Len 1 */

#define ACPI_PLD_GET_OSPM_EJECT(dword)          ACPI_GET_BITS (dword, 1, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_OSPM_EJECT(dword,value)    ACPI_SET_BITS (dword, 1, ACPI_1BIT_MASK, value)     /* Offset 96+1=97, Len 1 */

#define ACPI_PLD_GET_CABINET(dword)             ACPI_GET_BITS (dword, 2, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_CABINET(dword,value)       ACPI_SET_BITS (dword, 2, ACPI_8BIT_MASK, value)     /* Offset 96+2=98, Len 8 */

#define ACPI_PLD_GET_CARD_CAGE(dword)           ACPI_GET_BITS (dword, 10, ACPI_8BIT_MASK)
#define ACPI_PLD_SET_CARD_CAGE(dword,value)     ACPI_SET_BITS (dword, 10, ACPI_8BIT_MASK, value)    /* Offset 96+10=106, Len 8 */

#define ACPI_PLD_GET_REFERENCE(dword)           ACPI_GET_BITS (dword, 18, ACPI_1BIT_MASK)
#define ACPI_PLD_SET_REFERENCE(dword,value)     ACPI_SET_BITS (dword, 18, ACPI_1BIT_MASK, value)    /* Offset 96+18=114, Len 1 */

#define ACPI_PLD_GET_ROTATION(dword)            ACPI_GET_BITS (dword, 19, ACPI_4BIT_MASK)
#define ACPI_PLD_SET_ROTATION(dword,value)      ACPI_SET_BITS (dword, 19, ACPI_4BIT_MASK, value)    /* Offset 96+19=115, Len 4 */

#define ACPI_PLD_GET_ORDER(dword)               ACPI_GET_BITS (dword, 23, ACPI_5BIT_MASK)
#define ACPI_PLD_SET_ORDER(dword,value)         ACPI_SET_BITS (dword, 23, ACPI_5BIT_MASK, value)    /* Offset 96+23=119, Len 5 */

/* Fifth 32-bit dword, bits 128:159 (Revision 2 of _PLD only) */

#define ACPI_PLD_GET_VERT_OFFSET(dword)         ACPI_GET_BITS (dword, 0, ACPI_16BIT_MASK)
#define ACPI_PLD_SET_VERT_OFFSET(dword,value)   ACPI_SET_BITS (dword, 0, ACPI_16BIT_MASK, value)    /* Offset 128+0=128, Len 16 */

#define ACPI_PLD_GET_HORIZ_OFFSET(dword)        ACPI_GET_BITS (dword, 16, ACPI_16BIT_MASK)
#define ACPI_PLD_SET_HORIZ_OFFSET(dword,value)  ACPI_SET_BITS (dword, 16, ACPI_16BIT_MASK, value)   /* Offset 128+16=144, Len 16 */


#endif /* ACBUFFER_H */
