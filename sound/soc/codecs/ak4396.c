/*
 * AK4396 ALSA SoC (ASoC) driver
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <linux/spi/spi.h>
#include <sound/asoundef.h>
#include <linux/delay.h>
#include <mach/iomux.h>

/* AK4396 registers addresses */
#define AK4396_REG_CONTROL1		0x00
#define AK4396_REG_CONTROL2		0x01
#define AK4396_REG_CONTROL3		0x02
#define AK4396_REG_LCH_ATT		0x03
#define AK4396_REG_RCH_ATT    0x04
#define AK4396_NUM_REGS				5

#define AK4396_REG_MASK				0x1f
#define AK4396_WRITE					0x20  /*C1 C0 R/W A4 A3 A2 A1 A0 8bit==0010 0000 */

/* Bit masks for AK4396 registers */
#define AK4396_CONTROL1_RSTN		(1 << 0)
#define AK4396_CONTROL1_DIF0		(1 << 1)
#define AK4396_CONTROL1_DIF1		(1 << 2)
#define AK4396_CONTROL1_DIF2		(1 << 3)

#define DRV_NAME "AK4396"

struct ak4396_private {
	enum snd_soc_control_type control_type;
	void *control_data;
	unsigned int sysclk;
};

#if 0

static const u16 ak4396_reg[AK4396_NUM_REGS] = {
	0x87, 0x02, 0x00, 0xff, 0xff
}; //CONFIG_LINF
#else
static const u16 ak4396_reg[AK4396_NUM_REGS] = {
	0x05, 0x02, 0x00, 0xff, 0xff
}; //CONFIG_LINF

#endif

static void on_off_ext_amp(int i)
{

    #ifdef SPK_CTL
    //gpio_direction_output(SPK_CTL, GPIO_LOW);
    gpio_set_value(SPK_CTL, i);
    printk("*** %s() SPEAKER set as %d\n", __FUNCTION__, i);
    #endif
    #ifdef EAR_CON_PIN
    //gpio_direction_output(EAR_CON_PIN, GPIO_LOW);
    gpio_set_value(EAR_CON_PIN, i);
    printk("*** %s() HEADPHONE set as %d\n", __FUNCTION__, i);
    mdelay(50);
    #endif
}

void codec_set_spk(bool on)
{
	on_off_ext_amp(on);
}

static int ak4396_fill_cache(struct snd_soc_codec *codec)
{
	int i;
	u8 *reg_cache = codec->reg_cache;
	struct spi_device *spi = codec->control_data;

	for (i = 0; i < codec->driver->reg_cache_size; i++) {
		int ret = spi_w8r8(spi, i);
		if (ret < 0) {
			dev_err(&spi->dev, "SPI write failure\n");
			return ret;
		}

		reg_cache[i] = ret;
	}

	return 0;
}

/* read the reg_cache */
static unsigned int ak4396_read_reg_cache(struct snd_soc_codec *codec,
					  unsigned int reg)
{
	u8 *reg_cache = codec->reg_cache;

	if (reg >= codec->driver->reg_cache_size)
		return -EINVAL;
	
//	printk("read reg_cache[%x]====%d\n", reg, reg_cache[reg]);
	return reg_cache[reg];
}

static int ak4396_spi_write(struct snd_soc_codec *codec, unsigned int reg,
			    unsigned int value)
{
	u8 *cache = codec->reg_cache;
	struct spi_device *spi = codec->control_data;

	if (reg >= codec->driver->reg_cache_size)
		return -EINVAL;

	/* only write to the hardware if value has changed */
	//if (cache[reg] != value)
	//{
		u8 tmp[2] = { (reg & AK4396_REG_MASK) | AK4396_WRITE, value};
		//printk("tmp[0]===%d\n", tmp[0]);
		//printk("tmp[1]===%d\n", tmp[1]);
		if (spi_write(spi, tmp, sizeof(tmp))) {
			dev_err(&spi->dev, "SPI write failed\n");
			return -EIO;
		}
	
		cache[reg] = value;
	//}	
	return 0;
}

/* write the register space */
static ak4396_write(struct snd_soc_codec *codec)
{
	int ret, val, i;
	val = 0;
	int addr[5] = {0x00, 0x01, 0x02, 0x03, 0x04};
	int dat[5] = {0x87, 0x02, 0x00, 0xff, 0xff};
//	while(1){
	val |= AK4396_CONTROL1_RSTN;
	ak4396_spi_write(codec, AK4396_REG_CONTROL1, val);
	
	for(i=0; i<5; i++)
        {
		ret = ak4396_spi_write(codec, addr[i], dat[i]);	
		if (ret < 0)
			printk("ak4396_spi_write failed!\n");
	
                printk("write %d time(s)\n", i);	
	}
//	}
}

