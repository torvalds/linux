/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BOARD_ID_H
#define __BOARD_ID_H
#include <linux/miscdevice.h>
#include <linux/board-id-hw.h>

enum type_devices{
	DEVICE_TYPE_NULL = 0x0,	

	DEVICE_TYPE_SUM = 0x20,	
	DEVICE_TYPE_AREA = 0x24,	//
	DEVICE_TYPE_OPERATOR = 0x25,	
	DEVICE_TYPE_OPERATOR2 = 0x26,
	DEVICE_TYPE_RESERVE = 0x27,	
	DEVICE_TYPE_STATUS = 0x28,

	DEVICE_TYPE_TP = 0x29,		//one byte size
	DEVICE_TYPE_LCD,	
	DEVICE_TYPE_KEY,	
	DEVICE_TYPE_CODEC,
	DEVICE_TYPE_WIFI,
	DEVICE_TYPE_BT,	
	DEVICE_TYPE_GPS,	
	DEVICE_TYPE_FM,	
	DEVICE_TYPE_MODEM,	
	DEVICE_TYPE_DDR,
	DEVICE_TYPE_FLASH,
	DEVICE_TYPE_HDMI,
	DEVICE_TYPE_BATTERY,
	DEVICE_TYPE_CHARGE,	
	DEVICE_TYPE_BACKLIGHT,
	DEVICE_TYPE_HEADSET,
	DEVICE_TYPE_MICPHONE,
	DEVICE_TYPE_SPEAKER,
	DEVICE_TYPE_VIBRATOR,
	DEVICE_TYPE_TV,	
	DEVICE_TYPE_ECHIP,	//30
	DEVICE_TYPE_HUB,
	DEVICE_TYPE_TPAD,
	
	DEVICE_TYPE_PMIC,
	DEVICE_TYPE_REGULATOR,
	DEVICE_TYPE_RTC,
	DEVICE_TYPE_CAMERA_FRONT,
	DEVICE_TYPE_CAMERA_BACK,	//35
	DEVICE_TYPE_ANGLE,
	DEVICE_TYPE_ACCEL,
	DEVICE_TYPE_COMPASS,
	DEVICE_TYPE_GYRO,
	DEVICE_TYPE_LIGHT,
	DEVICE_TYPE_PROXIMITY,
	DEVICE_TYPE_TEMPERATURE,	
	DEVICE_TYPE_PRESSURE,
	
	DEVICE_NUM_TYPES,
};


#if 0
enum id_language{
	LANGUAGE_ID_NULL = 0,
	LANGUAGE_ID_EN,// 英文 
	LANGUAGE_ID_EN_US,// 英文 (美国) 
	LANGUAGE_ID_AR,// 阿拉伯文 
	LANGUAGE_ID_AR_AE,// 阿拉伯文 (阿拉伯联合酋长国) 
	LANGUAGE_ID_AR_BH,// 阿拉伯文 (巴林) 
	LANGUAGE_ID_AR_DZ,// 阿拉伯文 (阿尔及利亚) 
	LANGUAGE_ID_AR_EG,// 阿拉伯文 (埃及) 
	LANGUAGE_ID_AR_IQ,// 阿拉伯文 (伊拉克) 
	LANGUAGE_ID_AR_JO,// 阿拉伯文 (约旦) 
	LANGUAGE_ID_AR_KW,// 阿拉伯文 (科威特) 
	LANGUAGE_ID_AR_LB,// 阿拉伯文 (黎巴嫩) 
	LANGUAGE_ID_AR_LY,// 阿拉伯文 (利比亚) 
	LANGUAGE_ID_AR_MA,// 阿拉伯文 (摩洛哥) 
	LANGUAGE_ID_AR_OM,// 阿拉伯文 (阿曼) 
	LANGUAGE_ID_AR_QA,// 阿拉伯文 (卡塔尔) 
	LANGUAGE_ID_AR_SA,// 阿拉伯文 (沙特阿拉伯) 
	LANGUAGE_ID_AR_SD,// 阿拉伯文 (苏丹) 
	LANGUAGE_ID_AR_SY,// 阿拉伯文 (叙利亚) 
	LANGUAGE_ID_AR_TN,// 阿拉伯文 (突尼斯) 
	LANGUAGE_ID_AR_YE,// 阿拉伯文 (也门) 
	LANGUAGE_ID_BE,// 白俄罗斯文 
	LANGUAGE_ID_BE_BY,// 白俄罗斯文 (白俄罗斯) 
	LANGUAGE_ID_BG,// 保加利亚文 
	LANGUAGE_ID_BG_BG,// 保加利亚文 (保加利亚) 
	LANGUAGE_ID_CA,// 加泰罗尼亚文 
	LANGUAGE_ID_CA_ES,// 加泰罗尼亚文 (西班牙) 
	LANGUAGE_ID_CA_ES_EURO,// 加泰罗尼亚文 (西班牙,EURO) 
	LANGUAGE_ID_CS,// 捷克文 
	LANGUAGE_ID_CS_CZ,// 捷克文 (捷克共和国) 
	LANGUAGE_ID_DA,// 丹麦文 
	LANGUAGE_ID_DA_DK,// 丹麦文 (丹麦) 
	LANGUAGE_ID_DE,// 德文 
	LANGUAGE_ID_DE_AT,// 德文 (奥地利) 
	LANGUAGE_ID_DE_AT_EURO,// 德文 (奥地利,EURO) 
	LANGUAGE_ID_DE_CH,// 德文 (瑞士) 
	LANGUAGE_ID_DE_DE,// 德文 (德国) 
	LANGUAGE_ID_DE_DE_EURO,// 德文 (德国,EURO) 
	LANGUAGE_ID_DE_LU,// 德文 (卢森堡) 
	LANGUAGE_ID_DE_LU_EURO,// 德文 (卢森堡,EURO) 
	LANGUAGE_ID_EL,// 希腊文 
	LANGUAGE_ID_EL_GR,// 希腊文 (希腊) 
	LANGUAGE_ID_EN_AU,// 英文 (澳大利亚) 
	LANGUAGE_ID_EN_CA,// 英文 (加拿大) 
	LANGUAGE_ID_EN_GB,// 英文 (英国) 
	LANGUAGE_ID_EN_IE,// 英文 (爱尔兰) 
	LANGUAGE_ID_EN_IE_EURO,// 英文 (爱尔兰,EURO) 
	LANGUAGE_ID_EN_NZ,// 英文 (新西兰) 
	LANGUAGE_ID_EN_ZA,// 英文 (南非) 
	LANGUAGE_ID_ES,// 西班牙文 
	LANGUAGE_ID_ES_BO,// 西班牙文 (玻利维亚) 
	LANGUAGE_ID_ES_AR,// 西班牙文 (阿根廷) 
	LANGUAGE_ID_ES_CL,// 西班牙文 (智利) 
	LANGUAGE_ID_ES_CO,// 西班牙文 (哥伦比亚) 
	LANGUAGE_ID_ES_CR,// 西班牙文 (哥斯达黎加) 
	LANGUAGE_ID_ES_DO,// 西班牙文 (多米尼加共和国) 
	LANGUAGE_ID_ES_EC,// 西班牙文 (厄瓜多尔) 
	LANGUAGE_ID_ES_ES,// 西班牙文 (西班牙) 
	LANGUAGE_ID_ES_ES_EURO,// 西班牙文 (西班牙,EURO) 
	LANGUAGE_ID_ES_GT,// 西班牙文 (危地马拉) 
	LANGUAGE_ID_ES_HN,// 西班牙文 (洪都拉斯) 
	LANGUAGE_ID_ES_MX,// 西班牙文 (墨西哥) 
	LANGUAGE_ID_ES_NI,// 西班牙文 (尼加拉瓜) 
	LANGUAGE_ID_ET,// 爱沙尼亚文 
	LANGUAGE_ID_ES_PA,// 西班牙文 (巴拿马) 
	LANGUAGE_ID_ES_PE,// 西班牙文 (秘鲁) 
	LANGUAGE_ID_ES_PR,// 西班牙文 (波多黎哥) 
	LANGUAGE_ID_ES_PY,// 西班牙文 (巴拉圭) 
	LANGUAGE_ID_ES_SV,// 西班牙文 (萨尔瓦多) 
	LANGUAGE_ID_ES_UY,// 西班牙文 (乌拉圭) 
	LANGUAGE_ID_ES_VE,// 西班牙文 (委内瑞拉) 
	LANGUAGE_ID_ET_EE,// 爱沙尼亚文 (爱沙尼亚) 
	LANGUAGE_ID_FI,// 芬兰文 
	LANGUAGE_ID_FI_FI,// 芬兰文 (芬兰) 
	LANGUAGE_ID_FI_FI_EURO,// 芬兰文 (芬兰,EURO) 
	LANGUAGE_ID_FR,// 法文 
	LANGUAGE_ID_FR_BE,// 法文 (比利时) 
	LANGUAGE_ID_FR_BE_EURO,// 法文 (比利时,EURO) 
	LANGUAGE_ID_FR_CA,// 法文 (加拿大) 
	LANGUAGE_ID_FR_CH,// 法文 (瑞士) 
	LANGUAGE_ID_FR_FR,// 法文 (法国) 
	LANGUAGE_ID_FR_FR_EURO,// 法文 (法国,EURO) 
	LANGUAGE_ID_FR_LU,// 法文 (卢森堡) 
	LANGUAGE_ID_FR_LU_EURO,// 法文 (卢森堡,EURO) 
	LANGUAGE_ID_HR,// 克罗地亚文 
	LANGUAGE_ID_HR_HR,// 克罗地亚文 (克罗地亚) 
	LANGUAGE_ID_HU,// 匈牙利文 
	LANGUAGE_ID_HU_HU,// 匈牙利文 (匈牙利) 
	LANGUAGE_ID_IS,// 冰岛文 
	LANGUAGE_ID_IS_IS,// 冰岛文 (冰岛) 
	LANGUAGE_ID_IT,// 意大利文 
	LANGUAGE_ID_IT_CH,// 意大利文 (瑞士) 
	LANGUAGE_ID_IT_IT,// 意大利文 (意大利) 
	LANGUAGE_ID_IT_IT_EURO,// 意大利文 (意大利,EURO) 
	LANGUAGE_ID_IW,// 希伯来文 
	LANGUAGE_ID_IW_IL,// 希伯来文 (以色列) 
	LANGUAGE_ID_JA,// 日文 
	LANGUAGE_ID_JA_JP,// 日文 (日本) 
	LANGUAGE_ID_KO,// 朝鲜文 
	LANGUAGE_ID_KO_KR,// 朝鲜文 (南朝鲜) 
	LANGUAGE_ID_LT,// 立陶宛文 
	LANGUAGE_ID_LT_LT,// 立陶宛文 (立陶宛) 
	LANGUAGE_ID_LV,// 拉托维亚文(列托) 
	LANGUAGE_ID_LV_LV,// 拉托维亚文(列托) (拉脱维亚) 
	LANGUAGE_ID_MK,// 马其顿文 
	LANGUAGE_ID_MK_MK,// 马其顿文 (马其顿王国) 
	LANGUAGE_ID_NL,// 荷兰文 
	LANGUAGE_ID_NL_BE,// 荷兰文 (比利时) 
	LANGUAGE_ID_NL_BE_EURO,// 荷兰文 (比利时,EURO) 
	LANGUAGE_ID_NL_NL,// 荷兰文 (荷兰) 
	LANGUAGE_ID_NL_NL_EURO,// 荷兰文 (荷兰,EURO) 
	LANGUAGE_ID_NO,// 挪威文 
	LANGUAGE_ID_NO_NO,// 挪威文 (挪威) 
	LANGUAGE_ID_NO_NO_NY,// 挪威文 (挪威,NYNORSK) 
	LANGUAGE_ID_PL,// 波兰文 
	LANGUAGE_ID_PL_PL,// 波兰文 (波兰) 
	LANGUAGE_ID_PT,// 葡萄牙文 
	LANGUAGE_ID_PT_BR,// 葡萄牙文 (巴西) 
	LANGUAGE_ID_PT_PT,// 葡萄牙文 (葡萄牙) 
	LANGUAGE_ID_PT_PT_EURO,// 葡萄牙文 (葡萄牙,EURO) 
	LANGUAGE_ID_RO,// 罗马尼亚文 
	LANGUAGE_ID_RO_RO,// 罗马尼亚文 (罗马尼亚) 
	LANGUAGE_ID_RU,// 俄文 
	LANGUAGE_ID_RU_RU,// 俄文 (俄罗斯) 
	LANGUAGE_ID_SH,// 塞波尼斯-克罗地亚文 
	LANGUAGE_ID_SH_YU,// 塞波尼斯-克罗地亚文 (南斯拉夫) 
	LANGUAGE_ID_SK,// 斯洛伐克文 
	LANGUAGE_ID_SK_SK,// 斯洛伐克文 (斯洛伐克) 
	LANGUAGE_ID_SL,// 斯洛文尼亚文 
	LANGUAGE_ID_SL_SI,// 斯洛文尼亚文 (斯洛文尼亚) 
	LANGUAGE_ID_SQ,// 阿尔巴尼亚文 
	LANGUAGE_ID_SQ_AL,// 阿尔巴尼亚文 (阿尔巴尼亚) 
	LANGUAGE_ID_SR,// 塞尔维亚文 
	LANGUAGE_ID_SR_YU,// 塞尔维亚文 (南斯拉夫) 
	LANGUAGE_ID_SV,// 瑞典文 
	LANGUAGE_ID_SV_SE,// 瑞典文 (瑞典) 
	LANGUAGE_ID_TH,// 泰文 
	LANGUAGE_ID_TH_TH,// 泰文 (泰国) 
	LANGUAGE_ID_TR,// 土耳其文 
	LANGUAGE_ID_TR_TR,// 土耳其文 (土耳其) 
	LANGUAGE_ID_UK,// 乌克兰文 
	LANGUAGE_ID_UK_UA,// 乌克兰文 (乌克兰) 
	LANGUAGE_ID_ZH,// 中文 
	LANGUAGE_ID_ZH_CN,// 中文 (中国) 
	LANGUAGE_ID_ZH_HK,// 中文 (香港) 
	LANGUAGE_ID_ZH_TW,// 中文 (台湾) 　　 
	AREA_ID_NUMS,
};
#endif



