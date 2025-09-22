/*	$OpenBSD: azalia_codec.c,v 1.189 2022/09/08 01:35:39 jsg Exp $	*/
/*	$NetBSD: azalia_codec.c,v 1.8 2006/05/10 11:17:27 kent Exp $	*/

/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by TAMURA Kent
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#include <dev/pci/azalia.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#define XNAME(co)	(((struct device *)co->az)->dv_xname)
#define MIXER_DELTA(n)	(AUDIO_MAX_GAIN / (n))

int	azalia_add_convgroup(codec_t *, convgroupset_t *,
    struct io_pin *, int, nid_t *, int, uint32_t, uint32_t);

int	azalia_mixer_fix_indexes(codec_t *);
int	azalia_mixer_default(codec_t *);
int	azalia_mixer_ensure_capacity(codec_t *, size_t);
u_char	azalia_mixer_from_device_value(const codec_t *, nid_t, int, uint32_t );
uint32_t azalia_mixer_to_device_value(const codec_t *, nid_t, int, u_char);

void	azalia_devinfo_offon(mixer_devinfo_t *);
void	azalia_pin_config_ov(widget_t *, int, int);
void	azalia_ampcap_ov(widget_t *, int, int, int, int, int, int);
int	azalia_gpio_unmute(codec_t *, int);


int
azalia_codec_init_vtbl(codec_t *this)
{
	/**
	 * We can refer this->vid and this->subid.
	 */
	this->name = NULL;
	this->qrks = AZ_QRK_NONE;
	switch (this->vid) {
	case 0x10134206:
		this->name = "Cirrus Logic CS4206";
		if (this->subid == 0xcb8910de ||	/* APPLE_MBA3_1 */
		    this->subid == 0x72708086 ||	/* APPLE_MBA4_1 */
		    this->subid == 0xcb7910de) {	/* APPLE_MBP5_5 */
			this->qrks |= AZ_QRK_GPIO_UNMUTE_1 |
			    AZ_QRK_GPIO_UNMUTE_3;
		}
		break;
	case 0x10134208:
		this->name = "Cirrus Logic CS4208";
		if (this->subid == 0x72708086) {	/* APPLE_MBA6_1 */
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0 |
			    AZ_QRK_GPIO_UNMUTE_1;
		}
		break;
	case 0x10ec0221:
		this->name = "Realtek ALC221";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;
		break;
	case 0x10ec0225:
		this->name = "Realtek ALC225";
		break;
	case 0x10ec0233:
	case 0x10ec0235:
		this->name = "Realtek ALC233";
		break;
	case 0x10ec0236:
		if (PCI_VENDOR(this->subid) == PCI_VENDOR_DELL)
			this->name = "Realtek ALC3204";
		else
			this->name = "Realtek ALC236";
		break;
	case 0x10ec0245:
		this->name = "Realtek ALC245";
		break;
	case 0x10ec0255:
		this->name = "Realtek ALC255";
		break;
	case 0x10ec0256:
		this->name = "Realtek ALC256";
		break;
	case 0x10ec0257:
		this->name = "Realtek ALC257";
		break;
	case 0x10ec0260:
		this->name = "Realtek ALC260";
		if (this->subid == 0x008f1025)
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0;
		break;
	case 0x10ec0262:
		this->name = "Realtek ALC262";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;
		break;
	case 0x10ec0268:
		this->name = "Realtek ALC268";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;
		break;
	case 0x10ec0269:
		this->name = "Realtek ALC269";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;

		/*
		 * Enable dock audio on Thinkpad docks
		 * 0x17aa : 0x21f3 = Thinkpad T430
		 * 0x17aa : 0x21f6 = Thinkpad T530
		 * 0x17aa : 0x21fa = Thinkpad X230
		 * 0x17aa : 0x21fb = Thinkpad T430s
		 * 0x17aa : 0x2203 = Thinkpad X230t
		 * 0x17aa : 0x2208 = Thinkpad T431s
		 */
		if (this->subid == 0x21f317aa ||
		    this->subid == 0x21f617aa ||
		    this->subid == 0x21fa17aa ||
		    this->subid == 0x21fb17aa ||
		    this->subid == 0x220317aa ||
		    this->subid == 0x220817aa)
			this->qrks |= AZ_QRK_WID_TPDOCK1;
		break;
	case 0x10ec0270:
		this->name = "Realtek ALC270";
		break;
	case 0x10ec0272:
		this->name = "Realtek ALC272";
		break;
	case 0x10ec0275:
		this->name = "Realtek ALC275";
		break;
	case 0x10ec0280:
		this->name = "Realtek ALC280";
		break;
	case 0x10ec0282:
		this->name = "Realtek ALC282";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;
		break;
	case 0x10ec0283:
		this->name = "Realtek ALC283";
		break;
	case 0x10ec0285:
		this->name = "Realtek ALC285";
		if (this->subid == 0x229217aa) {
			/* Thinkpad X1 Carbon 7 */
			this->qrks |= AZ_QRK_ROUTE_SPKR2_DAC |
			    AZ_QRK_WID_CLOSE_PCBEEP;
		 } else if (this->subid == 0x22c017aa) {
			/* Thinkpad X1 Extreme 3 */
			this->qrks |= AZ_QRK_DOLBY_ATMOS |
			    AZ_QRK_ROUTE_SPKR2_DAC;
		}
		break;
	case 0x10ec0287:
		this->name = "Realtek ALC287";
		break;
	case 0x10ec0292:
		this->name = "Realtek ALC292";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;

		/*
		 * Enable dock audio on Thinkpad docks
		 * 0x17aa : 0x220c = Thinkpad T440s
		 * 0x17aa : 0x220e = Thinkpad T440p
		 * 0x17aa : 0x2210 = Thinkpad T540p
		 * 0x17aa : 0x2212 = Thinkpad T440
		 * 0x17aa : 0x2214 = Thinkpad X240
		 * 0x17aa : 0x2226 = Thinkpad X250
		 * 0x17aa : 0x501e = Thinkpad L440
		 * 0x17aa : 0x5034 = Thinkpad T450
		 * 0x17aa : 0x5036 = Thinkpad T450s
		 * 0x17aa : 0x503c = Thinkpad L450
		 */
		if (this->subid == 0x220c17aa ||
		    this->subid == 0x220e17aa ||
		    this->subid == 0x221017aa ||
		    this->subid == 0x221217aa ||
		    this->subid == 0x221417aa ||
		    this->subid == 0x222617aa ||
		    this->subid == 0x501e17aa ||
		    this->subid == 0x503417aa ||
		    this->subid == 0x503617aa ||
		    this->subid == 0x503c17aa)
			this->qrks |= AZ_QRK_WID_TPDOCK2;
		break;
	case 0x10ec0293:
		if (PCI_VENDOR(this->subid) == PCI_VENDOR_DELL)
			this->name = "Realtek ALC3235";
		else
			this->name = "Realtek ALC293";
		break;
	case 0x10ec0294:
		this->name = "Realtek ALC294";
		break;
	case 0x10ec0295:
		if (PCI_VENDOR(this->subid) == PCI_VENDOR_DELL)
			this->name = "Realtek ALC3254";
		else
			this->name = "Realtek ALC295";
		break;
	case 0x10ec0298:
		this->name = "Realtek ALC298";
		if (this->subid == 0x320019e5 ||
		    this->subid == 0x320119e5)		/* Huawei Matebook X */
			this->qrks |= AZ_QRK_DOLBY_ATMOS;
		break;
	case 0x10ec0299:
		this->name = "Realtek ALC299";
		break;
	case 0x10ec0660:
		this->name = "Realtek ALC660";
		if (this->subid == 0x13391043) {	/* ASUS_G2K */
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0;
		}
		break;
	case 0x10ec0662:
		this->name = "Realtek ALC662";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;
		break;
	case 0x10ec0663:
		this->name = "Realtek ALC663";
		break;
	case 0x10ec0668:
		if (PCI_VENDOR(this->subid) == PCI_VENDOR_DELL)
			this->name = "Realtek ALC3661";
		else
			this->name = "Realtek ALC668";
		break;
	case 0x10ec0671:
		this->name = "Realtek ALC671";
		break;
	case 0x10ec0700:
		this->name = "Realtek ALC700";
		break;
	case 0x10ec0861:
		this->name = "Realtek ALC861";
		break;
	case 0x10ec0880:
		this->name = "Realtek ALC880";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;
		if (this->subid == 0x19931043 ||	/* ASUS_M5200 */
		    this->subid == 0x13231043) {	/* ASUS_A7M */
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0;
		}
		if (this->subid == 0x203d161f) {	/* MEDION_MD95257 */
			this->qrks |= AZ_QRK_GPIO_UNMUTE_1;
		}
		break;
	case 0x10ec0882:
		this->name = "Realtek ALC882";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;
		if (this->subid == 0x13c21043 ||	/* ASUS_A7T */
		    this->subid == 0x19711043) {	/* ASUS_W2J */
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0;
		}
		break;
	case 0x10ec0883:
		this->name = "Realtek ALC883";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;
		if (this->subid == 0x00981025) {	/* ACER_ID */
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0 |
			    AZ_QRK_GPIO_UNMUTE_1;
		}
		break;
	case 0x10ec0885:
		this->name = "Realtek ALC885";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;
		if (this->subid == 0x00a1106b ||	/* APPLE_MB3 */
		    this->subid == 0xcb7910de ||	/* APPLE_MACMINI3_1 (line-in + hp) */
		    this->subid == 0x00a0106b ||	/* APPLE_MB3_1 */
		    this->subid == 0x00a3106b) {	/* APPLE_MB4 */
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0;
		}
		if (this->subid == 0x00a1106b ||
		    this->subid == 0xcb7910de ||	/* APPLE_MACMINI3_1 (internal spkr) */
		    this->subid == 0x00a0106b)
			this->qrks |= AZ_QRK_WID_OVREF50;
		break;
	case 0x10ec0887:
		this->name = "Realtek ALC887";
		break;
	case 0x10ec0888:
		this->name = "Realtek ALC888";
		this->qrks |= AZ_QRK_WID_CDIN_1C | AZ_QRK_WID_BEEP_1D;
		break;
	case 0x10ec0889:
		this->name = "Realtek ALC889";
		break;
	case 0x10ec0892:
		this->name = "Realtek ALC892";
		break;
	case 0x10ec0897:
		this->name = "Realtek ALC897";
		break;
	case 0x10ec0900:
		this->name = "Realtek ALC1150";
		break;
	case 0x10ec0b00:
		this->name = "Realtek ALC1200";
		break;
	case 0x10ec1168:
	case 0x10ec1220:
		this->name = "Realtek ALC1220";
		break;
	case 0x11060398:
	case 0x11061398:
	case 0x11062398:
	case 0x11063398:
	case 0x11064398:
	case 0x11065398:
	case 0x11066398:
	case 0x11067398:
		this->name = "VIA VT1702";
		break;
	case 0x111d7603:
		this->name = "IDT 92HD75B3/4";
		if (PCI_VENDOR(this->subid) == PCI_VENDOR_HP)
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0;
		break;
	case 0x111d7604:
		this->name = "IDT 92HD83C1X";
		break;
	case 0x111d7605:
		this->name = "IDT 92HD81B1X";
		break;
	case 0x111d7608:
		this->name = "IDT 92HD75B1/2";
		if (PCI_VENDOR(this->subid) == PCI_VENDOR_HP)
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0;
		break;
	case 0x111d7674:
		this->name = "IDT 92HD73D1";
		break;
	case 0x111d7675:
		this->name = "IDT 92HD73C1";	/* aka 92HDW74C1 */
		if (PCI_VENDOR(this->subid) == PCI_VENDOR_DELL)
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0;
		break;
	case 0x111d7676:
		this->name = "IDT 92HD73E1";	/* aka 92HDW74E1 */
		break;
	case 0x111d7695:
		this->name = "IDT 92HD95";	/* aka IDT/TSI 92HD95B */
		break;
	case 0x111d76b0:
		this->name = "IDT 92HD71B8";
		break;
	case 0x111d76b2:
		this->name = "IDT 92HD71B7";
		if (PCI_VENDOR(this->subid) == PCI_VENDOR_DELL ||
		    PCI_VENDOR(this->subid) == PCI_VENDOR_HP)
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0;
		break;
	case 0x111d76b6:
		this->name = "IDT 92HD71B5";
		break;
	case 0x111d76d4:
		this->name = "IDT 92HD83C1C";
		break;
	case 0x111d76d5:
		this->name = "IDT 92HD81B1C";
		break;
	case 0x11d4184a:
		this->name = "Analog Devices AD1884A";
		break;
	case 0x11d41882:
		this->name = "Analog Devices AD1882";
		break;
	case 0x11d41883:
		this->name = "Analog Devices AD1883";
		break;
	case 0x11d41884:
		this->name = "Analog Devices AD1884";
		break;
	case 0x11d4194a:
		this->name = "Analog Devices AD1984A";
		break;
	case 0x11d41981:
		this->name = "Analog Devices AD1981HD";
		this->qrks |= AZ_QRK_WID_AD1981_OAMP;
		break;
	case 0x11d41983:
		this->name = "Analog Devices AD1983";
		break;
	case 0x11d41984:
		this->name = "Analog Devices AD1984";
		break;
	case 0x11d41988:
		this->name = "Analog Devices AD1988A";
		break;
	case 0x11d4198b:
		this->name = "Analog Devices AD1988B";
		break;
	case 0x11d4882a:
		this->name = "Analog Devices AD1882A";
		break;
	case 0x11d4989a:
		this->name = "Analog Devices AD1989A";
		break;
	case 0x11d4989b:
		this->name = "Analog Devices AD1989B";
		break;
	case 0x14f15045:
		this->name = "Conexant CX20549";  /* Venice */
		break;
	case 0x14f15047:
		this->name = "Conexant CX20551";  /* Waikiki */
		break;
	case 0x14f15051:
		this->name = "Conexant CX20561";  /* Hermosa */
		break;
	case 0x14f1506e:
		this->name = "Conexant CX20590";
		/*
		 * Enable dock audio on Thinkpad docks
		 * 0x17aa : 0x20f2 = Thinkpad T400
		 * 0x17aa : 0x215e = Thinkpad T410
		 * 0x17aa : 0x215f = Thinkpad T510
		 * 0x17aa : 0x21ce = Thinkpad T420
		 * 0x17aa : 0x21cf = Thinkpad T520
		 * 0x17aa : 0x21da = Thinkpad X220
		 * 0x17aa : 0x21db = Thinkpad X220t
		 */
		if (this->subid == 0x20f217aa ||
		    this->subid == 0x215e17aa ||
		    this->subid == 0x215f17aa ||
		    this->subid == 0x21ce17aa ||
		    this->subid == 0x21cf17aa ||
		    this->subid == 0x21da17aa ||
		    this->subid == 0x21db17aa)
			this->qrks |= AZ_QRK_WID_TPDOCK3;
		break;
	case 0x434d4980:
		this->name = "CMedia CMI9880";
		break;
	case 0x83847612:
		this->name = "Sigmatel STAC9230X";
		break;
	case 0x83847613:
		this->name = "Sigmatel STAC9230D";
		break;
	case 0x83847614:
		this->name = "Sigmatel STAC9229X";
		break;
	case 0x83847615:
		this->name = "Sigmatel STAC9229D";
		break;
	case 0x83847616:
		this->name = "Sigmatel STAC9228X";
		if (this->subid == 0x02271028 ||	/* DELL_V1400 */
		    this->subid == 0x01f31028) {	/* DELL_I1400 */
			this->qrks |= AZ_QRK_GPIO_UNMUTE_2;
	 	}
		break;
	case 0x83847617:
		this->name = "Sigmatel STAC9228D";
		break;
	case 0x83847618:
		this->name = "Sigmatel STAC9227X";
		break;
	case 0x83847619:
		this->name = "Sigmatel STAC9227D";
		break;
	case 0x83847620:
		this->name = "Sigmatel STAC9274";
		break;
	case 0x83847621:
		this->name = "Sigmatel STAC9274D";
		break;
	case 0x83847626:
		this->name = "Sigmatel STAC9271X";
		break;
	case 0x83847627:
		this->name = "Sigmatel STAC9271D";
		break;
	case 0x83847632:
		this->name = "Sigmatel STAC9202";
		break;
	case 0x83847634:
		this->name = "Sigmatel STAC9250";
		break;
	case 0x83847636:
		this->name = "Sigmatel STAC9251";
		break;
	case 0x83847638:
		this->name = "IDT 92HD700X";
		break;
	case 0x83847639:
		this->name = "IDT 92HD700D";
		break;
	case 0x83847645:
		this->name = "IDT 92HD206X";
		break;
	case 0x83847646:
		this->name = "IDT 92HD206D";
		break;
	case 0x83847661:
		/* FALLTHROUGH */
	case 0x83847662:
		this->name = "Sigmatel STAC9225";
		break;
	case 0x83847680:
		this->name = "Sigmatel STAC9220/1";
		if (this->subid == 0x76808384) {	/* APPLE_ID */
			this->qrks |= AZ_QRK_GPIO_POL_0 | AZ_QRK_GPIO_UNMUTE_0 |
			     AZ_QRK_GPIO_UNMUTE_1;
		}
		break;
	case 0x83847682:
		/* FALLTHROUGH */
	case 0x83847683:
		this->name = "Sigmatel STAC9221D";	/* aka IDT 92HD202 */
		break;
	case 0x83847690:
		this->name = "Sigmatel STAC9200";	/* aka IDT 92HD001 */
		break;
	case 0x83847691:
		this->name = "Sigmatel STAC9200D";
		break;
	case 0x83847698:
		this->name = "IDT 92HD005";
		break;
	case 0x83847699:
		this->name = "IDT 92HD005D";
		break;
	case 0x838476a0:
		this->name = "Sigmatel STAC9205X";
		if (this->subid == 0x01f91028 ||	/* DELL_D630 */
		    this->subid == 0x02281028) {	/* DELL_V1500 */
			this->qrks |= AZ_QRK_GPIO_UNMUTE_0;
		}
		break;
	case 0x838476a1:
		this->name = "Sigmatel STAC9205D";
		break;
	case 0x838476a2:
		this->name = "Sigmatel STAC9204X";
		break;
	case 0x838476a3:
		this->name = "Sigmatel STAC9204D";
		break;
	}
	return 0;
}

