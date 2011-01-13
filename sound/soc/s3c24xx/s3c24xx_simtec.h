/* sound/soc/s3c24xx/s3c24xx_simtec.h
 *
 * Copyright 2009 Simtec Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

extern void simtec_audio_init(struct snd_soc_pcm_runtime *rtd);

extern int simtec_audio_core_probe(struct platform_device *pdev,
				   struct snd_soc_card *card);

extern int simtec_audio_remove(struct platform_device *pdev);

#ifdef CONFIG_PM
extern const struct dev_pm_ops simtec_audio_pmops;
#define simtec_audio_pm &simtec_audio_pmops
#else
#define simtec_audio_pm NULL
#endif
