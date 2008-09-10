#ifndef PXA2XX_LIB_H
#define PXA2XX_LIB_H

#include <linux/platform_device.h>
#include <sound/ac97_codec.h>

extern unsigned short pxa2xx_ac97_read(struct snd_ac97 *ac97, unsigned short reg);
extern void pxa2xx_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short val);

extern bool pxa2xx_ac97_try_warm_reset(struct snd_ac97 *ac97);
extern bool pxa2xx_ac97_try_cold_reset(struct snd_ac97 *ac97);
extern void pxa2xx_ac97_finish_reset(struct snd_ac97 *ac97);

extern int pxa2xx_ac97_hw_suspend(void);
extern int pxa2xx_ac97_hw_resume(void);

extern int pxa2xx_ac97_hw_probe(struct platform_device *dev);
extern void pxa2xx_ac97_hw_remove(struct platform_device *dev);

#endif
