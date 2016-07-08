#ifndef __LINUX_SOC_RENESAS_RCAR_SYSC_H__
#define __LINUX_SOC_RENESAS_RCAR_SYSC_H__

#include <linux/types.h>

struct rcar_sysc_ch {
	u16 chan_offs;
	u8 chan_bit;
	u8 isr_bit;
};

int rcar_sysc_power_down(const struct rcar_sysc_ch *sysc_ch);
int rcar_sysc_power_up(const struct rcar_sysc_ch *sysc_ch);
void __iomem *rcar_sysc_init(phys_addr_t base);

#endif /* __LINUX_SOC_RENESAS_RCAR_SYSC_H__ */