enum area_id{
	AREA_ID_NULL,
	AREA_ID_ALBANIA,
	AREA_ID_ALGERIA,
	AREA_ID_ANGOLA,
	AREA_ID_ARGENTINA,
	AREA_ID_AUSTRALIA,
	AREA_ID_AUSTRIA,
	AREA_ID_AZERBAIJAN,
	AREA_ID_BAHRAIN,
	AREA_ID_BANGLADESH,
	AREA_ID_BARBADOS,
	AREA_ID_BELARUS,
	AREA_ID_BELGIUM,
	AREA_ID_BOLIVIA,
	AREA_ID_BOSNIA_AND_HERZEGOVINA,
	AREA_ID_BOTSWANA,
	AREA_ID_BRAZIL,
	AREA_ID_BULGARIA,
	AREA_ID_CANADA,
	AREA_ID_CHILE,
	AREA_ID_CHINA,
	AREA_ID_COLOMBIA,
	AREA_ID_COTE_D_IVOIRE,
	AREA_ID_CROATIA,
	AREA_ID_CYPRUS,
	AREA_ID_CZECH_REPUBLIC,
	AREA_ID_DENMARK,
	AREA_ID_ECUADOR,
	AREA_ID_EGYPT,
	AREA_ID_ESTONIA,
	AREA_ID_FINLAND,
	AREA_ID_FRANCE_INC_GUADELOUPE,
	AREA_ID_GEORGIA,
	AREA_ID_GERMANY,
	AREA_ID_GREECE,
	AREA_ID_HAITI,
	AREA_ID_HONDURAS,
	AREA_ID_HONG_KONG,
	AREA_ID_HUNGARY,
	AREA_ID_ICELAND,
	AREA_ID_INDIA,
	AREA_ID_INDONESIA,
	AREA_ID_IRELAND,
	AREA_ID_ISRAEL,
	AREA_ID_ITALY,
	AREA_ID_JAMAICA,
	AREA_ID_JAPAN,
	AREA_ID_JORDAN,
	AREA_ID_KAZAKHSTAN,
	AREA_ID_KENYA,
	AREA_ID_KOREA_SOUTH,
	AREA_ID_KUWAIT,
	AREA_ID_LATVIA,
	AREA_ID_LEBANON,
	AREA_ID_LITHUANIA,
	AREA_ID_LUXEMBOURG,
	AREA_ID_MACEDONIA,
	AREA_ID_MALAYSIA,
	AREA_ID_MEXICO,
	AREA_ID_MOLDOVA,
	AREA_ID_MOROCCO,
	AREA_ID_NEPAL,
	AREA_ID_NETHERLAND_ANTILLES,
	AREA_ID_NETHERLANDS_INC_BONAIRE,
	AREA_ID_NEW_ZEALAND,
	AREA_ID_NIGERIA,
	AREA_ID_NORWAY,
	AREA_ID_OMAN,
	AREA_ID_PAKISTAN,
	AREA_ID_PARAGUAY,
	AREA_ID_PERU,
	AREA_ID_PHILIPPINES,
	AREA_ID_POLAND,
	AREA_ID_PORTUGAL,
	AREA_ID_QATAR,
	AREA_ID_ROMANIA,
	AREA_ID_RUSSIA,
	AREA_ID_SAUDI_ARABIA,
	AREA_ID_SERBIA,
	AREA_ID_SINGAPORE,
	AREA_ID_SLOVAKIA,
	AREA_ID_SLOVENIA,
	AREA_ID_SOUTH_AFRICA,
	AREA_ID_SPAIN,
	AREA_ID_SRI_LANKA,
	AREA_ID_SWEDEN,
	AREA_ID_SWITZERLAND,
	AREA_ID_TAIWAN,
	AREA_ID_THAILAND,
	AREA_ID_TRINIDAD_TOBAGO,
	AREA_ID_TUNISIA,
	AREA_ID_TURKEY,
	AREA_ID_TURKMENISTAN,
	AREA_ID_UGANDA,
	AREA_ID_UKRAINE,
	AREA_ID_UNITED_KINGDOM,
	AREA_ID_UNITED_STATES,
	AREA_ID_URUGUAY,
	AREA_ID_UZBEKISTAN,
	AREA_ID_VENEZUELA,
	AREA_ID_VIETNAM,
	AREA_ID_NORDICS,
	AREA_ID_BALTIC,
	AREA_ID_CZECH_SLOVAKIA,
	AREA_ID_CROATIA_SLOVENIA,
	AREA_ID_LA_GROUP,
	AREA_ID_UNITED_ARAB_EMIRATES,
	AREA_ID_EMAT_UK,
	AREA_ID_EMAT_FR,
	AREA_ID_EMAT_PO,
	AREA_ID_INDIA_HI,
	AREA_ID_UAE_EN,
	AREA_ID_ISRAEL_AR,
	AREA_ID_NETHERLANDS_INC_BONAIRE_ENUS,
	AREA_ID_NUMS,
};

#define DEVICE_TYPE_VALID 	0xff
#define DEVICE_TYPE_INVALID	-1

#define DEVICE_ID_VALID 	0xff
#define DEVICE_ID_INVALID	-1

#define DEVICE_ID_NULL 		0

enum id_mm_type{
	BID_MM_IOMEM,
	BID_MM_IOREMAP,
};


enum id_tp{	
	TP_ID_NULL = 0,
	TP_ID_GT813,
	TP_ID_EKTF2K,
	TP_ID_NUMS,
};

