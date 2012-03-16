/**
 * Freescale ALSA SoC Machine driver utility
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef _FSL_UTILS_H
#define _FSL_UTILS_H

#define DAI_NAME_SIZE	32

struct snd_soc_dai_link;
struct device_node;

int fsl_asoc_get_codec_dev_name(struct device_node *np, char *buf, size_t len);
int fsl_asoc_get_dma_channel(struct device_node *ssi_np, const char *name,
			     struct snd_soc_dai_link *dai,
			     unsigned int *dma_channel_id,
			     unsigned int *dma_id);

#endif /* _FSL_UTILS_H */
