/*	$OpenBSD: sff.c,v 1.23 2019/10/24 18:54:10 bluhm Exp $ */

/*
 * Copyright (c) 2019 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SMALL

#include <sys/ioctl.h>

#include <net/if.h>

#include <math.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <vis.h>

#include "ifconfig.h"

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

#ifndef ISSET
#define ISSET(_w, _m)	((_w) & (_m))
#endif

#define SFF_THRESH_HI_ALARM	0
#define SFF_THRESH_LO_ALARM	1
#define SFF_THRESH_HI_WARN	2
#define SFF_THRESH_LO_WARN	3
#define SFF_THRESH_COUNT	4

#define SFF_THRESH_REG(_i)	((_i) * 2)

struct sff_thresholds {
	 float		thresholds[SFF_THRESH_COUNT];
};

struct sff_media_map {
	float		factor_wavelength;
	int		scale_om1;
	int		scale_om2;
	int		scale_om3;
	uint8_t		connector_type;
	uint8_t		wavelength;
	uint8_t		dist_smf_m;
	uint8_t		dist_smf_km;
	uint8_t		dist_om1;
	uint8_t		dist_om2;
	uint8_t		dist_om3;
	uint8_t		dist_cu;
};

#define SFF8024_ID_UNKNOWN	0x00
#define SFF8024_ID_GBIC		0x01
#define SFF8024_ID_MOBO		0x02 /* Module/connector soldered to mobo */
				     /* using SFF-8472 */
#define SFF8024_ID_SFP		0x03 /* SFP/SFP+/SFP28 */
#define SFF8024_ID_300PIN_XBI	0x04 /* 300 pin XBI */
#define SFF8024_ID_XENPAK	0x05
#define SFF8024_ID_XFP		0x06
#define SFF8024_ID_XFF		0x07
#define SFF8024_ID_XFPE		0x08 /* XFP-E */
#define SFF8024_ID_XPAK		0x09
#define SFF8024_ID_X2		0x0a
#define SFF8024_ID_DWDM_SFP	0x0b /* DWDM-SFP/SFP+ */
				     /* not using SFF-8472 */
#define SFF8024_ID_QSFP		0x0c
#define SFF8024_ID_QSFP_PLUS	0x0d /* or later */
				     /* using SFF-8436/8665/8685 et al */
#define SFF8024_ID_CXP		0x0e /* or later */
#define SFF8024_ID_HD4X		0x0f /* shielded mini multilane HD 4X */
#define SFF8024_ID_HD8X		0x10 /* shielded mini multilane HD 8X */
#define SFF8024_ID_QSFP28	0x11 /* or later */
				     /* using SFF-8665 et al */
#define SFF8024_ID_CXP2		0x12 /* aka CXP28, or later */
#define SFF8024_ID_CDFP		0x13 /* style 1/style 2 */
#define SFF8024_ID_HD4X_FAN	0x14 /* shielded mini multilane HD 4X fanout */
#define SFF8024_ID_HD8X_FAN	0x15 /* shielded mini multilane HD 8X fanout */
#define SFF8024_ID_CDFP3	0x16 /* style 3 */
#define SFF8024_ID_uQSFP	0x17 /* microQSFP */
#define SFF8024_ID_QSFP_DD	0x18 /* QSFP-DD double density 8x */
				     /* INF-8628 */
#define SFF8024_ID_RESERVED	0x7f /* up to here is reserved */
				     /* 0x80 to 0xff is vendor specific */

#define SFF8024_ID_IS_RESERVED(_id)	((_id) <= SFF8024_ID_RESERVED)
#define SFF8024_ID_IS_VENDOR(_id)	((_id) > SFF8024_ID_RESERVED)

#define SFF8024_CON_UNKNOWN	0x00
#define SFF8024_CON_SC		0x01 /* Subscriber Connector */
#define SFF8024_CON_FC_1	0x02 /* Fibre Channel Style 1 copper */
#define SFF8024_CON_FC_2	0x03 /* Fibre Channel Style 2 copper */
#define SFF8024_CON_BNC_TNC	0x04 /* BNC/TNC */
#define SFF8024_CON_FC_COAX	0x05 /* Fibre Channel coax headers */
#define SFF8024_CON_FJ		0x06 /* Fibre Jack */
#define SFF8024_CON_LC		0x07 /* Lucent Connector */
#define SFF8024_CON_MT_RJ	0x08 /* Mechanical Transfer - Registered Jack */
#define SFF8024_CON_MU		0x09 /* Multiple Optical */
#define SFF8024_CON_SG		0x0a
#define SFF8024_CON_O_PIGTAIL	0x0b /* Optical Pigtail */
#define SFF8024_CON_MPO_1x12	0x0c /* Multifiber Parallel Optic 1x12 */
#define SFF8024_CON_MPO_2x16	0x0e /* Multifiber Parallel Optic 2x16 */
#define SFF8024_CON_HSSDC2	0x20 /* High Speed Serial Data Connector */
#define SFF8024_CON_Cu_PIGTAIL	0x21 /* Copper Pigtail */
#define SFF8024_CON_RJ45	0x22
#define SFF8024_CON_NO		0x23 /* No separable connector */
#define SFF8024_CON_MXC_2x16	0x24
#define SFF8024_CON_RESERVED	0x7f /* up to here is reserved */
				     /* 0x80 to 0xff is vendor specific */