enum id_lcd{
	LCD_ID_NULL = 0,
	LCD_ID_IVO_M101_NWN8,	
	LCD_ID_EDID_I2C,
	LCD_ID_NUMS,
};

enum id_key{
	KEY_ID_NULL = 0,
	KEY_ID_SHUTTLE,
	KEY_ID_BITLAND,
	KEY_ID_MALATA,
	KEY_ID_CAPSENSE,
	KEY_ID_ENGLISH_US,
	KEY_ID_ENGLISH_UK,
	KEY_ID_TURKISH,
	KEY_ID_SLOVENIAN,
	KEY_ID_RUSSIAN,
	KEY_ID_CZECH,
	KEY_ID_HUNGARIAN,
	KEY_ID_HINDI,
	KEY_ID_THAI,
	KEY_ID_PORTUGUESE,
	KEY_ID_ARABIC,
	KEY_ID_GREEK,
	KEY_ID_SWEDISH,
	KEY_ID_NORWEGIAN,
	KEY_ID_FINNISH,
	KEY_ID_DANISH,
	KEY_ID_ESTONIAN,
	KEY_ID_FRENCH,
	KEY_ID_GERMAN,
	KEY_ID_HEBREW,
	KEY_ID_ITALIAN,
	KEY_ID_SPANISH,
	KEY_ID_SWISS,
	KEY_ID_DUTCH,
	KEY_ID_BELGIAN,
	KEY_ID_NORDIC,
	KEY_ID_NUMS,
};

enum id_codec{
	CODEC_ID_NULL = 0,
	CODEC_ID_WM8994,
	CODEC_ID_WM8900,
	CODEC_ID_WM8988,
	CODEC_ID_RT5616,	
	CODEC_ID_RT5621,
	CODEC_ID_RT5623,
	CODEC_ID_RT3224,	
	CODEC_ID_RT5625,	
	CODEC_ID_RT5631,
	CODEC_ID_RT5639,	
	CODEC_ID_RT5640,
	CODEC_ID_RT5642,	
	CODEC_ID_RT3261,
	CODEC_ID_AIC3262,
	CODEC_ID_RK610,
	CODEC_ID_RK616,	
	CODEC_ID_RK1000,
	CODEC_ID_CS42L52,
	CODEC_ID_ES8323,
	CODEC_ID_NUMS,
};


enum id_wifi{
	WIFI_ID_NULL = 0,
		
	//brcm wifi
	WIFI_ID_BCM,
	WIFI_ID_BCM4319,
	WIFI_ID_BCM4330,
	WIFI_ID_RK903_26M,
	WIFI_ID_RK903_37M,
        WIFI_ID_BCM4329,
        WIFI_ID_RK901,
        WIFI_ID_AP6181,
        WIFI_ID_AP6210,
        WIFI_ID_AP6330,
        WIFI_ID_AP6476,
        WIFI_ID_AP6493,
        WIFI_ID_GB86302I,

	//RealTek wifi
	WIFI_ID_RTL8192CU,
	WIFI_ID_RTL8188EU,
	WIFI_ID_RTL8723AU,

	//Mediatek wifi
	WIFI_ID_COMBO,
	WIFI_ID_MT5931,
        WIFI_ID_RT5370,
	WIFI_ID_NUMS,
};


enum id_bt{
	BT_ID_NULL = 0,
		
	//brcm bluetooth
	BT_ID_NH660,
	BT_ID_BCM4330,
	BT_ID_RK903_26M,
	BT_ID_RK903,
	BT_ID_BCM4329,
	BT_ID_MV8787,
	BT_ID_AP6210,
	BT_ID_AP6330,
	BT_ID_AP6476,
	BT_ID_AP6493,
	BT_ID_RFKILL,
	
	//REALTEK bluetooth
	BT_ID_RTL8723,

	//MTK bluetooth
	BT_ID_MT6622,

	//RDA bluetooth

	BT_ID_NUMS,
};


enum id_gps{
	GPS_ID_NULL = 0,
	GPS_ID_RK_HV5820,
	GPS_ID_BCM4751,
	GPS_ID_GNS7560,	
	GPS_ID_MT3326,
	GPS_ID_NUMS,
};

enum id_fm{
	FM_ID_NULL = 0,
	FM_ID_NUMS,
};

//include/linux/bp-auto.h
enum id_modem
{
        MODEM_ID_NULL = 0,
        MODEM_ID_MT6229,	//USI MT6229 WCDMA
        MODEM_ID_MU509,		//huawei MU509 WCDMA
        MODEM_ID_MI700,		//thinkwill MI700 WCDMA
        MODEM_ID_MW100,		//thinkwill MW100 WCDMA
        MODEM_ID_TD8801,	//spreadtrum SC8803 TD-SCDMA
        MODEM_ID_SC6610,	//spreadtrum SC6610 GSM
        MODEM_ID_M50,		//spreadtrum RDA GSM
        MODEM_ID_MT6250,	//ZINN M50  EDGE
        MODEM_ID_C66A,		//zhongben
        MODEM_ID_NUMS,
};

	
enum id_ddr{
	DDR_ID_NULL = 0,
	DDR_ID_NUMS,
};

enum id_flash{
	FLASH_ID_NULL = 0,
	FLASH_ID_NUMS,
};

enum id_hdmi{
	HDMI_ID_NULL = 0,
	HDMI_ID_RK30,
	HDMI_ID_CAT66121,
	HDMI_ID_RK610,
	HDMI_ID_NUMS,
};

enum id_battery{
	BATTERY_ID_NULL = 0,
	BATTERY_ID_3300MAH,	
	BATTERY_ID_3600MAH,	
	BATTERY_ID_4700MAH,	
	BATTERY_ID_7000MAH,	
	BATTERY_ID_7700MAH,
	BATTERY_ID_9000MAH,
	BATTERY_ID_BLUEBERRY,
	BATTERY_ID_NUMS,
};

enum id_charge{
	CHARGE_ID_NULL = 0,
	CHARGE_ID_CW2015,	
	CHARGE_ID_BQ24193,
	CHARGE_ID_BQ27541,
	CHARGE_ID_OZ8806,
	CHARGE_ID_NUMS,
};
	
enum id_backlight{
	BACKLIGHT_ID_NULL = 0,
	BACKLIGHT_ID_RK29,
	BACKLIGHT_ID_WM831X,
	BACKLIGHT_ID_NUMS,
};

enum id_headset{
	HEADSET_ID_NULL = 0,
	HEADSET_ID_RK29,
	HEADSET_ID_NUMS,
};

enum id_micphone{
	MICPHONE_ID_NULL = 0,
	MICPHONE_ID_ANALOGIC,
	MICPHONE_ID_DIGITAL,
	MICPHONE_ID_NUMS,
};

enum id_speaker{
	SPEAKER_ID_NULL = 0,
	SPEAKER_ID_0W8,
	SPEAKER_ID_1W0,
	SPEAKER_ID_1W5,
	SPEAKER_ID_NUMS,
};

enum id_vibrator{
	VIBRATOR_ID_NULL = 0,
	VIBRATOR_ID_RK29,
	VIBRATOR_ID_NUMS,
};

enum id_tv{
	TV_ID_NULL = 0,
	TV_ID_RK610,
	TV_ID_NUMS,
};
	
enum id_echip{
	ECHIP_ID_NULL = 0,
	ECHIP_ID_IT8561,	
	ECHIP_ID_ITE,
	ECHIP_ID_NUMS,
};

enum id_hub{
	HUB_ID_NULL = 0,
	HUB_ID_USB4604,	
	HUB_ID_NUMS,
};

enum id_tpad{
	TPAD_ID_NULL = 0,
	TPAD_ID_ELAN,	
	TPAD_ID_SYNS,
	TPAD_ID_NUMS,
};	
	
enum id_pmic{
	PMIC_ID_NULL = 0,
	PMIC_ID_WM831X,
	PMIC_ID_WM8326,
	PMIC_ID_TPS65910,
	PMIC_ID_ACT8846,
	PMIC_ID_NUMS,
};

enum id_regulator{
	REGULATOR_ID_NULL = 0,
	REGULATOR_ID_PWM3,
	REGULATOR_ID_NUMS,
};

enum id_rtc{
	RTC_ID_NULL = 0,
	RTC_ID_HYM8563,
	RTC_ID_PCF8563,
	RTC_ID_TPS65910,
	RTC_ID_WM8326,
	RTC_ID_RK,
	RTC_ID_NUMS,
};

enum id_camera_front{
	CAMERA_FRONT_ID_NULL = 0,
	CAMERA_FRONT_ID_NUMS,
};

enum id_camera_back{
	CAMERA_BACK_ID_NULL = 0,
	CAMERA_BACK_ID_NUMS,
};
	
enum id_sensor_angle{
	ANGLE_ID_NULL = 0,
	ANGLE_ID_NUMS,
};

enum id_sensor_accel{
	ACCEL_ID_NULL = 0,
	ACCEL_ID_NUMS,
};

enum id_sensor_compass{
	COMPASS_ID_NULL = 0,
	COMPASS_ID_NUMS,
};

enum id_sensor_gyro{
	GYRO_ID_NULL = 0,
	GYRO_ID_NUMS,
};

enum id_sensor_light{
	LIGHT_ID_NULL = 0,
	LIGHT_ID_NUMS,
};

enum id_sensor_proximity{
	PROXIMITY_ID_NULL = 0,
	PROXIMITY_ID_NUMS,
};

enum id_sensor_temperature{
	TEMPERATURE_ID_NULL = 0,
	TEMPERATURE_ID_NUMS,
};
	
enum id_sensor_pressure{
	PRESSURE_ID_NULL = 0,
	PRESSURE_ID_NUMS,
};

enum id_led{
	LED_ID_NULL = 0,
	LED_ID_NUMS,
};


