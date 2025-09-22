/* SPDX-License-Identifier: MIT */
/*
 * drm_panel_orientation_quirks.c -- Quirks for non-normal panel orientation
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Note the quirks in this file are shared with fbdev/efifb and as such
 * must not depend on other drm code.
 */

#include <linux/dmi.h>
#include <linux/module.h>
#include <drm/drm_connector.h>
#include <drm/drm_utils.h>

#ifdef CONFIG_DMI

/*
 * Some x86 clamshell design devices use portrait tablet screens and a display
 * engine which cannot rotate in hardware, so we need to rotate the fbcon to
 * compensate. Unfortunately these (cheap) devices also typically have quite
 * generic DMI data, so we match on a combination of DMI data, screen resolution
 * and a list of known BIOS dates to avoid false positives.
 */

struct drm_dmi_panel_orientation_data {
	int width;
	int height;
	const char * const *bios_dates;
	int orientation;
};

static const struct drm_dmi_panel_orientation_data gpd_micropc = {
	.width = 720,
	.height = 1280,
	.bios_dates = (const char * const []){ "04/26/2019",
		NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data gpd_onemix2s = {
	.width = 1200,
	.height = 1920,
	.bios_dates = (const char * const []){ "05/21/2018", "10/26/2018",
		"03/04/2019", NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data gpd_pocket = {
	.width = 1200,
	.height = 1920,
	.bios_dates = (const char * const []){ "05/26/2017", "06/28/2017",
		"07/05/2017", "08/07/2017", NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data gpd_pocket2 = {
	.width = 1200,
	.height = 1920,
	.bios_dates = (const char * const []){ "06/28/2018", "08/28/2018",
		"12/07/2018", NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data gpd_win = {
	.width = 720,
	.height = 1280,
	.bios_dates = (const char * const []){
		"10/25/2016", "11/18/2016", "12/23/2016", "12/26/2016",
		"02/21/2017", "03/20/2017", "05/25/2017", NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data gpd_win2 = {
	.width = 720,
	.height = 1280,
	.bios_dates = (const char * const []){
		"12/07/2017", "05/24/2018", "06/29/2018", NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data itworks_tw891 = {
	.width = 800,
	.height = 1280,
	.bios_dates = (const char * const []){ "10/16/2015", NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data onegx1_pro = {
	.width = 1200,
	.height = 1920,
	.bios_dates = (const char * const []){ "12/17/2020", NULL },
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd640x960_leftside_up = {
	.width = 640,
	.height = 960,
	.orientation = DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd720x1280_rightside_up = {
	.width = 720,
	.height = 1280,
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd800x1280_leftside_up = {
	.width = 800,
	.height = 1280,
	.orientation = DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd800x1280_rightside_up = {
	.width = 800,
	.height = 1280,
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd1080x1920_leftside_up = {
	.width = 1080,
	.height = 1920,
	.orientation = DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd1080x1920_rightside_up = {
	.width = 1080,
	.height = 1920,
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd1200x1920_leftside_up = {
	.width = 1200,
	.height = 1920,
	.orientation = DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd1200x1920_rightside_up = {
	.width = 1200,
	.height = 1920,
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd1280x1920_rightside_up = {
	.width = 1280,
	.height = 1920,
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd1600x2560_leftside_up = {
	.width = 1600,
	.height = 2560,
	.orientation = DRM_MODE_PANEL_ORIENTATION_LEFT_UP,
};

static const struct drm_dmi_panel_orientation_data lcd1600x2560_rightside_up = {
	.width = 1600,
	.height = 2560,
	.orientation = DRM_MODE_PANEL_ORIENTATION_RIGHT_UP,
};

static const struct dmi_system_id orientation_data[] = {
	{	/* Acer One 10 (S1003) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Acer"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "One S1003"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* Acer Switch V 10 (SW5-017) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Acer"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "SW5-017"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* Anbernic Win600 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Anbernic"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Win600"),
		},
		.driver_data = (void *)&lcd720x1280_rightside_up,
	}, {	/* Asus T100HA */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "T100HAN"),
		},
		.driver_data = (void *)&lcd800x1280_leftside_up,
	}, {	/* Asus T101HA */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "T101HA"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* Asus T103HAF */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "T103HAF"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* AYA NEO AYANEO 2/2S */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AYANEO"),
		  DMI_MATCH(DMI_PRODUCT_NAME, "AYANEO 2"),
		},
		.driver_data = (void *)&lcd1200x1920_rightside_up,
	}, {	/* AYA NEO 2021 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AYADEVICE"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "AYA NEO 2021"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* AYA NEO AIR */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AYANEO"),
		  DMI_MATCH(DMI_PRODUCT_NAME, "AIR"),
		},
		.driver_data = (void *)&lcd1080x1920_leftside_up,
	}, {    /* AYA NEO Flip DS Bottom Screen */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AYANEO"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "FLIP DS"),
		},
		.driver_data = (void *)&lcd640x960_leftside_up,
	}, {    /* AYA NEO Flip KB/DS Top Screen */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AYANEO"),
		  DMI_MATCH(DMI_PRODUCT_NAME, "FLIP"),
		},
		.driver_data = (void *)&lcd1080x1920_leftside_up,
	}, {	/* AYA NEO Founder */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AYA NEO"),
		  DMI_MATCH(DMI_PRODUCT_NAME, "AYA NEO Founder"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* AYA NEO GEEK */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AYANEO"),
		  DMI_MATCH(DMI_PRODUCT_NAME, "GEEK"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* AYA NEO NEXT */
		.matches = {
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
		  DMI_MATCH(DMI_BOARD_NAME, "NEXT"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* AYA NEO KUN */
		.matches = {
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AYANEO"),
		  DMI_MATCH(DMI_BOARD_NAME, "KUN"),
		},
		.driver_data = (void *)&lcd1600x2560_rightside_up,
	}, {	/* AYA NEO SLIDE */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "AYANEO"),
		  DMI_MATCH(DMI_PRODUCT_NAME, "SLIDE"),
		},
		.driver_data = (void *)&lcd1080x1920_leftside_up,
	}, {    /* AYN Loki Max */
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ayn"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Loki Max"),
		},
		.driver_data = (void *)&lcd1080x1920_leftside_up,
	}, {	/* AYN Loki Zero */
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ayn"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Loki Zero"),
		},
		.driver_data = (void *)&lcd1080x1920_leftside_up,
	}, {	/* Chuwi HiBook (CWI514) */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Hampoo"),
			DMI_MATCH(DMI_BOARD_NAME, "Cherry Trail CR"),
			/* Above matches are too generic, add bios-date match */
			DMI_MATCH(DMI_BIOS_DATE, "05/07/2016"),
		},
		.driver_data = (void *)&lcd1200x1920_rightside_up,
	}, {	/* Chuwi Hi10 Pro (CWI529) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Hampoo"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Hi10 pro tablet"),
		},
		.driver_data = (void *)&lcd1200x1920_rightside_up,
	}, {	/* Dynabook K50 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Dynabook Inc."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "dynabook K50/FR"),
		},
		.driver_data = (void *)&lcd800x1280_leftside_up,
	}, {	/* GPD MicroPC (generic strings, also match on bios date) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Default string"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		},
		.driver_data = (void *)&gpd_micropc,
	}, {	/* GPD MicroPC (later BIOS versions with proper DMI strings) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "GPD"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "MicroPC"),
		},
		.driver_data = (void *)&lcd720x1280_rightside_up,
	}, {	/* GPD Win Max */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "GPD"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "G1619-01"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/*
		 * GPD Pocket, note that the DMI data is less generic then
		 * it seems, devices with a board-vendor of "AMI Corporation"
		 * are quite rare, as are devices which have both board- *and*
		 * product-id set to "Default String"
		 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_SERIAL, "Default string"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
		},
		.driver_data = (void *)&gpd_pocket,
	}, {	/* GPD Pocket 2 (generic strings, also match on bios date) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Default string"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		},
		.driver_data = (void *)&gpd_pocket2,
	}, {	/* GPD Win (same note on DMI match as GPD Pocket) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_SERIAL, "Default string"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
		},
		.driver_data = (void *)&gpd_win,
	}, {	/* GPD Win 2 (too generic strings, also match on bios date) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Default string"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		},
		.driver_data = (void *)&gpd_win2,
	}, {	/* GPD Win 2 (correct DMI strings) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "GPD"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "WIN2")
		},
		.driver_data = (void *)&lcd720x1280_rightside_up,
	}, {	/* GPD Win 3 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "GPD"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "G1618-03")
		},
		.driver_data = (void *)&lcd720x1280_rightside_up,
	}, {	/* GPD Win Mini */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "GPD"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "G1617-01")
		},
		.driver_data = (void *)&lcd1080x1920_rightside_up,
	}, {	/* I.T.Works TW891 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "To be filled by O.E.M."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "TW891"),
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "To be filled by O.E.M."),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "TW891"),
		},
		.driver_data = (void *)&itworks_tw891,
	}, {	/* Kogan Atlas 2-in-1 D300 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Kogan"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "KALAP10D300EA"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* KD Kurio Smart C15200 2-in-1 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "KD Interactive"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Kurio Smart"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "KDM960BCP"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/*
		 * Lenovo Ideapad Miix 310 laptop, only some production batches
		 * have a portrait screen, the resolution checks makes the quirk
		 * apply only to those batches.
		 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "80SG"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "MIIX 310-10ICR"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* Lenovo Ideapad Miix 320 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "80XF"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "Lenovo MIIX 320-10ICR"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* Lenovo Ideapad D330-10IGM (HD) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "Lenovo ideapad D330-10IGM"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* Lenovo Ideapad D330-10IGM (FHD) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "Lenovo ideapad D330-10IGM"),
		},
		.driver_data = (void *)&lcd1200x1920_rightside_up,
	}, {	/* Lenovo Ideapad D330-10IGL (HD) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "Lenovo ideapad D330-10IGL"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* Lenovo IdeaPad Duet 3 10IGL5 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "IdeaPad Duet 3 10IGL5"),
		},
		.driver_data = (void *)&lcd1200x1920_rightside_up,
	}, {	/* Lenovo Legion Go 8APU1 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "Legion Go 8APU1"),
		},
		.driver_data = (void *)&lcd1600x2560_leftside_up,
	}, {	/* Lenovo Yoga Book X90F / X90L */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "CHERRYVIEW D1 PLATFORM"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "YETI-11"),
		},
		.driver_data = (void *)&lcd1200x1920_rightside_up,
	}, {	/* Lenovo Yoga Book X91F / X91L */
		.matches = {
		  /* Non exact match to match F + L versions */
		  DMI_MATCH(DMI_PRODUCT_NAME, "Lenovo YB1-X91"),
		},
		.driver_data = (void *)&lcd1200x1920_rightside_up,
	}, {	/* Lenovo Yoga Tablet 2 830F / 830L */
		.matches = {
		 /*
		  * Note this also matches the Lenovo Yoga Tablet 2 1050F/L
		  * since that uses the same mainboard. The resolution match
		  * will limit this to only matching on the 830F/L. Neither has
		  * any external video outputs so those are not a concern.
		  */
		 DMI_MATCH(DMI_SYS_VENDOR, "Intel Corp."),
		 DMI_MATCH(DMI_PRODUCT_NAME, "VALLEYVIEW C0 PLATFORM"),
		 DMI_MATCH(DMI_BOARD_NAME, "BYT-T FFD8"),
		 /* Partial match on beginning of BIOS version */
		 DMI_MATCH(DMI_BIOS_VERSION, "BLADE_21"),
		},
		.driver_data = (void *)&lcd1200x1920_rightside_up,
	}, {	/* Lenovo Yoga Tab 3 X90F */
		.matches = {
		 DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
		 DMI_MATCH(DMI_PRODUCT_VERSION, "Blade3-10A-001"),
		},
		.driver_data = (void *)&lcd1600x2560_rightside_up,
	}, {	/* Nanote UMPC-01 */
		.matches = {
		 DMI_MATCH(DMI_SYS_VENDOR, "RWC CO.,LTD"),
		 DMI_MATCH(DMI_PRODUCT_NAME, "UMPC-01"),
		},
		.driver_data = (void *)&lcd1200x1920_rightside_up,
	}, {	/* OneGX1 Pro */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "SYSTEM_MANUFACTURER"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "SYSTEM_PRODUCT_NAME"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "Default string"),
		},
		.driver_data = (void *)&onegx1_pro,
	}, {	/* OneXPlayer */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ONE-NETBOOK TECHNOLOGY CO., LTD."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "ONE XPLAYER"),
		},
		.driver_data = (void *)&lcd1600x2560_leftside_up,
	}, {	/* OneXPlayer Mini (Intel) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ONE-NETBOOK TECHNOLOGY CO., LTD."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "ONE XPLAYER"),
		},
		.driver_data = (void *)&lcd1200x1920_leftside_up,
	}, {	/* OrangePi Neo */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "OrangePi"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "NEO-01"),
		},
		.driver_data = (void *)&lcd1200x1920_rightside_up,
	}, {	/* Samsung GalaxyBook 10.6 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Galaxy Book 10.6"),
		},
		.driver_data = (void *)&lcd1280x1920_rightside_up,
	}, {	/* Valve Steam Deck (Jupiter) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Valve"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jupiter"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "1"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* Valve Steam Deck (Galileo) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Valve"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Galileo"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "1"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* VIOS LTH17 */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "VIOS"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "LTH17"),
		},
		.driver_data = (void *)&lcd800x1280_rightside_up,
	}, {	/* One Mix 2S (generic strings, also match on bios date) */
		.matches = {
		  DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Default string"),
		  DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Default string"),
		  DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		},
		.driver_data = (void *)&gpd_onemix2s,
	},
	{}
};

/**
 * drm_get_panel_orientation_quirk - Check for panel orientation quirks
 * @width: width in pixels of the panel
 * @height: height in pixels of the panel
 *
 * This function checks for platform specific (e.g. DMI based) quirks
 * providing info on panel_orientation for systems where this cannot be
 * probed from the hard-/firm-ware. To avoid false-positive this function
 * takes the panel resolution as argument and checks that against the
 * resolution expected by the quirk-table entry.
 *
 * Note this function is also used outside of the drm-subsys, by for example
 * the efifb code. Because of this this function gets compiled into its own
 * kernel-module when built as a module.
 *
 * Returns:
 * A DRM_MODE_PANEL_ORIENTATION_* value if there is a quirk for this system,
 * or DRM_MODE_PANEL_ORIENTATION_UNKNOWN if there is no quirk.
 */
int drm_get_panel_orientation_quirk(int width, int height)
{
	const struct dmi_system_id *match;
	const struct drm_dmi_panel_orientation_data *data;
	const char *bios_date;
	int i;

	for (match = dmi_first_match(orientation_data);
	     match;
	     match = dmi_first_match(match + 1)) {
		data = match->driver_data;

		if (data->width != width ||
		    data->height != height)
			continue;

		if (!data->bios_dates)
			return data->orientation;

		bios_date = dmi_get_system_info(DMI_BIOS_DATE);
		if (!bios_date)
			continue;

		i = match_string(data->bios_dates, -1, bios_date);
		if (i >= 0)
			return data->orientation;
	}

	return DRM_MODE_PANEL_ORIENTATION_UNKNOWN;
}
EXPORT_SYMBOL(drm_get_panel_orientation_quirk);

#else

/* There are no quirks for non x86 devices yet */
int drm_get_panel_orientation_quirk(int width, int height)
{
	return DRM_MODE_PANEL_ORIENTATION_UNKNOWN;
}
EXPORT_SYMBOL(drm_get_panel_orientation_quirk);

#endif

MODULE_DESCRIPTION("Quirks for non-normal panel orientation");
MODULE_LICENSE("Dual MIT/GPL");
