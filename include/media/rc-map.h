/*
 * rc-map.h - define RC map names used by RC drivers
 *
 * Copyright (c) 2010 by Mauro Carvalho Chehab
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/input.h>
#include <uapi/linux/lirc.h>

#define RC_PROTO_BIT_NONE		0ULL
#define RC_PROTO_BIT_UNKNOWN		BIT_ULL(RC_PROTO_UNKNOWN)
#define RC_PROTO_BIT_OTHER		BIT_ULL(RC_PROTO_OTHER)
#define RC_PROTO_BIT_RC5		BIT_ULL(RC_PROTO_RC5)
#define RC_PROTO_BIT_RC5X_20		BIT_ULL(RC_PROTO_RC5X_20)
#define RC_PROTO_BIT_RC5_SZ		BIT_ULL(RC_PROTO_RC5_SZ)
#define RC_PROTO_BIT_JVC		BIT_ULL(RC_PROTO_JVC)
#define RC_PROTO_BIT_SONY12		BIT_ULL(RC_PROTO_SONY12)
#define RC_PROTO_BIT_SONY15		BIT_ULL(RC_PROTO_SONY15)
#define RC_PROTO_BIT_SONY20		BIT_ULL(RC_PROTO_SONY20)
#define RC_PROTO_BIT_NEC		BIT_ULL(RC_PROTO_NEC)
#define RC_PROTO_BIT_NECX		BIT_ULL(RC_PROTO_NECX)
#define RC_PROTO_BIT_NEC32		BIT_ULL(RC_PROTO_NEC32)
#define RC_PROTO_BIT_SANYO		BIT_ULL(RC_PROTO_SANYO)
#define RC_PROTO_BIT_MCIR2_KBD		BIT_ULL(RC_PROTO_MCIR2_KBD)
#define RC_PROTO_BIT_MCIR2_MSE		BIT_ULL(RC_PROTO_MCIR2_MSE)
#define RC_PROTO_BIT_RC6_0		BIT_ULL(RC_PROTO_RC6_0)
#define RC_PROTO_BIT_RC6_6A_20		BIT_ULL(RC_PROTO_RC6_6A_20)
#define RC_PROTO_BIT_RC6_6A_24		BIT_ULL(RC_PROTO_RC6_6A_24)
#define RC_PROTO_BIT_RC6_6A_32		BIT_ULL(RC_PROTO_RC6_6A_32)
#define RC_PROTO_BIT_RC6_MCE		BIT_ULL(RC_PROTO_RC6_MCE)
#define RC_PROTO_BIT_SHARP		BIT_ULL(RC_PROTO_SHARP)
#define RC_PROTO_BIT_XMP		BIT_ULL(RC_PROTO_XMP)
#define RC_PROTO_BIT_CEC		BIT_ULL(RC_PROTO_CEC)
#define RC_PROTO_BIT_IMON		BIT_ULL(RC_PROTO_IMON)

#define RC_PROTO_BIT_ALL \
			(RC_PROTO_BIT_UNKNOWN | RC_PROTO_BIT_OTHER | \
			 RC_PROTO_BIT_RC5 | RC_PROTO_BIT_RC5X_20 | \
			 RC_PROTO_BIT_RC5_SZ | RC_PROTO_BIT_JVC | \
			 RC_PROTO_BIT_SONY12 | RC_PROTO_BIT_SONY15 | \
			 RC_PROTO_BIT_SONY20 | RC_PROTO_BIT_NEC | \
			 RC_PROTO_BIT_NECX | RC_PROTO_BIT_NEC32 | \
			 RC_PROTO_BIT_SANYO | \
			 RC_PROTO_BIT_MCIR2_KBD | RC_PROTO_BIT_MCIR2_MSE | \
			 RC_PROTO_BIT_RC6_0 | RC_PROTO_BIT_RC6_6A_20 | \
			 RC_PROTO_BIT_RC6_6A_24 | RC_PROTO_BIT_RC6_6A_32 | \
			 RC_PROTO_BIT_RC6_MCE | RC_PROTO_BIT_SHARP | \
			 RC_PROTO_BIT_XMP | RC_PROTO_BIT_CEC | \
			 RC_PROTO_BIT_IMON)
/* All rc protocols for which we have decoders */
#define RC_PROTO_BIT_ALL_IR_DECODER \
			(RC_PROTO_BIT_RC5 | RC_PROTO_BIT_RC5X_20 | \
			 RC_PROTO_BIT_RC5_SZ | RC_PROTO_BIT_JVC | \
			 RC_PROTO_BIT_SONY12 | RC_PROTO_BIT_SONY15 | \
			 RC_PROTO_BIT_SONY20 | RC_PROTO_BIT_NEC | \
			 RC_PROTO_BIT_NECX | RC_PROTO_BIT_NEC32 | \
			 RC_PROTO_BIT_SANYO | RC_PROTO_BIT_MCIR2_KBD | \
			 RC_PROTO_BIT_MCIR2_MSE | \
			 RC_PROTO_BIT_RC6_0 | RC_PROTO_BIT_RC6_6A_20 | \
			 RC_PROTO_BIT_RC6_6A_24 |  RC_PROTO_BIT_RC6_6A_32 | \
			 RC_PROTO_BIT_RC6_MCE | RC_PROTO_BIT_SHARP | \
			 RC_PROTO_BIT_XMP | RC_PROTO_BIT_IMON)