#define COUNTRY_AREA_NULL		"no"
#define LOCALE_LANGUAGE_NULL	"no"
#define LOCALE_REGION_NULL		"no"
#define COUNTRY_GEO_NULL		"no"
#define TIME_ZONE_NULL			"no"
#define USER_DEFINE_NULL		"no"


#define LOCALE_LANGUAGE_AR        "ar" //阿拉伯文
#define LOCALE_LANGUAGE_BE        "be" //白俄罗斯文
#define LOCALE_LANGUAGE_BG        "bg" //保加利亚文
#define LOCALE_LANGUAGE_CA        "ca" //加泰罗尼亚文
#define LOCALE_LANGUAGE_CS        "cs" //捷克文
#define LOCALE_LANGUAGE_DA        "da" //丹麦文
#define LOCALE_LANGUAGE_DE        "de" //德文
#define LOCALE_LANGUAGE_EL        "el" //希腊文
#define LOCALE_LANGUAGE_EN        "en" //英文
#define LOCALE_LANGUAGE_ES        "es" //西班牙文
#define LOCALE_LANGUAGE_ET        "et" //爱沙尼亚文
#define LOCALE_LANGUAGE_FI        "fi" //芬兰文
#define LOCALE_LANGUAGE_FR        "fr" //法文
#define LOCALE_LANGUAGE_HR        "hr" //克罗地亚文
#define LOCALE_LANGUAGE_HU        "hu" //匈牙利文
#define LOCALE_LANGUAGE_IN        "in" //印度尼西亚文(印度尼西亚)
#define LOCALE_LANGUAGE_IS        "is" //冰岛文
#define LOCALE_LANGUAGE_IT        "it" //意大利文
#define LOCALE_LANGUAGE_IW        "iw" //希伯来文
#define LOCALE_LANGUAGE_JA        "ja" //日文
#define LOCALE_LANGUAGE_KO        "ko" //朝鲜文
#define LOCALE_LANGUAGE_LT        "lt" //立陶宛文
#define LOCALE_LANGUAGE_LV        "lv" //拉托维亚文(列托)
#define LOCALE_LANGUAGE_MK        "mk" //马其顿文
#define LOCALE_LANGUAGE_MS        "ms" //马来西亚语(马来西亚)
#define LOCALE_LANGUAGE_NL        "nl" //荷兰文
#define LOCALE_LANGUAGE_NO        "no" //挪威文
#define LOCALE_LANGUAGE_PL        "pl" //波兰文
#define LOCALE_LANGUAGE_PT        "pt" //葡萄牙文
#define LOCALE_LANGUAGE_RO        "ro" //罗马尼亚文
#define LOCALE_LANGUAGE_RU        "ru" //俄文
#define LOCALE_LANGUAGE_SH        "sh" //塞波尼斯-克罗地亚文
#define LOCALE_LANGUAGE_SK        "sk" //斯洛伐克文
#define LOCALE_LANGUAGE_SL        "sl" //斯洛文尼亚文
#define LOCALE_LANGUAGE_SQ        "sq" //阿尔巴尼亚文
#define LOCALE_LANGUAGE_SR        "sr" //塞尔维亚文
#define LOCALE_LANGUAGE_SV        "sv" //瑞典文
#define LOCALE_LANGUAGE_SW        "sw" //斯瓦希里语(肯尼亚)
#define LOCALE_LANGUAGE_TH        "th" //泰文
#define LOCALE_LANGUAGE_TL        "tl" //菲律宾语(菲律宾)
#define LOCALE_LANGUAGE_TR        "tr" //土耳其文
#define LOCALE_LANGUAGE_UK        "uk" //乌克兰文
#define LOCALE_LANGUAGE_VI        "vi" //越南语(越南)
#define LOCALE_LANGUAGE_ZH        "zh" //中文 


#define LOCALE_REGION_AE        "AE" // 阿拉伯文 (阿拉伯联合酋长国) 
#define LOCALE_REGION_AL        "AL" // 阿尔巴尼亚文 (阿尔巴尼亚)
#define LOCALE_REGION_AN        "AN" //Netherland Antilles
#define LOCALE_REGION_AO        "AO" //Angola
#define LOCALE_REGION_AR        "AR" // 西班牙文 (阿根廷) 
#define LOCALE_REGION_AT        "AT" // 德文 (奥地利) 
#define LOCALE_REGION_AT_EURO   "AT_EURO" // 德文 (奥地利 EURO) 
#define LOCALE_REGION_AU        "AU" // 英文 (澳大利亚) 
#define LOCALE_REGION_AZ        "AZ" //Azerbaijan
#define LOCALE_REGION_BA        "BA" //Bosnia and Herzegovina
#define LOCALE_REGION_BB        "BB" //Barbados
#define LOCALE_REGION_BD        "BD" //Bangladesh
#define LOCALE_REGION_BE        "BE" // 法文 (比利时) 
#define LOCALE_REGION_BE_EURO   "BE_EURO" // 法文 (比利时 EURO) 
#define LOCALE_REGION_BG        "BG" // 保加利亚文 (保加利亚)
#define LOCALE_REGION_BH        "BH" // 阿拉伯文 (巴林) 
#define LOCALE_REGION_BO        "BO" // 西班牙文 (玻利维亚) 
#define LOCALE_REGION_BR        "BR" // 葡萄牙文 (巴西) 
#define LOCALE_REGION_BW        "BW" //Botswana
#define LOCALE_REGION_BY        "BY" // 白俄罗斯文 (白俄罗斯)
#define LOCALE_REGION_CA        "CA" // 英文 (加拿大) 
#define LOCALE_REGION_CH        "CH" // 德文 (瑞士) 
#define LOCALE_REGION_CI        "CI" //Cote d'Ivoire
#define LOCALE_REGION_CL        "CL" // 西班牙文 (智利) 
#define LOCALE_REGION_CN        "CN" // 中文 (中国) 
#define LOCALE_REGION_CO        "CO" // 西班牙文 (哥伦比亚) 
#define LOCALE_REGION_CR        "CR" // 西班牙文 (哥斯达黎加) 
#define LOCALE_REGION_CS        "CS" //Serbia
#define LOCALE_REGION_CY        "CY" //Cyprus
#define LOCALE_REGION_CZ        "CZ" // 捷克文 (捷克共和国)
#define LOCALE_REGION_DE        "DE" // 德文 (德国) 
#define LOCALE_REGION_DE_EURO   "DE_EURO" // 德文 (德国 EURO) 
#define LOCALE_REGION_DK        "DK" // 丹麦文 (丹麦)
#define LOCALE_REGION_DO        "DO" // 西班牙文 (多米尼加共和国) 
#define LOCALE_REGION_DZ        "DZ" // 阿拉伯文 (阿尔及利亚) 
#define LOCALE_REGION_EC        "EC" // 西班牙文 (厄瓜多尔) 
#define LOCALE_REGION_EE        "EE" // 爱沙尼亚文 (爱沙尼亚)
#define LOCALE_REGION_EG        "EG" // 阿拉伯文 (埃及) 
#define LOCALE_REGION_ES        "ES" // 西班牙文 (西班牙) 
#define LOCALE_REGION_ES_EURO   "ES_EURO" // 西班牙文 (西班牙 EURO) 
#define LOCALE_REGION_FI        "FI" // 芬兰文 (芬兰) 
#define LOCALE_REGION_FI_EURO   "FI_EURO" // 芬兰文 (芬兰 EURO)
#define LOCALE_REGION_FR        "FR" // 法文 (法国) 
#define LOCALE_REGION_FR_EURO   "FR_EURO" // 法文 (法国 EURO) 
#define LOCALE_REGION_GB        "GB" // 英文 (英国) 
#define LOCALE_REGION_GE        "GE" //Georgia
#define LOCALE_REGION_GR        "GR" // 希腊文 (希腊) 
#define LOCALE_REGION_GT        "GT" // 西班牙文 (危地马拉) 
#define LOCALE_REGION_HK        "HK" // 中文 (香港) 
#define LOCALE_REGION_HN        "HN" // 西班牙文 (洪都拉斯) 
#define LOCALE_REGION_HR        "HR" // 克罗地亚文 (克罗地亚)
#define LOCALE_REGION_HT        "HT" //Haiti
#define LOCALE_REGION_HU        "HU" // 匈牙利文 (匈牙利)
#define LOCALE_REGION_ID        "ID" //Indonesia
#define LOCALE_REGION_IE        "IE" // 英文 (爱尔兰) 
#define LOCALE_REGION_IE_EURO   "IE_EURO" // 英文 (爱尔兰 EURO) 
#define LOCALE_REGION_IL        "IL" // 希伯来文 (以色列)
#define LOCALE_REGION_IN        "IN" //India
#define LOCALE_REGION_IQ        "IQ" // 阿拉伯文 (伊拉克) 
#define LOCALE_REGION_IS        "IS" // 冰岛文 (冰岛)
#define LOCALE_REGION_IT        "IT" // 意大利文 (意大利) 
#define LOCALE_REGION_IT_EURO   "IT_EURO" // 意大利文 (意大利 EURO)
#define LOCALE_REGION_JM        "JM" //Jamaica
#define LOCALE_REGION_JO        "JO" // 阿拉伯文 (约旦) 
#define LOCALE_REGION_JP        "JP" // 日文 (日本)
#define LOCALE_REGION_KE        "KE" //Kenya
#define LOCALE_REGION_KR        "KR" // 朝鲜文 (南朝鲜)
#define LOCALE_REGION_KW        "KW" // 阿拉伯文 (科威特) 
#define LOCALE_REGION_KZ        "KZ" //Kazakhstan
#define LOCALE_REGION_LB        "LB" // 阿拉伯文 (黎巴嫩) 
#define LOCALE_REGION_LK        "LK" //Sri Lanka
#define LOCALE_REGION_LT        "LT" // 立陶宛文 (立陶宛)
#define LOCALE_REGION_LU        "LU" // 德文 (卢森堡) 
#define LOCALE_REGION_LU_EURO   "LU_EURO" // 德文 (卢森堡 EURO)
#define LOCALE_REGION_LV        "LV" // 拉托维亚文(列托) (拉脱维亚)
#define LOCALE_REGION_LY        "LY" // 阿拉伯文 (利比亚) 
#define LOCALE_REGION_MA        "MA" // 阿拉伯文 (摩洛哥) 
#define LOCALE_REGION_MD        "MD" //Moldova
#define LOCALE_REGION_MK        "MK" // 马其顿文 (马其顿王国)
#define LOCALE_REGION_MX        "MX" // 西班牙文 (墨西哥) 
#define LOCALE_REGION_MY        "MY" //Malaysia
#define LOCALE_REGION_NG        "NG" //Nigeria
#define LOCALE_REGION_NI        "NI" // 西班牙文 (尼加拉瓜) 
#define LOCALE_REGION_NL        "NL" // 荷兰文 (荷兰) 
#define LOCALE_REGION_NL_EURO   "NL_EURO " // 荷兰文 (荷兰 EURO)
#define LOCALE_REGION_NO        "NO" // 挪威文 (挪威) 
#define LOCALE_REGION_NP        "NP" //Nepal
#define LOCALE_REGION_NY        "NO_NY" // 挪威文 (挪威 NYNORSK)
#define LOCALE_REGION_NZ        "NZ" // 英文 (新西兰) 
#define LOCALE_REGION_OM        "QM" // 阿拉伯文 (阿曼) 
#define LOCALE_REGION_PA        "PA" // 西班牙文 (巴拿马) 
#define LOCALE_REGION_PE        "PE" // 西班牙文 (秘鲁) 
#define LOCALE_REGION_PH        "PH" //Philippines
#define LOCALE_REGION_PK        "PK" //Pakistan
#define LOCALE_REGION_PL        "PL" // 波兰文 (波兰) 
#define LOCALE_REGION_PR        "PR" // 西班牙文 (波多黎哥) 
#define LOCALE_REGION_PT        "PT" // 葡萄牙文 (葡萄牙) 
#define LOCALE_REGION_PT_EURO   "PT_EURO" // 葡萄牙文 (葡萄牙 EURO)
#define LOCALE_REGION_PY        "PY" // 西班牙文 (巴拉圭) 
#define LOCALE_REGION_QA        "QA" // 阿拉伯文 (卡塔尔) 
#define LOCALE_REGION_RO        "RO" // 罗马尼亚文 (罗马尼亚) 
#define LOCALE_REGION_RU        "RU" // 俄文 (俄罗斯)
#define LOCALE_REGION_SA        "SA" // 阿拉伯文 (沙特阿拉伯) 
#define LOCALE_REGION_SD        "SD" // 阿拉伯文 (苏丹) 
#define LOCALE_REGION_SE        "SE" // 瑞典文 (瑞典)
#define LOCALE_REGION_SG        "SG" //Singapore
#define LOCALE_REGION_SI        "SI" // 斯洛文尼亚文 (斯洛文尼亚)
#define LOCALE_REGION_SK        "SK" // 斯洛伐克文 (斯洛伐克)
#define LOCALE_REGION_SV        "SV" // 西班牙文 (萨尔瓦多) 
#define LOCALE_REGION_SY        "SY" // 阿拉伯文 (叙利亚) 
#define LOCALE_REGION_TH        "TH" // 泰文 (泰国)
#define LOCALE_REGION_TM        "TM" //Turkmenistan
#define LOCALE_REGION_TN        "TN" // 阿拉伯文 (突尼斯) 
#define LOCALE_REGION_TR        "TR" // 土耳其文 (土耳其) 
#define LOCALE_REGION_TT        "TT" //Trinidad Tobago
#define LOCALE_REGION_TW        "TW" // 中文 (台湾)
#define LOCALE_REGION_UA        "UA" // 乌克兰文 (乌克兰)
#define LOCALE_REGION_UG        "UG" //Uganda
#define LOCALE_REGION_US        "US" // 英文 (美国)
#define LOCALE_REGION_UY        "UY" // 西班牙文 (乌拉圭) 
#define LOCALE_REGION_UZ        "UZ" //Uzbekistan
#define LOCALE_REGION_VE        "VE" // 西班牙文 (委内瑞拉) 
#define LOCALE_REGION_VN        "VN" //Vietnam
#define LOCALE_REGION_YE        "YE" // 阿拉伯文 (也门)
#define LOCALE_REGION_YU        "YU" // 塞尔维亚文 (南斯拉夫)
#define LOCALE_REGION_ZA        "ZA" // 英文 (南非)