#define SFF8024_CON_IS_RESERVED(_id)	((_id) <= SFF8024_CON_RESERVED)
#define SFF8024_CON_IS_VENDOR(_id)	((_id) > SFF8024_CON_RESERVED)

static const char *sff8024_id_names[] = {
	[SFF8024_ID_UNKNOWN]	= "Unknown",
	[SFF8024_ID_GBIC]	= "GBIC",
	[SFF8024_ID_SFP]	= "SFP",
	[SFF8024_ID_300PIN_XBI]	= "XBI",
	[SFF8024_ID_XENPAK]	= "XENPAK",
	[SFF8024_ID_XFP]	= "XFP",
	[SFF8024_ID_XFF]	= "XFF",
	[SFF8024_ID_XFPE]	= "XFPE",
	[SFF8024_ID_XPAK]	= "XPAK",
	[SFF8024_ID_X2]		= "X2",
	[SFF8024_ID_DWDM_SFP]	= "DWDM-SFP",
	[SFF8024_ID_QSFP]	= "QSFP",
	[SFF8024_ID_QSFP_PLUS]	= "QSFP+",
	[SFF8024_ID_CXP]	= "CXP",
	[SFF8024_ID_HD4X]	= "HD 4X",
	[SFF8024_ID_HD8X]	= "HD 8X",
	[SFF8024_ID_QSFP28]	= "QSFP28",
	[SFF8024_ID_CXP2]	= "CXP2",
	[SFF8024_ID_CDFP]	= "CDFP Style 1/2",
	[SFF8024_ID_HD4X_FAN]	= "HD 4X Fanout",
	[SFF8024_ID_HD8X_FAN]	= "HD 8X Fanout",
	[SFF8024_ID_CDFP3]	= "CDFP Style 3",
	[SFF8024_ID_uQSFP]	= "microQSFP",
	[SFF8024_ID_QSFP_DD]	= "QSFP-DD",
};

static const char *sff8024_con_names[] = {
	[SFF8024_CON_UNKNOWN]	= "Unknown",
	[SFF8024_CON_SC]	= "SC",
	[SFF8024_CON_FC_1]	= "FC Style 1",
	[SFF8024_CON_FC_2]	= "FC Style 2",
	[SFF8024_CON_BNC_TNC]	= "BNC/TNC",
	[SFF8024_CON_FC_COAX]	= "FC coax headers",
	[SFF8024_CON_FJ]	= "FJ",
	[SFF8024_CON_LC]	= "LC",
	[SFF8024_CON_MT_RJ]	= "MT-RJ",
	[SFF8024_CON_MU]	= "MU",
	[SFF8024_CON_SG]	= "SG",
	[SFF8024_CON_O_PIGTAIL]	= "AOC",
	[SFF8024_CON_MPO_1x12]	= "MPO 1x12",
	[SFF8024_CON_MPO_2x16]	= "MPO 2x16",
	[SFF8024_CON_HSSDC2]	= "HSSDC II",
	[SFF8024_CON_Cu_PIGTAIL]
				= "DAC",
	[SFF8024_CON_RJ45]	= "RJ45",
	[SFF8024_CON_NO]	= "No connector",
	[SFF8024_CON_MXC_2x16]	= "MXC 2x16",
};

