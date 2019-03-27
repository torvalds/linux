/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Stephane E. Potvin <sepotvin@videotron.ca>
 * Copyright (c) 2006 Ariff Abdullah <ariff@FreeBSD.org>
 * Copyright (c) 2008-2012 Alexander Motin <mav@FreeBSD.org>
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
 */

/*
 * Intel High Definition Audio (CODEC) driver for FreeBSD.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

#include <sys/ctype.h>

#include <dev/sound/pci/hda/hda_reg.h>
#include <dev/sound/pci/hda/hdac.h>

SND_DECLARE_FILE("$FreeBSD$");

struct hdacc_fg {
	device_t	dev;
	nid_t		nid;
	uint8_t		type;
	uint32_t	subsystem_id;
};

struct hdacc_softc {
	device_t	dev;
	struct mtx	*lock;
	nid_t		cad;
	device_t	streams[2][16];
	device_t	tags[64];
	int		fgcnt;
	struct hdacc_fg	*fgs;
};

#define hdacc_lock(codec)	snd_mtxlock((codec)->lock)
#define hdacc_unlock(codec)	snd_mtxunlock((codec)->lock)
#define hdacc_lockassert(codec)	snd_mtxassert((codec)->lock)
#define hdacc_lockowned(codec)	mtx_owned((codec)->lock)

MALLOC_DEFINE(M_HDACC, "hdacc", "HDA CODEC");

/* CODECs */
static const struct {
	uint32_t id;
	uint16_t revid;
	const char *name;
} hdacc_codecs[] = {
	{ HDA_CODEC_CS4206, 0,		"Cirrus Logic CS4206" },
	{ HDA_CODEC_CS4207, 0,		"Cirrus Logic CS4207" },
	{ HDA_CODEC_CS4210, 0,		"Cirrus Logic CS4210" },
	{ HDA_CODEC_ALC221, 0,		"Realtek ALC221" },
	{ HDA_CODEC_ALC225, 0,		"Realtek ALC225" },
	{ HDA_CODEC_ALC231, 0,		"Realtek ALC231" },
	{ HDA_CODEC_ALC233, 0,		"Realtek ALC233" },
	{ HDA_CODEC_ALC234, 0,		"Realtek ALC234" },
	{ HDA_CODEC_ALC235, 0,		"Realtek ALC235" },
	{ HDA_CODEC_ALC255, 0,		"Realtek ALC255" },
	{ HDA_CODEC_ALC256, 0,		"Realtek ALC256" },
	{ HDA_CODEC_ALC260, 0,		"Realtek ALC260" },
	{ HDA_CODEC_ALC262, 0,		"Realtek ALC262" },
	{ HDA_CODEC_ALC267, 0,		"Realtek ALC267" },
	{ HDA_CODEC_ALC268, 0,		"Realtek ALC268" },
	{ HDA_CODEC_ALC269, 0,		"Realtek ALC269" },
	{ HDA_CODEC_ALC270, 0,		"Realtek ALC270" },
	{ HDA_CODEC_ALC272, 0,		"Realtek ALC272" },
	{ HDA_CODEC_ALC273, 0,		"Realtek ALC273" },
	{ HDA_CODEC_ALC274, 0,		"Realtek ALC274" },
	{ HDA_CODEC_ALC275, 0,		"Realtek ALC275" },
	{ HDA_CODEC_ALC276, 0,		"Realtek ALC276" },
	{ HDA_CODEC_ALC292, 0,		"Realtek ALC292" },
	{ HDA_CODEC_ALC295, 0,		"Realtek ALC295" },
	{ HDA_CODEC_ALC280, 0,		"Realtek ALC280" },
	{ HDA_CODEC_ALC282, 0,		"Realtek ALC282" },
	{ HDA_CODEC_ALC283, 0,		"Realtek ALC283" },
	{ HDA_CODEC_ALC284, 0,		"Realtek ALC284" },
	{ HDA_CODEC_ALC285, 0,		"Realtek ALC285" },
	{ HDA_CODEC_ALC286, 0,		"Realtek ALC286" },
	{ HDA_CODEC_ALC288, 0,		"Realtek ALC288" },
	{ HDA_CODEC_ALC290, 0,		"Realtek ALC290" },
	{ HDA_CODEC_ALC292, 0,		"Realtek ALC292" },
	{ HDA_CODEC_ALC293, 0,		"Realtek ALC293" },
	{ HDA_CODEC_ALC294, 0,		"Realtek ALC294" },
	{ HDA_CODEC_ALC295, 0,		"Realtek ALC295" },
	{ HDA_CODEC_ALC298, 0,		"Realtek ALC298" },
	{ HDA_CODEC_ALC299, 0,		"Realtek ALC299" },
	{ HDA_CODEC_ALC660, 0,		"Realtek ALC660-VD" },
	{ HDA_CODEC_ALC662, 0x0002,	"Realtek ALC662 rev2" },
	{ HDA_CODEC_ALC662, 0,		"Realtek ALC662" },
	{ HDA_CODEC_ALC663, 0,		"Realtek ALC663" },
	{ HDA_CODEC_ALC665, 0,		"Realtek ALC665" },
	{ HDA_CODEC_ALC670, 0,		"Realtek ALC670" },
	{ HDA_CODEC_ALC671, 0,		"Realtek ALC671" },
	{ HDA_CODEC_ALC680, 0,		"Realtek ALC680" },
	{ HDA_CODEC_ALC700, 0,		"Realtek ALC700" },
	{ HDA_CODEC_ALC701, 0,		"Realtek ALC701" },
	{ HDA_CODEC_ALC703, 0,		"Realtek ALC703" },
	{ HDA_CODEC_ALC861, 0x0340,	"Realtek ALC660" },
	{ HDA_CODEC_ALC861, 0,		"Realtek ALC861" },
	{ HDA_CODEC_ALC861VD, 0,	"Realtek ALC861-VD" },
	{ HDA_CODEC_ALC880, 0,		"Realtek ALC880" },
	{ HDA_CODEC_ALC882, 0,		"Realtek ALC882" },
	{ HDA_CODEC_ALC883, 0,		"Realtek ALC883" },
	{ HDA_CODEC_ALC885, 0x0101,	"Realtek ALC889A" },
	{ HDA_CODEC_ALC885, 0x0103,	"Realtek ALC889A" },
	{ HDA_CODEC_ALC885, 0,		"Realtek ALC885" },
	{ HDA_CODEC_ALC887, 0,		"Realtek ALC887" },
	{ HDA_CODEC_ALC888, 0x0101,	"Realtek ALC1200" },
	{ HDA_CODEC_ALC888, 0,		"Realtek ALC888" },
	{ HDA_CODEC_ALC889, 0,		"Realtek ALC889" },
	{ HDA_CODEC_ALC892, 0,		"Realtek ALC892" },
	{ HDA_CODEC_ALC899, 0,		"Realtek ALC899" },
	{ HDA_CODEC_ALC1150, 0,		"Realtek ALC1150" },
	{ HDA_CODEC_ALC1220, 0,		"Realtek ALC1220" },
	{ HDA_CODEC_AD1882, 0,		"Analog Devices AD1882" },
	{ HDA_CODEC_AD1882A, 0,		"Analog Devices AD1882A" },
	{ HDA_CODEC_AD1883, 0,		"Analog Devices AD1883" },
	{ HDA_CODEC_AD1884, 0,		"Analog Devices AD1884" },
	{ HDA_CODEC_AD1884A, 0,		"Analog Devices AD1884A" },
	{ HDA_CODEC_AD1981HD, 0,	"Analog Devices AD1981HD" },
	{ HDA_CODEC_AD1983, 0,		"Analog Devices AD1983" },
	{ HDA_CODEC_AD1984, 0,		"Analog Devices AD1984" },
	{ HDA_CODEC_AD1984A, 0,		"Analog Devices AD1984A" },
	{ HDA_CODEC_AD1984B, 0,		"Analog Devices AD1984B" },
	{ HDA_CODEC_AD1986A, 0,		"Analog Devices AD1986A" },
	{ HDA_CODEC_AD1987, 0,		"Analog Devices AD1987" },
	{ HDA_CODEC_AD1988, 0,		"Analog Devices AD1988A" },
	{ HDA_CODEC_AD1988B, 0,		"Analog Devices AD1988B" },
	{ HDA_CODEC_AD1989A, 0,		"Analog Devices AD1989A" },
	{ HDA_CODEC_AD1989B, 0,		"Analog Devices AD1989B" },
	{ HDA_CODEC_CA0110, 0,		"Creative CA0110-IBG" },
	{ HDA_CODEC_CA0110_2, 0,	"Creative CA0110-IBG" },
	{ HDA_CODEC_CA0132, 0,		"Creative CA0132" },
	{ HDA_CODEC_SB0880, 0,		"Creative SB0880 X-Fi" },
	{ HDA_CODEC_CMI9880, 0,		"CMedia CMI9880" },
	{ HDA_CODEC_CMI98802, 0,	"CMedia CMI9880" },
	{ HDA_CODEC_CXD9872RDK, 0,	"Sigmatel CXD9872RD/K" },
	{ HDA_CODEC_CXD9872AKD, 0,	"Sigmatel CXD9872AKD" },
	{ HDA_CODEC_STAC9200D, 0,	"Sigmatel STAC9200D" },
	{ HDA_CODEC_STAC9204X, 0,	"Sigmatel STAC9204X" },
	{ HDA_CODEC_STAC9204D, 0,	"Sigmatel STAC9204D" },
	{ HDA_CODEC_STAC9205X, 0,	"Sigmatel STAC9205X" },
	{ HDA_CODEC_STAC9205D, 0,	"Sigmatel STAC9205D" },
	{ HDA_CODEC_STAC9220, 0,	"Sigmatel STAC9220" },
	{ HDA_CODEC_STAC9220_A1, 0,	"Sigmatel STAC9220_A1" },
	{ HDA_CODEC_STAC9220_A2, 0,	"Sigmatel STAC9220_A2" },
	{ HDA_CODEC_STAC9221, 0,	"Sigmatel STAC9221" },
	{ HDA_CODEC_STAC9221_A2, 0,	"Sigmatel STAC9221_A2" },
	{ HDA_CODEC_STAC9221D, 0,	"Sigmatel STAC9221D" },
	{ HDA_CODEC_STAC922XD, 0,	"Sigmatel STAC9220D/9223D" },
	{ HDA_CODEC_STAC9227X, 0,	"Sigmatel STAC9227X" },
	{ HDA_CODEC_STAC9227D, 0,	"Sigmatel STAC9227D" },
	{ HDA_CODEC_STAC9228X, 0,	"Sigmatel STAC9228X" },
	{ HDA_CODEC_STAC9228D, 0,	"Sigmatel STAC9228D" },
	{ HDA_CODEC_STAC9229X, 0,	"Sigmatel STAC9229X" },
	{ HDA_CODEC_STAC9229D, 0,	"Sigmatel STAC9229D" },
	{ HDA_CODEC_STAC9230X, 0,	"Sigmatel STAC9230X" },
	{ HDA_CODEC_STAC9230D, 0,	"Sigmatel STAC9230D" },
	{ HDA_CODEC_STAC9250, 0, 	"Sigmatel STAC9250" },
	{ HDA_CODEC_STAC9251, 0, 	"Sigmatel STAC9251" },
	{ HDA_CODEC_STAC9255, 0, 	"Sigmatel STAC9255" },
	{ HDA_CODEC_STAC9255D, 0, 	"Sigmatel STAC9255D" },
	{ HDA_CODEC_STAC9254, 0, 	"Sigmatel STAC9254" },
	{ HDA_CODEC_STAC9254D, 0, 	"Sigmatel STAC9254D" },
	{ HDA_CODEC_STAC9271X, 0,	"Sigmatel STAC9271X" },
	{ HDA_CODEC_STAC9271D, 0,	"Sigmatel STAC9271D" },
	{ HDA_CODEC_STAC9272X, 0,	"Sigmatel STAC9272X" },
	{ HDA_CODEC_STAC9272D, 0,	"Sigmatel STAC9272D" },
	{ HDA_CODEC_STAC9273X, 0,	"Sigmatel STAC9273X" },
	{ HDA_CODEC_STAC9273D, 0,	"Sigmatel STAC9273D" },
	{ HDA_CODEC_STAC9274, 0, 	"Sigmatel STAC9274" },
	{ HDA_CODEC_STAC9274D, 0,	"Sigmatel STAC9274D" },
	{ HDA_CODEC_STAC9274X5NH, 0,	"Sigmatel STAC9274X5NH" },
	{ HDA_CODEC_STAC9274D5NH, 0,	"Sigmatel STAC9274D5NH" },
	{ HDA_CODEC_STAC9872AK, 0,	"Sigmatel STAC9872AK" },
	{ HDA_CODEC_IDT92HD005, 0,	"IDT 92HD005" },
	{ HDA_CODEC_IDT92HD005D, 0,	"IDT 92HD005D" },
	{ HDA_CODEC_IDT92HD206X, 0,	"IDT 92HD206X" },
	{ HDA_CODEC_IDT92HD206D, 0,	"IDT 92HD206D" },
	{ HDA_CODEC_IDT92HD66B1X5, 0,	"IDT 92HD66B1X5" },
	{ HDA_CODEC_IDT92HD66B2X5, 0,	"IDT 92HD66B2X5" },
	{ HDA_CODEC_IDT92HD66B3X5, 0,	"IDT 92HD66B3X5" },
	{ HDA_CODEC_IDT92HD66C1X5, 0,	"IDT 92HD66C1X5" },
	{ HDA_CODEC_IDT92HD66C2X5, 0,	"IDT 92HD66C2X5" },
	{ HDA_CODEC_IDT92HD66C3X5, 0,	"IDT 92HD66C3X5" },
	{ HDA_CODEC_IDT92HD66B1X3, 0,	"IDT 92HD66B1X3" },
	{ HDA_CODEC_IDT92HD66B2X3, 0,	"IDT 92HD66B2X3" },
	{ HDA_CODEC_IDT92HD66B3X3, 0,	"IDT 92HD66B3X3" },
	{ HDA_CODEC_IDT92HD66C1X3, 0,	"IDT 92HD66C1X3" },
	{ HDA_CODEC_IDT92HD66C2X3, 0,	"IDT 92HD66C2X3" },
	{ HDA_CODEC_IDT92HD66C3_65, 0,	"IDT 92HD66C3_65" },
	{ HDA_CODEC_IDT92HD700X, 0,	"IDT 92HD700X" },
	{ HDA_CODEC_IDT92HD700D, 0,	"IDT 92HD700D" },
	{ HDA_CODEC_IDT92HD71B5, 0,	"IDT 92HD71B5" },
	{ HDA_CODEC_IDT92HD71B5_2, 0,	"IDT 92HD71B5" },
	{ HDA_CODEC_IDT92HD71B6, 0,	"IDT 92HD71B6" },
	{ HDA_CODEC_IDT92HD71B6_2, 0,	"IDT 92HD71B6" },
	{ HDA_CODEC_IDT92HD71B7, 0,	"IDT 92HD71B7" },
	{ HDA_CODEC_IDT92HD71B7_2, 0,	"IDT 92HD71B7" },
	{ HDA_CODEC_IDT92HD71B8, 0,	"IDT 92HD71B8" },
	{ HDA_CODEC_IDT92HD71B8_2, 0,	"IDT 92HD71B8" },
	{ HDA_CODEC_IDT92HD73C1, 0,	"IDT 92HD73C1" },
	{ HDA_CODEC_IDT92HD73D1, 0,	"IDT 92HD73D1" },
	{ HDA_CODEC_IDT92HD73E1, 0,	"IDT 92HD73E1" },
	{ HDA_CODEC_IDT92HD75B3, 0,	"IDT 92HD75B3" },
	{ HDA_CODEC_IDT92HD75BX, 0,	"IDT 92HD75BX" },
	{ HDA_CODEC_IDT92HD81B1C, 0,	"IDT 92HD81B1C" },
	{ HDA_CODEC_IDT92HD81B1X, 0,	"IDT 92HD81B1X" },
	{ HDA_CODEC_IDT92HD83C1C, 0,	"IDT 92HD83C1C" },
	{ HDA_CODEC_IDT92HD83C1X, 0,	"IDT 92HD83C1X" },
	{ HDA_CODEC_IDT92HD87B1_3, 0,	"IDT 92HD87B1/3" },
	{ HDA_CODEC_IDT92HD87B2_4, 0,	"IDT 92HD87B2/4" },
	{ HDA_CODEC_IDT92HD89C3, 0,	"IDT 92HD89C3" },
	{ HDA_CODEC_IDT92HD89C2, 0,	"IDT 92HD89C2" },
	{ HDA_CODEC_IDT92HD89C1, 0,	"IDT 92HD89C1" },
	{ HDA_CODEC_IDT92HD89B3, 0,	"IDT 92HD89B3" },
	{ HDA_CODEC_IDT92HD89B2, 0,	"IDT 92HD89B2" },
	{ HDA_CODEC_IDT92HD89B1, 0,	"IDT 92HD89B1" },
	{ HDA_CODEC_IDT92HD89E3, 0,	"IDT 92HD89E3" },
	{ HDA_CODEC_IDT92HD89E2, 0,	"IDT 92HD89E2" },
	{ HDA_CODEC_IDT92HD89E1, 0,	"IDT 92HD89E1" },
	{ HDA_CODEC_IDT92HD89D3, 0,	"IDT 92HD89D3" },
	{ HDA_CODEC_IDT92HD89D2, 0,	"IDT 92HD89D2" },
	{ HDA_CODEC_IDT92HD89D1, 0,	"IDT 92HD89D1" },
	{ HDA_CODEC_IDT92HD89F3, 0,	"IDT 92HD89F3" },
	{ HDA_CODEC_IDT92HD89F2, 0,	"IDT 92HD89F2" },
	{ HDA_CODEC_IDT92HD89F1, 0,	"IDT 92HD89F1" },
	{ HDA_CODEC_IDT92HD90BXX, 0,	"IDT 92HD90BXX" },
	{ HDA_CODEC_IDT92HD91BXX, 0,	"IDT 92HD91BXX" },
	{ HDA_CODEC_IDT92HD93BXX, 0,	"IDT 92HD93BXX" },
	{ HDA_CODEC_IDT92HD98BXX, 0,	"IDT 92HD98BXX" },
	{ HDA_CODEC_IDT92HD99BXX, 0,	"IDT 92HD99BXX" },
	{ HDA_CODEC_CX20549, 0,		"Conexant CX20549 (Venice)" },
	{ HDA_CODEC_CX20551, 0,		"Conexant CX20551 (Waikiki)" },
	{ HDA_CODEC_CX20561, 0,		"Conexant CX20561 (Hermosa)" },
	{ HDA_CODEC_CX20582, 0,		"Conexant CX20582 (Pebble)" },
	{ HDA_CODEC_CX20583, 0,		"Conexant CX20583 (Pebble HSF)" },
	{ HDA_CODEC_CX20584, 0,		"Conexant CX20584" },
	{ HDA_CODEC_CX20585, 0,		"Conexant CX20585" },
	{ HDA_CODEC_CX20588, 0,		"Conexant CX20588" },
	{ HDA_CODEC_CX20590, 0,		"Conexant CX20590" },
	{ HDA_CODEC_CX20631, 0,		"Conexant CX20631" },
	{ HDA_CODEC_CX20632, 0,		"Conexant CX20632" },
	{ HDA_CODEC_CX20641, 0,		"Conexant CX20641" },
	{ HDA_CODEC_CX20642, 0,		"Conexant CX20642" },
	{ HDA_CODEC_CX20651, 0,		"Conexant CX20651" },
	{ HDA_CODEC_CX20652, 0,		"Conexant CX20652" },
	{ HDA_CODEC_CX20664, 0,		"Conexant CX20664" },
	{ HDA_CODEC_CX20665, 0,		"Conexant CX20665" },
	{ HDA_CODEC_CX21722, 0,		"Conexant CX21722" },
	{ HDA_CODEC_CX20722, 0,		"Conexant CX20722" },
	{ HDA_CODEC_CX21724, 0,		"Conexant CX21724" },
	{ HDA_CODEC_CX20724, 0,		"Conexant CX20724" },
	{ HDA_CODEC_CX20751, 0,		"Conexant CX20751/2" },
	{ HDA_CODEC_CX20751_2, 0,		"Conexant CX20751/2" },
	{ HDA_CODEC_CX20753, 0,		"Conexant CX20753/4" },
	{ HDA_CODEC_CX20755, 0,		"Conexant CX20755" },
	{ HDA_CODEC_CX20756, 0,		"Conexant CX20756" },
	{ HDA_CODEC_CX20757, 0,		"Conexant CX20757" },
	{ HDA_CODEC_CX20952, 0,		"Conexant CX20952" },
	{ HDA_CODEC_VT1708_8, 0,	"VIA VT1708_8" },
	{ HDA_CODEC_VT1708_9, 0,	"VIA VT1708_9" },
	{ HDA_CODEC_VT1708_A, 0,	"VIA VT1708_A" },
	{ HDA_CODEC_VT1708_B, 0,	"VIA VT1708_B" },
	{ HDA_CODEC_VT1709_0, 0,	"VIA VT1709_0" },
	{ HDA_CODEC_VT1709_1, 0,	"VIA VT1709_1" },
	{ HDA_CODEC_VT1709_2, 0,	"VIA VT1709_2" },
	{ HDA_CODEC_VT1709_3, 0,	"VIA VT1709_3" },
	{ HDA_CODEC_VT1709_4, 0,	"VIA VT1709_4" },
	{ HDA_CODEC_VT1709_5, 0,	"VIA VT1709_5" },
	{ HDA_CODEC_VT1709_6, 0,	"VIA VT1709_6" },
	{ HDA_CODEC_VT1709_7, 0,	"VIA VT1709_7" },
	{ HDA_CODEC_VT1708B_0, 0,	"VIA VT1708B_0" },
	{ HDA_CODEC_VT1708B_1, 0,	"VIA VT1708B_1" },
	{ HDA_CODEC_VT1708B_2, 0,	"VIA VT1708B_2" },
	{ HDA_CODEC_VT1708B_3, 0,	"VIA VT1708B_3" },
	{ HDA_CODEC_VT1708B_4, 0,	"VIA VT1708B_4" },
	{ HDA_CODEC_VT1708B_5, 0,	"VIA VT1708B_5" },
	{ HDA_CODEC_VT1708B_6, 0,	"VIA VT1708B_6" },
	{ HDA_CODEC_VT1708B_7, 0,	"VIA VT1708B_7" },
	{ HDA_CODEC_VT1708S_0, 0,	"VIA VT1708S_0" },
	{ HDA_CODEC_VT1708S_1, 0,	"VIA VT1708S_1" },
	{ HDA_CODEC_VT1708S_2, 0,	"VIA VT1708S_2" },
	{ HDA_CODEC_VT1708S_3, 0,	"VIA VT1708S_3" },
	{ HDA_CODEC_VT1708S_4, 0,	"VIA VT1708S_4" },
	{ HDA_CODEC_VT1708S_5, 0,	"VIA VT1708S_5" },
	{ HDA_CODEC_VT1708S_6, 0,	"VIA VT1708S_6" },
	{ HDA_CODEC_VT1708S_7, 0,	"VIA VT1708S_7" },
	{ HDA_CODEC_VT1702_0, 0,	"VIA VT1702_0" },
	{ HDA_CODEC_VT1702_1, 0,	"VIA VT1702_1" },
	{ HDA_CODEC_VT1702_2, 0,	"VIA VT1702_2" },
	{ HDA_CODEC_VT1702_3, 0,	"VIA VT1702_3" },
	{ HDA_CODEC_VT1702_4, 0,	"VIA VT1702_4" },
	{ HDA_CODEC_VT1702_5, 0,	"VIA VT1702_5" },
	{ HDA_CODEC_VT1702_6, 0,	"VIA VT1702_6" },
	{ HDA_CODEC_VT1702_7, 0,	"VIA VT1702_7" },
	{ HDA_CODEC_VT1716S_0, 0,	"VIA VT1716S_0" },
	{ HDA_CODEC_VT1716S_1, 0,	"VIA VT1716S_1" },
	{ HDA_CODEC_VT1718S_0, 0,	"VIA VT1718S_0" },
	{ HDA_CODEC_VT1718S_1, 0,	"VIA VT1718S_1" },
	{ HDA_CODEC_VT1802_0, 0,	"VIA VT1802_0" },
	{ HDA_CODEC_VT1802_1, 0,	"VIA VT1802_1" },
	{ HDA_CODEC_VT1812, 0,		"VIA VT1812" },
	{ HDA_CODEC_VT1818S, 0,		"VIA VT1818S" },
	{ HDA_CODEC_VT1828S, 0,		"VIA VT1828S" },
	{ HDA_CODEC_VT2002P_0, 0,	"VIA VT2002P_0" },
	{ HDA_CODEC_VT2002P_1, 0,	"VIA VT2002P_1" },
	{ HDA_CODEC_VT2020, 0,		"VIA VT2020" },
	{ HDA_CODEC_ATIRS600_1, 0,	"ATI RS600" },
	{ HDA_CODEC_ATIRS600_2, 0,	"ATI RS600" },
	{ HDA_CODEC_ATIRS690, 0,	"ATI RS690/780" },
	{ HDA_CODEC_ATIR6XX, 0,		"ATI R6xx" },
	{ HDA_CODEC_NVIDIAMCP67, 0,	"NVIDIA MCP67" },
	{ HDA_CODEC_NVIDIAMCP73, 0,	"NVIDIA MCP73" },
	{ HDA_CODEC_NVIDIAMCP78, 0,	"NVIDIA MCP78" },
	{ HDA_CODEC_NVIDIAMCP78_2, 0,	"NVIDIA MCP78" },
	{ HDA_CODEC_NVIDIAMCP78_3, 0,	"NVIDIA MCP78" },
	{ HDA_CODEC_NVIDIAMCP78_4, 0,	"NVIDIA MCP78" },
	{ HDA_CODEC_NVIDIAMCP7A, 0,	"NVIDIA MCP7A" },
	{ HDA_CODEC_NVIDIAGT220, 0,	"NVIDIA GT220" },
	{ HDA_CODEC_NVIDIAGT21X, 0,	"NVIDIA GT21x" },
	{ HDA_CODEC_NVIDIAMCP89, 0,	"NVIDIA MCP89" },
	{ HDA_CODEC_NVIDIAGT240, 0,	"NVIDIA GT240" },
	{ HDA_CODEC_NVIDIAGTS450, 0,	"NVIDIA GTS450" },
	{ HDA_CODEC_NVIDIAGT440, 0,	"NVIDIA GT440" },
	{ HDA_CODEC_NVIDIAGTX550, 0,	"NVIDIA GTX550" },
	{ HDA_CODEC_NVIDIAGTX570, 0,	"NVIDIA GTX570" },
	{ HDA_CODEC_NVIDIATEGRA30, 0,	"NVIDIA Tegra30" },
	{ HDA_CODEC_NVIDIATEGRA114, 0,	"NVIDIA Tegra114" },
	{ HDA_CODEC_NVIDIATEGRA124, 0,	"NVIDIA Tegra124" },
	{ HDA_CODEC_NVIDIATEGRA210, 0,	"NVIDIA Tegra210" },
	{ HDA_CODEC_INTELIP, 0,		"Intel Ibex Peak" },
	{ HDA_CODEC_INTELBL, 0,		"Intel Bearlake" },
	{ HDA_CODEC_INTELCA, 0,		"Intel Cantiga" },
	{ HDA_CODEC_INTELEL, 0,		"Intel Eaglelake" },
	{ HDA_CODEC_INTELIP2, 0,	"Intel Ibex Peak" },
	{ HDA_CODEC_INTELCPT, 0,	"Intel Cougar Point" },
	{ HDA_CODEC_INTELPPT, 0,	"Intel Panther Point" },
	{ HDA_CODEC_INTELHSW, 0,	"Intel Haswell" },
	{ HDA_CODEC_INTELBDW, 0,	"Intel Broadwell" },
	{ HDA_CODEC_INTELSKLK, 0,	"Intel Skylake" },
	{ HDA_CODEC_INTELKBLK, 0,	"Intel Kaby Lake" },
	{ HDA_CODEC_INTELCL, 0,		"Intel Crestline" },
	{ HDA_CODEC_SII1390, 0,		"Silicon Image SiI1390" },
	{ HDA_CODEC_SII1392, 0,		"Silicon Image SiI1392" },
	/* Unknown CODECs */
	{ HDA_CODEC_ADXXXX, 0,		"Analog Devices" },
	{ HDA_CODEC_AGEREXXXX, 0,	"Lucent/Agere Systems" },
	{ HDA_CODEC_ALCXXXX, 0,		"Realtek" },
	{ HDA_CODEC_ATIXXXX, 0,		"ATI" },
	{ HDA_CODEC_CAXXXX, 0,		"Creative" },
	{ HDA_CODEC_CMIXXXX, 0,		"CMedia" },
	{ HDA_CODEC_CMIXXXX2, 0,	"CMedia" },
	{ HDA_CODEC_CSXXXX, 0,		"Cirrus Logic" },
	{ HDA_CODEC_CXXXXX, 0,		"Conexant" },
	{ HDA_CODEC_CHXXXX, 0,		"Chrontel" },
	{ HDA_CODEC_IDTXXXX, 0,		"IDT" },
	{ HDA_CODEC_INTELXXXX, 0,	"Intel" },
	{ HDA_CODEC_MOTOXXXX, 0,	"Motorola" },
	{ HDA_CODEC_NVIDIAXXXX, 0,	"NVIDIA" },
	{ HDA_CODEC_SIIXXXX, 0,		"Silicon Image" },
	{ HDA_CODEC_STACXXXX, 0,	"Sigmatel" },
	{ HDA_CODEC_VTXXXX, 0,		"VIA" },
};

