
#include "ad1848.h"

/*	From gus_card.c */
int gus_set_midi_irq(int num);
irqreturn_t gusintr(int irq, void *dev_id, struct pt_regs * dummy);

/*	From gus_wave.c */
int gus_wave_detect(int baseaddr);
void gus_wave_init(struct address_info *hw_config);
void gus_wave_unload (struct address_info *hw_config);
void gus_voice_irq(void);
void gus_write8(int reg, unsigned int data);
void guswave_dma_irq(void);
void gus_delay(void);
int gus_default_mixer_ioctl (int dev, unsigned int cmd, void __user *arg);
void gus_timer_command (unsigned int addr, unsigned int val);

/*	From gus_midi.c */
void gus_midi_init(struct address_info *hw_config);
void gus_midi_interrupt(int dummy);

/*	From ics2101.c */
int ics2101_mixer_init(void);