#define TIME_ZONE_MARSHALL_ISLANDS		"Pacific/Majuro"
#define TIME_ZONE_MIDWAY_ISLAND			"Pacific/Midway"
#define TIME_ZONE_HAWAII			"Pacific/Honolulu"
#define TIME_ZONE_ALASKA			"America/Anchorage"
#define TIME_ZONE_PACIFIC_TIME			"America/Los_Angeles"
#define TIME_ZONE_TIJUANA			"America/Tijuana" 
#define TIME_ZONE_ARIZONA			"America/Phoenix" 
#define TIME_ZONE_CHIHUAHUA			"America/Chihuahua" 
#define TIME_ZONE_MOUNTAIN_TIME			"America/Denver" 
#define TIME_ZONE_CENTRAL_AMERICA		"America/Costa_Rica" 
#define TIME_ZONE_CENTRAL_TIME			"America/Chicago" 
#define TIME_ZONE_MEXICO_CITY			"America/Mexico_City" 
#define TIME_ZONE_SASKATCHEWAN			"America/Regina" 
#define TIME_ZONE_BOGOTA			"America/Bogota" 
#define TIME_ZONE_EASTERN_TIME			"America/New_York" 
#define TIME_ZONE_VENEZUELA			"America/Caracas" 
#define TIME_ZONE_ATLANTIC_TIME_BARBADOS	"America/Barbados" 
#define TIME_ZONE_ATLANTIC_TIME_CANADA		"America/Halifax"
#define TIME_ZONE_MANAUS			"America/Manaus"
#define TIME_ZONE_SANTIAGO			"America/Santiago"
#define TIME_ZONE_NEWFOUNDLAND			"America/St_Johns"
#define TIME_ZONE_BRASILIA			"America/Sao_Paulo"
#define TIME_ZONE_BUENOS_AIRES			"America/Argentina/Buenos_Aires"
#define TIME_ZONE_GREENLAND			"America/Godthab"
#define TIME_ZONE_MONTEVIDEO			"America/Montevideo"
#define TIME_ZONE_MID_ATLANTIC			"Atlantic/South_Georgia"
#define TIME_ZONE_AZORES			"Atlantic/Azores"
#define TIME_ZONE_CAPE_VERDE_ISLANDS		"Atlantic/Cape_Verde"
#define TIME_ZONE_CASABLANCA			"Africa/Casablanca"
#define TIME_ZONE_LONDON_DUBLIN			"Europe/London"
#define TIME_ZONE_AMSTERDAM_BERLIN		"Europe/Amsterdam" 
#define TIME_ZONE_BELGRADE			"Europe/Belgrade"
#define TIME_ZONE_BRUSSELS			"Europe/Brussels"
#define TIME_ZONE_SARAJEVO			"Europe/Sarajevo"
#define TIME_ZONE_WINDHOEK			"Africa/Windhoek"
#define TIME_ZONE_W_AFRICA_TIME			"Africa/Brazzaville"
#define TIME_ZONE_AMMAN_JORDAN			"Asia/Amman"
#define TIME_ZONE_ATHENS_ISTANBUL		"Europe/Athens"
#define TIME_ZONE_BEIRUT_LEBANON		"Asia/Beirut"
#define TIME_ZONE_CAIRO				"Africa/Cairo"
#define TIME_ZONE_HELSINKI			"Europe/Helsinki"
#define TIME_ZONE_JERUSALEM			"Asia/Jerusalem"
#define TIME_ZONE_MINSK				"Europe/Minsk"
#define TIME_ZONE_HARARE			"Africa/Harare"
#define TIME_ZONE_BAGHDAD			"Asia/Baghdad"
#define TIME_ZONE_MOSCOW			"Europe/Moscow"
#define TIME_ZONE_KUWAIT			"Asia/Kuwait"
#define TIME_ZONE_NAIROBI			"Africa/Nairobi"
#define TIME_ZONE_TEHRAN			"Asia/Tehran"
#define TIME_ZONE_BAKU				"Asia/Baku"
#define TIME_ZONE_TBILISI			"Asia/Tbilisi"
#define TIME_ZONE_YEREVAN			"Asia/Yerevan"
#define TIME_ZONE_DUBAI				"Asia/Dubai"
#define TIME_ZONE_KABUL				"Asia/Kabul"
#define TIME_ZONE_ISLAMABAD_KARACHI		"Asia/Karachi"
#define TIME_ZONE_URAL_SK			"Asia/Oral"
#define TIME_ZONE_YEKATERINBURG			"Asia/Yekaterinburg"
#define TIME_ZONE_KOLKATA			"Asia/Calcutta"
#define TIME_ZONE_SRI_LANKA			"Asia/Colombo"
#define TIME_ZONE_KATHMANDU			"Asia/Katmandu"
#define TIME_ZONE_ASTANA			"Asia/Almaty"
#define TIME_ZONE_YANGON			"Asia/Rangoon"
#define TIME_ZONE_KRASNOYARSK			"Asia/Krasnoyarsk"
#define TIME_ZONE_BANGKOK			"Asia/Bangkok"
#define TIME_ZONE_BEIJING			"Asia/Shanghai"
#define TIME_ZONE_HONG_KONG			"Asia/Hong_Kong"
#define TIME_ZONE_IRKUTSK			"Asia/Irkutsk"
#define TIME_ZONE_KUALA_LUMPUR			"Asia/Kuala_Lumpur"
#define TIME_ZONE_PERTH				"Australia/Perth"
#define TIME_ZONE_TAIPEI			"Asia/Taipei"
#define TIME_ZONE_SEOUL				"Asia/Seoul"
#define TIME_ZONE_TOKYO_OSAKA			"Asia/Tokyo"
#define TIME_ZONE_YAKUTSK			"Asia/Yakutsk"
#define TIME_ZONE_ADELAIDE			"Australia/Adelaide"
#define TIME_ZONE_DARWIN			"Australia/Darwin"
#define TIME_ZONE_BRISBANE			"Australia/Brisbane"
#define TIME_ZONE_HOBART			"Australia/Hobart"
#define TIME_ZONE_SYDNEY_CANBERRA		"Australia/Sydney"
#define TIME_ZONE_VLADIVOSTOK			"Asia/Vladivostok"
#define TIME_ZONE_GUAM				"Pacific/Guam"
#define TIME_ZONE_MAGADAN			"Asia/Magadan"
#define TIME_ZONE_AUCKLAND			"Pacific/Auckland"
#define TIME_ZONE_FIJI				"Pacific/Fiji"
#define TIME_ZONE_TONGA				"Pacific/Tongatapu"