#define SFF8472_ID			0 /* SFF8027 for identifier values */
#define SFF8472_EXT_ID			1
#define SFF8472_EXT_ID_UNSPECIFIED		0x00
#define SFF8472_EXT_ID_MOD_DEF_1		0x01
#define SFF8472_EXT_ID_MOD_DEF_2		0x02
#define SFF8472_EXT_ID_MOD_DEF_3		0x03
#define SFF8472_EXT_ID_2WIRE			0x04
#define SFF8472_EXT_ID_MOD_DEF_5		0x05
#define SFF8472_EXT_ID_MOD_DEF_6		0x06
#define SFF8472_EXT_ID_MOD_DEF_7		0x07
#define SFF8472_CON			2 /* SFF8027 for connector values */
#define SFF8472_DIST_SMF_KM		14
#define SFF8472_DIST_SMF_M		15
#define SFF8472_DIST_OM2		16
#define SFF8472_DIST_OM1		17
#define SFF8472_DIST_CU			18
#define SFF8472_DIST_OM3		19
#define SFF8472_VENDOR_START		20
#define SFF8472_VENDOR_END		35
#define SFF8472_PRODUCT_START		40
#define SFF8472_PRODUCT_END		55
#define SFF8472_REVISION_START		56
#define SFF8472_REVISION_END		59
#define SFF8472_WAVELENGTH		60
#define SFF8472_SERIAL_START		68
#define SFF8472_SERIAL_END		83
#define SFF8472_DATECODE		84
#define SFF8472_DDM_TYPE		92
#define SFF8472_DDM_TYPE_AVG_POWER		(1U << 3)
#define SFF8472_DDM_TYPE_CAL_EXT		(1U << 4)
#define SFF8472_DDM_TYPE_CAL_INT		(1U << 5)
#define SFF8472_DDM_TYPE_IMPL			(1U << 6)
#define SFF8472_COMPLIANCE		94
#define SFF8472_COMPLIANCE_NONE			0x00
#define SFF8472_COMPLIANCE_9_3			0x01 /* SFF-8472 Rev 9.3 */
#define SFF8472_COMPLIANCE_9_5			0x02 /* SFF-8472 Rev 9.5 */
#define SFF8472_COMPLIANCE_10_2			0x03 /* SFF-8472 Rev 10.2 */
#define SFF8472_COMPLIANCE_10_4			0x04 /* SFF-8472 Rev 10.4 */
#define SFF8472_COMPLIANCE_11_0			0x05 /* SFF-8472 Rev 11.0 */
#define SFF8472_COMPLIANCE_11_3			0x06 /* SFF-8472 Rev 11.3 */
#define SFF8472_COMPLIANCE_11_4			0x07 /* SFF-8472 Rev 11.4 */
#define SFF8472_COMPLIANCE_12_3			0x08 /* SFF-8472 Rev 12.3 */

static const struct sff_media_map sff8472_media_map = {
	.connector_type		= SFF8472_CON,
	.wavelength		= SFF8472_WAVELENGTH,
	.factor_wavelength	= 1.0,
	.dist_smf_m		= SFF8472_DIST_SMF_M,
	.dist_smf_km		= SFF8472_DIST_SMF_KM,
	.dist_om1		= SFF8472_DIST_OM1,
	.scale_om1		= 10,
	.dist_om2		= SFF8472_DIST_OM2,
	.scale_om2		= 10,
	.dist_om3		= SFF8472_DIST_OM3,
	.scale_om3		= 20,
	.dist_cu		= SFF8472_DIST_CU,
};

/*
 * page 0xa2
 */
#define SFF8472_AW_TEMP			0
#define SFF8472_AW_VCC			8
#define SFF8472_AW_TX_BIAS		16
#define SFF8472_AW_TX_POWER		24
#define SFF8472_AW_RX_POWER		32
#define ALRM_HIGH		0
#define ALRM_LOW		2
#define WARN_HIGH		4
#define WARN_LOW		6
#define SFF8472_DDM_TEMP		96
#define SFF8472_DDM_VCC			98
#define SFF8472_DDM_TX_BIAS		100
#define SFF8472_DDM_TX_POWER		102
#define SFF8472_DDM_RX_POWER		104
#define SFF8472_DDM_LASER		106 /* laser temp/wavelength */
					    /* optional */
#define SFF8472_DDM_TEC			108 /* Measured TEC current */
					    /* optional */

#define SFF_TEMP_FACTOR		256.0
#define SFF_VCC_FACTOR		10000.0
#define SFF_BIAS_FACTOR		500.0
#define SFF_POWER_FACTOR	10000.0

/*
 * QSFP is defined by SFF-8436, but the management interface is
 * updated and maintained by SFF-8636.
 */

#define SFF8436_STATUS1		1
#define SFF8436_STATUS2		2
#define SFF8436_STATUS2_DNR		(1 << 0) /* Data_Not_Ready */
#define SFF8436_STATUS2_INTL		(1 << 1) /* Interrupt output state */
#define SFF8436_STATUS2_FLAT_MEM	(1 << 2) /* Upper memory flat/paged */

#define SFF8436_TEMP		22
#define SFF8436_VCC		26

#define SFF8436_CHANNELS	4	/* number of TX and RX channels */
#define SFF8436_RX_POWER_BASE	34
#define SFF8436_RX_POWER(_i)	(SFF8436_RX_POWER_BASE + ((_i) * 2))
#define SFF8436_TX_BIAS_BASE	42
#define SFF8436_TX_BIAS(_i)	(SFF8436_TX_BIAS_BASE + ((_i) * 2))
#define SFF8436_TX_POWER_BASE	50
#define SFF8436_TX_POWER(_i)	(SFF8436_TX_POWER_BASE + ((_i) * 2))

