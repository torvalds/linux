/*
 * $Id: ftl.h,v 1.6 2003/01/24 13:20:04 dwmw2 Exp $
 * 
 * Derived from (and probably identical to):
 * ftl.h 1.7 1999/10/25 20:23:17
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#ifndef _LINUX_FTL_H
#define _LINUX_FTL_H

typedef struct erase_unit_header_t {
    u_int8_t	LinkTargetTuple[5];
    u_int8_t	DataOrgTuple[10];
    u_int8_t	NumTransferUnits;
    u_int32_t	EraseCount;
    u_int16_t	LogicalEUN;
    u_int8_t	BlockSize;
    u_int8_t	EraseUnitSize;
    u_int16_t	FirstPhysicalEUN;
    u_int16_t	NumEraseUnits;
    u_int32_t	FormattedSize;
    u_int32_t	FirstVMAddress;
    u_int16_t	NumVMPages;
    u_int8_t	Flags;
    u_int8_t	Code;
    u_int32_t	SerialNumber;
    u_int32_t	AltEUHOffset;
    u_int32_t	BAMOffset;
    u_int8_t	Reserved[12];
    u_int8_t	EndTuple[2];
} erase_unit_header_t;

/* Flags in erase_unit_header_t */
#define HIDDEN_AREA		0x01
#define REVERSE_POLARITY	0x02
#define DOUBLE_BAI		0x04

/* Definitions for block allocation information */

#define BLOCK_FREE(b)		((b) == 0xffffffff)
#define BLOCK_DELETED(b)	(((b) == 0) || ((b) == 0xfffffffe))

#define BLOCK_TYPE(b)		((b) & 0x7f)
#define BLOCK_ADDRESS(b)	((b) & ~0x7f)
#define BLOCK_NUMBER(b)		((b) >> 9)
#define BLOCK_CONTROL		0x30
#define BLOCK_DATA		0x40
#define BLOCK_REPLACEMENT	0x60
#define BLOCK_BAD		0x70

#endif /* _LINUX_FTL_H */
