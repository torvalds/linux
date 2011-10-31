/*
 * Generic AC97 sound support for SH7760
 *
 * (c) 2007 Manuel Lauss
 *
 * Licensed under the GPLv2.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <asm/io.h>

#define IPSEL 0xFE400034

/* platform specific structs can be declared here */
extern struct snd_soc_dai_driver sh4_hac_dai[2];
extern struct snd_soc_platform_driver sh7760_soc_platform;

static struct snd_soc_dai_link sh7760_ac97_dai = {
	.name = "AC97",
	.stream_name = "AC97 HiFi",
	.cpu_dai_name = "hac-dai.0",	/* HAC0 */
	.codec_dai_name = "ac97-hifi",
	.platform_name = "sh7760-pcm-audio",
	.codec_name = "ac97-codec",
	.ops = NULL,
};

static struct snd_soc_card sh7760_ac97_soc_machine  = {
	.name = "SH7760 AC97",
	.dai_link = &sh7760_ac97_dai,
	.num_links = 1,
};

static struct platform_device *sh7760_ac97_snd_device;

static int __init sh7760_ac97_init(void)
{
	int ret;
	unsigned short ipsel;

	/* enable both AC97 controllers in pinmux reg */
	ipsel = __raw_readw(IPSEL);
	__raw_writew(ipsel | (3 << 10), IPSEL);

	ret = -ENOMEM;
	sh7760_ac97_snd_device = platform_device_alloc("soc-audio", -1);
	if (!sh7760_ac97_snd_device)
		goto out;

	platform_set_drvdata(sh7760_ac97_snd_device,
			     &sh7760_ac97_soc_machine);
	ret = platform_device_add(sh7760_ac97_snd_device);

	if (ret)
		platform_device_put(sh7760_ac97_snd_device);

out:
	return ret;
}

static void __exit sh7760_ac97_exit(void)
{
	platform_device_unregister(sh7760_ac97_snd_device);
}

module_init(sh7760_ac97_init);
module_exit(sh7760_ac97_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Generic SH7760 AC97 sound machine");
MODULE_AUTHOR("Manuel Lauss <mano@roarinelk.homelinux.net>");