static int
hdacc_suspend(device_t dev)
{

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Suspend...\n");
	);
	bus_generic_suspend(dev);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Suspend done\n");
	);
	return (0);
}

static int
hdacc_resume(device_t dev)
{

	HDA_BOOTHVERBOSE(
		device_printf(dev, "Resume...\n");
	);
	bus_generic_resume(dev);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Resume done\n");
	);
	return (0);
}

static int
hdacc_probe(device_t dev)
{
	uint32_t id, revid;
	char buf[128];
	int i;

	id = ((uint32_t)hda_get_vendor_id(dev) << 16) + hda_get_device_id(dev);
	revid = ((uint32_t)hda_get_revision_id(dev) << 8) + hda_get_stepping_id(dev);

	for (i = 0; i < nitems(hdacc_codecs); i++) {
		if (!HDA_DEV_MATCH(hdacc_codecs[i].id, id))
			continue;
		if (hdacc_codecs[i].revid != 0 &&
		    hdacc_codecs[i].revid != revid)
			continue;
		break;
	}
	if (i < nitems(hdacc_codecs)) {
		if ((hdacc_codecs[i].id & 0xffff) != 0xffff)
			strlcpy(buf, hdacc_codecs[i].name, sizeof(buf));
		else
			snprintf(buf, sizeof(buf), "%s (0x%04x)",
			    hdacc_codecs[i].name, hda_get_device_id(dev));
	} else
		snprintf(buf, sizeof(buf), "Generic (0x%04x)", id);
	strlcat(buf, " HDA CODEC", sizeof(buf));
	device_set_desc_copy(dev, buf);
	return (BUS_PROBE_DEFAULT);
}

