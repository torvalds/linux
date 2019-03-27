/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000,2001 Jonathan Chen.
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
 * $FreeBSD$
 */

/*
 * Register definitions for the Cardbus Bus
 */

/* EXROM offsets for reading CIS */
#define	CARDBUS_EXROM_SIGNATURE	0x00
#define	CARDBUS_EXROM_DATA_PTR	0x18

#define	CARDBUS_EXROM_DATA_SIGNATURE	0x00 /* Signature ("PCIR") */
#define	CARDBUS_EXROM_DATA_VENDOR_ID	0x04 /* Vendor Identification */
#define	CARDBUS_EXROM_DATA_DEVICE_ID	0x06 /* Device Identification */
#define	CARDBUS_EXROM_DATA_LENGTH	0x0a /* PCI Data Structure Length */
#define	CARDBUS_EXROM_DATA_REV		0x0c /* PCI Data Structure Revision */
#define	CARDBUS_EXROM_DATA_CLASS_CODE	0x0d /* Class Code */
#define	CARDBUS_EXROM_DATA_IMAGE_LENGTH	0x10 /* Image Length */
#define	CARDBUS_EXROM_DATA_DATA_REV	0x12 /* Revision Level of Code/Data */
#define	CARDBUS_EXROM_DATA_CODE_TYPE	0x14 /* Code Type */
#define	CARDBUS_EXROM_DATA_INDICATOR	0x15 /* Indicator */