/* Upper Page 00h */

#define SFF8436_MAXCASETEMP	190	/* C */
#define SFF8436_MAXCASETEMP_DEFAULT	70 /* if SFF8436_MAXCASETEMP is 0 */

/* Upper page 03h */

#define SFF8436_AW_TEMP		128
#define SFF8436_AW_VCC		144
#define SFF8436_AW_RX_POWER	176
#define SFF8436_AW_TX_BIAS	184
#define SFF8436_AW_TX_POWER	192

/*
 * XFP stuff is defined by INF-8077.
 *
 * The "Serial ID Memory Map" on page 1 contains the interesting strings
 */

/* SFF-8636 and INF-8077 share a layout for various strings */

#define UPPER_CON			130 /* connector type */
#define UPPER_DIST_SMF			142
#define UPPER_DIST_OM3			143
#define UPPER_DIST_OM2			144
#define UPPER_DIST_OM1			145
#define UPPER_DIST_CU			146
#define UPPER_WAVELENGTH		186

#define UPPER_VENDOR_START		148
#define UPPER_VENDOR_END		163
#define UPPER_PRODUCT_START		168
#define UPPER_PRODUCT_END		183
#define UPPER_REVISION_START		184
#define UPPER_REVISION_END		185
#define UPPER_SERIAL_START		196
#define UPPER_SERIAL_END		211
#define UPPER_DATECODE			212
#define UPPER_LOT_START			218
#define UPPER_LOT_END			219

static const struct sff_media_map upper_media_map = {
	.connector_type		= UPPER_CON,
	.wavelength		= UPPER_WAVELENGTH,
	.factor_wavelength	= 20.0,
	.dist_smf_m		= 0,
	.dist_smf_km		= UPPER_DIST_SMF,
	.dist_om1		= UPPER_DIST_OM1,
	.scale_om1		= 1,
	.dist_om2		= UPPER_DIST_OM1,
	.scale_om2		= 1,
	.dist_om3		= UPPER_DIST_OM3,
	.scale_om3		= 2,
	.dist_cu		= UPPER_DIST_CU,
};

static void	hexdump(const void *, size_t);
static int	if_sff8472(int, const struct if_sffpage *);
static int	if_sff8636(int, const struct if_sffpage *);
static int	if_inf8077(int, const struct if_sffpage *);

static const char *
sff_id_name(uint8_t id)
{
	const char *name = NULL;

	if (id < nitems(sff8024_id_names)) {
		name = sff8024_id_names[id];
		if (name != NULL)
			return (name);
	}

	if (SFF8024_ID_IS_VENDOR(id))
		return ("Vendor Specific");

	return ("Reserved");
}

static const char *
sff_con_name(uint8_t id)
{
	const char *name = NULL;

	if (id < nitems(sff8024_con_names)) {
		name = sff8024_con_names[id];
		if (name != NULL)
			return (name);
	}

	if (SFF8024_CON_IS_VENDOR(id))
		return ("Vendor Specific");

	return ("Reserved");
}

static void
if_sffpage_init(struct if_sffpage *sff, uint8_t addr, uint8_t page)
{
	memset(sff, 0, sizeof(*sff));

	if (strlcpy(sff->sff_ifname, ifname, sizeof(sff->sff_ifname)) >=
	    sizeof(sff->sff_ifname))
		errx(1, "interface name too long");

	sff->sff_addr = addr;
	sff->sff_page = page;
}

static void
if_sffpage_dump(const struct if_sffpage *sff)
{
	printf("%s: addr %02x", ifname, sff->sff_addr);
	if (sff->sff_addr == IFSFF_ADDR_EEPROM)
		printf(" page %u", sff->sff_page);
	putchar('\n');
	hexdump(sff->sff_data, sizeof(sff->sff_data));
}