/* ----------------------------------------------------------------
 * functions for generic codecs
 * ---------------------------------------------------------------- */

int
azalia_widget_enabled(const codec_t *this, nid_t nid)
{
	if (!VALID_WIDGET_NID(nid, this) || !this->w[nid].enable)
		return 0;
	return 1;
}

int
azalia_init_dacgroup(codec_t *this)
{
	this->dacs.ngroups = 0;
	if (this->na_dacs > 0)
		azalia_add_convgroup(this, &this->dacs,
		    this->opins, this->nopins,
		    this->a_dacs, this->na_dacs,
		    COP_AWTYPE_AUDIO_OUTPUT, 0);
	if (this->na_dacs_d > 0)
		azalia_add_convgroup(this, &this->dacs,
		    this->opins_d, this->nopins_d,
		    this->a_dacs_d, this->na_dacs_d,
		    COP_AWTYPE_AUDIO_OUTPUT, COP_AWCAP_DIGITAL);
	this->dacs.cur = 0;

	this->adcs.ngroups = 0;
	if (this->na_adcs > 0)
		azalia_add_convgroup(this, &this->adcs,
		    this->ipins, this->nipins,
		    this->a_adcs, this->na_adcs,
		    COP_AWTYPE_AUDIO_INPUT, 0);
	if (this->na_adcs_d > 0)
		azalia_add_convgroup(this, &this->adcs,
		    this->ipins_d, this->nipins_d,
		    this->a_adcs_d, this->na_adcs_d,
		    COP_AWTYPE_AUDIO_INPUT, COP_AWCAP_DIGITAL);
	this->adcs.cur = 0;

	return 0;
}

int
azalia_add_convgroup(codec_t *this, convgroupset_t *group,
    struct io_pin *pins, int npins, nid_t *all_convs, int nall_convs,
    uint32_t type, uint32_t digital)
{
	nid_t convs[HDA_MAX_CHANNELS];
	int nconvs;
	nid_t conv;
	int i, j, k;

	nconvs = 0;

	/* default pin connections */
	for (i = 0; i < npins; i++) {
		conv = pins[i].conv;
		if (conv < 0)
			continue;
		for (j = 0; j < nconvs; j++) {
			if (convs[j] == conv)
				break;
		}
		if (j < nconvs)
			continue;
		convs[nconvs++] = conv;
		if (nconvs >= nall_convs) {
			goto done;
		}
	}
	/* non-default connections */
	for (i = 0; i < npins; i++) {
		for (j = 0; j < nall_convs; j++) {
			conv = all_convs[j];
			for (k = 0; k < nconvs; k++) {
				if (convs[k] == conv)
					break;
			}
			if (k < nconvs)
				continue;
			if (type == COP_AWTYPE_AUDIO_OUTPUT) {
				k = azalia_codec_fnode(this, conv,
				    pins[i].nid, 0);
				if (k < 0)
					continue;
			} else {
				if (!azalia_widget_enabled(this, conv))
					continue;
				k = azalia_codec_fnode(this, pins[i].nid,
				    conv, 0);
				if (k < 0)
					continue;
			}
			convs[nconvs++] = conv;
			if (nconvs >= nall_convs) {
				goto done;
			}
		}
	}
	/* Make sure the speaker dac is part of the analog output convgroup
	 * or it won't get connected by azalia_codec_connect_stream().
	 */
	if (type == COP_AWTYPE_AUDIO_OUTPUT && !digital &&
	    nconvs < nall_convs && this->spkr_dac != -1) {
		for (i = 0; i < nconvs; i++)
			if (convs[i] == this->spkr_dac)
				break;
		if (i == nconvs)
			convs[nconvs++] = this->spkr_dac;
	}
done:
	for (i = 0; i < nconvs; i++)
		group->groups[group->ngroups].conv[i] = convs[i];
	if (nconvs > 0) {
		group->groups[group->ngroups].nconv = i;
		group->ngroups++;
	}

	/* Disable converters that aren't in a convgroup. */
	for (i = 0; i < nall_convs; i++) {
		conv = all_convs[i];
		for (j = 0; j < nconvs; j++)
			if (convs[j] == conv)
				break;
		if (j == nconvs)
			this->w[conv].enable = 0;
	}

	return 0;
}

int
azalia_codec_fnode(codec_t *this, nid_t node, int index, int depth)
{
	const widget_t *w;
	int i, ret;

	w = &this->w[index];
	if (w->nid == node) {
		return index;
	}
	/* back at the beginning or a bad end */
	if (depth > 0 &&
	    (w->type == COP_AWTYPE_PIN_COMPLEX ||
	    w->type == COP_AWTYPE_BEEP_GENERATOR ||
	    w->type == COP_AWTYPE_AUDIO_OUTPUT ||
	    w->type == COP_AWTYPE_AUDIO_INPUT))
		return -1;
	if (++depth >= 10)
		return -1;
	for (i = 0; i < w->nconnections; i++) {
		if (!azalia_widget_enabled(this, w->connections[i]))
			continue;
		ret = azalia_codec_fnode(this, node, w->connections[i], depth);
		if (ret >= 0)
			return ret;
	}
	return -1;
}

int
azalia_unsol_event(codec_t *this, int tag)
{
	mixer_ctrl_t mc;
	uint32_t result;
	int i, err, vol, vol2;

	err = 0;
	tag = CORB_UNSOL_TAG(tag);
	switch (tag) {
	case AZ_TAG_SPKR:
		mc.type = AUDIO_MIXER_ENUM;
		vol = 0;
		for (i = 0; !vol && !err && i < this->nsense_pins; i++) {
			if (!(this->spkr_muters & (1 << i)))
				continue;
			err = azalia_comresp(this, this->sense_pins[i],
			    CORB_GET_PIN_WIDGET_CONTROL, 0, &result);
			if (err || !(result & CORB_PWC_OUTPUT))
				continue;
			err = azalia_comresp(this, this->sense_pins[i],
			    CORB_GET_PIN_SENSE, 0, &result);
			if (!err && (result & CORB_PS_PRESENCE))
				vol = 1;
		}
		if (err)
			break;
		this->spkr_muted = vol;
		switch(this->spkr_mute_method) {
		case AZ_SPKR_MUTE_SPKR_MUTE:
			mc.un.ord = vol;
			err = azalia_mixer_set(this, this->speaker,
			    MI_TARGET_OUTAMP, &mc);
			if (!err && this->speaker2 != -1 &&
			    (this->w[this->speaker2].widgetcap & COP_AWCAP_OUTAMP) &&
			    (this->w[this->speaker2].outamp_cap & COP_AMPCAP_MUTE))
				err = azalia_mixer_set(this, this->speaker2,
				    MI_TARGET_OUTAMP, &mc);
			break;
		case AZ_SPKR_MUTE_SPKR_DIR:
			mc.un.ord = vol ? 0 : 1;
			err = azalia_mixer_set(this, this->speaker,
			    MI_TARGET_PINDIR, &mc);
			if (!err && this->speaker2 != -1 &&
			    (this->w[this->speaker2].d.pin.cap & COP_PINCAP_OUTPUT) &&
			    (this->w[this->speaker2].d.pin.cap & COP_PINCAP_INPUT))
				err = azalia_mixer_set(this, this->speaker2,
				    MI_TARGET_PINDIR, &mc);
			break;
		case AZ_SPKR_MUTE_DAC_MUTE:
			mc.un.ord = vol;
			err = azalia_mixer_set(this, this->spkr_dac,
			    MI_TARGET_OUTAMP, &mc);
			break;
		}
		break;

	case AZ_TAG_PLAYVOL:
		if (this->playvols.master == this->audiofunc)
			return EINVAL;
		err = azalia_comresp(this, this->playvols.master,
		    CORB_GET_VOLUME_KNOB, 0, &result);
		if (err)
			return err;

		vol = CORB_VKNOB_VOLUME(result) - this->playvols.hw_step;
		vol2 = vol * (AUDIO_MAX_GAIN / this->playvols.hw_nsteps);
		this->playvols.hw_step = CORB_VKNOB_VOLUME(result);

		vol = vol2 + this->playvols.vol_l;
		if (vol < 0)
			vol = 0;
		else if (vol > AUDIO_MAX_GAIN)
			vol = AUDIO_MAX_GAIN;
		this->playvols.vol_l = vol;

		vol = vol2 + this->playvols.vol_r;
		if (vol < 0)
			vol = 0;
		else if (vol > AUDIO_MAX_GAIN)
			vol = AUDIO_MAX_GAIN;
		this->playvols.vol_r = vol;

		mc.type = AUDIO_MIXER_VALUE;
		mc.un.value.num_channels = 2;
		mc.un.value.level[0] = this->playvols.vol_l;
		mc.un.value.level[1] = this->playvols.vol_r;
		err = azalia_mixer_set(this, this->playvols.master,
		    MI_TARGET_PLAYVOL, &mc);
		break;

	default:
		DPRINTF(("%s: unknown tag %d\n", __func__, tag));
		break;
	}

	return err;
}