static int
hdacc_attach(device_t dev)
{
	struct hdacc_softc *codec = device_get_softc(dev);
	device_t child;
	int cad = (intptr_t)device_get_ivars(dev);
	uint32_t subnode;
	int startnode;
	int endnode;
	int i, n;

	codec->lock = HDAC_GET_MTX(device_get_parent(dev), dev);
	codec->dev = dev;
	codec->cad = cad;

	hdacc_lock(codec);
	subnode = hda_command(dev,
	    HDA_CMD_GET_PARAMETER(0, 0x0, HDA_PARAM_SUB_NODE_COUNT));
	hdacc_unlock(codec);
	if (subnode == HDA_INVALID)
		return (EIO);
	codec->fgcnt = HDA_PARAM_SUB_NODE_COUNT_TOTAL(subnode);
	startnode = HDA_PARAM_SUB_NODE_COUNT_START(subnode);
	endnode = startnode + codec->fgcnt;

	HDA_BOOTHVERBOSE(
		device_printf(dev,
		    "Root Node at nid=0: %d subnodes %d-%d\n",
		    HDA_PARAM_SUB_NODE_COUNT_TOTAL(subnode),
		    startnode, endnode - 1);
	);

	codec->fgs = malloc(sizeof(struct hdacc_fg) * codec->fgcnt,
	    M_HDACC, M_ZERO | M_WAITOK);
	for (i = startnode, n = 0; i < endnode; i++, n++) {
		codec->fgs[n].nid = i;
		hdacc_lock(codec);
		codec->fgs[n].type =
		    HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE(hda_command(dev,
		    HDA_CMD_GET_PARAMETER(0, i, HDA_PARAM_FCT_GRP_TYPE)));
		codec->fgs[n].subsystem_id = hda_command(dev,
		    HDA_CMD_GET_SUBSYSTEM_ID(0, i));
		hdacc_unlock(codec);
		codec->fgs[n].dev = child = device_add_child(dev, NULL, -1);
		if (child == NULL) {
			device_printf(dev, "Failed to add function device\n");
			continue;
		}
		device_set_ivars(child, &codec->fgs[n]);
	}

	bus_generic_attach(dev);

	return (0);
}