#define RC_PROTO_BIT_ALL_IR_ENCODER \
			(RC_PROTO_BIT_RC5 | RC_PROTO_BIT_RC5X_20 | \
			 RC_PROTO_BIT_RC5_SZ | RC_PROTO_BIT_JVC | \
			 RC_PROTO_BIT_SONY12 | RC_PROTO_BIT_SONY15 | \
			 RC_PROTO_BIT_SONY20 |  RC_PROTO_BIT_NEC | \
			 RC_PROTO_BIT_NECX | RC_PROTO_BIT_NEC32 | \
			 RC_PROTO_BIT_SANYO | RC_PROTO_BIT_MCIR2_KBD | \
			 RC_PROTO_BIT_MCIR2_MSE | \
			 RC_PROTO_BIT_RC6_0 | RC_PROTO_BIT_RC6_6A_20 | \
			 RC_PROTO_BIT_RC6_6A_24 | \
			 RC_PROTO_BIT_RC6_6A_32 | RC_PROTO_BIT_RC6_MCE | \
			 RC_PROTO_BIT_SHARP | RC_PROTO_BIT_IMON)

#define RC_SCANCODE_UNKNOWN(x)			(x)
#define RC_SCANCODE_OTHER(x)			(x)
#define RC_SCANCODE_NEC(addr, cmd)		(((addr) << 8) | (cmd))
#define RC_SCANCODE_NECX(addr, cmd)		(((addr) << 8) | (cmd))
#define RC_SCANCODE_NEC32(data)			((data) & 0xffffffff)
#define RC_SCANCODE_RC5(sys, cmd)		(((sys) << 8) | (cmd))
#define RC_SCANCODE_RC5_SZ(sys, cmd)		(((sys) << 8) | (cmd))
#define RC_SCANCODE_RC6_0(sys, cmd)		(((sys) << 8) | (cmd))
#define RC_SCANCODE_RC6_6A(vendor, sys, cmd)	(((vendor) << 16) | ((sys) << 8) | (cmd))

/**
 * struct rc_map_table - represents a scancode/keycode pair
 *
 * @scancode: scan code (u32)
 * @keycode: Linux input keycode
 */
struct rc_map_table {
	u32	scancode;
	u32	keycode;
};

/**
 * struct rc_map - represents a keycode map table
 *
 * @scan: pointer to struct &rc_map_table
 * @size: Max number of entries
 * @len: Number of entries that are in use
 * @alloc: size of \*scan, in bytes
 * @rc_proto: type of the remote controller protocol, as defined at
 *	     enum &rc_proto
 * @name: name of the key map table
 * @lock: lock to protect access to this structure
 */
struct rc_map {
	struct rc_map_table	*scan;
	unsigned int		size;
	unsigned int		len;
	unsigned int		alloc;
	enum rc_proto		rc_proto;
	const char		*name;
	spinlock_t		lock;
};

/**
 * struct rc_map_list - list of the registered &rc_map maps
 *
 * @list: pointer to struct &list_head
 * @map: pointer to struct &rc_map
 */
struct rc_map_list {
	struct list_head	 list;
	struct rc_map map;
};

/* Routines from rc-map.c */

/**
 * rc_map_register() - Registers a Remote Controler scancode map
 *
 * @map:	pointer to struct rc_map_list
 */
int rc_map_register(struct rc_map_list *map);

/**
 * rc_map_unregister() - Unregisters a Remote Controler scancode map
 *
 * @map:	pointer to struct rc_map_list
 */
void rc_map_unregister(struct rc_map_list *map);

/**
 * rc_map_get - gets an RC map from its name
 * @name: name of the RC scancode map
 */
struct rc_map *rc_map_get(const char *name);

/* Names of the several keytables defined in-kernel */

