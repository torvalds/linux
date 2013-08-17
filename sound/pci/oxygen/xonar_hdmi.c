/*
 * helper functions for HDMI models (Xonar HDAV1.3/HDAV1.3 Slim)
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 *
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this driver; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <sound/asoundef.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include "xonar.h"

static void hdmi_write_command(struct oxygen *chip, u8 command,
			       unsigned int count, const u8 *params)
{
	unsigned int i;
	u8 checksum;

	oxygen_write_uart(chip, 0xfb);
	oxygen_write_uart(chip, 0xef);
	oxygen_write_uart(chip, command);
	oxygen_write_uart(chip, count);
	for (i = 0; i < count; ++i)
		oxygen_write_uart(chip, params[i]);
	checksum = 0xfb + 0xef + command + count;
	for (i = 0; i < count; ++i)
		checksum += params[i];
	oxygen_write_uart(chip, checksum);
}

static void xonar_hdmi_init_commands(struct oxygen *chip,
				     struct xonar_hdmi *hdmi)
{
	u8 param;

	oxygen_reset_uart(chip);
	param = 0;
	hdmi_write_command(chip, 0x61, 1, &param);
	param = 1;
	hdmi_write_command(chip, 0x74, 1, &param);
	hdmi_write_command(chip, 0x54, 5, hdmi->params);
}

void xonar_hdmi_init(struct oxygen *chip, struct xonar_hdmi *hdmi)
{
	hdmi->params[1] = IEC958_AES3_CON_FS_48000;
	hdmi->params[4] = 1;
	xonar_hdmi_init_commands(chip, hdmi);
}

void xonar_hdmi_cleanup(struct oxygen *chip)
{
	u8 param = 0;

	hdmi_write_command(chip, 0x74, 1, &param);
}

void xonar_hdmi_resume(struct oxygen *chip, struct xonar_hdmi *hdmi)
{
	xonar_hdmi_init_commands(chip, hdmi);
}

void xonar_hdmi_pcm_hardware_filter(unsigned int channel,
				    struct snd_pcm_hardware *hardware)
{
	if (channel == PCM_MULTICH) {
		hardware->rates = SNDRV_PCM_RATE_44100 |
				  SNDRV_PCM_RATE_48000 |
				  SNDRV_PCM_RATE_96000 |
				  SNDRV_PCM_RATE_192000;
		hardware->rate_min = 44100;
	}
}

void xonar_set_hdmi_params(struct oxygen *chip, struct xonar_hdmi *hdmi,
			   struct snd_pcm_hw_params *params)
{
	hdmi->params[0] = 0; /* 1 = non-audio */
	switch (params_rate(params)) {
	case 44100:
		hdmi->params[1] = IEC958_AES3_CON_FS_44100;
		break;
	case 48000:
		hdmi->params[1] = IEC958_AES3_CON_FS_48000;
		break;
	default: /* 96000 */
		hdmi->params[1] = IEC958_AES3_CON_FS_96000;
		break;
	case 192000:
		hdmi->params[1] = IEC958_AES3_CON_FS_192000;
		break;
	}
	hdmi->params[2] = params_channels(params) / 2 - 1;
	if (params_format(params) == SNDRV_PCM_FORMAT_S16_LE)
		hdmi->params[3] = 0;
	else
		hdmi->params[3] = 0xc0;
	hdmi->params[4] = 1; /* ? */
	hdmi_write_command(chip, 0x54, 5, hdmi->params);
}

void xonar_hdmi_uart_input(struct oxygen *chip)
{
	if (chip->uart_input_count >= 2 &&
	    chip->uart_input[chip->uart_input_count - 2] == 'O' &&
	    chip->uart_input[chip->uart_input_count - 1] == 'K') {
		printk(KERN_DEBUG "message from HDMI chip received:\n");
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET,
				     chip->uart_input, chip->uart_input_count);
		chip->uart_input_count = 0;
	}
}