/*
 * Note that this should be called from init rather than from hw_params.
 */
static int ak4396_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct ak4396_private *ak4396 = snd_soc_codec_get_drvdata(codec);

        printk("Enter::%s----%d\n",__FUNCTION__,__LINE__);
	printk("freq======%d\n", freq);	
	ak4396->sysclk = freq;

	return 0;
}

static int ak4396_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int val = 0;
	
 	printk("%s----%d, format[%02x]\n",__FUNCTION__,__LINE__,format);
	val = ak4396_read_reg_cache(codec, AK4396_REG_CONTROL1);
	if (val < 0)
		return val;
	val &= ~(AK4396_CONTROL1_DIF0 | AK4396_CONTROL1_DIF1 | AK4396_CONTROL1_DIF2);
//	printk("ak4396 val=%d\n", val);

	/* set DAI format */
	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		val |= AK4396_CONTROL1_DIF2 ;
		printk("SND_SOC_DAIFMT_RIGHT_J: \n");
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val |= AK4396_CONTROL1_DIF1 ;
		printk("SND_SOC_DAIFMT_LEFT_J: \n");
		break;
	case SND_SOC_DAIFMT_I2S:
		val |= AK4396_CONTROL1_DIF0 | AK4396_CONTROL1_DIF1 ;
		//val |= 0x87;
	
		printk("SND_SOC_DAIFMT_I2S is ok!\n");
		break;
	default:
		dev_err(codec->dev, "invalid dai format\n");
		return -EINVAL;
	}

	/* This device can only be slave */
	if ((format & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS)
	{
		printk("%s failed!----%d\n",__FUNCTION__,__LINE__);
		return -EINVAL;
	}
	
	//val |= AK4396_CONTROL1_RSTN;
	printk("AK4396 CONTROL1 val ==== %d\n", val);
	ak4396_spi_write(codec, AK4396_REG_CONTROL1, val);

	return 0;
}

static int ak4396_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	int val = 0;

	switch (params_rate(params)) {

        case 176400:
                val |= IEC958_AES3_CON_FS_176400;
                printk("params_rate::=176400!\n");
                break;

        case 192000:
                val |= IEC958_AES3_CON_FS_192000;
                printk("params_rate::=192000!\n");
                break;

        case 88200:
                val |= IEC958_AES3_CON_FS_88200;
                printk("params_rate::=88200!\n");
                break;


        case 96000:
                val |= IEC958_AES3_CON_FS_96000;
                printk("params_rate::=96000!\n");
                break;

	case 44100:
		val |= IEC958_AES3_CON_FS_44100;
		printk("params_rate::=44100!\n");
		break;
	case 48000:
		val |= IEC958_AES3_CON_FS_48000;
		break;
	case 32000:
		val |= IEC958_AES3_CON_FS_32000;
		break;
	default:
		dev_err(codec->dev, "unsupported sampling rate\n");
		return -EINVAL;
	}
	val = 0;
	val = ak4396_read_reg_cache(codec, AK4396_REG_CONTROL1);
	//reset RSTN bit;
	val &= 0xFE;
	printk("val ==== %d\n", val);
        ak4396_spi_write(codec, AK4396_REG_CONTROL1, val);
        val |= 0x01;
        printk("val ==== %d\n", val);
        ak4396_spi_write(codec, AK4396_REG_CONTROL1, val);
	
	//printk("val === %d\n", val);
	//ak4396_spi_write(codec, AK4396_REG_CONTROL2, val);
	return 0;
}

static struct snd_soc_dai_ops ak4396_dai_ops = {
	.hw_params = ak4396_hw_params,
	.set_fmt = ak4396_set_dai_fmt,
	.set_sysclk = ak4396_set_dai_sysclk,
};

static struct snd_soc_dai_driver ak4396_dai = {
	.name = "AK4396 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE  |
			   SNDRV_PCM_FMTBIT_S24_3LE |
			   SNDRV_PCM_FMTBIT_S24_LE  |
			   SNDRV_PCM_FMTBIT_S32_LE
	},
	.ops = &ak4396_dai_ops,
};