enum xml_gms_id{
	GMS_ID_SEARCH,
	GMS_ID_SEARCH_BY_VOICE,
	GMS_ID_GMAIL,
	GMS_ID_CONTACT_SYNC,
	GMS_ID_CALENDAR_SYNC,
	GMS_ID_TALK,
	GMS_ID_CHROME,
	GMS_ID_GOOGLES,
	GMS_ID_MAPS,
	GMS_ID_STREET_VIEW,
	GMS_ID_YOUTUBE,
	GMS_ID_GOOGLE_PLAY_STORE,
	GMS_ID_GOOGLE_PLAY_BOOKS,
	GMS_ID_GOOGLE_PLAY_MOVIES,
	GMS_ID_GOOGLE_PLAY_MAGAZINES,
	GMS_ID_GOOGLE_PLAY_MUSIC,
	//GMS_ID_WIDEVINE,
	GMS_ID_FACELOCK,
	GMS_ID_GOOGLE_TTS,
	//GMS_ID_GOOGLE_VOICE,
	//GMS_ID_GOGGLES,
	//GMS_ID_EARTH,
	//GMS_ID_ORKUT,
	//GMS_ID_DOCS_DRIVE,
	GMS_ID_NEWS_WEATHER,
	//GMS_ID_SHOPPER,
	//GMS_ID_BLOGGER,
	//GMS_ID_CURRENTS,
	//GMS_ID_KEEP,
	//GMS_ID_TRANSLATE,
	//GMS_ID_KOREAN_IME,
	//GMS_ID_PINYIN_IME,
	GMS_ID_NUMS,
};


struct xml_gms_name{
	int gms_id;
	char gms_name[48];
};


static struct xml_gms_name gms_name[GMS_ID_NUMS] = 
{
	{GMS_ID_SEARCH, "QuickSearchBox.apk"},
	{GMS_ID_SEARCH_BY_VOICE, "VoiceSearchStub.apk"},
	{GMS_ID_GMAIL, "Gmail2.apk"},
	{GMS_ID_CONTACT_SYNC, "GoogleContactsSyncAdapter.apk"},
	{GMS_ID_CALENDAR_SYNC, "GoogleCalendarSyncAdapter.apk"},
	{GMS_ID_TALK, "Talk.apk"},
	{GMS_ID_CHROME, "Chrome.apk"},
	{GMS_ID_GOOGLES, "PlusOne.apk"},
	{GMS_ID_MAPS, "GMS_Maps.apk"},
	{GMS_ID_STREET_VIEW, "Street.apk"},
	{GMS_ID_YOUTUBE, "YouTube.apk"},
	{GMS_ID_GOOGLE_PLAY_STORE, "Phonesky.apk"},
	{GMS_ID_GOOGLE_PLAY_BOOKS, "Books.apk"},
	{GMS_ID_GOOGLE_PLAY_MOVIES, "Videos.apk"},
	{GMS_ID_GOOGLE_PLAY_MAGAZINES, "Magazines.apk"},
	{GMS_ID_GOOGLE_PLAY_MUSIC, "Music.apk"},
	//{GMS_ID_WIDEVINE, ""},
	{GMS_ID_FACELOCK, "FaceLock.apk"},
	{GMS_ID_GOOGLE_TTS, "PicoTts.apk"},
	//{GMS_ID_GOOGLE_VOICE, "Velvet.apk"},//?
	//{GMS_ID_GOGGLES, "Velvet.apk"},//?
	//{GMS_ID_EARTH, "Velvet.apk"},
	//{GMS_ID_ORKUT, "Velvet.apk"},
	//{GMS_ID_DOCS_DRIVE, "Velvet.apk"},
	{GMS_ID_NEWS_WEATHER, "GenieWidget.apk"},
	//{GMS_ID_SHOPPER, "Velvet.apk"},
	//{GMS_ID_BLOGGER, "Velvet.apk"},
	//{GMS_ID_CURRENTS, "Velvet.apk"},
	//{GMS_ID_KEEP, "Velvet.apk"},
	//{GMS_ID_TRANSLATE, "Velvet.apk"},
	//{GMS_ID_KOREAN_IME, "Velvet.apk"},
	//{GMS_ID_PINYIN_IME, "PinyinIME.apk"},
};



struct auto_xml_config{
	int area_id;
	int gms_flag[GMS_ID_NUMS];
};


