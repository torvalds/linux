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
      audio-codec
      audio-controller

      format

      continuous-clock

      bitclock-inversion
      frame-inversion

      bitclock-master
      frame-master

  Get audio-codec and audio-controller in this fun,
  and get oher infos in fun snd_soc_of_parse_daifmt().

  Set in dts:
		dais {
			dai0 {
				audio-codec = <&codec_of_node>;
				audio-controller = <&cpu_of_node>;
				format = "i2s";
				//continuous-clock;
				//bitclock-inversion;
				//frame-inversion;
				//bitclock-master;
				//frame-master;
			};

			dai1 {
				audio-codec = <&codec_of_node>;
				audio-controller = <&cpu_of_node>;
				format = "dsp_a";
				//continuous-clock;
				bitclock-inversion;
				//frame-inversion;
				//bitclock-master;
				//frame-master;
			};
		};
 */
int rockchip_of_get_sound_card_info_(struct snd_soc_card *card,
				     bool is_need_fmt)
{
	struct device_node *dai_node, *child_dai_node;
	int dai_num;

	dai_node = of_get_child_by_name(card->dev->of_node, "dais");
	if (!dai_node) {
		dev_err(card->dev, "%s() Can not get child: dais\n",
			__func__);
		return -EINVAL;
	}

	dai_num = 0;

	for_each_child_of_node(dai_node, child_dai_node) {
		if (is_need_fmt) {
			card->dai_link[dai_num].dai_fmt =
				snd_soc_of_parse_daifmt(child_dai_node, NULL);
			if ((card->dai_link[dai_num].dai_fmt &
				SND_SOC_DAIFMT_MASTER_MASK) == 0) {
				dev_err(card->dev,
					"Property 'format' missing or invalid\n");
				return -EINVAL;
			}
		}

		card->dai_link[dai_num].codec_name = NULL;
		card->dai_link[dai_num].cpu_dai_name = NULL;
		card->dai_link[dai_num].platform_name = NULL;

		card->dai_link[dai_num].codec_of_node = of_parse_phandle(
			child_dai_node,
			"audio-codec", 0);
		if (!card->dai_link[dai_num].codec_of_node) {
			dev_err(card->dev,
				"Property 'audio-codec' missing or invalid\n");
			return -EINVAL;
		}

		card->dai_link[dai_num].cpu_of_node = of_parse_phandle(
			child_dai_node,
			"audio-controller", 0);
		if (!card->dai_link[dai_num].cpu_of_node) {
			dev_err(card->dev,
				"Property 'audio-controller' missing or invalid\n");
			return -EINVAL;
		}

		card->dai_link[dai_num].platform_of_node =
			card->dai_link[dai_num].cpu_of_node;

		if (++dai_num >= card->num_links)
			break;
	}

	if (dai_num < card->num_links) {
		dev_err(card->dev, "%s() Can not get enough property for dais, dai: %d, max dai num: %d\n",
			__func__, dai_num, card->num_links);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rockchip_of_get_sound_card_info_);

int rockchip_of_get_sound_card_info(struct snd_soc_card *card)
{
	return rockchip_of_get_sound_card_info_(card, true);
}
EXPORT_SYMBOL_GPL(rockchip_of_get_sound_card_info);

/* Module information */
MODULE_AUTHOR("Jear <Jear.Chen@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP ASoC Interface");
MODULE_LICENSE("GPL");