static int
hdacc_detach(device_t dev)
{
	struct hdacc_softc *codec = device_get_softc(dev);
	int error;

	error = device_delete_children(dev);
	free(codec->fgs, M_HDACC);
	return (error);
}

static int
hdacc_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	struct hdacc_fg *fg = device_get_ivars(child);

	snprintf(buf, buflen, "nid=%d", fg->nid);
	return (0);
}

static int
hdacc_child_pnpinfo_str_method(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	struct hdacc_fg *fg = device_get_ivars(child);

	snprintf(buf, buflen, "type=0x%02x subsystem=0x%08x",
	    fg->type, fg->subsystem_id);
	return (0);
}

static int
hdacc_print_child(device_t dev, device_t child)
{
	struct hdacc_fg *fg = device_get_ivars(child);
	int retval;

	retval = bus_print_child_header(dev, child);
	retval += printf(" at nid %d", fg->nid);
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

static void
hdacc_probe_nomatch(device_t dev, device_t child)
{
	struct hdacc_softc *codec = device_get_softc(dev);
	struct hdacc_fg *fg = device_get_ivars(child);

	device_printf(child, "<%s %s Function Group> at nid %d on %s "
	    "(no driver attached)\n",
	    device_get_desc(dev),
	    fg->type == HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_AUDIO ? "Audio" :
	    (fg->type == HDA_PARAM_FCT_GRP_TYPE_NODE_TYPE_MODEM ? "Modem" :
	    "Unknown"), fg->nid, device_get_nameunit(dev));
	HDA_BOOTVERBOSE(
		device_printf(dev, "Subsystem ID: 0x%08x\n",
		    hda_get_subsystem_id(dev));
	);
	HDA_BOOTHVERBOSE(
		device_printf(dev, "Power down FG nid=%d to the D3 state...\n",
		    fg->nid);
	);
	hdacc_lock(codec);
	hda_command(dev, HDA_CMD_SET_POWER_STATE(0,
	    fg->nid, HDA_CMD_POWER_STATE_D3));
	hdacc_unlock(codec);
}

static int
hdacc_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct hdacc_fg *fg = device_get_ivars(child);

	switch (which) {
	case HDA_IVAR_NODE_ID:
		*result = fg->nid;
		break;
	case HDA_IVAR_NODE_TYPE:
		*result = fg->type;
		break;
	case HDA_IVAR_SUBSYSTEM_ID:
		*result = fg->subsystem_id;
		break;
	default:
		return(BUS_READ_IVAR(device_get_parent(dev), dev,
		    which, result));
	}
	return (0);
}

