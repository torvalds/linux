/*	$OpenBSD: ediddevs_data.h,v 1.1 2009/10/08 20:35:44 matthieu Exp $	*/

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

const struct edid_vendor edid_vendors[] = {
	{ "AAC", EDID_VENDOR_AAC },
	{ "AOC", EDID_VENDOR_AOC },
	{ "APP", EDID_VENDOR_APP },
	{ "AST", EDID_VENDOR_AST },
	{ "CPL", EDID_VENDOR_CPL },
	{ "CPQ", EDID_VENDOR_CPQ },
	{ "CTX", EDID_VENDOR_CTX },
	{ "DEC", EDID_VENDOR_DEC },
	{ "DEL", EDID_VENDOR_DEL },
	{ "DPC", EDID_VENDOR_DPC },
	{ "DWE", EDID_VENDOR_DWE },
	{ "EIZ", EDID_VENDOR_EIZ },
	{ "ELS", EDID_VENDOR_ELS },
	{ "EPI", EDID_VENDOR_EPI },
	{ "FCM", EDID_VENDOR_FCM },
	{ "FUJ", EDID_VENDOR_FUJ },
	{ "GSM", EDID_VENDOR_GSM },
	{ "GWY", EDID_VENDOR_GWY },
	{ "HEI", EDID_VENDOR_HEI },
	{ "HIT", EDID_VENDOR_HIT },
	{ "HSL", EDID_VENDOR_HSL },
	{ "HTC", EDID_VENDOR_HTC },
	{ "HWP", EDID_VENDOR_HWP },
	{ "IBM", EDID_VENDOR_IBM },
	{ "ICL", EDID_VENDOR_ICL },
	{ "IVM", EDID_VENDOR_IVM },
	{ "KDS", EDID_VENDOR_KDS },
	{ "MEI", EDID_VENDOR_MEI },
	{ "MEL", EDID_VENDOR_MEL },
	{ "NAN", EDID_VENDOR_NAN },
	{ "NEC", EDID_VENDOR_NEC },
	{ "NOK", EDID_VENDOR_NOK },
	{ "PHL", EDID_VENDOR_PHL },
	{ "REL", EDID_VENDOR_REL },
	{ "SAM", EDID_VENDOR_SAM },
	{ "SGI", EDID_VENDOR_SGI },
	{ "SNY", EDID_VENDOR_SNY },
	{ "SRC", EDID_VENDOR_SRC },
	{ "SUN", EDID_VENDOR_SUN },
	{ "TAT", EDID_VENDOR_TAT },
	{ "TOS", EDID_VENDOR_TOS },
	{ "TSB", EDID_VENDOR_TSB },
	{ "VSC", EDID_VENDOR_VSC },
	{ "ZCM", EDID_VENDOR_ZCM },
};
const int edid_nvendors = 44;

const struct edid_product edid_products[] = {
	{
	    "DEL", EDID_PRODUCT_DEL_ULTRASCAN14XE_REVA,
	    "Ultrascan 14XE",
	},
	{
	    "DEL", EDID_PRODUCT_DEL_ULTRASCAN14XE_REVB,
	    "Ultrascan 14XE",
	},
	{
	    "VSC", EDID_PRODUCT_VSC_17GS,
	    "17GS",
	},
	{
	    "VSC", EDID_PRODUCT_VSC_17PS,
	    "17PS",
	},
};
const int edid_nproducts = 4;
