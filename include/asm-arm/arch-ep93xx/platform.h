/*
 * linux/include/asm-arm/arch-ep93xx/platform.h
 */

#ifndef __ASSEMBLY__

void ep93xx_map_io(void);
void ep93xx_init_irq(void);
void ep93xx_init_time(unsigned long);
void ep93xx_init_devices(void);
extern struct sys_timer ep93xx_timer;

struct ep93xx_eth_data
{
	unsigned char	dev_addr[6];
	unsigned char	phy_id;
};


#endif