int
if_sff_info(int dump)
{
	struct if_sffpage pg0;
	int error = 0;
	uint8_t id, ext_id;

	if_sffpage_init(&pg0, IFSFF_ADDR_EEPROM, 0);
	if (ioctl(sock, SIOCGIFSFFPAGE, (caddr_t)&pg0) == -1) {
		if (errno == ENXIO) {
			/* try 1 for XFP cos myx which can't switch pages... */
			if_sffpage_init(&pg0, IFSFF_ADDR_EEPROM, 1);
			if (ioctl(sock, SIOCGIFSFFPAGE, (caddr_t)&pg0) == -1)
				return (-1);
		} else
			return (-1);
	}

	if (dump)
		if_sffpage_dump(&pg0);

	id = pg0.sff_data[0]; /* SFF8472_ID */

	printf("\ttransceiver: %s ", sff_id_name(id));
	switch (id) {
	case SFF8024_ID_SFP:
		ext_id = pg0.sff_data[SFF8472_EXT_ID];
		if (ext_id != SFF8472_EXT_ID_2WIRE) {
			printf("extended-id %02xh\n", ext_id);
			break;
		}
		/* FALLTHROUGH */
	case SFF8024_ID_GBIC:
		error = if_sff8472(dump, &pg0);
		break;
	case SFF8024_ID_XFP:
		if (pg0.sff_page != 1) {
			if_sffpage_init(&pg0, IFSFF_ADDR_EEPROM, 1);
			if (ioctl(sock, SIOCGIFSFFPAGE, (caddr_t)&pg0) == -1)
				return (-1);
			if (dump)
				if_sffpage_dump(&pg0);
		}
		error = if_inf8077(dump, &pg0);
		break;
	case SFF8024_ID_QSFP:
	case SFF8024_ID_QSFP_PLUS:
	case SFF8024_ID_QSFP28:
		error = if_sff8636(dump, &pg0);
		break;
	default:
		printf("\n");
		break;
	}

	return (error);
}

static void
if_sff_ascii_print(const struct if_sffpage *sff, const char *name,
    size_t start, size_t end, const char *trailer)
{
	const uint8_t *d = sff->sff_data;
	int ch;

	for (;;) {
		ch = d[start];
		if (!isspace(ch) && ch != '\0')
			break;

		start++;
		if (start == end)
			return;
	}

	printf("%s", name);

	for (;;) {
		ch = d[end];
		if (!isspace(ch) && ch != '\0')
			break;

		end--;
	}

	do {
		char dst[8];
		vis(dst, d[start], VIS_TAB | VIS_NL, 0);
		printf("%s", dst);
	} while (++start <= end);

	printf("%s", trailer);
}

static void
if_sff_date_print(const struct if_sffpage *sff, const char *name,
    size_t start, const char *trailer)
{
	const uint8_t *d = sff->sff_data + start;
	size_t i;

	/* YYMMDD */
	for (i = 0; i < 6; i++) {
		if (!isdigit(d[i])) {
			if_sff_ascii_print(sff, name, start,
			    start + 5, trailer);
			return;
		}
	}

	printf("%s20%c%c-%c%c-%c%c%s", name,
	    d[0], d[1], d[2], d[3], d[4], d[5], trailer);
}

static int16_t
if_sff_int(const struct if_sffpage *sff, size_t start)
{
	const uint8_t *d = sff->sff_data + start;

	return (d[0] << 8 | d[1]);
}

static uint16_t
if_sff_uint(const struct if_sffpage *sff, size_t start)
{
	const uint8_t *d = sff->sff_data + start;

	return (d[0] << 8 | d[1]);
}

static float
if_sff_power2dbm(const struct if_sffpage *sff, size_t start)
{
	const uint8_t *d = sff->sff_data + start;

	int power = d[0] << 8 | d[1];
	return (10.0 * log10f((float)power / 10000.0));
}

static void
if_sff_printalarm(const char *unit, int range, float actual,
    float alrm_high, float alrm_low, float warn_high, float warn_log)
{
	printf("%.02f%s", actual, unit);
	if (range == 1)
		printf(" (low %.02f%s, high %.02f%s)", alrm_low,
		    unit, alrm_high, unit);

	if(actual > alrm_high || actual < alrm_low)
		printf(" [ALARM]");
	else if(actual > warn_high || actual < warn_log)
		printf(" [WARNING]");
}

static void
if_sff_printdist(const char *type, int value, int scale)
{
	int distance = value * scale;

	if (value == 0)
		return;

	if (distance < 10000)
		printf (", %s%u%s", value > 254 ? ">" : "", distance, type);
	else
		printf (", %s%0.1fk%s", value > 254 ? ">" : "",
		    distance / 1000.0, type);
}

static void
if_sff_printmedia(const struct if_sffpage *pg, const struct sff_media_map *m)
{
	uint8_t con;
	unsigned int wavelength;

	con = pg->sff_data[m->connector_type];
	printf("%s", sff_con_name(con));

	wavelength = if_sff_uint(pg, m->wavelength);
	switch (wavelength) {
	case 0x0000:
		/* not known or is unavailable */
		break;
	/* Copper Cable */
	case 0x0100: /* SFF-8431 Appendix E */
	case 0x0400: /* SFF-8431 limiting */
	case 0x0c00: /* SFF-8431 limiting and FC-PI-4 limiting */
		break;
	default:
		printf(", %.f nm", wavelength / m->factor_wavelength);
	}

	if (m->dist_smf_m != 0 &&
	    pg->sff_data[m->dist_smf_m] > 0 &&
	    pg->sff_data[m->dist_smf_m] < 255)
		if_sff_printdist("m SMF", pg->sff_data[m->dist_smf_m], 100);
	else
		if_sff_printdist("km SMF", pg->sff_data[m->dist_smf_km], 1);
	if_sff_printdist("m OM1", pg->sff_data[m->dist_om1], m->scale_om1);
	if_sff_printdist("m OM2", pg->sff_data[m->dist_om2], m->scale_om2);
	if_sff_printdist("m OM3", pg->sff_data[m->dist_om3], m->scale_om3);
	if_sff_printdist("m", pg->sff_data[m->dist_cu], 1);
}

