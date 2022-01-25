// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright 2021 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>

#include <sound/soc.h>
#include <sound/sof.h>
#include <sound/compress_driver.h>
#include "sof-audio.h"
#include "sof-priv.h"

static void snd_sof_compr_fragment_elapsed_work(struct work_struct *work)
{
	struct snd_sof_pcm_stream *sps =
		container_of(work, struct snd_sof_pcm_stream,
			     period_elapsed_work);

	snd_compr_fragment_elapsed(sps->cstream);
}

void snd_sof_compr_init_elapsed_work(struct work_struct *work)
{
	INIT_WORK(work, snd_sof_compr_fragment_elapsed_work);
}

/*
 * sof compr fragment elapse, this could be called in irq thread context
 */
void snd_sof_compr_fragment_elapsed(struct snd_compr_stream *cstream)
{
	struct snd_soc_component *component;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_sof_pcm *spcm;

	if (!cstream)
		return;

	rtd = cstream->private_data;
	component = snd_soc_rtdcom_lookup(rtd, SOF_AUDIO_PCM_DRV_NAME);

	spcm = snd_sof_find_spcm_dai(component, rtd);
	if (!spcm) {
		dev_err(component->dev,
			"fragment elapsed called for unknown stream!\n");
		return;
	}

	/* use the same workqueue-based solution as for PCM, cf. snd_sof_pcm_elapsed */
	schedule_work(&spcm->stream[cstream->direction].period_elapsed_work);
}