#define RC_MAP_ADSTECH_DVB_T_PCI         "rc-adstech-dvb-t-pci"
#define RC_MAP_ALINK_DTU_M               "rc-alink-dtu-m"
#define RC_MAP_ANYSEE                    "rc-anysee"
#define RC_MAP_APAC_VIEWCOMP             "rc-apac-viewcomp"
#define RC_MAP_ASUS_PC39                 "rc-asus-pc39"
#define RC_MAP_ASUS_PS3_100              "rc-asus-ps3-100"
#define RC_MAP_ATI_TV_WONDER_HD_600      "rc-ati-tv-wonder-hd-600"
#define RC_MAP_ATI_X10                   "rc-ati-x10"
#define RC_MAP_AVERMEDIA_A16D            "rc-avermedia-a16d"
#define RC_MAP_AVERMEDIA_CARDBUS         "rc-avermedia-cardbus"
#define RC_MAP_AVERMEDIA_DVBT            "rc-avermedia-dvbt"
#define RC_MAP_AVERMEDIA_M135A           "rc-avermedia-m135a"
#define RC_MAP_AVERMEDIA_M733A_RM_K6     "rc-avermedia-m733a-rm-k6"
#define RC_MAP_AVERMEDIA_RM_KS           "rc-avermedia-rm-ks"
#define RC_MAP_AVERMEDIA                 "rc-avermedia"
#define RC_MAP_AVERTV_303                "rc-avertv-303"
#define RC_MAP_AZUREWAVE_AD_TU700        "rc-azurewave-ad-tu700"
#define RC_MAP_BEHOLD_COLUMBUS           "rc-behold-columbus"
#define RC_MAP_BEHOLD                    "rc-behold"
#define RC_MAP_BUDGET_CI_OLD             "rc-budget-ci-old"
#define RC_MAP_CEC                       "rc-cec"
#define RC_MAP_CINERGY_1400              "rc-cinergy-1400"
#define RC_MAP_CINERGY                   "rc-cinergy"
#define RC_MAP_DELOCK_61959              "rc-delock-61959"
#define RC_MAP_DIB0700_NEC_TABLE         "rc-dib0700-nec"
#define RC_MAP_DIB0700_RC5_TABLE         "rc-dib0700-rc5"
#define RC_MAP_DIGITALNOW_TINYTWIN       "rc-digitalnow-tinytwin"
#define RC_MAP_DIGITTRADE                "rc-digittrade"
#define RC_MAP_DM1105_NEC                "rc-dm1105-nec"
#define RC_MAP_DNTV_LIVE_DVBT_PRO        "rc-dntv-live-dvbt-pro"
#define RC_MAP_DNTV_LIVE_DVB_T           "rc-dntv-live-dvb-t"
#define RC_MAP_DVBSKY                    "rc-dvbsky"
#define RC_MAP_EMPTY                     "rc-empty"
#define RC_MAP_EM_TERRATEC               "rc-em-terratec"
#define RC_MAP_ENCORE_ENLTV2             "rc-encore-enltv2"
#define RC_MAP_ENCORE_ENLTV_FM53         "rc-encore-enltv-fm53"
#define RC_MAP_ENCORE_ENLTV              "rc-encore-enltv"
#define RC_MAP_EVGA_INDTUBE              "rc-evga-indtube"
#define RC_MAP_EZTV                      "rc-eztv"
#define RC_MAP_FLYDVB                    "rc-flydvb"
#define RC_MAP_FLYVIDEO                  "rc-flyvideo"
#define RC_MAP_FUSIONHDTV_MCE            "rc-fusionhdtv-mce"
#define RC_MAP_GADMEI_RM008Z             "rc-gadmei-rm008z"
#define RC_MAP_GEEKBOX                   "rc-geekbox"
#define RC_MAP_GENIUS_TVGO_A11MCE        "rc-genius-tvgo-a11mce"
#define RC_MAP_GOTVIEW7135               "rc-gotview7135"
#define RC_MAP_HAUPPAUGE_NEW             "rc-hauppauge"
#define RC_MAP_IMON_MCE                  "rc-imon-mce"
#define RC_MAP_IMON_PAD                  "rc-imon-pad"
#define RC_MAP_IMON_RSC                  "rc-imon-rsc"
#define RC_MAP_IODATA_BCTV7E             "rc-iodata-bctv7e"
#define RC_MAP_IT913X_V1                 "rc-it913x-v1"
#define RC_MAP_IT913X_V2                 "rc-it913x-v2"
#define RC_MAP_KAIOMY                    "rc-kaiomy"
#define RC_MAP_KWORLD_315U               "rc-kworld-315u"
#define RC_MAP_KWORLD_PC150U             "rc-kworld-pc150u"
#define RC_MAP_KWORLD_PLUS_TV_ANALOG     "rc-kworld-plus-tv-analog"
#define RC_MAP_LEADTEK_Y04G0051          "rc-leadtek-y04g0051"
#define RC_MAP_LME2510                   "rc-lme2510"
#define RC_MAP_MANLI                     "rc-manli"
#define RC_MAP_MEDION_X10                "rc-medion-x10"
#define RC_MAP_MEDION_X10_DIGITAINER     "rc-medion-x10-digitainer"
#define RC_MAP_MEDION_X10_OR2X           "rc-medion-x10-or2x"
#define RC_MAP_MSI_DIGIVOX_II            "rc-msi-digivox-ii"
#define RC_MAP_MSI_DIGIVOX_III           "rc-msi-digivox-iii"
#define RC_MAP_MSI_TVANYWHERE_PLUS       "rc-msi-tvanywhere-plus"
#define RC_MAP_MSI_TVANYWHERE            "rc-msi-tvanywhere"
#define RC_MAP_NEBULA                    "rc-nebula"
#define RC_MAP_NEC_TERRATEC_CINERGY_XS   "rc-nec-terratec-cinergy-xs"
#define RC_MAP_NORWOOD                   "rc-norwood"
#define RC_MAP_NPGTECH                   "rc-npgtech"
#define RC_MAP_PCTV_SEDNA                "rc-pctv-sedna"
#define RC_MAP_PINNACLE_COLOR            "rc-pinnacle-color"
#define RC_MAP_PINNACLE_GREY             "rc-pinnacle-grey"
#define RC_MAP_PINNACLE_PCTV_HD          "rc-pinnacle-pctv-hd"
#define RC_MAP_PIXELVIEW_NEW             "rc-pixelview-new"
#define RC_MAP_PIXELVIEW                 "rc-pixelview"
#define RC_MAP_PIXELVIEW_002T		 "rc-pixelview-002t"
#define RC_MAP_PIXELVIEW_MK12            "rc-pixelview-mk12"
#define RC_MAP_POWERCOLOR_REAL_ANGEL     "rc-powercolor-real-angel"
#define RC_MAP_PROTEUS_2309              "rc-proteus-2309"
#define RC_MAP_PURPLETV                  "rc-purpletv"
#define RC_MAP_PV951                     "rc-pv951"
#define RC_MAP_HAUPPAUGE                 "rc-hauppauge"
#define RC_MAP_RC5_TV                    "rc-rc5-tv"
#define RC_MAP_RC6_MCE                   "rc-rc6-mce"
#define RC_MAP_REAL_AUDIO_220_32_KEYS    "rc-real-audio-220-32-keys"
#define RC_MAP_REDDO                     "rc-reddo"
#define RC_MAP_SNAPSTREAM_FIREFLY        "rc-snapstream-firefly"
#define RC_MAP_STREAMZAP                 "rc-streamzap"
#define RC_MAP_TBS_NEC                   "rc-tbs-nec"
#define RC_MAP_TECHNISAT_TS35            "rc-technisat-ts35"
#define RC_MAP_TECHNISAT_USB2            "rc-technisat-usb2"
#define RC_MAP_TERRATEC_CINERGY_C_PCI    "rc-terratec-cinergy-c-pci"
#define RC_MAP_TERRATEC_CINERGY_S2_HD    "rc-terratec-cinergy-s2-hd"
#define RC_MAP_TERRATEC_CINERGY_XS       "rc-terratec-cinergy-xs"
#define RC_MAP_TERRATEC_SLIM             "rc-terratec-slim"
#define RC_MAP_TERRATEC_SLIM_2           "rc-terratec-slim-2"
#define RC_MAP_TEVII_NEC                 "rc-tevii-nec"
#define RC_MAP_TIVO                      "rc-tivo"
#define RC_MAP_TOTAL_MEDIA_IN_HAND       "rc-total-media-in-hand"
#define RC_MAP_TOTAL_MEDIA_IN_HAND_02    "rc-total-media-in-hand-02"
#define RC_MAP_TREKSTOR                  "rc-trekstor"
#define RC_MAP_TT_1500                   "rc-tt-1500"
#define RC_MAP_TWINHAN_DTV_CAB_CI        "rc-twinhan-dtv-cab-ci"
#define RC_MAP_TWINHAN_VP1027_DVBS       "rc-twinhan1027"
#define RC_MAP_VIDEOMATE_K100            "rc-videomate-k100"
#define RC_MAP_VIDEOMATE_S350            "rc-videomate-s350"
#define RC_MAP_VIDEOMATE_TV_PVR          "rc-videomate-tv-pvr"
#define RC_MAP_WINFAST                   "rc-winfast"
#define RC_MAP_WINFAST_USBII_DELUXE      "rc-winfast-usbii-deluxe"
#define RC_MAP_SU3000                    "rc-su3000"

/*
 * Please, do not just append newer Remote Controller names at the end.
 * The names should be ordered in alphabetical order
 */