static int
if_sff8472(int dump, const struct if_sffpage *pg0)
{
	struct if_sffpage ddm;
	uint8_t ddm_types;

	if_sff_printmedia(pg0, &sff8472_media_map);

	printf("\n\tmodel: ");
	if_sff_ascii_print(pg0, "",
	    SFF8472_VENDOR_START, SFF8472_VENDOR_END, " ");
	if_sff_ascii_print(pg0, "",
	    SFF8472_PRODUCT_START, SFF8472_PRODUCT_END, "");
	if_sff_ascii_print(pg0, " rev ",
	    SFF8472_REVISION_START, SFF8472_REVISION_END, "");

	if_sff_ascii_print(pg0, "\n\tserial: ",
	    SFF8472_SERIAL_START, SFF8472_SERIAL_END, ", ");
	if_sff_date_print(pg0, "date: ", SFF8472_DATECODE, "\n");

	ddm_types = pg0->sff_data[SFF8472_DDM_TYPE];
	if (pg0->sff_data[SFF8472_COMPLIANCE] == SFF8472_COMPLIANCE_NONE ||
	    !ISSET(ddm_types, SFF8472_DDM_TYPE_IMPL))
		return (0);

	if_sffpage_init(&ddm, IFSFF_ADDR_DDM, 0);
	if (ioctl(sock, SIOCGIFSFFPAGE, (caddr_t)&ddm) == -1)
		return (-1);

	if (dump)
		if_sffpage_dump(&ddm);

	if (ISSET(ddm_types, SFF8472_DDM_TYPE_CAL_EXT)) {
		printf("\tcalibration: external "
		    "(WARNING: needs more code)\n");
	}

	printf("\tvoltage: ");
	if_sff_printalarm(" V", 0,
	    if_sff_uint(&ddm, SFF8472_DDM_VCC) / SFF_VCC_FACTOR,
	    if_sff_uint(&ddm, SFF8472_AW_VCC + ALRM_HIGH) / SFF_VCC_FACTOR,
	    if_sff_uint(&ddm, SFF8472_AW_VCC + ALRM_LOW) / SFF_VCC_FACTOR,
	    if_sff_uint(&ddm, SFF8472_AW_VCC + WARN_HIGH) / SFF_VCC_FACTOR,
	    if_sff_uint(&ddm, SFF8472_AW_VCC + WARN_LOW) / SFF_VCC_FACTOR);

	printf(", bias current: ");
	if_sff_printalarm(" mA", 0,
	    if_sff_uint(&ddm, SFF8472_DDM_TX_BIAS) / SFF_BIAS_FACTOR,
	    if_sff_uint(&ddm, SFF8472_AW_TX_BIAS + ALRM_HIGH) / SFF_BIAS_FACTOR,
	    if_sff_uint(&ddm, SFF8472_AW_TX_BIAS + ALRM_LOW) / SFF_BIAS_FACTOR,
	    if_sff_uint(&ddm, SFF8472_AW_TX_BIAS + WARN_HIGH) / SFF_BIAS_FACTOR,
	    if_sff_uint(&ddm, SFF8472_AW_TX_BIAS + WARN_LOW) / SFF_BIAS_FACTOR);

	printf("\n\ttemp: ");
	if_sff_printalarm(" C", 1,
	    if_sff_int(&ddm, SFF8472_DDM_TEMP) / SFF_TEMP_FACTOR,
	    if_sff_int(&ddm, SFF8472_AW_TEMP + ALRM_HIGH) / SFF_TEMP_FACTOR,
	    if_sff_int(&ddm, SFF8472_AW_TEMP + ALRM_LOW) / SFF_TEMP_FACTOR,
	    if_sff_int(&ddm, SFF8472_AW_TEMP + WARN_HIGH) / SFF_TEMP_FACTOR,
	    if_sff_int(&ddm, SFF8472_AW_TEMP + WARN_LOW) / SFF_TEMP_FACTOR);

	printf("\n\ttx: ");
	if_sff_printalarm(" dBm", 1,
	    if_sff_power2dbm(&ddm, SFF8472_DDM_TX_POWER),
	    if_sff_power2dbm(&ddm, SFF8472_AW_TX_POWER + ALRM_HIGH),
	    if_sff_power2dbm(&ddm, SFF8472_AW_TX_POWER + ALRM_LOW),
	    if_sff_power2dbm(&ddm, SFF8472_AW_TX_POWER + WARN_HIGH),
	    if_sff_power2dbm(&ddm, SFF8472_AW_TX_POWER + WARN_LOW));

	printf("\n\trx: ");
	if_sff_printalarm(" dBm", 1,
	    if_sff_power2dbm(&ddm, SFF8472_DDM_RX_POWER),
	    if_sff_power2dbm(&ddm, SFF8472_AW_RX_POWER + ALRM_HIGH),
	    if_sff_power2dbm(&ddm, SFF8472_AW_RX_POWER + ALRM_LOW),
	    if_sff_power2dbm(&ddm, SFF8472_AW_RX_POWER + WARN_HIGH),
	    if_sff_power2dbm(&ddm, SFF8472_AW_RX_POWER + WARN_LOW));

	putchar('\n');
	return (0);
}

