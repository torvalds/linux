/*	$OpenBSD: ediddevs.h,v 1.1 2009/10/08 20:35:44 matthieu Exp $	*/

/*
 * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.
 *
 * generated from:
 *	OpenBSD
 */

/*-
 * Copyright (c) 2006 Itronix Inc.
 * All rights reserved.
 *
 * Written by Garrett D'Amore for Itronix Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Itronix Inc. may not be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ITRONIX INC. ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL ITRONIX INC. BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */ 
#define	EDID_VENDOR_AAC	"AcerView"
#define	EDID_VENDOR_AOC	"AOC"
#define	EDID_VENDOR_APP	"Apple Computer"
#define	EDID_VENDOR_AST	"AST Research"
#define	EDID_VENDOR_CPL	"Compal"
#define	EDID_VENDOR_CPQ	"Compaq"
#define	EDID_VENDOR_CTX	"CTX"
#define	EDID_VENDOR_DEC	"DEC"
#define	EDID_VENDOR_DEL	"Dell"
#define	EDID_VENDOR_DPC	"Delta"
#define	EDID_VENDOR_DWE	"Daewoo"
#define	EDID_VENDOR_EIZ	"EIZO"
#define	EDID_VENDOR_ELS	"ELSA"
#define	EDID_VENDOR_EPI	"Envision"
#define	EDID_VENDOR_FCM	"Funai"
#define	EDID_VENDOR_FUJ	"Fujitsu"
#define	EDID_VENDOR_GSM	"LG Electronics"
#define	EDID_VENDOR_GWY	"Gateway 2000"
#define	EDID_VENDOR_HEI	"Hyundai"
#define	EDID_VENDOR_HIT	"Hitachi"
#define	EDID_VENDOR_HSL	"Hansol"
#define	EDID_VENDOR_HTC	"Hitachi/Nissei"
#define	EDID_VENDOR_HWP	"HP"
#define	EDID_VENDOR_IBM	"IBM"
#define	EDID_VENDOR_ICL	"Fujitsu ICL"
#define	EDID_VENDOR_IVM	"Iiyama"
#define	EDID_VENDOR_KDS	"Korea Data Systems"
#define	EDID_VENDOR_MEI	"Panasonic"
#define	EDID_VENDOR_MEL	"Mitsubishi Electronics"
#define	EDID_VENDOR_NAN	"Nanao"
#define	EDID_VENDOR_NEC	"NEC"
#define	EDID_VENDOR_NOK	"Nokia Data"
#define	EDID_VENDOR_PHL	"Philips"
#define	EDID_VENDOR_REL	"Relisys"
#define	EDID_VENDOR_SAM	"Samsung"
#define	EDID_VENDOR_SGI	"SGI"
#define	EDID_VENDOR_SNY	"Sony"
#define	EDID_VENDOR_SRC	"Shamrock"
#define	EDID_VENDOR_SUN	"Sun Microsystems"
#define	EDID_VENDOR_TAT	"Tatung"
#define	EDID_VENDOR_TOS	"Toshiba"
#define	EDID_VENDOR_TSB	"Toshiba"
#define	EDID_VENDOR_VSC	"ViewSonic"
#define	EDID_VENDOR_ZCM	"Zenith"

/*
 * List of known products, grouped and sorted by vendor.
 *
 * EDID version 1.3 requires that monitors expose the monitor name with
 * the ASCII descriptor type 0xFC, so for monitors using that block, this
 * information is redundant, and there is not point in listing them here,
 * unless it is desired to have a symbolic macro to detect the monitor in
 * special handling code or somesuch.
 */

/* Dell  - this exists for now as a sample.  I don't have one of these.  */
#define	EDID_PRODUCT_DEL_ULTRASCAN14XE_REVA	0x139A		/* Ultrascan 14XE */
#define	EDID_PRODUCT_DEL_ULTRASCAN14XE_REVB	0x139B		/* Ultrascan 14XE */

/* ViewSonic */
#define	EDID_PRODUCT_VSC_17GS	0x0c00		/* 17GS */
#define	EDID_PRODUCT_VSC_17PS	0x0c0f		/* 17PS */