static struct auto_xml_config xml_config[AREA_ID_NUMS] = {
	{AREA_ID_NULL, {1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},
	{AREA_ID_ALBANIA,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_ALGERIA,{0,1,0,0,0,0,0,1,0,1,0,1,1,1,1,1,0,1,1}},
	{AREA_ID_ANGOLA,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_ARGENTINA,{0,0,0,0,0,0,0,0,0,1,0,0,1,1,1,1,0,0,0}},
	{AREA_ID_AUSTRALIA,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},
	{AREA_ID_AUSTRIA,{0,0,0,0,0,0,0,0,0,1,1,0,1,1,1,0,0,0,0}},
	{AREA_ID_AZERBAIJAN,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_BAHRAIN,{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_BANGLADESH,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_BARBADOS,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_BELARUS,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_BELGIUM,{0,1,0,0,0,0,0,0,0,1,0,0,1,1,1,0,0,0,0}},
	{AREA_ID_BOLIVIA,{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_BOSNIA_AND_HERZEGOVINA,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_BOTSWANA,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0}},
	{AREA_ID_BRAZIL,{0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,1,0,0,0}},
	{AREA_ID_BULGARIA,{0,0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,1,1}},
	{AREA_ID_CANADA,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0}},
	{AREA_ID_CHILE,{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1,0,0,0}},
	{AREA_ID_CHINA,{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_COLOMBIA,{0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,1,0,0,0}},
	{AREA_ID_COTE_D_IVOIRE,{0,1,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,0,1}},
	{AREA_ID_CROATIA,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_CYPRUS,{0,1,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,0,1}},
	{AREA_ID_CZECH_REPUBLIC,{0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0}},
	{AREA_ID_DENMARK,{0,1,0,0,0,0,0,0,0,1,1,0,1,1,1,1,0,0,1}},
	{AREA_ID_ECUADOR,{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_EGYPT,{0,0,0,0,0,0,0,1,0,1,0,1,1,1,1,1,0,1,1}},
	{AREA_ID_ESTONIA,{0,1,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,1,1}},
	{AREA_ID_FINLAND,{0,0,0,0,0,0,0,0,0,1,1,0,1,1,1,1,0,0,1}},
	{AREA_ID_FRANCE_INC_GUADELOUPE,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0}},
	{AREA_ID_GEORGIA,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_GERMANY,{0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,0,0}},
	{AREA_ID_GREECE,{0,1,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,0,0}},
	{AREA_ID_HAITI,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_HONDURAS,{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_HONG_KONG,{0,0,0,0,0,0,0,0,0,1,0,0,1,1,1,1,0,0,0}},
	{AREA_ID_HUNGARY,{0,0,0,0,0,0,0,0,0,1,1,0,1,1,1,1,0,0,0}},
	{AREA_ID_ICELAND,{0,0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,1,1}},
	{AREA_ID_INDIA,{0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,1,0,0,0}},
	{AREA_ID_INDONESIA,{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,0}},
	{AREA_ID_IRELAND,{0,1,0,0,0,0,0,0,1,1,0,0,1,1,1,0,0,0,0}},
	{AREA_ID_ISRAEL,{0,0,0,0,0,0,0,0,1,1,0,0,1,1,1,1,0,1,0}},
	{AREA_ID_ITALY,{0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0}},
	{AREA_ID_JAMAICA,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_JAPAN,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,1,0}},
	{AREA_ID_JORDAN,{0,0,0,0,0,0,0,1,0,1,0,1,1,1,1,1,0,1,1}},
	{AREA_ID_KAZAKHSTAN,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_KENYA,{0,1,0,0,0,0,0,0,0,1,1,0,1,1,1,1,0,0,0}},
	{AREA_ID_KOREA_SOUTH,{0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,1,0,1,0}},
	{AREA_ID_KUWAIT,{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_LATVIA,{0,1,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,0,1}},
	{AREA_ID_LEBANON,{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1,0,1,0}},
	{AREA_ID_LITHUANIA,{0,1,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,1,1}},
	{AREA_ID_LUXEMBOURG,{0,1,0,0,0,0,0,0,1,1,1,0,1,1,1,0,0,0,1}},
	{AREA_ID_MACEDONIA,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_MALAYSIA,{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1,0,1,0}},
	{AREA_ID_MEXICO,{0,0,0,0,0,0,0,1,0,1,0,0,0,0,1,1,0,0,0}},
	{AREA_ID_MOLDOVA,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_MOROCCO,{0,1,0,0,0,0,0,1,1,1,0,1,1,1,1,1,0,1,1}},
	{AREA_ID_NEPAL,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_NETHERLAND_ANTILLES,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_NETHERLANDS_INC_BONAIRE,{0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,1,0}},
	{AREA_ID_NEW_ZEALAND,{0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0}},
	{AREA_ID_NIGERIA,{0,1,0,0,0,0,0,1,1,1,0,1,1,1,1,1,0,0,0}},
	{AREA_ID_NORWAY,{0,0,0,0,0,0,0,0,0,1,1,0,1,1,1,1,0,0,0}},
	{AREA_ID_OMAN,{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_PAKISTAN,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_PARAGUAY,{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_PERU,{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0}},
	{AREA_ID_PHILIPPINES,{0,1,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,0,0}},
	{AREA_ID_POLAND,{0,0,0,0,0,0,0,0,0,1,0,0,1,1,1,1,0,0,0}},
	{AREA_ID_PORTUGAL,{0,0,0,0,0,0,0,0,0,0,1,0,1,1,1,0,0,0,0}},
	{AREA_ID_QATAR,{0,0,0,0,0,0,0,1,0,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_ROMANIA,{0,0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,0,1}},
	{AREA_ID_RUSSIA,{0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,1,0,1,0}},
	{AREA_ID_SAUDI_ARABIA,{0,0,0,0,0,0,0,0,0,1,0,1,1,1,1,1,0,1,0}},
	{AREA_ID_SERBIA,{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_SINGAPORE,{0,1,0,0,0,0,0,0,0,1,1,0,1,1,1,1,0,0,0}},
	{AREA_ID_SLOVAKIA,{0,0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,0,1}},
	{AREA_ID_SLOVENIA,{0,1,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,1,1}},
	{AREA_ID_SOUTH_AFRICA,{0,0,0,0,0,0,0,0,0,1,0,0,1,1,1,1,0,0,0}},
	{AREA_ID_SPAIN,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0}},
	{AREA_ID_SRI_LANKA,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_SWEDEN,{0,0,0,0,0,0,0,0,0,1,0,0,1,1,1,1,0,0,0}},
	{AREA_ID_SWITZERLAND,{0,1,0,0,0,0,0,0,0,0,1,0,1,1,1,1,0,0,0}},
	{AREA_ID_TAIWAN,{0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,1,0}},
	{AREA_ID_THAILAND,{0,1,0,0,0,0,0,0,0,1,1,0,1,1,1,1,0,1,1}},
	{AREA_ID_TRINIDAD_TOBAGO,{0,1,0,0,0,0,0,1,0,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_TUNISIA,{0,1,0,0,0,0,0,0,1,1,0,1,1,1,1,1,0,1,1}},
	{AREA_ID_TURKEY,{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0}},
	{AREA_ID_TURKMENISTAN,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_UGANDA,{0,1,0,0,0,0,0,1,1,1,0,1,1,1,1,1,0,0,0}},
	{AREA_ID_UKRAINE,{0,1,0,0,0,0,0,0,0,1,1,0,1,1,1,1,0,1,0}},
	{AREA_ID_UNITED_KINGDOM,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},
	{AREA_ID_UNITED_STATES,{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}},
	{AREA_ID_URUGUAY,{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_UZBEKISTAN,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,1}},
	{AREA_ID_VENEZUELA,{0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,0}},
	{AREA_ID_VIETNAM,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,1,0}},
	{AREA_ID_NORDICS,{0,0,0,0,0,0,0,0,0,1,0,0,1,1,1,1,0,0,0}},
	{AREA_ID_BALTIC,{0,1,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,0,1}},
	{AREA_ID_CZECH_SLOVAKIA,{0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0}},
	{AREA_ID_CROATIA_SLOVENIA,{0,0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,0,0,1}},
	{AREA_ID_LA_GROUP,{0,0,0,0,0,0,0,1,1,1,0,1,1,1,1,1,0,0,0}},
	{AREA_ID_UNITED_ARAB_EMIRATES,{0,0,0,0,0,0,0,1,0,1,0,1,1,1,1,1,0,0,0}},
	{AREA_ID_EMAT_UK,{0,1,0,0,0,0,0,0,0,1,0,0,1,1,1,1,0,0,0}},
	{AREA_ID_EMAT_FR,{0,1,0,0,0,0,0,0,0,1,0,0,1,1,1,1,0,0,1}},
	{AREA_ID_EMAT_PO,{0,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,0,0,1}},
	{AREA_ID_INDIA_HI,{0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,1,0,0,0}},
	{AREA_ID_UAE_EN,{0,0,0,0,0,0,0,1,0,1,0,1,1,1,1,1,0,0,0}},
	{AREA_ID_ISRAEL_AR,{0,0,0,0,0,0,0,0,1,1,0,0,1,1,1,1,0,1,0}},
	{AREA_ID_NETHERLANDS_INC_BONAIRE_ENUS,{0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,1,0}},
};


struct area_id_name{
	int type;
	int id;
	char country_area[32];		//country or area name such as china
	char locale_language[4];	//locale language name such as zh
	char locale_region[8];		//locale region name such as CN
	char country_geo[20];		//country geographical position such as asia		
	char timezone[32];		//time zone such as Asia/Shanghai
	char user_define[20];		//user-defined name such as A10,A12,A13 
};


struct operator_id_name{
	int type;		//type
	int id;	
	char operator_name[20];	//operator name such as CHINA MOBILE
	char locale_region[8];	//area name such as CN
};

struct reserve_id_name{
	int type;		//type
	int id;			
	char reserve_name[20];	//reserve name	
	char locale_region[20];	
};


struct device_id_name{
	char type;	//device type
	char id;	//board id
	char type_name[14];
	char driver_name[16];
	char dev_name[16];	//name
	char description[30];	// description
	unsigned short device_id;//device_id and only one
	//short select;	// 1:device is selected 0:not
};

struct board_id_flag{	
	atomic_t debug_flag;	
};



enum I2C_BUS_NUM{
	BUS_NUM_I2C_0,
	BUS_NUM_I2C_1,
	BUS_NUM_I2C_2,
	BUS_NUM_I2C_3,
	BUS_NUM_I2C_4,
	BUS_NUM_I2C_GPIO,
	BUS_NUM_I2C_MAX,
};


enum {
	BUS_NUM_SPI_0,
	BUS_NUM_SPI_1,
	BUS_NUM_SPI_MAX,
};


enum board_device_type{
	BOARD_DEVICE_TYPE_VALID,
	BOARD_DEVICE_TYPE_INVALID,
	BOARD_DEVICE_TYPE_I2C,	
	BOARD_DEVICE_TYPE_SPI,
	BOARD_DEVICE_TYPE_PLATFORM,	
	BOARD_DEVICE_TYPE_NUMS,
};

struct valid_invalid_name{
	char name[32];
};


struct board_device_table{
	void *addr;
	int size;
	int type;
	int bus;
};


#include <linux/board-id-operator.h>

struct board_id_private_data{		
	struct device *dev;	
	struct mutex operation_mutex;
	struct area_id_name  area_area_id_name[AREA_ID_NUMS];
	struct operator_id_name  area_operator_id_name[OPERATOR_ID_NUMS];
	struct reserve_id_name  area_reserve_id_name[RESERVE_ID_NUMS];
	struct area_id_name  area_select;	
	struct operator_id_name  operator_select;	
	struct reserve_id_name  reserve_select;
	
	struct device_id_name  tp_id_name[TP_ID_NUMS];
	struct device_id_name  lcd_id_name[LCD_ID_NUMS];
	struct device_id_name  key_id_name[KEY_ID_NUMS];
	struct device_id_name  codec_id_name[CODEC_ID_NUMS];
	struct device_id_name  wifi_id_name[WIFI_ID_NUMS];
	struct device_id_name  bt_id_name[BT_ID_NUMS];
	struct device_id_name  gps_id_name[GPS_ID_NUMS];
	struct device_id_name  fm_id_name[FM_ID_NUMS];
	struct device_id_name  modem_id_name[MODEM_ID_NUMS];	
	struct device_id_name  ddr_id_name[DDR_ID_NUMS];
	struct device_id_name  flash_id_name[FLASH_ID_NUMS];
	struct device_id_name  hdmi_id_name[HDMI_ID_NUMS];
	struct device_id_name  battery_id_name[BATTERY_ID_NUMS];
	struct device_id_name  charge_id_name[CHARGE_ID_NUMS];
	struct device_id_name  backlight_id_name[BACKLIGHT_ID_NUMS];
	struct device_id_name  headset_id_name[HEADSET_ID_NUMS];
	struct device_id_name  micphone_id_name[MICPHONE_ID_NUMS];
	struct device_id_name  speaker_id_name[SPEAKER_ID_NUMS];
	struct device_id_name  vibrator_id_name[VIBRATOR_ID_NUMS];
	struct device_id_name  tv_id_name[TV_ID_NUMS];
	struct device_id_name  echip_id_name[ECHIP_ID_NUMS];	
	struct device_id_name  hub_id_name[HUB_ID_NUMS];	
	struct device_id_name  tpad_id_name[TPAD_ID_NUMS];
	
	struct device_id_name  pmic_id_name[PMIC_ID_NUMS];
	struct device_id_name  regulator_id_name[REGULATOR_ID_NUMS];
	struct device_id_name  rtc_id_name[RTC_ID_NUMS];
	struct device_id_name  camera_front_id_name[CAMERA_FRONT_ID_NUMS];
	struct device_id_name  camera_back_id_name[CAMERA_BACK_ID_NUMS];	
	struct device_id_name  sensor_angle_id_name[ANGLE_ID_NUMS];
	struct device_id_name  sensor_accel_id_name[ACCEL_ID_NUMS];
	struct device_id_name  sensor_compass_id_name[COMPASS_ID_NUMS];
	struct device_id_name  sensor_gyroscope_id_name[GYRO_ID_NUMS];
	struct device_id_name  sensor_light_id_name[LIGHT_ID_NUMS];
	struct device_id_name  sensor_proximity_id_name[PROXIMITY_ID_NUMS];
	struct device_id_name  sensor_temperature_id_name[TEMPERATURE_ID_NUMS];	
	struct device_id_name  sensor_pressure_id_name[PRESSURE_ID_NUMS];
	struct device_id_name  device_selected[DEVICE_NUM_TYPES];

	
	struct device_id_name  *device_start_addr[DEVICE_NUM_TYPES];	
	char  device_num_max[DEVICE_NUM_TYPES];
	
	struct board_id_flag flags;
	struct file_operations id_fops;
	struct miscdevice id_miscdev;
	struct board_id_platform_data *pdata;
	
	//for debug
	struct file* board_id_data_filp;
	mm_segment_t board_id_data_fs;
	struct file* board_id_area_filp;
	mm_segment_t board_id_area_fs;
	struct file* board_id_device_filp;
	mm_segment_t board_id_device_fs;

	char vendor_data[DEVICE_NUM_TYPES];
	
};


extern char board_id_get(enum type_devices type);
extern int board_id_get_from_flash(char *pbuf, int type);


#if 1
#define DBG_ID(x...) if(g_board_id && (atomic_read(&g_board_id->flags.debug_flag) == 1)) printk(x)
#else
#define DBG_ID(x...)
#endif


#define BOARD_ID_IOCTL_BASE 'b'

//#define BOARD_ID_IOCTL_READ_ALL 			_IOWR(BOARD_ID_IOCTL_BASE, 0x00, struct board_id_private_data)
//#define BOARD_ID_IOCTL_WRITE_ALL 			_IOWR(BOARD_ID_IOCTL_BASE, 0x30, struct board_id_private_data)


#define BOARD_ID_IOCTL_READ_AREA_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x80, struct area_id_name)
#define BOARD_ID_IOCTL_READ_OPERATOR_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x81, struct operator_id_name)
#define BOARD_ID_IOCTL_READ_RESERVE_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x82, struct reserve_id_name)

#define BOARD_ID_IOCTL_READ_AREA_NAME_BY_ID 		_IOWR(BOARD_ID_IOCTL_BASE, 0x70, struct area_id_name)
#define BOARD_ID_IOCTL_READ_OPERATOR_NAME_BY_ID 	_IOWR(BOARD_ID_IOCTL_BASE, 0x71, struct operator_id_name)
#define BOARD_ID_IOCTL_READ_RESERVE_NAME_BY_ID 		_IOWR(BOARD_ID_IOCTL_BASE, 0x72, struct reserve_id_name)
#define BOARD_ID_IOCTL_READ_DEVICE_NAME_BY_ID 		_IOWR(BOARD_ID_IOCTL_BASE, 0x73, struct device_id_name)



#define BOARD_ID_IOCTL_READ_TP_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x01, struct device_id_name)
#define BOARD_ID_IOCTL_READ_LCD_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x02, struct device_id_name)
#define BOARD_ID_IOCTL_READ_KEY_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x03, struct device_id_name)
#define BOARD_ID_IOCTL_READ_CODEC_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x04, struct device_id_name)
#define BOARD_ID_IOCTL_READ_WIFI_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x05, struct device_id_name)
#define BOARD_ID_IOCTL_READ_BT_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x06, struct device_id_name)	
#define BOARD_ID_IOCTL_READ_GPS_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x07, struct device_id_name)
#define BOARD_ID_IOCTL_READ_FM_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x08, struct device_id_name)
#define BOARD_ID_IOCTL_READ_MODEM_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x09, struct device_id_name)	
#define BOARD_ID_IOCTL_READ_DDR_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x0a, struct device_id_name)
#define BOARD_ID_IOCTL_READ_FLASH_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x0b, struct device_id_name)
#define BOARD_ID_IOCTL_READ_HDMI_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x0c, struct device_id_name)
#define BOARD_ID_IOCTL_READ_BATTERY_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x0d, struct device_id_name)
#define BOARD_ID_IOCTL_READ_CHARGE_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x0e, struct device_id_name)	
#define BOARD_ID_IOCTL_READ_BACKLIGHT_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x0f, struct device_id_name)
#define BOARD_ID_IOCTL_READ_HEADSET_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x10, struct device_id_name)
#define BOARD_ID_IOCTL_READ_MICPHONE_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x11, struct device_id_name)
#define BOARD_ID_IOCTL_READ_SPEAKER_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x12, struct device_id_name)
#define BOARD_ID_IOCTL_READ_VIBRATOR_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x13, struct device_id_name)
#define BOARD_ID_IOCTL_READ_TV_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x14, struct device_id_name)
#define BOARD_ID_IOCTL_READ_ECHIP_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x15, struct device_id_name)		
#define BOARD_ID_IOCTL_READ_HUB_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x16, struct device_id_name)
#define BOARD_ID_IOCTL_READ_TPAD_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x17, struct device_id_name)


#define BOARD_ID_IOCTL_READ_PMIC_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x20, struct device_id_name)
#define BOARD_ID_IOCTL_READ_REGULATOR_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x21, struct device_id_name)
#define BOARD_ID_IOCTL_READ_RTC_ID 			_IOR(BOARD_ID_IOCTL_BASE, 0x22, struct device_id_name)
#define BOARD_ID_IOCTL_READ_CAMERA_FRONT_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x23, struct device_id_name)
#define BOARD_ID_IOCTL_READ_CAMERA_BACK_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x24, struct device_id_name)	
#define BOARD_ID_IOCTL_READ_SENSOR_ANGLE_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x25, struct device_id_name)
#define BOARD_ID_IOCTL_READ_SENSOR_ACCEL_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x26, struct device_id_name)
#define BOARD_ID_IOCTL_READ_SENSOR_COMPASS_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x27, struct device_id_name)
#define BOARD_ID_IOCTL_READ_SENSOR_GYRO_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x28, struct device_id_name)
#define BOARD_ID_IOCTL_READ_SENSOR_LIGHT_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x29, struct device_id_name)
#define BOARD_ID_IOCTL_READ_SENSOR_PROXIMITY_ID 	_IOR(BOARD_ID_IOCTL_BASE, 0x2A, struct device_id_name)
#define BOARD_ID_IOCTL_READ_SENSOR_TEMPERATURE_ID 	_IOR(BOARD_ID_IOCTL_BASE, 0x2B, struct device_id_name)	
#define BOARD_ID_IOCTL_READ_SENSOR_PRESSURE_ID 		_IOR(BOARD_ID_IOCTL_BASE, 0x2C, struct device_id_name)


#define BOARD_ID_IOCTL_WRITE_AREA_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x90, struct area_id_name)
#define BOARD_ID_IOCTL_WRITE_OPERATOR_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x91, struct operator_id_name)
#define BOARD_ID_IOCTL_WRITE_RESERVE_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x92, struct reserve_id_name)


#define BOARD_ID_IOCTL_WRITE_TP_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x31, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_LCD_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x32, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_KEY_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x33, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_CODEC_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x34, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_WIFI_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x35, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_BT_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x36, struct device_id_name)	
#define BOARD_ID_IOCTL_WRITE_GPS_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x37, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_FM_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x38, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_MODEM_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x39, struct device_id_name)	
#define BOARD_ID_IOCTL_WRITE_DDR_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x3a, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_FLASH_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x3b, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_HDMI_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x3c, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_BATTERY_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x3d, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_CHARGE_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x3e, struct device_id_name)	
#define BOARD_ID_IOCTL_WRITE_BACKLIGHT_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x3f, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_HEADSET_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x40, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_MICPHONE_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x41, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_SPEAKER_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x42, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_VIBRATOR_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x43, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_TV_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x44, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_ECHIP_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x45, struct device_id_name)		
#define BOARD_ID_IOCTL_WRITE_HUB_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x46, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_TPAD_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x47, struct device_id_name)

#define BOARD_ID_IOCTL_WRITE_PMIC_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x50, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_REGULATOR_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x51, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_RTC_ID 			_IOW(BOARD_ID_IOCTL_BASE, 0x52, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_CAMERA_FRONT_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x53, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_CAMERA_BACK_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x54, struct device_id_name)	
#define BOARD_ID_IOCTL_WRITE_SENSOR_ANGLE_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x55, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_SENSOR_ACCEL_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x56, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_SENSOR_COMPASS_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x57, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_SENSOR_GYRO_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x58, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_SENSOR_LIGHT_ID 		_IOW(BOARD_ID_IOCTL_BASE, 0x59, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_SENSOR_PROXIMITY_ID 	_IOW(BOARD_ID_IOCTL_BASE, 0x5A, struct device_id_name)
#define BOARD_ID_IOCTL_WRITE_SENSOR_TEMPERATURE_ID 	_IOW(BOARD_ID_IOCTL_BASE, 0x5B, struct device_id_name)	
#define BOARD_ID_IOCTL_WRITE_SENSOR_PRESSURE_ID 	_IOW(BOARD_ID_IOCTL_BASE, 0x5C, struct device_id_name)

#define BOARD_ID_IOCTL_WRITE_AREA_FLASH 		_IOW(BOARD_ID_IOCTL_BASE, 0x60, struct area_id_name)
#define BOARD_ID_IOCTL_WRITE_DEVICE_FLASH 		_IOW(BOARD_ID_IOCTL_BASE, 0x61, struct device_id_name)
#define BOARD_ID_IOCTL_READ_STATUS 			_IOR(BOARD_ID_IOCTL_BASE, 0x62,	char)
#define BOARD_ID_IOCTL_READ_VENDOR_DATA 		_IOR(BOARD_ID_IOCTL_BASE, 0x63,	char[DEVICE_NUM_TYPES])

#endif