static void
if_upper_strings(const struct if_sffpage *pg)
{
	if_sff_printmedia(pg, &upper_media_map);

	printf("\n\tmodel: ");
	if_sff_ascii_print(pg, "",
	    UPPER_VENDOR_START, UPPER_VENDOR_END, " ");
	if_sff_ascii_print(pg, "",
	    UPPER_PRODUCT_START, UPPER_PRODUCT_END, "");
	if_sff_ascii_print(pg, " rev ",
	    UPPER_REVISION_START, UPPER_REVISION_END, "");

	if_sff_ascii_print(pg, "\n\tserial: ",
	    UPPER_SERIAL_START, UPPER_SERIAL_END, " ");
	if_sff_date_print(pg, "date: ", UPPER_DATECODE, " ");
	if_sff_ascii_print(pg, "lot: ",
	    UPPER_LOT_START, UPPER_LOT_END, "");

	putchar('\n');
}

static int
if_inf8077(int dump, const struct if_sffpage *pg1)
{
	if_upper_strings(pg1);

	return (0);
}

static int
if_sff8636_thresh(int dump, const struct if_sffpage *pg0)
{
	struct if_sffpage pg3;
	unsigned int i;
	struct sff_thresholds temp, vcc, tx, rx, bias;

	if_sffpage_init(&pg3, IFSFF_ADDR_EEPROM, 3);
	if (ioctl(sock, SIOCGIFSFFPAGE, (caddr_t)&pg3) == -1) {
		if (dump)
			warn("%s SIOCGIFSFFPAGE page 3", ifname);
		return (-1);
	}

	if (dump)
		if_sffpage_dump(&pg3);

	if (pg3.sff_data[0x7f] != 3) { /* just in case... */
		if (dump) {
			warnx("%s SIOCGIFSFFPAGE: page select unsupported",
			    ifname);
		}
		return (-1);
	}

	for (i = 0; i < SFF_THRESH_COUNT; i++) {
		temp.thresholds[i] = if_sff_int(&pg3,
		    SFF8436_AW_TEMP + SFF_THRESH_REG(i)) / SFF_TEMP_FACTOR;

		vcc.thresholds[i] = if_sff_uint(&pg3,
		    SFF8436_AW_VCC + SFF_THRESH_REG(i)) / SFF_VCC_FACTOR;

		rx.thresholds[i] = if_sff_power2dbm(&pg3,
		    SFF8436_AW_RX_POWER + SFF_THRESH_REG(i));

		bias.thresholds[i] = if_sff_uint(&pg3,
		    SFF8436_AW_TX_BIAS + SFF_THRESH_REG(i)) / SFF_BIAS_FACTOR;

		tx.thresholds[i] = if_sff_power2dbm(&pg3,
		    SFF8436_AW_TX_POWER + SFF_THRESH_REG(i));
	}

	printf("\ttemp: ");
	if_sff_printalarm(" C", 1,
	    if_sff_int(&pg3, SFF8436_TEMP) / SFF_TEMP_FACTOR,
	    temp.thresholds[SFF_THRESH_HI_ALARM],
	    temp.thresholds[SFF_THRESH_LO_ALARM],
	    temp.thresholds[SFF_THRESH_HI_WARN],
	    temp.thresholds[SFF_THRESH_LO_WARN]);
	printf("\n");

	printf("\tvoltage: ");
	if_sff_printalarm(" V", 1,
	    if_sff_uint(&pg3, SFF8436_VCC) / SFF_VCC_FACTOR,
	    vcc.thresholds[SFF_THRESH_HI_ALARM],
	    vcc.thresholds[SFF_THRESH_LO_ALARM],
	    vcc.thresholds[SFF_THRESH_HI_WARN],
	    vcc.thresholds[SFF_THRESH_LO_WARN]);
	printf("\n");

	for (i = 0; i < SFF8436_CHANNELS; i++) {
		unsigned int channel = i + 1;

		printf("\tchannel %u bias current: ", channel);
		if_sff_printalarm(" mA", 1,
		    if_sff_uint(&pg3, SFF8436_TX_BIAS(i)) / SFF_BIAS_FACTOR,
		    bias.thresholds[SFF_THRESH_HI_ALARM],
		    bias.thresholds[SFF_THRESH_LO_ALARM],
		    bias.thresholds[SFF_THRESH_HI_WARN],
		    bias.thresholds[SFF_THRESH_LO_WARN]);
		printf("\n");

		printf("\tchannel %u tx: ", channel);
		if_sff_printalarm(" dBm", 1,
		    if_sff_power2dbm(&pg3, SFF8436_TX_POWER(i)),
		    tx.thresholds[SFF_THRESH_HI_ALARM],
		    tx.thresholds[SFF_THRESH_LO_ALARM],
		    tx.thresholds[SFF_THRESH_HI_WARN],
		    tx.thresholds[SFF_THRESH_LO_WARN]);
		printf("\n");

		printf("\tchannel %u rx: ", channel);
		if_sff_printalarm(" dBm", 1,
		    if_sff_power2dbm(&pg3, SFF8436_RX_POWER(i)),
		    rx.thresholds[SFF_THRESH_HI_ALARM],
		    rx.thresholds[SFF_THRESH_LO_ALARM],
		    rx.thresholds[SFF_THRESH_HI_WARN],
		    rx.thresholds[SFF_THRESH_LO_WARN]);
		printf("\n");
	}

	return (0);
}

