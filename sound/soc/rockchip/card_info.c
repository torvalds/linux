/*
 *  card_info.c
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

/*
  Get sound card infos:
      codec-name
      cpu-dai-name

      format

      continuous-clock

      bitclock-inversion
      frame-inversion

      bitclock-master
      frame-master

  Get codec-name and cpu-dai-name in this fun,
  and get oher infos in fun snd_soc_of_parse_daifmt().

  Set in dts:
		dais {
			dai0 {
				codec-name = "codec_name.i2c_bus-i2c_addr";
				cpu-dai-name = "cpu_dai_name";
				format = "i2s";
				//continuous-clock;
				//bitclock-inversion;
				//frame-inversion;
				//bitclock-master;
				//frame-master;
			};

			dai1 {
				codec-name = "codec_name.i2c_bus-i2c_addr";
				cpu-dai-name = "cpu_dai_name";
			};
		};
 */
int rockchip_of_get_sound_card_info(struct snd_soc_card *card)
{
	struct device_node *dai_node, *child_dai_node;
	int dai_num;

	dai_node = of_get_child_by_name(card->dev->of_node, "dais");
	if (!dai_node) {
		dev_err(card->dev, "%s() Can not get child: dais\n", __FUNCTION__);
		return -EINVAL;
	}

	dai_num = 0;

	for_each_child_of_node(dai_node, child_dai_node) {
		//We only need to set fmt to cpu for dai 0.
		if (dai_num == 0)
			card->dai_link[dai_num].dai_fmt = snd_soc_of_parse_daifmt(child_dai_node, NULL);


		card->dai_link[dai_num].codec_name = NULL;
		card->dai_link[dai_num].cpu_dai_name = NULL;
		card->dai_link[dai_num].platform_name= NULL;

		card->dai_link[dai_num].codec_of_node= of_parse_phandle(child_dai_node,
			"codec-of-node", 0);
		if (!card->dai_link[dai_num].codec_of_node) {
			dev_err(card->dev,
				"Property 'codec-of-node' missing or invalid\n");
			return -EINVAL;
		}

		card->dai_link[dai_num].cpu_of_node= of_parse_phandle(child_dai_node,
			"cpu-of-node", 0);
		if (!card->dai_link[dai_num].cpu_of_node) {
			dev_err(card->dev,
				"Property 'cpu-of-node' missing or invalid\n");
			return -EINVAL;
		}

		card->dai_link[dai_num].platform_of_node = card->dai_link[dai_num].cpu_of_node;

		if (++dai_num >= card->num_links)
			break;
	}

	if (dai_num < card->num_links) {
		dev_err(card->dev, "%s() Can not get enough property for dais, dai: %d, max dai num: %d\n",
			__FUNCTION__, dai_num, card->num_links);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_of_get_sound_card_info);

/* Module information */
MODULE_AUTHOR("Jear <Jear.Chen@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP ASoC Interface");
MODULE_LICENSE("GPL");
