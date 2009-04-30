#ifndef __SH_MTU2_H__
#define __SH_MTU2_H__

struct sh_mtu2_config {
	char *name;
	int channel_offset;
	int timer_bit;
	char *clk;
	unsigned long clockevent_rating;
};

#endif /* __SH_MTU2_H__ */
