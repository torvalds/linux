
#include <linux/interrupt.h>

#define AD_F_CS4231     0x0001  /* Returned if a CS4232 (or compatible) detected */
#define AD_F_CS4248     0x0001  /* Returned if a CS4248 (or compatible) detected */

#define         AD1848_SET_XTAL         1
#define         AD1848_MIXER_REROUTE    2

#define AD1848_REROUTE(oldctl, newctl) \
                ad1848_control(AD1848_MIXER_REROUTE, ((oldctl)<<8)|(newctl))
		

int ad1848_init(char *name, struct resource *ports, int irq, int dma_playback,
	int dma_capture, int share_dma, int *osp, struct module *owner);
void ad1848_unload (int io_base, int irq, int dma_playback, int dma_capture, int share_dma);

int ad1848_detect (struct resource *ports, int *flags, int *osp);
int ad1848_control(int cmd, int arg);

irqreturn_t adintr(int irq, void *dev_id, struct pt_regs * dummy);
void attach_ms_sound(struct address_info * hw_config, struct resource *ports, struct module * owner);

int probe_ms_sound(struct address_info *hw_config, struct resource *ports);
void unload_ms_sound(struct address_info *hw_info);