struct snd_soc_codec *codec_temp; 
static int ak4396_probe(struct snd_soc_codec *codec)
{
	struct ak4396_private *ak4396 = snd_soc_codec_get_drvdata(codec);
	int ret;
	printk("ak4396_probe begin!\n");
	codec->control_data = ak4396->control_data;
	codec_temp = codec;
#if 0
	/* read all regs and fill the cache */
	ret = ak4396_fill_cache(codec);
	if (ret < 0) {
		dev_err(codec->dev, "failed to fill register cache\n");
		return ret;
	}
#endif
	/* write to ak4396_reg */
//	ak4396_write(codec);
	
	printk("ak4396_probe is ok!\n");
	dev_info(codec->dev, "SPI device initialized\n");
	return 0;
}

static int ak4396_remove(struct snd_soc_codec *codec)
{
	int val, ret;

	val = ak4396_read_reg_cache(codec, AK4396_REG_CONTROL1);
	if (val < 0)
		return val;

	/* set non-reset bits */
	val &= ~AK4396_CONTROL1_RSTN;
	ret = ak4396_spi_write(codec, AK4396_REG_CONTROL1, val);

	return ret;
}

static int ak4396_suspend(struct snd_soc_codec *codec, pm_message_t state)
{	
	return 0;
}

static int ak4396_resume(struct snd_soc_codec *codec)
{
	//ak4396_write(codec);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_device_ak4396 = {
	.probe =	ak4396_probe,
	.remove =	ak4396_remove,
	.suspend = 	ak4396_suspend,
	.resume = 	ak4396_resume,
	.reg_cache_size = AK4396_NUM_REGS,
	.reg_word_size = sizeof(u16),
	.reg_cache_default = ak4396_reg,
};

static struct class *cls = NULL;

static ssize_t store_ak4396_reg(struct class *dev,
               struct class_attribute *attr, const char *buf, size_t count)
{
	int reg, value, ret;
//	char buf[10] = "123 11";
	char *start = buf;

	printk("%s, the first dat  is reg, the second dat is  data, data type is dex\n", __FUNCTION__);
	while (*start == ' ')
		start++;
	reg = simple_strtoull(start, &start, 16);

	while (*start == ' ')
		start++;
	value = simple_strtoull(start, &start, 16);

	
	ret = ak4396_spi_write(codec_temp, reg, value);	
	if (ret < 0)
			printk("ak4396_spi_write failed!\n");

	printk("reg = %d, value =%d\n", reg, value);
//	return 0;		
}
static struct class_attribute attr[] = {
       __ATTR(write_reg, 0644, NULL, store_ak4396_reg),
       __ATTR_NULL,
};
static int ak4396_spi_probe(struct spi_device *spi)
{
	struct ak4396_private *ak4396;
	int ret;
	printk("ak4396_spi_probe begin!\n");

#if 0 //defined(CONFIG_ARCH_RK3188)
	iomux_set(SPI1_CS0);
	iomux_set(SPI1_CLK);
	iomux_set(SPI1_TXD);
	printk("iomux_set is OK!!!\n");
#endif

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	ak4396 = kzalloc(sizeof(struct ak4396_private), GFP_KERNEL);
	if (ak4396 == NULL)
		return -ENOMEM;

	ak4396->control_data = spi;
	ak4396->control_type = SND_SOC_SPI;
	spi_set_drvdata(spi, ak4396);

	cls = class_create(THIS_MODULE, DRV_NAME);
	if (IS_ERR(cls))
	{
		printk("class_create failed!\n");		
	}
	ret = class_create_file(cls, attr);
	if (ret < 0)
	{
		printk("class_create_file failed!\n");		
	}

	ret = snd_soc_register_codec(&spi->dev,
			&soc_codec_device_ak4396, &ak4396_dai, 1);
	if (ret < 0)
		kfree(ak4396);

	printk("ak4396_spi_probe successful!\n");
	return ret;
}

static int __devexit ak4396_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	kfree(spi_get_drvdata(spi));
	return 0;
}

static struct spi_driver ak4396_spi_driver = {
	.driver  = {
		.name   = DRV_NAME,
		.owner  = THIS_MODULE,
	},
	.probe  = ak4396_spi_probe,
	.remove = __devexit_p(ak4396_spi_remove),
};

static int __init ak4396_init(void)
{
	printk("%s\n", __FUNCTION__);
	return spi_register_driver(&ak4396_spi_driver);
}
module_init(ak4396_init);

static void __exit ak4396_exit(void)
{
	spi_unregister_driver(&ak4396_spi_driver);
}
module_exit(ak4396_exit);

MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_DESCRIPTION("Asahi Kasei AK4396 ALSA SoC driver");
MODULE_LICENSE("GPL");