static struct mtx *
hdacc_get_mtx(device_t dev, device_t child)
{
	struct hdacc_softc *codec = device_get_softc(dev);

	return (codec->lock);
}

static uint32_t
hdacc_codec_command(device_t dev, device_t child, uint32_t verb)
{

	return (HDAC_CODEC_COMMAND(device_get_parent(dev), dev, verb));
}

static int
hdacc_stream_alloc(device_t dev, device_t child, int dir, int format,
    int stripe, uint32_t **dmapos)
{
	struct hdacc_softc *codec = device_get_softc(dev);
	int stream;

	stream = HDAC_STREAM_ALLOC(device_get_parent(dev), dev,
	    dir, format, stripe, dmapos);
	if (stream > 0)
		codec->streams[dir][stream] = child;
	return (stream);
}

static void
hdacc_stream_free(device_t dev, device_t child, int dir, int stream)
{
	struct hdacc_softc *codec = device_get_softc(dev);

	codec->streams[dir][stream] = NULL;
	HDAC_STREAM_FREE(device_get_parent(dev), dev, dir, stream);
}

static int
hdacc_stream_start(device_t dev, device_t child,
    int dir, int stream, bus_addr_t buf, int blksz, int blkcnt)
{

	return (HDAC_STREAM_START(device_get_parent(dev), dev,
	    dir, stream, buf, blksz, blkcnt));
}

