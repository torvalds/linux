#ifndef __LINUX_SARADC_H
#define __LINUX_SARADC_H

enum {
	CHAN_0 = 0,
	CHAN_1,
	CHAN_2,
	CHAN_3,
	CHAN_4,
	CHAN_5,
	CHAN_6,
	CHAN_7,
	SARADC_CHAN_NUM,
};

enum {
	NO_AVG = 0,
	SIMPLE_AVG_1,
	SIMPLE_AVG_2,
	SIMPLE_AVG_4,
	SIMPLE_AVG_8,
	MEDIAN_AVG_8,
};

// touchscreen command
enum {
	CMD_GET_X = 0,
	CMD_GET_Y,
	CMD_GET_Z1,
	CMD_GET_Z2,
	CMD_GET_PENDOWN,
	CMD_INIT_PENIRQ,
	CMD_SET_PENIRQ,
	CMD_CLEAR_PENIRQ,
};


extern int get_adc_sample(int chan);
extern int saradc_ts_service(int cmd);
extern void  temp_sensor_adc_init(int triming);
#endif
