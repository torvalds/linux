/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIS3LV02D_H_
#define __LIS3LV02D_H_

/**
 * struct lis3lv02d_platform_data - lis3 chip family platform data
 * @click_flags:	Click detection unit configuration
 * @click_thresh_x:	Click detection unit x axis threshold
 * @click_thresh_y:	Click detection unit y axis threshold
 * @click_thresh_z:	Click detection unit z axis threshold
 * @click_time_limit:	Click detection unit time parameter
 * @click_latency:	Click detection unit latency parameter
 * @click_window:	Click detection unit window parameter
 * @irq_cfg:		On chip irq source and type configuration (click /
 *			data available / wake up, open drain, polarity)
 * @irq_flags1:		Additional irq triggering flags for irq channel 0
 * @irq_flags2:		Additional irq triggering flags for irq channel 1
 * @duration1:		Wake up unit 1 duration parameter
 * @duration2:		Wake up unit 2 duration parameter
 * @wakeup_flags:	Wake up unit 1 flags
 * @wakeup_thresh:	Wake up unit 1 threshold value
 * @wakeup_flags2:	Wake up unit 2 flags
 * @wakeup_thresh2:	Wake up unit 2 threshold value
 * @hipass_ctrl:	High pass filter control (enable / disable, cut off
 *			frequency)
 * @axis_x:		Sensor orientation remapping for x-axis
 * @axis_y:		Sensor orientation remapping for y-axis
 * @axis_z:		Sensor orientation remapping for z-axis
 * @driver_features:	Enable bits for different features. Disabled by default
 * @default_rate:	Default sampling rate. 0 means reset default
 * @setup_resources:	Interrupt line setup call back function
 * @release_resources:	Interrupt line release call back function
 * @st_min_limits[3]:	Selftest acceptance minimum values
 * @st_max_limits[3]:	Selftest acceptance maximum values
 * @irq2:		Irq line 2 number
 *
 * Platform data is used to setup the sensor chip. Meaning of the different
 * chip features can be found from the data sheet. It is publicly available
 * at www.st.com web pages. Currently the platform data is used
 * only for the 8 bit device. The 8 bit device has two wake up / free fall
 * detection units and click detection unit. There are plenty of ways to
 * configure the chip which makes is quite hard to explain deeper meaning of
 * the fields here. Behaviour of the detection blocks varies heavily depending
 * on the configuration. For example, interrupt detection block can use high
 * pass filtered data which makes it react to the changes in the acceleration.
 * Irq_flags can be used to enable interrupt detection on the both edges.
 * With proper chip configuration this produces interrupt when some trigger
 * starts and when it goes away.
 */

struct lis3lv02d_platform_data {
	/* please note: the 'click' feature is only supported for
	 * LIS[32]02DL variants of the chip and will be ignored for
	 * others */
#define LIS3_CLICK_SINGLE_X	(1 << 0)
#define LIS3_CLICK_DOUBLE_X	(1 << 1)
#define LIS3_CLICK_SINGLE_Y	(1 << 2)
#define LIS3_CLICK_DOUBLE_Y	(1 << 3)
#define LIS3_CLICK_SINGLE_Z	(1 << 4)
#define LIS3_CLICK_DOUBLE_Z	(1 << 5)
	unsigned char click_flags;
	unsigned char click_thresh_x;
	unsigned char click_thresh_y;
	unsigned char click_thresh_z;
	unsigned char click_time_limit;
	unsigned char click_latency;
	unsigned char click_window;

#define LIS3_IRQ1_DISABLE	(0 << 0)
#define LIS3_IRQ1_FF_WU_1	(1 << 0)
#define LIS3_IRQ1_FF_WU_2	(2 << 0)
#define LIS3_IRQ1_FF_WU_12	(3 << 0)
#define LIS3_IRQ1_DATA_READY	(4 << 0)
#define LIS3_IRQ1_CLICK		(7 << 0)
#define LIS3_IRQ1_MASK		(7 << 0)
#define LIS3_IRQ2_DISABLE	(0 << 3)
#define LIS3_IRQ2_FF_WU_1	(1 << 3)
#define LIS3_IRQ2_FF_WU_2	(2 << 3)
#define LIS3_IRQ2_FF_WU_12	(3 << 3)
#define LIS3_IRQ2_DATA_READY	(4 << 3)
#define LIS3_IRQ2_CLICK		(7 << 3)
#define LIS3_IRQ2_MASK		(7 << 3)
#define LIS3_IRQ_OPEN_DRAIN	(1 << 6)
#define LIS3_IRQ_ACTIVE_LOW	(1 << 7)
	unsigned char irq_cfg;
	unsigned char irq_flags1; /* Additional irq edge / level flags */
	unsigned char irq_flags2; /* Additional irq edge / level flags */
	unsigned char duration1;
	unsigned char duration2;
#define LIS3_WAKEUP_X_LO	(1 << 0)
#define LIS3_WAKEUP_X_HI	(1 << 1)
#define LIS3_WAKEUP_Y_LO	(1 << 2)
#define LIS3_WAKEUP_Y_HI	(1 << 3)
#define LIS3_WAKEUP_Z_LO	(1 << 4)
#define LIS3_WAKEUP_Z_HI	(1 << 5)
	unsigned char wakeup_flags;
	unsigned char wakeup_thresh;
	unsigned char wakeup_flags2;
	unsigned char wakeup_thresh2;
#define LIS3_HIPASS_CUTFF_8HZ   0
#define LIS3_HIPASS_CUTFF_4HZ   1
#define LIS3_HIPASS_CUTFF_2HZ   2
#define LIS3_HIPASS_CUTFF_1HZ   3
#define LIS3_HIPASS1_DISABLE    (1 << 2)
#define LIS3_HIPASS2_DISABLE    (1 << 3)
	unsigned char hipass_ctrl;
#define LIS3_NO_MAP		0
#define LIS3_DEV_X		1
#define LIS3_DEV_Y		2
#define LIS3_DEV_Z		3
#define LIS3_INV_DEV_X	       -1
#define LIS3_INV_DEV_Y	       -2
#define LIS3_INV_DEV_Z	       -3
	s8 axis_x;
	s8 axis_y;
	s8 axis_z;
#define LIS3_USE_BLOCK_READ	0x02
	u16 driver_features;
	int default_rate;
	int (*setup_resources)(void);
	int (*release_resources)(void);
	/* Limits for selftest are specified in chip data sheet */
	s16 st_min_limits[3]; /* min pass limit x, y, z */
	s16 st_max_limits[3]; /* max pass limit x, y, z */
	int irq2;
};

#endif /* __LIS3LV02D_H_ */