static void
hdacc_stream_stop(device_t dev, device_t child, int dir, int stream)
{

	HDAC_STREAM_STOP(device_get_parent(dev), dev, dir, stream);
}

static void
hdacc_stream_reset(device_t dev, device_t child, int dir, int stream)
{

	HDAC_STREAM_RESET(device_get_parent(dev), dev, dir, stream);
}

static uint32_t
hdacc_stream_getptr(device_t dev, device_t child, int dir, int stream)
{

	return (HDAC_STREAM_GETPTR(device_get_parent(dev), dev, dir, stream));
}

static void
hdacc_stream_intr(device_t dev, int dir, int stream)
{
	struct hdacc_softc *codec = device_get_softc(dev);
	device_t child;

	if ((child = codec->streams[dir][stream]) != NULL)
		HDAC_STREAM_INTR(child, dir, stream);
}

static int
hdacc_unsol_alloc(device_t dev, device_t child, int wanted)
{
	struct hdacc_softc *codec = device_get_softc(dev);
	int tag;

	wanted &= 0x3f;
	tag = wanted;
	do {
		if (codec->tags[tag] == NULL) {
			codec->tags[tag] = child;
			HDAC_UNSOL_ALLOC(device_get_parent(dev), dev, tag);
			return (tag);
		}
		tag++;
		tag &= 0x3f;
	} while (tag != wanted);
	return (-1);
}

