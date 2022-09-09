// SPDX-License-Identifier: GPL-2.0
//
// Generic AC97 sound support for SH7760
//
// (c) 2007 Manuel Lauss

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <asm/io.h>

#define IPSEL 0xFE400034

SND_SOC_DAILINK_DEFS(ac97,
	DAILINK_COMP_ARRAY(COMP_CPU("hac-dai.0")),	/* HAC0 */
	DAILINK_COMP_ARRAY(COMP_CODEC("ac97-codec", "ac97-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("sh7760-pcm-audio")));

static struct snd_soc_dai_link sh7760_ac97_dai = {
	.name = "AC97",
	.stream_name = "AC97 HiFi",
	SND_SOC_DAILINK_REG(ac97),
};

static struct snd_soc_card sh7760_ac97_soc_machine  = {
	.name = "SH7760 AC97",
	.owner = THIS_MODULE,
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

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Generic SH7760 AC97 sound machine");
MODULE_AUTHOR("Manuel Lauss <mano@roarinelk.homelinux.net>");
