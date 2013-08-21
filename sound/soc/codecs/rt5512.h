#ifndef __SND_SOC_CODEC_RT5512_H
#define __SND_SOC_CODEC_RT5512_H

#define RT5512_CODEC_NAME "rt5512-codec"
#define RT5512_DRV_VER	   "1.0.1_G"

#define RT5512_CLK_DIV_ID 1
#define FOR_MID 1


struct rt5512_codec_chip
{
	struct device *dev;
	struct i2c_client *client;
	struct snd_soc_jack *rt_jack;
	enum snd_soc_control_type control_type;
	int curr_outpath;
	int curr_inpath;
};

enum {
	OFF,
	RCV,
	SPK_PATH,
	HP_PATH,
	HP_NO_MIC,
	BT,
	SPK_HP,
	RING_SPK,
	RING_HP,
	RING_HP_NO_MIC,
	RING_SPK_HP,
};

enum {
	MIC_OFF,
	Main_Mic,
	Hands_Free_Mic,
	BT_Sco_Mic,
};


#if 1
#define RT_DBG(format, args...) pr_info("%s:%s() line-%d: " format, RT5512_CODEC_NAME, __FUNCTION__, __LINE__, ##args)
#else
#define RT_DBG(format, args...)
#endif  /* #if 1 */

#endif /* #ifndef __SND_SOC_CODEC_RT5512_H */