static void
hdacc_unsol_free(device_t dev, device_t child, int tag)
{
	struct hdacc_softc *codec = device_get_softc(dev);

	KASSERT(tag >= 0 && tag <= 0x3f, ("Wrong tag value %d\n", tag));
	codec->tags[tag] = NULL;
	HDAC_UNSOL_FREE(device_get_parent(dev), dev, tag);
}

static void
hdacc_unsol_intr(device_t dev, uint32_t resp)
{
	struct hdacc_softc *codec = device_get_softc(dev);
	device_t child;
	int tag;

	tag = resp >> 26;
	if ((child = codec->tags[tag]) != NULL)
		HDAC_UNSOL_INTR(child, resp);
	else
		device_printf(codec->dev, "Unexpected unsolicited "
		    "response with tag %d: %08x\n", tag, resp);
}

static void
hdacc_pindump(device_t dev)
{
	device_t *devlist;
	int devcount, i;

	if (device_get_children(dev, &devlist, &devcount) != 0)
		return;
	for (i = 0; i < devcount; i++)
		HDAC_PINDUMP(devlist[i]);
	free(devlist, M_TEMP);
}

static device_method_t hdacc_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		hdacc_probe),
	DEVMETHOD(device_attach,	hdacc_attach),
	DEVMETHOD(device_detach,	hdacc_detach),
	DEVMETHOD(device_suspend,	hdacc_suspend),
	DEVMETHOD(device_resume,	hdacc_resume),
	/* Bus interface */
	DEVMETHOD(bus_child_location_str, hdacc_child_location_str),
	DEVMETHOD(bus_child_pnpinfo_str, hdacc_child_pnpinfo_str_method),
	DEVMETHOD(bus_print_child,	hdacc_print_child),
	DEVMETHOD(bus_probe_nomatch,	hdacc_probe_nomatch),
	DEVMETHOD(bus_read_ivar,	hdacc_read_ivar),
	DEVMETHOD(hdac_get_mtx,		hdacc_get_mtx),
	DEVMETHOD(hdac_codec_command,	hdacc_codec_command),
	DEVMETHOD(hdac_stream_alloc,	hdacc_stream_alloc),
	DEVMETHOD(hdac_stream_free,	hdacc_stream_free),
	DEVMETHOD(hdac_stream_start,	hdacc_stream_start),
	DEVMETHOD(hdac_stream_stop,	hdacc_stream_stop),
	DEVMETHOD(hdac_stream_reset,	hdacc_stream_reset),
	DEVMETHOD(hdac_stream_getptr,	hdacc_stream_getptr),
	DEVMETHOD(hdac_stream_intr,	hdacc_stream_intr),
	DEVMETHOD(hdac_unsol_alloc,	hdacc_unsol_alloc),
	DEVMETHOD(hdac_unsol_free,	hdacc_unsol_free),
	DEVMETHOD(hdac_unsol_intr,	hdacc_unsol_intr),
	DEVMETHOD(hdac_pindump,		hdacc_pindump),
	DEVMETHOD_END
};

static driver_t hdacc_driver = {
	"hdacc",
	hdacc_methods,
	sizeof(struct hdacc_softc),
};

static devclass_t hdacc_devclass;

DRIVER_MODULE(snd_hda, hdac, hdacc_driver, hdacc_devclass, NULL, NULL);