/* ----------------------------------------------------------------
 * Generic mixer functions
 * ---------------------------------------------------------------- */

int
azalia_mixer_init(codec_t *this)
{
	/*
	 * pin		"<color>%2.2x"
	 * audio output	"dac%2.2x"
	 * audio input	"adc%2.2x"
	 * mixer	"mixer%2.2x"
	 * selector	"sel%2.2x"
	 */
	const widget_t *w, *ww;
	mixer_item_t *m;
	int err, i, j, k, bits;

	this->maxmixers = 10;
	this->nmixers = 0;
	this->mixers = mallocarray(this->maxmixers, sizeof(mixer_item_t),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (this->mixers == NULL) {
		printf("%s: out of memory in %s\n", XNAME(this), __func__);
		return ENOMEM;
	}

	/* register classes */
	m = &this->mixers[AZ_CLASS_INPUT];
	m->devinfo.index = AZ_CLASS_INPUT;
	strlcpy(m->devinfo.label.name, AudioCinputs,
	    sizeof(m->devinfo.label.name));
	m->devinfo.type = AUDIO_MIXER_CLASS;
	m->devinfo.mixer_class = AZ_CLASS_INPUT;
	m->devinfo.next = AUDIO_MIXER_LAST;
	m->devinfo.prev = AUDIO_MIXER_LAST;
	m->nid = 0;

	m = &this->mixers[AZ_CLASS_OUTPUT];
	m->devinfo.index = AZ_CLASS_OUTPUT;
	strlcpy(m->devinfo.label.name, AudioCoutputs,
	    sizeof(m->devinfo.label.name));
	m->devinfo.type = AUDIO_MIXER_CLASS;
	m->devinfo.mixer_class = AZ_CLASS_OUTPUT;
	m->devinfo.next = AUDIO_MIXER_LAST;
	m->devinfo.prev = AUDIO_MIXER_LAST;
	m->nid = 0;

	m = &this->mixers[AZ_CLASS_RECORD];
	m->devinfo.index = AZ_CLASS_RECORD;
	strlcpy(m->devinfo.label.name, AudioCrecord,
	    sizeof(m->devinfo.label.name));
	m->devinfo.type = AUDIO_MIXER_CLASS;
	m->devinfo.mixer_class = AZ_CLASS_RECORD;
	m->devinfo.next = AUDIO_MIXER_LAST;
	m->devinfo.prev = AUDIO_MIXER_LAST;
	m->nid = 0;

	this->nmixers = AZ_CLASS_RECORD + 1;

#define MIXER_REG_PROLOG	\
	mixer_devinfo_t *d; \
	err = azalia_mixer_ensure_capacity(this, this->nmixers + 1); \
	if (err) \
		return err; \
	m = &this->mixers[this->nmixers]; \
	d = &m->devinfo; \
	m->nid = i

	FOR_EACH_WIDGET(this, i) {

		w = &this->w[i];
		if (!w->enable)
			continue;

		/* selector */
		if (w->nconnections > 0 && w->type != COP_AWTYPE_AUDIO_MIXER &&
		    !(w->nconnections == 1 &&
		    azalia_widget_enabled(this, w->connections[0]) &&
		    strcmp(w->name, this->w[w->connections[0]].name) == 0) &&
		    w->nid != this->mic) {
			MIXER_REG_PROLOG;
			snprintf(d->label.name, sizeof(d->label.name),
			    "%s_source", w->name);
			d->type = AUDIO_MIXER_ENUM;
			if (w->mixer_class >= 0)
				d->mixer_class = w->mixer_class;
			else {
				if (w->type == COP_AWTYPE_AUDIO_SELECTOR)
					d->mixer_class = AZ_CLASS_INPUT;
				else
					d->mixer_class = AZ_CLASS_OUTPUT;
			}
			m->target = MI_TARGET_CONNLIST;
			for (j = 0, k = 0; j < w->nconnections && k < 32; j++) {
				if (!azalia_widget_enabled(this,
				    w->connections[j]))
					continue;
				d->un.e.member[k].ord = j;
				strlcpy(d->un.e.member[k].label.name,
				    this->w[w->connections[j]].name,
				    MAX_AUDIO_DEV_LEN);
				k++;
			}
			d->un.e.num_mem = k;
			this->nmixers++;
		}

		/* output mute */
		if (w->widgetcap & COP_AWCAP_OUTAMP &&
		    w->outamp_cap & COP_AMPCAP_MUTE &&
		    w->nid != this->mic) {
			MIXER_REG_PROLOG;
			snprintf(d->label.name, sizeof(d->label.name),
			    "%s_mute", w->name);
			if (w->mixer_class >= 0)
				d->mixer_class = w->mixer_class;
			else {
				if (w->type == COP_AWTYPE_AUDIO_MIXER ||
				    w->type == COP_AWTYPE_AUDIO_SELECTOR ||
				    w->type == COP_AWTYPE_PIN_COMPLEX)
					d->mixer_class = AZ_CLASS_OUTPUT;
				else
					d->mixer_class = AZ_CLASS_INPUT;
			}
			m->target = MI_TARGET_OUTAMP;
			azalia_devinfo_offon(d);
			this->nmixers++;
		}

		/* output gain */
		if (w->widgetcap & COP_AWCAP_OUTAMP &&
		    COP_AMPCAP_NUMSTEPS(w->outamp_cap) &&
		    w->nid != this->mic) {
			MIXER_REG_PROLOG;
			snprintf(d->label.name, sizeof(d->label.name),
			    "%s", w->name);
			d->type = AUDIO_MIXER_VALUE;
			if (w->mixer_class >= 0)
				d->mixer_class = w->mixer_class;
			else {
				if (w->type == COP_AWTYPE_AUDIO_MIXER ||
				    w->type == COP_AWTYPE_AUDIO_SELECTOR ||
				    w->type == COP_AWTYPE_PIN_COMPLEX)
					d->mixer_class = AZ_CLASS_OUTPUT;
				else
					d->mixer_class = AZ_CLASS_INPUT;
			}
			m->target = MI_TARGET_OUTAMP;
			d->un.v.num_channels = WIDGET_CHANNELS(w);
			d->un.v.units.name[0] = 0;
			d->un.v.delta =
			    MIXER_DELTA(COP_AMPCAP_NUMSTEPS(w->outamp_cap));
			this->nmixers++;
		}

		/* input mute */
		if (w->widgetcap & COP_AWCAP_INAMP &&
		    w->inamp_cap & COP_AMPCAP_MUTE &&
		    w->nid != this->speaker &&
		    w->nid != this->speaker2) {
			if (w->type != COP_AWTYPE_AUDIO_MIXER) {
				MIXER_REG_PROLOG;
				snprintf(d->label.name, sizeof(d->label.name),
				    "%s_mute", w->name);
				if (w->mixer_class >= 0)
					d->mixer_class = w->mixer_class;
				else
					d->mixer_class = AZ_CLASS_INPUT;
				m->target = 0;
				azalia_devinfo_offon(d);
				this->nmixers++;
			} else {
				MIXER_REG_PROLOG;
				snprintf(d->label.name, sizeof(d->label.name),
				    "%s_source", w->name);
				m->target = MI_TARGET_MUTESET;
				d->type = AUDIO_MIXER_SET;
				if (w->mixer_class >= 0)
					d->mixer_class = w->mixer_class;
				else
					d->mixer_class = AZ_CLASS_INPUT;
				for (j = 0, k = 0;
				    j < w->nconnections && k < 32; j++) {
					if (!azalia_widget_enabled(this,
					    w->connections[j]))
						continue;
					if (w->connections[j] == this->speaker ||
					    w->connections[j] == this->speaker2)
						continue;
					d->un.s.member[k].mask = 1 << j;
					strlcpy(d->un.s.member[k].label.name,
					    this->w[w->connections[j]].name,
					    MAX_AUDIO_DEV_LEN);
					k++;
				}
				d->un.s.num_mem = k;
				if (k != 0)
					this->nmixers++;
			}
		}

		/* input gain */
		if (w->widgetcap & COP_AWCAP_INAMP &&
		    COP_AMPCAP_NUMSTEPS(w->inamp_cap) &&
		    w->nid != this->speaker &&
		    w->nid != this->speaker2) {
			if (w->type != COP_AWTYPE_AUDIO_SELECTOR &&
			    w->type != COP_AWTYPE_AUDIO_MIXER) {
				MIXER_REG_PROLOG;
				snprintf(d->label.name, sizeof(d->label.name),
				    "%s", w->name);
				d->type = AUDIO_MIXER_VALUE;
				if (w->mixer_class >= 0)
					d->mixer_class = w->mixer_class;
				else
					d->mixer_class = AZ_CLASS_INPUT;
				m->target = 0;
				d->un.v.num_channels = WIDGET_CHANNELS(w);
				d->un.v.units.name[0] = 0;
				d->un.v.delta =
				    MIXER_DELTA(COP_AMPCAP_NUMSTEPS(w->inamp_cap));
				this->nmixers++;
			} else {
				for (j = 0; j < w->nconnections; j++) {
					if (!azalia_widget_enabled(this,
					    w->connections[j]))
						continue;
					if (w->connections[j] == this->speaker ||
					    w->connections[j] == this->speaker2)
						continue;
					MIXER_REG_PROLOG;
					snprintf(d->label.name,
					    sizeof(d->label.name), "%s_%s",
					    w->name,
					    this->w[w->connections[j]].name);
					d->type = AUDIO_MIXER_VALUE;
					if (w->mixer_class >= 0)
						d->mixer_class = w->mixer_class;
					else
						d->mixer_class = AZ_CLASS_INPUT;
					m->target = j;
					d->un.v.num_channels = WIDGET_CHANNELS(w);
					d->un.v.units.name[0] = 0;
					d->un.v.delta =
					    MIXER_DELTA(COP_AMPCAP_NUMSTEPS(w->inamp_cap));
					this->nmixers++;
				}
			}
		}

		/* hardcoded mixer inputs */
		if (w->type == COP_AWTYPE_AUDIO_MIXER &&
		    !(w->widgetcap & COP_AWCAP_INAMP)) {
			MIXER_REG_PROLOG;
			snprintf(d->label.name, sizeof(d->label.name),
			    "%s_source", w->name);
			m->target = MI_TARGET_MIXERSET;
			d->type = AUDIO_MIXER_SET;
			if (w->mixer_class >= 0)
				d->mixer_class = w->mixer_class;
			else
				d->mixer_class = AZ_CLASS_INPUT;
			for (j = 0, k = 0;
			    j < w->nconnections && k < 32; j++) {
				if (!azalia_widget_enabled(this,
				    w->connections[j]))
					continue;
				if (w->connections[j] == this->speaker ||
				    w->connections[j] == this->speaker2)
					continue;
				d->un.s.member[k].mask = 1 << j;
				strlcpy(d->un.s.member[k].label.name,
				    this->w[w->connections[j]].name,
				    MAX_AUDIO_DEV_LEN);
				k++;
			}
			d->un.s.num_mem = k;
			if (k != 0)
				this->nmixers++;
		}

		/* pin direction */
		if (w->type == COP_AWTYPE_PIN_COMPLEX &&
		    ((w->d.pin.cap & COP_PINCAP_OUTPUT &&
		    w->d.pin.cap & COP_PINCAP_INPUT) ||
		    COP_PINCAP_VREF(w->d.pin.cap) > 1)) {

			MIXER_REG_PROLOG;
			snprintf(d->label.name, sizeof(d->label.name),
			    "%s_dir", w->name);
			d->type = AUDIO_MIXER_ENUM;
			d->mixer_class = AZ_CLASS_OUTPUT;
			m->target = MI_TARGET_PINDIR;

			k = 0;
			d->un.e.member[k].ord = 0;
			strlcpy(d->un.e.member[k].label.name, "none",
			    MAX_AUDIO_DEV_LEN);
			k++;

			if (w->d.pin.cap & COP_PINCAP_OUTPUT) {
				d->un.e.member[k].ord = 1;
				strlcpy(d->un.e.member[k].label.name,
				    AudioNoutput, MAX_AUDIO_DEV_LEN);
				k++;
			}

			if (w->d.pin.cap & COP_PINCAP_INPUT) {
				d->un.e.member[k].ord = 2;
				strlcpy(d->un.e.member[k].label.name,
				    AudioNinput, MAX_AUDIO_DEV_LEN);
				k++;

				for (j = 0; j < 4; j++) {
					if (j == 0) {
						bits = (1 << CORB_PWC_VREF_GND);
						strlcpy(d->un.e.member[k].label.name,
						    AudioNinput "-vr0",
						    MAX_AUDIO_DEV_LEN);
					} else if (j == 1) {
						bits = (1 << CORB_PWC_VREF_50);
						strlcpy(d->un.e.member[k].label.name,
						    AudioNinput "-vr50",
						    MAX_AUDIO_DEV_LEN);
					} else if (j == 2) {
						bits = (1 << CORB_PWC_VREF_80);
						strlcpy(d->un.e.member[k].label.name,
						    AudioNinput "-vr80",
						    MAX_AUDIO_DEV_LEN);
					} else if (j == 3) {
						bits = (1 << CORB_PWC_VREF_100);
						strlcpy(d->un.e.member[k].label.name,
						    AudioNinput "-vr100",
						    MAX_AUDIO_DEV_LEN);
					}
					if ((COP_PINCAP_VREF(w->d.pin.cap) &
					    bits) == bits) {
						d->un.e.member[k].ord = j + 3;
						k++;
					}
				}
			}
			d->un.e.num_mem = k;
			this->nmixers++;
		}

		/* pin headphone-boost */
		if (w->type == COP_AWTYPE_PIN_COMPLEX &&
		    w->d.pin.cap & COP_PINCAP_HEADPHONE &&
		    w->nid != this->mic) {
			MIXER_REG_PROLOG;
			snprintf(d->label.name, sizeof(d->label.name),
			    "%s_boost", w->name);
			d->mixer_class = AZ_CLASS_OUTPUT;
			m->target = MI_TARGET_PINBOOST;
			azalia_devinfo_offon(d);
			this->nmixers++;
		}

		if (w->type == COP_AWTYPE_PIN_COMPLEX &&
		    w->d.pin.cap & COP_PINCAP_EAPD) {
			MIXER_REG_PROLOG;
			snprintf(d->label.name, sizeof(d->label.name),
			    "%s_eapd", w->name);
			d->mixer_class = AZ_CLASS_OUTPUT;
			m->target = MI_TARGET_EAPD;
			azalia_devinfo_offon(d);
			this->nmixers++;
		}
	}

	/* sense pins */
	for (i = 0; i < this->nsense_pins; i++) {
		if (!azalia_widget_enabled(this, this->sense_pins[i])) {
			DPRINTF(("%s: sense pin %2.2x not found\n",
			    __func__, this->sense_pins[i]));
			continue;
		}

		MIXER_REG_PROLOG;
		m->nid = this->w[this->sense_pins[i]].nid;
		snprintf(d->label.name, sizeof(d->label.name), "%s_sense",
		    this->w[this->sense_pins[i]].name);
		d->type = AUDIO_MIXER_ENUM;
		d->mixer_class = AZ_CLASS_OUTPUT;
		m->target = MI_TARGET_PINSENSE;
		d->un.e.num_mem = 2;
		d->un.e.member[0].ord = 0;
		strlcpy(d->un.e.member[0].label.name, "unplugged",
		    MAX_AUDIO_DEV_LEN);
		d->un.e.member[1].ord = 1;
		strlcpy(d->un.e.member[1].label.name, "plugged",
		    MAX_AUDIO_DEV_LEN);
		this->nmixers++;
	}

	/* spkr mute by jack sense */
	this->spkr_mute_method = AZ_SPKR_MUTE_NONE;
	if (this->speaker != -1 && this->spkr_dac != -1 && this->nsense_pins > 0) {
		w = &this->w[this->speaker];
		if ((w->widgetcap & COP_AWCAP_OUTAMP) &&
		    (w->outamp_cap & COP_AMPCAP_MUTE))
			this->spkr_mute_method = AZ_SPKR_MUTE_SPKR_MUTE;
		else if ((w->d.pin.cap & COP_PINCAP_OUTPUT) &&
		    (w->d.pin.cap & COP_PINCAP_INPUT))
			this->spkr_mute_method = AZ_SPKR_MUTE_SPKR_DIR;
		else {
			w = &this->w[this->spkr_dac];
			if (w->nid != this->dacs.groups[0].conv[0] &&
			    (w->widgetcap & COP_AWCAP_OUTAMP) &&
			    (w->outamp_cap & COP_AMPCAP_MUTE))
				this->spkr_mute_method = AZ_SPKR_MUTE_DAC_MUTE;
		}
	}
	if (this->spkr_mute_method != AZ_SPKR_MUTE_NONE) {
		w = &this->w[this->speaker];
		MIXER_REG_PROLOG;
		m->nid = w->nid;
		snprintf(d->label.name, sizeof(d->label.name),
		    "%s_muters", w->name);
		m->target = MI_TARGET_SENSESET;
		d->type = AUDIO_MIXER_SET;
		d->mixer_class = AZ_CLASS_OUTPUT;
		this->spkr_muters = 0;
		for (i = 0, j = 0; i < this->nsense_pins; i++) {
			ww = &this->w[this->sense_pins[i]];
			if (!(ww->d.pin.cap & COP_PINCAP_OUTPUT))
				continue;
			if (!(ww->widgetcap & COP_AWCAP_UNSOL))
				continue;
			d->un.s.member[j].mask = 1 << i;
			this->spkr_muters |= (1 << i);
			strlcpy(d->un.s.member[j++].label.name, ww->name,
			    MAX_AUDIO_DEV_LEN);
		}
		d->un.s.num_mem = j;
		if (j != 0)
			this->nmixers++;
	}

	/* playback volume group */
	if (this->playvols.nslaves > 0) {
		mixer_devinfo_t *d;
		err = azalia_mixer_ensure_capacity(this,
		    this->nmixers + 3);

		/* volume */
		m = &this->mixers[this->nmixers];
		m->nid = this->playvols.master;
		m->target = MI_TARGET_PLAYVOL;
		d = &m->devinfo;
		d->mixer_class = AZ_CLASS_OUTPUT;
		snprintf(d->label.name, sizeof(d->label.name),
		    "%s", AudioNmaster);
		d->type = AUDIO_MIXER_VALUE;
		d->un.v.num_channels = 2;
		d->un.v.delta = 8;
		this->nmixers++;
		d->next = this->nmixers;

		/* mute */
		m = &this->mixers[this->nmixers];
		m->nid = this->playvols.master;
		m->target = MI_TARGET_PLAYVOL;
		d = &m->devinfo;
		d->prev = this->nmixers - 1;
		d->mixer_class = AZ_CLASS_OUTPUT;
		snprintf(d->label.name, sizeof(d->label.name),
		    "%s", AudioNmute);
		azalia_devinfo_offon(d);
		this->nmixers++;
		d->next = this->nmixers;

		/* slaves */
		m = &this->mixers[this->nmixers];
		m->nid = this->playvols.master;
		m->target = MI_TARGET_PLAYVOL;
		d = &m->devinfo;
		d->prev = this->nmixers - 1;
		d->mixer_class = AZ_CLASS_OUTPUT;
		snprintf(d->label.name, sizeof(d->label.name),
		    "%s", "slaves");
		d->type = AUDIO_MIXER_SET;
		for (i = 0, j = 0; i < this->playvols.nslaves; i++) {
			ww = &this->w[this->playvols.slaves[i]];
			d->un.s.member[j].mask = (1 << i);
			strlcpy(d->un.s.member[j++].label.name, ww->name,
			    MAX_AUDIO_DEV_LEN);
		}
		d->un.s.num_mem = j;
		this->nmixers++;
	}

	/* recording volume group */
	if (this->recvols.nslaves > 0) {
		mixer_devinfo_t *d;
		err = azalia_mixer_ensure_capacity(this,
		    this->nmixers + 3);

		/* volume */
		m = &this->mixers[this->nmixers];
		m->nid = this->recvols.master;
		m->target = MI_TARGET_RECVOL;
		d = &m->devinfo;
		d->mixer_class = AZ_CLASS_RECORD;
		snprintf(d->label.name, sizeof(d->label.name),
		    "%s", AudioNvolume);
		d->type = AUDIO_MIXER_VALUE;
		d->un.v.num_channels = 2;
		d->un.v.delta = 8;
		this->nmixers++;
		d->next = this->nmixers;

		/* mute */
		m = &this->mixers[this->nmixers];
		m->nid = this->recvols.master;
		m->target = MI_TARGET_RECVOL;
		d = &m->devinfo;
		d->prev = this->nmixers - 1;
		d->mixer_class = AZ_CLASS_RECORD;
		snprintf(d->label.name, sizeof(d->label.name),
		    "%s", AudioNmute);
		azalia_devinfo_offon(d);
		this->nmixers++;
		d->next = this->nmixers;

		/* slaves */
		m = &this->mixers[this->nmixers];
		m->nid = this->recvols.master;
		m->target = MI_TARGET_RECVOL;
		d = &m->devinfo;
		d->prev = this->nmixers - 1;
		d->mixer_class = AZ_CLASS_RECORD;
		snprintf(d->label.name, sizeof(d->label.name),
		    "%s", "slaves");
		d->type = AUDIO_MIXER_SET;
		for (i = 0, j = 0; i < this->recvols.nslaves; i++) {
			ww = &this->w[this->recvols.slaves[i]];
			d->un.s.member[j].mask = (1 << i);
			strlcpy(d->un.s.member[j++].label.name, ww->name,
			    MAX_AUDIO_DEV_LEN);
		}
		d->un.s.num_mem = j;
		this->nmixers++;
	}

	/* if the codec has more than one DAC group, the first is analog
	 * and the second is digital.
	 */
	if (this->dacs.ngroups > 1) {
		MIXER_REG_PROLOG;
		strlcpy(d->label.name, AudioNmode, sizeof(d->label.name));
		d->type = AUDIO_MIXER_ENUM;
		d->mixer_class = AZ_CLASS_OUTPUT;
		m->target = MI_TARGET_DAC;
		m->nid = this->audiofunc;
		d->un.e.member[0].ord = 0;
		strlcpy(d->un.e.member[0].label.name, "analog",
		    MAX_AUDIO_DEV_LEN);
		d->un.e.member[1].ord = 1;
		strlcpy(d->un.e.member[1].label.name, "digital",
		    MAX_AUDIO_DEV_LEN);
		d->un.e.num_mem = 2;
		this->nmixers++;
	}

	/* if the codec has more than one ADC group, the first is analog
	 * and the second is digital.
	 */
	if (this->adcs.ngroups > 1) {
		MIXER_REG_PROLOG;
		strlcpy(d->label.name, AudioNmode, sizeof(d->label.name));
		d->type = AUDIO_MIXER_ENUM;
		d->mixer_class = AZ_CLASS_RECORD;
		m->target = MI_TARGET_ADC;
		m->nid = this->audiofunc;
		d->un.e.member[0].ord = 0;
		strlcpy(d->un.e.member[0].label.name, "analog",
		    MAX_AUDIO_DEV_LEN);
		d->un.e.member[1].ord = 1;
		strlcpy(d->un.e.member[1].label.name, "digital",
		    MAX_AUDIO_DEV_LEN);
		d->un.e.num_mem = 2;
		this->nmixers++;
	}

	azalia_mixer_fix_indexes(this);
	azalia_mixer_default(this);
	return 0;
}

void
azalia_devinfo_offon(mixer_devinfo_t *d)
{
	d->type = AUDIO_MIXER_ENUM;
	d->un.e.num_mem = 2;
	d->un.e.member[0].ord = 0;
	strlcpy(d->un.e.member[0].label.name, AudioNoff, MAX_AUDIO_DEV_LEN);
	d->un.e.member[1].ord = 1;
	strlcpy(d->un.e.member[1].label.name, AudioNon, MAX_AUDIO_DEV_LEN);
}

int
azalia_mixer_ensure_capacity(codec_t *this, size_t newsize)
{
	size_t newmax;
	void *newbuf;

	if (this->maxmixers >= newsize)
		return 0;
	newmax = this->maxmixers + 10;
	if (newmax < newsize)
		newmax = newsize;
	newbuf = mallocarray(newmax, sizeof(mixer_item_t), M_DEVBUF,
	    M_NOWAIT | M_ZERO);
	if (newbuf == NULL) {
		printf("%s: out of memory in %s\n", XNAME(this), __func__);
		return ENOMEM;
	}
	bcopy(this->mixers, newbuf, this->maxmixers * sizeof(mixer_item_t));
	free(this->mixers, M_DEVBUF, this->maxmixers * sizeof(mixer_item_t));
	this->mixers = newbuf;
	this->maxmixers = newmax;
	return 0;
}

int
azalia_mixer_fix_indexes(codec_t *this)
{
	int i;
	mixer_devinfo_t *d;

	for (i = 0; i < this->nmixers; i++) {
		d = &this->mixers[i].devinfo;
#ifdef DIAGNOSTIC
		if (d->index != 0 && d->index != i)
			printf("%s: index mismatch %d %d\n", __func__,
			    d->index, i);
#endif
		d->index = i;
		if (d->prev == 0)
			d->prev = AUDIO_MIXER_LAST;
		if (d->next == 0)
			d->next = AUDIO_MIXER_LAST;
	}
	return 0;
}

int
azalia_mixer_default(codec_t *this)
{
	widget_t *w;
	mixer_item_t *m;
	mixer_ctrl_t mc;
	int i, j, tgt, cap, err;

	/* unmute all */
	for (i = 0; i < this->nmixers; i++) {
		m = &this->mixers[i];
		if (!IS_MI_TARGET_INAMP(m->target) &&
		    m->target != MI_TARGET_OUTAMP)
			continue;
		if (m->devinfo.type != AUDIO_MIXER_ENUM)
			continue;
		bzero(&mc, sizeof(mc));
		mc.dev = i;
		mc.type = AUDIO_MIXER_ENUM;
		azalia_mixer_set(this, m->nid, m->target, &mc);
	}

	/* set unextreme volume */
	for (i = 0; i < this->nmixers; i++) {
		m = &this->mixers[i];
		if (!IS_MI_TARGET_INAMP(m->target) &&
		    m->target != MI_TARGET_OUTAMP)
			continue;
		if (m->devinfo.type != AUDIO_MIXER_VALUE)
			continue;
		bzero(&mc, sizeof(mc));
		mc.dev = i;
		mc.type = AUDIO_MIXER_VALUE;
		mc.un.value.num_channels = 1;
		mc.un.value.level[0] = AUDIO_MAX_GAIN / 2;
		if (WIDGET_CHANNELS(&this->w[m->nid]) == 2) {
			mc.un.value.num_channels = 2;
			mc.un.value.level[1] = mc.un.value.level[0];
		}
		azalia_mixer_set(this, m->nid, m->target, &mc);
	}

	/* unmute all */
	for (i = 0; i < this->nmixers; i++) {
		m = &this->mixers[i];
		if (m->target != MI_TARGET_MUTESET)
			continue;
		if (m->devinfo.type != AUDIO_MIXER_SET)
			continue;
		bzero(&mc, sizeof(mc));
		mc.dev = i;
		mc.type = AUDIO_MIXER_SET;
		if (!azalia_widget_enabled(this, m->nid)) {
			DPRINTF(("%s: invalid set nid\n", __func__));
			return EINVAL;
		}
		w = &this->w[m->nid];
		for (j = 0; j < w->nconnections; j++) {
			if (!azalia_widget_enabled(this, w->connections[j]))
				continue;
			if (w->nid == this->input_mixer &&
			    w->connections[j] == this->mic)
				continue;
			mc.un.mask |= 1 << j;
		}
		azalia_mixer_set(this, m->nid, m->target, &mc);
	}

	/* make sure default connection is valid */
	for (i = 0; i < this->nmixers; i++) {
		m = &this->mixers[i];
		if (m->target != MI_TARGET_CONNLIST)
			continue;

		azalia_mixer_get(this, m->nid, m->target, &mc);
		for (j = 0; j < m->devinfo.un.e.num_mem; j++) {
			if (mc.un.ord == m->devinfo.un.e.member[j].ord)
				break;
		}
		if (j >= m->devinfo.un.e.num_mem) {
			bzero(&mc, sizeof(mc));
			mc.dev = i;
			mc.type = AUDIO_MIXER_ENUM;
			mc.un.ord = m->devinfo.un.e.member[0].ord;
		}
		azalia_mixer_set(this, m->nid, m->target, &mc);
	}

	/* get default value for play group master */
	for (i = 0; i < this->playvols.nslaves; i++) {
		if (!(this->playvols.cur & (1 << i)))
 			continue;
		w = &this->w[this->playvols.slaves[i]];
		if (!(COP_AMPCAP_NUMSTEPS(w->outamp_cap)))
			continue;
		mc.type = AUDIO_MIXER_VALUE;
		tgt = MI_TARGET_OUTAMP;
		azalia_mixer_get(this, w->nid, tgt, &mc);
		this->playvols.vol_l = mc.un.value.level[0];
		this->playvols.vol_r = mc.un.value.level[0];
		break;
 	}
	this->playvols.mute = 0;
 
	/* get default value for record group master */
	for (i = 0; i < this->recvols.nslaves; i++) {
		if (!(this->recvols.cur & (1 << i)))
			continue;
		w = &this->w[this->recvols.slaves[i]];
		mc.type = AUDIO_MIXER_VALUE;
		tgt = MI_TARGET_OUTAMP;
		cap = w->outamp_cap;
		if (w->type == COP_AWTYPE_PIN_COMPLEX ||
		    w->type == COP_AWTYPE_AUDIO_INPUT) {
			tgt = 0;
			cap = w->inamp_cap;
 		}
		if (!(COP_AMPCAP_NUMSTEPS(cap)))
			continue;
		azalia_mixer_get(this, w->nid, tgt, &mc);
		this->recvols.vol_l = mc.un.value.level[0];
		this->recvols.vol_r = mc.un.value.level[0];
		break;
 	}
	this->recvols.mute = 0;

	err = azalia_codec_enable_unsol(this);
	if (err)
		return(err);

	return 0;
}

int
azalia_codec_enable_unsol(codec_t *this)
{
	widget_t *w;
	uint32_t result;
	int i, err;

	/* jack sense */
	for (i = 0; i < this->nsense_pins; i++) {
		if (this->spkr_muters & (1 << i)) {
			azalia_comresp(this, this->sense_pins[i],
			    CORB_SET_UNSOLICITED_RESPONSE,
			    CORB_UNSOL_ENABLE | AZ_TAG_SPKR, NULL);
		}
	}
	if (this->spkr_muters != 0)
		azalia_unsol_event(this, AZ_TAG_SPKR);

	/* volume knob */
	if (this->playvols.master != this->audiofunc) {

		w = &this->w[this->playvols.master];
		err = azalia_comresp(this, w->nid, CORB_GET_VOLUME_KNOB,
		    0, &result);
		if (err) {
			DPRINTF(("%s: get volume knob error\n", __func__));
			return err;
		}

		/* current level */
		this->playvols.hw_step = CORB_VKNOB_VOLUME(result);
		this->playvols.hw_nsteps = COP_VKCAP_NUMSTEPS(w->d.volume.cap);

		/* indirect mode */
		result &= ~(CORB_VKNOB_DIRECT);
		err = azalia_comresp(this, w->nid, CORB_SET_VOLUME_KNOB,
		    result, NULL);
		if (err) {
			DPRINTF(("%s: set volume knob error\n", __func__));
			/* XXX If there was an error setting indirect
			 * mode, do not return an error.  However, do not
			 * enable unsolicited responses either.  Most
			 * likely the volume knob doesn't work right.
			 * Perhaps it's simply not wired/enabled.
			 */
			return 0;
		}

		/* enable unsolicited responses */
		result = CORB_UNSOL_ENABLE | AZ_TAG_PLAYVOL;
		err = azalia_comresp(this, w->nid,
		    CORB_SET_UNSOLICITED_RESPONSE, result, NULL);
		if (err) {
			DPRINTF(("%s: set vknob unsol resp error\n", __func__));
			return err;
		}
	}

	return 0;
}

int
azalia_mixer_delete(codec_t *this)
{
	if (this->mixers != NULL) {
		free(this->mixers, M_DEVBUF, 0);
		this->mixers = NULL;
	}
	return 0;
}

/**
 * @param mc	mc->type must be set by the caller before the call
 */
int
azalia_mixer_get(const codec_t *this, nid_t nid, int target,
    mixer_ctrl_t *mc)
{
	uint32_t result, cap, value;
	nid_t n;
	int i, err;

	if (mc->type == AUDIO_MIXER_CLASS) {
		return(0);
	}

	/* inamp mute */
	else if (IS_MI_TARGET_INAMP(target) && mc->type == AUDIO_MIXER_ENUM) {
		err = azalia_comresp(this, nid, CORB_GET_AMPLIFIER_GAIN_MUTE,
		    CORB_GAGM_INPUT | CORB_GAGM_LEFT |
		    MI_TARGET_INAMP(target), &result);
		if (err)
			return err;
		mc->un.ord = result & CORB_GAGM_MUTE ? 1 : 0;
	}

	/* inamp gain */
	else if (IS_MI_TARGET_INAMP(target) && mc->type == AUDIO_MIXER_VALUE) {
		err = azalia_comresp(this, nid, CORB_GET_AMPLIFIER_GAIN_MUTE,
		      CORB_GAGM_INPUT | CORB_GAGM_LEFT |
		      MI_TARGET_INAMP(target), &result);
		if (err)
			return err;
		mc->un.value.level[0] = azalia_mixer_from_device_value(this,
		    nid, target, CORB_GAGM_GAIN(result));
		if (this->w[nid].type == COP_AWTYPE_AUDIO_SELECTOR ||
		    this->w[nid].type == COP_AWTYPE_AUDIO_MIXER) {
			n = this->w[nid].connections[MI_TARGET_INAMP(target)];
			if (!azalia_widget_enabled(this, n)) {
				DPRINTF(("%s: nid %2.2x invalid index %d\n",
				   __func__, nid,  MI_TARGET_INAMP(target)));
				n = nid;
			}
		} else
			n = nid;
		mc->un.value.num_channels = WIDGET_CHANNELS(&this->w[n]);
		if (mc->un.value.num_channels == 2) {
			err = azalia_comresp(this, nid,
			    CORB_GET_AMPLIFIER_GAIN_MUTE, CORB_GAGM_INPUT |
			    CORB_GAGM_RIGHT | MI_TARGET_INAMP(target),
			    &result);
			if (err)
				return err;
			mc->un.value.level[1] = azalia_mixer_from_device_value
			    (this, nid, target, CORB_GAGM_GAIN(result));
		}
	}

	/* outamp mute */
	else if (target == MI_TARGET_OUTAMP && mc->type == AUDIO_MIXER_ENUM) {
		err = azalia_comresp(this, nid, CORB_GET_AMPLIFIER_GAIN_MUTE,
		    CORB_GAGM_OUTPUT | CORB_GAGM_LEFT | 0, &result);
		if (err)
			return err;
		mc->un.ord = result & CORB_GAGM_MUTE ? 1 : 0;
	}

	/* outamp gain */
	else if (target == MI_TARGET_OUTAMP && mc->type == AUDIO_MIXER_VALUE) {
		err = azalia_comresp(this, nid, CORB_GET_AMPLIFIER_GAIN_MUTE,
		      CORB_GAGM_OUTPUT | CORB_GAGM_LEFT | 0, &result);
		if (err)
			return err;
		mc->un.value.level[0] = azalia_mixer_from_device_value(this,
		    nid, target, CORB_GAGM_GAIN(result));
		mc->un.value.num_channels = WIDGET_CHANNELS(&this->w[nid]);
		if (mc->un.value.num_channels == 2) {
			err = azalia_comresp(this, nid,
			    CORB_GET_AMPLIFIER_GAIN_MUTE,
			    CORB_GAGM_OUTPUT | CORB_GAGM_RIGHT | 0, &result);
			if (err)
				return err;
			mc->un.value.level[1] = azalia_mixer_from_device_value
			    (this, nid, target, CORB_GAGM_GAIN(result));
		}
	}

	/* selection */
	else if (target == MI_TARGET_CONNLIST) {
		err = azalia_comresp(this, nid,
		    CORB_GET_CONNECTION_SELECT_CONTROL, 0, &result);
		if (err)
			return err;
		result = CORB_CSC_INDEX(result);
		if (!azalia_widget_enabled(this,
		    this->w[nid].connections[result]))
			mc->un.ord = -1;
		else
			mc->un.ord = result;
	}

	/* pin I/O */
	else if (target == MI_TARGET_PINDIR) {
		err = azalia_comresp(this, nid,
		    CORB_GET_PIN_WIDGET_CONTROL, 0, &result);
		if (err)
			return err;

		value = result;
		if (!(result & (CORB_PWC_INPUT | CORB_PWC_OUTPUT)))
			mc->un.ord = 0;
		else if (result & CORB_PWC_OUTPUT)
			mc->un.ord = 1;
		else {
			cap = COP_PINCAP_VREF(this->w[nid].d.pin.cap);
			result &= CORB_PWC_VREF_MASK;
			if (result == CORB_PWC_VREF_GND)
				mc->un.ord = 3;
			else if (result == CORB_PWC_VREF_50)
				mc->un.ord = 4;
			else if (result == CORB_PWC_VREF_80)
				mc->un.ord = 5;
			else if (result == CORB_PWC_VREF_100)
				mc->un.ord = 6;
			else
				mc->un.ord = 2;
		}
	}

	/* pin headphone-boost */
	else if (target == MI_TARGET_PINBOOST) {
		err = azalia_comresp(this, nid,
		    CORB_GET_PIN_WIDGET_CONTROL, 0, &result);
		if (err)
			return err;
		mc->un.ord = result & CORB_PWC_HEADPHONE ? 1 : 0;
	}

	/* DAC group selection */
	else if (target == MI_TARGET_DAC) {
		mc->un.ord = this->dacs.cur;
	}

	/* ADC selection */
	else if (target == MI_TARGET_ADC) {
		mc->un.ord = this->adcs.cur;
	}

	/* S/PDIF */
	else if (target == MI_TARGET_SPDIF) {
		err = azalia_comresp(this, nid, CORB_GET_DIGITAL_CONTROL,
		    0, &result);
		if (err)
			return err;
		mc->un.mask = result & 0xff & ~(CORB_DCC_DIGEN | CORB_DCC_NAUDIO);
	} else if (target == MI_TARGET_SPDIF_CC) {
		err = azalia_comresp(this, nid, CORB_GET_DIGITAL_CONTROL,
		    0, &result);
		if (err)
			return err;
		mc->un.value.num_channels = 1;
		mc->un.value.level[0] = CORB_DCC_CC(result);
	}

	/* EAPD */
	else if (target == MI_TARGET_EAPD) {
		err = azalia_comresp(this, nid, CORB_GET_EAPD_BTL_ENABLE,
		    0, &result);
		if (err)
			return err;
		mc->un.ord = result & CORB_EAPD_EAPD ? 1 : 0;
	}

	/* sense pin */
	else if (target == MI_TARGET_PINSENSE) {
		err = azalia_comresp(this, nid, CORB_GET_PIN_SENSE,
		    0, &result);
		if (err)
			return err;
		mc->un.ord = result & CORB_PS_PRESENCE ? 1 : 0;
	}

	/* mute set */
	else if (target == MI_TARGET_MUTESET && mc->type == AUDIO_MIXER_SET) {
		const widget_t *w;

		if (!azalia_widget_enabled(this, nid)) {
			DPRINTF(("%s: invalid muteset nid\n", XNAME(this)));
			return EINVAL;
		}
		w = &this->w[nid];
		mc->un.mask = 0;
		for (i = 0; i < w->nconnections; i++) {
			if (!azalia_widget_enabled(this, w->connections[i]))
				continue;
			err = azalia_comresp(this, nid,
			    CORB_GET_AMPLIFIER_GAIN_MUTE,
			    CORB_GAGM_INPUT | CORB_GAGM_LEFT |
			    MI_TARGET_INAMP(i), &result);
			if (err)
				return err;
			mc->un.mask |= (result & CORB_GAGM_MUTE) ? 0 : (1 << i);
		}
	}

	/* mixer set - show all connections */
	else if (target == MI_TARGET_MIXERSET && mc->type == AUDIO_MIXER_SET) {
		const widget_t *w;

		if (!azalia_widget_enabled(this, nid)) {
			DPRINTF(("%s: invalid mixerset nid\n", XNAME(this)));
			return EINVAL;
		}
		w = &this->w[nid];
		mc->un.mask = 0;
		for (i = 0; i < w->nconnections; i++) {
			if (!azalia_widget_enabled(this, w->connections[i]))
				continue;
			mc->un.mask |= (1 << i);
		}
	}

	else if (target == MI_TARGET_SENSESET && mc->type == AUDIO_MIXER_SET) {

		if (nid == this->speaker) {
			mc->un.mask = this->spkr_muters;
		} else {
			DPRINTF(("%s: invalid senseset nid\n", XNAME(this)));
			return EINVAL;
		}
	}

	else if (target == MI_TARGET_PLAYVOL) {

		if (mc->type == AUDIO_MIXER_VALUE) {
			mc->un.value.num_channels = 2;
			mc->un.value.level[0] = this->playvols.vol_l;
			mc->un.value.level[1] = this->playvols.vol_r;

		} else if (mc->type == AUDIO_MIXER_ENUM) {
			mc->un.ord = this->playvols.mute;

		} else if (mc->type == AUDIO_MIXER_SET) {
			mc->un.mask = this->playvols.cur;

		} else {
			DPRINTF(("%s: invalid outmaster mixer type\n",
				XNAME(this)));
			return EINVAL;
		}
	}

	else if (target == MI_TARGET_RECVOL) {

		if (mc->type == AUDIO_MIXER_VALUE) {
			mc->un.value.num_channels = 2;
			mc->un.value.level[0] = this->recvols.vol_l;
			mc->un.value.level[1] = this->recvols.vol_r;

		} else if (mc->type == AUDIO_MIXER_ENUM) {
			mc->un.ord = this->recvols.mute;

		} else if (mc->type == AUDIO_MIXER_SET) {
			mc->un.mask = this->recvols.cur;

		} else {
			DPRINTF(("%s: invalid inmaster mixer type\n",
				XNAME(this)));
			return EINVAL;
		}
	}

	else {
		DPRINTF(("%s: internal error in %s: target=%x\n",
		    XNAME(this), __func__, target));
		return -1;
	}
	return 0;
}

int
azalia_mixer_set(codec_t *this, nid_t nid, int target, const mixer_ctrl_t *mc)
{
	uint32_t result, value;
	int i, err;

	if (mc->type == AUDIO_MIXER_CLASS) {
		return(0);
	}

	/* inamp mute */
	else if (IS_MI_TARGET_INAMP(target) && mc->type == AUDIO_MIXER_ENUM) {
		/* set stereo mute separately to keep each gain value */
		err = azalia_comresp(this, nid, CORB_GET_AMPLIFIER_GAIN_MUTE,
		    CORB_GAGM_INPUT | CORB_GAGM_LEFT |
		    MI_TARGET_INAMP(target), &result);
		if (err)
			return err;
		value = CORB_AGM_INPUT | CORB_AGM_LEFT |
		    (target << CORB_AGM_INDEX_SHIFT) |
		    CORB_GAGM_GAIN(result);
		if (mc->un.ord)
			value |= CORB_AGM_MUTE;
		err = azalia_comresp(this, nid, CORB_SET_AMPLIFIER_GAIN_MUTE,
		    value, &result);
		if (err)
			return err;
		if (WIDGET_CHANNELS(&this->w[nid]) == 2) {
			err = azalia_comresp(this, nid,
			    CORB_GET_AMPLIFIER_GAIN_MUTE, CORB_GAGM_INPUT |
			    CORB_GAGM_RIGHT | MI_TARGET_INAMP(target),
			    &result);
			if (err)
				return err;
			value = CORB_AGM_INPUT | CORB_AGM_RIGHT |
			    (target << CORB_AGM_INDEX_SHIFT) |
			    CORB_GAGM_GAIN(result);
			if (mc->un.ord)
				value |= CORB_AGM_MUTE;
			err = azalia_comresp(this, nid,
			    CORB_SET_AMPLIFIER_GAIN_MUTE, value, &result);
			if (err)
				return err;
		}
	}

	/* inamp gain */
	else if (IS_MI_TARGET_INAMP(target) && mc->type == AUDIO_MIXER_VALUE) {
		if (mc->un.value.num_channels < 1)
			return EINVAL;
		err = azalia_comresp(this, nid, CORB_GET_AMPLIFIER_GAIN_MUTE,
		      CORB_GAGM_INPUT | CORB_GAGM_LEFT |
		      MI_TARGET_INAMP(target), &result);
		if (err)
			return err;
		value = azalia_mixer_to_device_value(this, nid, target,
		    mc->un.value.level[0]);
		value = CORB_AGM_INPUT | CORB_AGM_LEFT |
		    (target << CORB_AGM_INDEX_SHIFT) |
		    (result & CORB_GAGM_MUTE ? CORB_AGM_MUTE : 0) |
		    (value & CORB_AGM_GAIN_MASK);
		err = azalia_comresp(this, nid, CORB_SET_AMPLIFIER_GAIN_MUTE,
		    value, &result);
		if (err)
			return err;
		if (mc->un.value.num_channels >= 2 &&
		    WIDGET_CHANNELS(&this->w[nid]) == 2) {
			err = azalia_comresp(this, nid,
			      CORB_GET_AMPLIFIER_GAIN_MUTE, CORB_GAGM_INPUT |
			      CORB_GAGM_RIGHT | MI_TARGET_INAMP(target),
			      &result);
			if (err)
				return err;
			value = azalia_mixer_to_device_value(this, nid, target,
			    mc->un.value.level[1]);
			value = CORB_AGM_INPUT | CORB_AGM_RIGHT |
			    (target << CORB_AGM_INDEX_SHIFT) |
			    (result & CORB_GAGM_MUTE ? CORB_AGM_MUTE : 0) |
			    (value & CORB_AGM_GAIN_MASK);
			err = azalia_comresp(this, nid,
			    CORB_SET_AMPLIFIER_GAIN_MUTE, value, &result);
			if (err)
				return err;
		}
	}

	/* outamp mute */
	else if (target == MI_TARGET_OUTAMP && mc->type == AUDIO_MIXER_ENUM) {
		err = azalia_comresp(this, nid, CORB_GET_AMPLIFIER_GAIN_MUTE,
		    CORB_GAGM_OUTPUT | CORB_GAGM_LEFT, &result);
		if (err)
			return err;
		value = CORB_AGM_OUTPUT | CORB_AGM_LEFT | CORB_GAGM_GAIN(result);
		if (mc->un.ord)
			value |= CORB_AGM_MUTE;
		err = azalia_comresp(this, nid, CORB_SET_AMPLIFIER_GAIN_MUTE,
		    value, &result);
		if (err)
			return err;
		if (WIDGET_CHANNELS(&this->w[nid]) == 2) {
			err = azalia_comresp(this, nid,
			    CORB_GET_AMPLIFIER_GAIN_MUTE,
			    CORB_GAGM_OUTPUT | CORB_GAGM_RIGHT, &result);
			if (err)
				return err;
			value = CORB_AGM_OUTPUT | CORB_AGM_RIGHT |
			    CORB_GAGM_GAIN(result);
			if (mc->un.ord)
				value |= CORB_AGM_MUTE;
			err = azalia_comresp(this, nid,
			    CORB_SET_AMPLIFIER_GAIN_MUTE, value, &result);
			if (err)
				return err;
		}
	}

	/* outamp gain */
	else if (target == MI_TARGET_OUTAMP && mc->type == AUDIO_MIXER_VALUE) {
		if (mc->un.value.num_channels < 1)
			return EINVAL;
		err = azalia_comresp(this, nid, CORB_GET_AMPLIFIER_GAIN_MUTE,
		      CORB_GAGM_OUTPUT | CORB_GAGM_LEFT, &result);
		if (err)
			return err;
		value = azalia_mixer_to_device_value(this, nid, target,
		    mc->un.value.level[0]);
		value = CORB_AGM_OUTPUT | CORB_AGM_LEFT |
		    (result & CORB_GAGM_MUTE ? CORB_AGM_MUTE : 0) |
		    (value & CORB_AGM_GAIN_MASK);
		err = azalia_comresp(this, nid, CORB_SET_AMPLIFIER_GAIN_MUTE,
		    value, &result);
		if (err)
			return err;
		if (mc->un.value.num_channels >= 2 &&
		    WIDGET_CHANNELS(&this->w[nid]) == 2) {
			err = azalia_comresp(this, nid,
			      CORB_GET_AMPLIFIER_GAIN_MUTE, CORB_GAGM_OUTPUT |
			      CORB_GAGM_RIGHT, &result);
			if (err)
				return err;
			value = azalia_mixer_to_device_value(this, nid, target,
			    mc->un.value.level[1]);
			value = CORB_AGM_OUTPUT | CORB_AGM_RIGHT |
			    (result & CORB_GAGM_MUTE ? CORB_AGM_MUTE : 0) |
			    (value & CORB_AGM_GAIN_MASK);
			err = azalia_comresp(this, nid,
			    CORB_SET_AMPLIFIER_GAIN_MUTE, value, &result);
			if (err)
				return err;
		}
	}

	/* selection */
	else if (target == MI_TARGET_CONNLIST) {
		if (mc->un.ord < 0 ||
		    mc->un.ord >= this->w[nid].nconnections ||
		    !azalia_widget_enabled(this,
		    this->w[nid].connections[mc->un.ord]))
			return EINVAL;
		err = azalia_comresp(this, nid,
		    CORB_SET_CONNECTION_SELECT_CONTROL, mc->un.ord, &result);
		if (err)
			return err;
	}

	/* pin I/O */
	else if (target == MI_TARGET_PINDIR) {

		err = azalia_comresp(this, nid,
		    CORB_GET_PIN_WIDGET_CONTROL, 0, &result);
		if (err)
			return err;

		value = result;
		value &= ~(CORB_PWC_VREF_MASK);
		if (mc->un.ord == 0) {
			value &= ~(CORB_PWC_OUTPUT | CORB_PWC_INPUT);
		} else if (mc->un.ord == 1) {
			value &= ~CORB_PWC_INPUT;
			value |= CORB_PWC_OUTPUT;
			if (this->qrks & AZ_QRK_WID_OVREF50)
				value |= CORB_PWC_VREF_50;
		} else {
			value &= ~CORB_PWC_OUTPUT;
			value |= CORB_PWC_INPUT;

			if (mc->un.ord == 3)
				value |= CORB_PWC_VREF_GND;
			if (mc->un.ord == 4)
				value |= CORB_PWC_VREF_50;
			if (mc->un.ord == 5)
				value |= CORB_PWC_VREF_80;
			if (mc->un.ord == 6)
				value |= CORB_PWC_VREF_100;
		}
		err = azalia_comresp(this, nid,
		    CORB_SET_PIN_WIDGET_CONTROL, value, &result);
		if (err)
			return err;

		/* Run the unsolicited response handler for speaker mute
		 * since it depends on pin direction.
		 */
		for (i = 0; i < this->nsense_pins; i++) {
			if (this->sense_pins[i] == nid)
				break;
		}
		if (i < this->nsense_pins) {
			azalia_unsol_event(this, AZ_TAG_SPKR);
		}
	}

	/* pin headphone-boost */
	else if (target == MI_TARGET_PINBOOST) {
		if (mc->un.ord >= 2)
			return EINVAL;
		err = azalia_comresp(this, nid,
		    CORB_GET_PIN_WIDGET_CONTROL, 0, &result);
		if (err)
			return err;
		if (mc->un.ord == 0) {
			result &= ~CORB_PWC_HEADPHONE;
		} else {
			result |= CORB_PWC_HEADPHONE;
		}
		err = azalia_comresp(this, nid,
		    CORB_SET_PIN_WIDGET_CONTROL, result, &result);
		if (err)
			return err;
	}

	/* DAC group selection */
	else if (target == MI_TARGET_DAC) {
		if (this->running)
			return EBUSY;
		if (mc->un.ord >= this->dacs.ngroups)
			return EINVAL;
		if (mc->un.ord != this->dacs.cur)
			return azalia_codec_construct_format(this,
			    mc->un.ord, this->adcs.cur);
		else
			return 0;
	}

	/* ADC selection */
	else if (target == MI_TARGET_ADC) {
		if (this->running)
			return EBUSY;
		if (mc->un.ord >= this->adcs.ngroups)
			return EINVAL;
		if (mc->un.ord != this->adcs.cur)
			return azalia_codec_construct_format(this,
			    this->dacs.cur, mc->un.ord);
		else
			return 0;
	}

	/* S/PDIF */
	else if (target == MI_TARGET_SPDIF) {
		err = azalia_comresp(this, nid, CORB_GET_DIGITAL_CONTROL,
		    0, &result);
		result &= CORB_DCC_DIGEN | CORB_DCC_NAUDIO;
		result |= mc->un.mask & 0xff & ~CORB_DCC_DIGEN;
		err = azalia_comresp(this, nid, CORB_SET_DIGITAL_CONTROL_L,
		    result, NULL);
		if (err)
			return err;
	} else if (target == MI_TARGET_SPDIF_CC) {
		if (mc->un.value.num_channels != 1)
			return EINVAL;
		if (mc->un.value.level[0] > 127)
			return EINVAL;
		err = azalia_comresp(this, nid, CORB_SET_DIGITAL_CONTROL_H,
		    mc->un.value.level[0], NULL);
		if (err)
			return err;
	}

	/* EAPD */
	else if (target == MI_TARGET_EAPD) {
		if (mc->un.ord >= 2)
			return EINVAL;
		err = azalia_comresp(this, nid,
		    CORB_GET_EAPD_BTL_ENABLE, 0, &result);
		if (err)
			return err;
		result &= 0xff;
		if (mc->un.ord == 0) {
			result &= ~CORB_EAPD_EAPD;
		} else {
			result |= CORB_EAPD_EAPD;
		}
		err = azalia_comresp(this, nid,
		    CORB_SET_EAPD_BTL_ENABLE, result, &result);
		if (err)
			return err;
	}

	else if (target == MI_TARGET_PINSENSE) {
		/* do nothing, control is read only */
	}

	else if (target == MI_TARGET_MUTESET && mc->type == AUDIO_MIXER_SET) {
		const widget_t *w;

		if (!azalia_widget_enabled(this, nid)) {
			DPRINTF(("%s: invalid muteset nid\n", XNAME(this)));
			return EINVAL;
		}
		w = &this->w[nid];
		for (i = 0; i < w->nconnections; i++) {
			if (!azalia_widget_enabled(this, w->connections[i]))
				continue;

			/* We have to set stereo mute separately
			 * to keep each gain value.
			 */
			err = azalia_comresp(this, nid,
			    CORB_GET_AMPLIFIER_GAIN_MUTE,
			    CORB_GAGM_INPUT | CORB_GAGM_LEFT |
			    MI_TARGET_INAMP(i), &result);
			if (err)
				return err;
			value = CORB_AGM_INPUT | CORB_AGM_LEFT |
			    (i << CORB_AGM_INDEX_SHIFT) |
			    CORB_GAGM_GAIN(result);
			if ((mc->un.mask & (1 << i)) == 0)
				value |= CORB_AGM_MUTE;
			err = azalia_comresp(this, nid,
			    CORB_SET_AMPLIFIER_GAIN_MUTE, value, &result);
			if (err)
				return err;

			if (WIDGET_CHANNELS(w) == 2) {
				err = azalia_comresp(this, nid,
				    CORB_GET_AMPLIFIER_GAIN_MUTE,
				    CORB_GAGM_INPUT | CORB_GAGM_RIGHT |
				    MI_TARGET_INAMP(i), &result);
				if (err)
					return err;
				value = CORB_AGM_INPUT | CORB_AGM_RIGHT |
				    (i << CORB_AGM_INDEX_SHIFT) |
				    CORB_GAGM_GAIN(result);
				if ((mc->un.mask & (1 << i)) == 0)
					value |= CORB_AGM_MUTE;
				err = azalia_comresp(this, nid,
				    CORB_SET_AMPLIFIER_GAIN_MUTE,
				    value, &result);
				if (err)
					return err;
			}
		}
	}

	else if (target == MI_TARGET_MIXERSET && mc->type == AUDIO_MIXER_SET) {
		/* do nothing, control is read only */
	}

	else if (target == MI_TARGET_SENSESET && mc->type == AUDIO_MIXER_SET) {

		if (nid == this->speaker) {
			this->spkr_muters = mc->un.mask;
			azalia_unsol_event(this, AZ_TAG_SPKR);
		} else {
			DPRINTF(("%s: invalid senseset nid\n", XNAME(this)));
			return EINVAL;
		}
	}

	else if (target == MI_TARGET_PLAYVOL) {

		const widget_t *w;
		mixer_ctrl_t mc2;

		if (mc->type == AUDIO_MIXER_VALUE) {
			if (mc->un.value.num_channels != 2)
				return EINVAL;
			this->playvols.vol_l = mc->un.value.level[0];
			this->playvols.vol_r = mc->un.value.level[1];
			for (i = 0; i < this->playvols.nslaves; i++) {
				if (!(this->playvols.cur & (1 << i)))
					continue;
				w = &this->w[this->playvols.slaves[i]];
				if (!(COP_AMPCAP_NUMSTEPS(w->outamp_cap)))
					continue;

				/* don't change volume if muted */
				if (w->outamp_cap & COP_AMPCAP_MUTE) {
					mc2.type = AUDIO_MIXER_ENUM;
					azalia_mixer_get(this, w->nid,
					    MI_TARGET_OUTAMP, &mc2);
					if (mc2.un.ord)
						continue;
				}
				mc2.type = AUDIO_MIXER_VALUE;
				mc2.un.value.num_channels = WIDGET_CHANNELS(w);
				mc2.un.value.level[0] = this->playvols.vol_l;
				mc2.un.value.level[1] = this->playvols.vol_r;
				err = azalia_mixer_set(this, w->nid,
				    MI_TARGET_OUTAMP, &mc2);
				if (err) {
					DPRINTF(("%s: out slave %2.2x vol\n",
					    __func__, w->nid));
					return err;
				}
			}
		} else if (mc->type == AUDIO_MIXER_ENUM) {
			if (mc->un.ord != 0 && mc->un.ord != 1)
				return EINVAL;
			this->playvols.mute = mc->un.ord;
			for (i = 0; i < this->playvols.nslaves; i++) {
				if (!(this->playvols.cur & (1 << i)))
					continue;
				w = &this->w[this->playvols.slaves[i]];
				if (!(w->outamp_cap & COP_AMPCAP_MUTE))
					continue;
				if (this->spkr_muted == 1 &&
				    ((this->spkr_mute_method ==
				    AZ_SPKR_MUTE_SPKR_MUTE &&
				    (w->nid == this->speaker ||
				    w->nid == this->speaker2)) ||
				    (this->spkr_mute_method ==
				    AZ_SPKR_MUTE_DAC_MUTE &&
				    w->nid == this->spkr_dac))) {
					continue;
				}
				mc2.type = AUDIO_MIXER_ENUM;
				mc2.un.ord = this->playvols.mute;
				err = azalia_mixer_set(this, w->nid,
				    MI_TARGET_OUTAMP, &mc2);
				if (err) {
					DPRINTF(("%s: out slave %2.2x mute\n",
					    __func__, w->nid));
					return err;
				}
			}

		} else if (mc->type == AUDIO_MIXER_SET) {
			this->playvols.cur =
			    (mc->un.mask & this->playvols.mask);

		} else {
			DPRINTF(("%s: invalid output master mixer type\n",
				XNAME(this)));
			return EINVAL;
		}
	}

	else if (target == MI_TARGET_RECVOL) {

		const widget_t *w;
		mixer_ctrl_t mc2;
		uint32_t cap;
		int tgt;

		if (mc->type == AUDIO_MIXER_VALUE) {
			if (mc->un.value.num_channels != 2)
				return EINVAL;
			this->recvols.vol_l = mc->un.value.level[0];
			this->recvols.vol_r = mc->un.value.level[1];
			for (i = 0; i < this->recvols.nslaves; i++) {
				if (!(this->recvols.cur & (1 << i)))
					continue;
				w = &this->w[this->recvols.slaves[i]];
				tgt = MI_TARGET_OUTAMP;
				cap = w->outamp_cap;
				if (w->type == COP_AWTYPE_AUDIO_INPUT ||
				    w->type == COP_AWTYPE_PIN_COMPLEX) {
					tgt = 0;
					cap = w->inamp_cap;
				}
				if (!(COP_AMPCAP_NUMSTEPS(cap)))
					continue;
				mc2.type = AUDIO_MIXER_VALUE;
				mc2.un.value.num_channels = WIDGET_CHANNELS(w);
				mc2.un.value.level[0] = this->recvols.vol_l;
				mc2.un.value.level[1] = this->recvols.vol_r;
				err = azalia_mixer_set(this, w->nid,
				    tgt, &mc2);
				if (err) {
					DPRINTF(("%s: in slave %2.2x vol\n",
					    __func__, w->nid));
					return err;
				}
			}
		} else if (mc->type == AUDIO_MIXER_ENUM) {
			if (mc->un.ord != 0 && mc->un.ord != 1)
				return EINVAL;
			this->recvols.mute = mc->un.ord;
			for (i = 0; i < this->recvols.nslaves; i++) {
				if (!(this->recvols.cur & (1 << i)))
					continue;
				w = &this->w[this->recvols.slaves[i]];
				tgt = MI_TARGET_OUTAMP;
				cap = w->outamp_cap;
				if (w->type == COP_AWTYPE_AUDIO_INPUT ||
				    w->type == COP_AWTYPE_PIN_COMPLEX) {
					tgt = 0;
					cap = w->inamp_cap;
				}
				if (!(cap & COP_AMPCAP_MUTE))
					continue;
				mc2.type = AUDIO_MIXER_ENUM;
				mc2.un.ord = this->recvols.mute;
				err = azalia_mixer_set(this, w->nid,
				    tgt, &mc2);
				if (err) {
					DPRINTF(("%s: out slave %2.2x mute\n",
					    __func__, w->nid));
					return err;
				}
			}

		} else if (mc->type == AUDIO_MIXER_SET) {
			this->recvols.cur = (mc->un.mask & this->recvols.mask);

		} else {
			DPRINTF(("%s: invalid input master mixer type\n",
				XNAME(this)));
			return EINVAL;
		}
	}

	else {
		DPRINTF(("%s: internal error in %s: target=%x\n",
		    XNAME(this), __func__, target));
		return -1;
	}
	return 0;
}

u_char
azalia_mixer_from_device_value(const codec_t *this, nid_t nid, int target,
    uint32_t dv)
{
	uint32_t steps;
	int max_gain, ctloff;

	if (IS_MI_TARGET_INAMP(target)) {
		steps = COP_AMPCAP_NUMSTEPS(this->w[nid].inamp_cap);
		ctloff = COP_AMPCAP_CTLOFF(this->w[nid].inamp_cap);
	} else if (target == MI_TARGET_OUTAMP) {
		steps = COP_AMPCAP_NUMSTEPS(this->w[nid].outamp_cap);
		ctloff = COP_AMPCAP_CTLOFF(this->w[nid].outamp_cap);
	} else {
		DPRINTF(("%s: unknown target: %d\n", __func__, target));
		steps = 255;
		ctloff = 0;
	}
	dv -= ctloff;
	if (dv <= 0 || steps == 0)
		return(AUDIO_MIN_GAIN);
	max_gain = AUDIO_MAX_GAIN - AUDIO_MAX_GAIN % steps;
	if (dv >= steps)
		return(max_gain);
	return(dv * max_gain / steps);
}

uint32_t
azalia_mixer_to_device_value(const codec_t *this, nid_t nid, int target,
    u_char uv)
{
	uint32_t steps;
	int max_gain, ctloff;

	if (IS_MI_TARGET_INAMP(target)) {
		steps = COP_AMPCAP_NUMSTEPS(this->w[nid].inamp_cap);
		ctloff = COP_AMPCAP_CTLOFF(this->w[nid].inamp_cap);
	} else if (target == MI_TARGET_OUTAMP) {
		steps = COP_AMPCAP_NUMSTEPS(this->w[nid].outamp_cap);
		ctloff = COP_AMPCAP_CTLOFF(this->w[nid].outamp_cap);
	} else {
		DPRINTF(("%s: unknown target: %d\n", __func__, target));
		steps = 255;
		ctloff = 0;
	}
	if (uv <= AUDIO_MIN_GAIN || steps == 0)
		return(ctloff);
	max_gain = AUDIO_MAX_GAIN - AUDIO_MAX_GAIN % steps;
	if (uv >= max_gain)
		return(steps + ctloff);
	return(uv * steps / max_gain + ctloff);
}

int
azalia_gpio_unmute(codec_t *this, int pin)
{
	uint32_t data, mask, dir;

	azalia_comresp(this, this->audiofunc, CORB_GET_GPIO_DATA, 0, &data);
	azalia_comresp(this, this->audiofunc, CORB_GET_GPIO_ENABLE_MASK, 0, &mask);
	azalia_comresp(this, this->audiofunc, CORB_GET_GPIO_DIRECTION, 0, &dir);

	data |= 1 << pin;
	mask |= 1 << pin;
	dir |= 1 << pin;

	azalia_comresp(this, this->audiofunc, CORB_SET_GPIO_ENABLE_MASK, mask, NULL);
	azalia_comresp(this, this->audiofunc, CORB_SET_GPIO_DIRECTION, dir, NULL);
	DELAY(1000);
	azalia_comresp(this, this->audiofunc, CORB_SET_GPIO_DATA, data, NULL);

	return 0;
}

void
azalia_ampcap_ov(widget_t *w, int type, int offset, int steps, int size,
   int ctloff, int mute)
{
	uint32_t cap;

	cap = (offset & 0x7f) | ((steps & 0x7f) << 8) |
	    ((size & 0x7f) << 16) | ((ctloff & 0x7f) << 24) |
	    (mute ? COP_AMPCAP_MUTE : 0);  

	if (type == COP_OUTPUT_AMPCAP) {
		w->outamp_cap = cap;
	} else if (type == COP_INPUT_AMPCAP) {
		w->inamp_cap = cap;
	}
}

void
azalia_pin_config_ov(widget_t *w, int mask, int val)
{
	int bits, offset;

	switch (mask) {
	case CORB_CD_DEVICE_MASK:
		bits = CORB_CD_DEVICE_BITS;
		offset = CORB_CD_DEVICE_OFFSET;
		break;
	case CORB_CD_PORT_MASK:
		bits = CORB_CD_PORT_BITS;
		offset = CORB_CD_PORT_OFFSET;
		break;
	default:
		return;
	}
	val &= bits;
	w->d.pin.config &= ~(mask);
	w->d.pin.config |= val << offset;
	if (mask == CORB_CD_DEVICE_MASK)
		w->d.pin.device = val;
}

int
azalia_codec_gpio_quirks(codec_t *this)
{
	if (this->qrks & AZ_QRK_GPIO_POL_0) {
		azalia_comresp(this, this->audiofunc,
		    CORB_SET_GPIO_POLARITY, 0, NULL);
	}
	if (this->qrks & AZ_QRK_GPIO_UNMUTE_0) {
		azalia_gpio_unmute(this, 0);
	}
	if (this->qrks & AZ_QRK_GPIO_UNMUTE_1) {
		azalia_gpio_unmute(this, 1);
	}
	if (this->qrks & AZ_QRK_GPIO_UNMUTE_2) {
		azalia_gpio_unmute(this, 2);
	}
	if (this->qrks & AZ_QRK_GPIO_UNMUTE_3) {
		azalia_gpio_unmute(this, 3);
	}

	return(0);
}

int
azalia_codec_widget_quirks(codec_t *this, nid_t nid)
{
	widget_t *w;

	w = &this->w[nid];

	if (this->qrks & AZ_QRK_WID_BEEP_1D &&
	    nid == 0x1d && w->enable == 0) {
		azalia_pin_config_ov(w, CORB_CD_DEVICE_MASK, CORB_CD_BEEP);
		azalia_pin_config_ov(w, CORB_CD_PORT_MASK, CORB_CD_FIXED);
		w->widgetcap |= COP_AWCAP_STEREO;
		w->enable = 1;
	}

	if (this->qrks & AZ_QRK_WID_TPDOCK1 &&
	    nid == 0x19) {
		/* Thinkpad x230/t430 style dock microphone */
		w->d.pin.config = 0x23a11040;
		w->enable = 1;
	}

	if (this->qrks & AZ_QRK_WID_TPDOCK1 &&
	    nid == 0x1b) {
		/* Thinkpad x230/t430 style dock headphone */
		w->d.pin.config = 0x2121103f;
		w->enable = 1;
	}

	if (this->qrks & AZ_QRK_WID_TPDOCK2 &&
	    nid == 0x16) {
		/* Thinkpad x240/t440 style dock headphone */
		w->d.pin.config = 0x21211010;
		w->enable = 1;
	}

	if (this->qrks & AZ_QRK_WID_TPDOCK2 &&
	    nid == 0x19) {
		/* Thinkpad x240/t440 style dock microphone */
		w->d.pin.config = 0x21a11010;
		w->enable = 1;
	}

	if (this->qrks & AZ_QRK_WID_TPDOCK3 &&
	    nid == 0x1a) {
		/* Thinkpad x220/t420 style dock microphone */
		w->d.pin.config = 0x21a190f0;
		w->enable = 1;
	}

	if (this->qrks & AZ_QRK_WID_TPDOCK3 &&
	    nid == 0x1c) {
		/* Thinkpad x220/t420 style dock headphone */
		w->d.pin.config = 0x212140ff;
		w->enable = 1;
	}

	if (this->qrks & AZ_QRK_WID_CDIN_1C &&
	    nid == 0x1c && w->enable == 0 && w->d.pin.device == CORB_CD_CD) {
		azalia_pin_config_ov(w, CORB_CD_PORT_MASK, CORB_CD_FIXED);
		w->widgetcap |= COP_AWCAP_STEREO;
		w->enable = 1;
	}

	if ((this->qrks & AZ_QRK_WID_AD1981_OAMP) &&
	    ((nid == 0x05) || (nid == 0x06) || (nid == 0x07) ||
	    (nid == 0x09) || (nid == 0x18))) {
		azalia_ampcap_ov(w, COP_OUTPUT_AMPCAP, 31, 33, 6, 30, 1);
	}

	if ((this->qrks & AZ_QRK_WID_CLOSE_PCBEEP) && (nid == 0x20))  {
		/* Close PC beep passthrough to avoid headphone noise */
		azalia_comresp(this, nid, CORB_SET_COEFFICIENT_INDEX, 0x36,
		    NULL);
		azalia_comresp(this, nid, CORB_SET_PROCESSING_COEFFICIENT,
		    0x57d7, NULL);
	}

	return(0);
}

/* Magic init sequence to make the right speaker work (reverse-engineered) */
void
azalia_codec_init_dolby_atmos(codec_t *this)
{
	static uint16_t atmos_init[] = {
		0x06, 0x73e, 0x00, 0x06, 0x73e, 0x80,
		0x20, 0x500, 0x26, 0x20, 0x4f0, 0x00,
		0x20, 0x500, 0x22, 0x20, 0x400, 0x31,
		0x20, 0x500, 0x23, 0x20, 0x400, 0x0b,
		0x20, 0x500, 0x25, 0x20, 0x400, 0x00,
		0x20, 0x500, 0x26, 0x20, 0x4b0, 0x10,
	};
	static struct {
		unsigned char v23;
		unsigned char v25;
	} atmos_v23_v25[] = {
		{ 0x0c, 0x00 }, { 0x0d, 0x00 }, { 0x0e, 0x00 }, { 0x0f, 0x00 },
		{ 0x10, 0x00 }, { 0x1a, 0x40 }, { 0x1b, 0x82 }, { 0x1c, 0x00 },
		{ 0x1d, 0x00 }, { 0x1e, 0x00 }, { 0x1f, 0x00 }, { 0x20, 0xc2 },
		{ 0x21, 0xc8 }, { 0x22, 0x26 }, { 0x23, 0x24 }, { 0x27, 0xff },
		{ 0x28, 0xff }, { 0x29, 0xff }, { 0x2a, 0x8f }, { 0x2b, 0x02 },
		{ 0x2c, 0x48 }, { 0x2d, 0x34 }, { 0x2e, 0x00 }, { 0x2f, 0x00 },
		{ 0x30, 0x00 }, { 0x31, 0x00 }, { 0x32, 0x00 }, { 0x33, 0x00 },
		{ 0x34, 0x00 }, { 0x35, 0x01 }, { 0x36, 0x93 }, { 0x37, 0x0c },
		{ 0x38, 0x00 }, { 0x39, 0x00 }, { 0x3a, 0xf8 }, { 0x38, 0x80 },
	};
	int i;

	for (i = 0; i < nitems(atmos_init) / 3; i++) {
		if (azalia_comresp(this, atmos_init[i * 3],
		    atmos_init[(i * 3) + 1], atmos_init[(i * 3) + 2], NULL))
			return;
	}

	for (i = 0; i < nitems(atmos_v23_v25); i++) {
		if (azalia_comresp(this, 0x06, 0x73e, 0x00, NULL))
			return;
		if (azalia_comresp(this, 0x20, 0x500, 0x26, NULL))
			return;
		if (azalia_comresp(this, 0x20, 0x4b0, 0x00, NULL))
			return;
		if (i == 0) {
			if (azalia_comresp(this, 0x21, 0xf09, 0x00, NULL))
				return;
		}
		if (i != 20) {
			if (azalia_comresp(this, 0x06, 0x73e, 0x80, NULL))
				return;
		}

		if (azalia_comresp(this, 0x20, 0x500, 0x26, NULL))
			return;
		if (azalia_comresp(this, 0x20, 0x4f0, 0x00, NULL))
			return;
		if (azalia_comresp(this, 0x20, 0x500, 0x23, NULL))
			return;

		if (azalia_comresp(this, 0x20, 0x400,
		    atmos_v23_v25[i].v23, NULL))
			return;

		if (atmos_v23_v25[i].v23 != 0x1e) {
			if (azalia_comresp(this, 0x20, 0x500, 0x25, NULL))
				return;
			if (azalia_comresp(this, 0x20, 0x400,
			    atmos_v23_v25[i].v25, NULL))
				return;
		}

		if (azalia_comresp(this, 0x20, 0x500, 0x26, NULL))
			return;
		if (azalia_comresp(this, 0x20, 0x4b0, 0x10, NULL))
			return;
	}
}
