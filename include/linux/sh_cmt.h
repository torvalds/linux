#ifndef __SH_CMT_H__
#define __SH_CMT_H__

struct sh_cmt_config {
	char *name;
	unsigned long channel_offset;
	int timer_bit;
	char *clk;
	unsigned long clockevent_rating;
	unsigned long clocksource_rating;
};

#endif /* __SH_CMT_H__ */