static int
if_sff8636(int dump, const struct if_sffpage *pg0)
{
	int16_t temp;
	uint8_t maxcasetemp;
	uint8_t flat;
	unsigned int i;

	if_upper_strings(pg0);

	if (pg0->sff_data[SFF8436_STATUS2] & SFF8436_STATUS2_DNR) {
		printf("\tmonitor data not ready\n");
		return (0);
	}

	maxcasetemp = pg0->sff_data[SFF8436_MAXCASETEMP];
	if (maxcasetemp == 0x00)
		maxcasetemp = SFF8436_MAXCASETEMP_DEFAULT;
	printf("\tmax case temp: %u C\n", maxcasetemp);

	temp = if_sff_int(pg0, SFF8436_TEMP);
	/* the temp reading look unset, assume the rest will be unset too */
	if ((uint16_t)temp == 0 || (uint16_t)temp == 0xffffU) {
		if (!dump)
			return (0);
	}

	flat = pg0->sff_data[SFF8436_STATUS2] & SFF8436_STATUS2_FLAT_MEM;
	if (!flat && if_sff8636_thresh(dump, pg0) == 0) {
		if (!dump)
			return (0);
	}

	printf("\t");
	printf("temp: %.02f%s", temp / SFF_TEMP_FACTOR, " C");
	printf(", ");
	printf("voltage: %.02f%s",
	    if_sff_uint(pg0, SFF8436_VCC) / SFF_VCC_FACTOR, " V");
	printf("\n");

	for (i = 0; i < SFF8436_CHANNELS; i++) {
		printf("\t");
		printf("channel %u: ", i + 1);
		printf("bias current: %.02f mA",
		    if_sff_uint(pg0, SFF8436_TX_BIAS(i)) / SFF_BIAS_FACTOR);
		printf(", ");
		printf("rx: %.02f dBm",
		    if_sff_power2dbm(pg0, SFF8436_RX_POWER(i)));
		printf(", ");
		printf("tx: %.02f dBm",
		    if_sff_power2dbm(pg0, SFF8436_TX_POWER(i)));
		printf("\n");
	}

	return (0);
}

static int
printable(int ch)
{
	if (ch == '\0')
		return ('_');
	if (!isprint(ch))
		return ('~');

	return (ch);
}

static void
hexdump(const void *d, size_t datalen)
{
	const uint8_t *data = d;
	int i, j = 0;

	for (i = 0; i < datalen; i += j) {
		printf("% 4d: ", i);
		for (j = 0; j < 16 && i+j < datalen; j++)
			printf("%02x ", data[i + j]);
		while (j++ < 16)
			printf("   ");
		printf("|");
		for (j = 0; j < 16 && i+j < datalen; j++)
			putchar(printable(data[i + j]));
		printf("|\n");
	}
}

#endif /* SMALL */
