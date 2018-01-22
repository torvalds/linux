/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BFIN_CAPTURE_H_
#define _BFIN_CAPTURE_H_

#include <linux/i2c.h>

struct v4l2_input;
struct ppi_info;

struct bcap_route {
	u32 input;
	u32 output;
	u32 ppi_control;
};

struct bfin_capture_config {
	/* card name */
	char *card_name;
	/* inputs available at the sub device */
	struct v4l2_input *inputs;
	/* number of inputs supported */
	int num_inputs;
	/* routing information for each input */
	struct bcap_route *routes;
	/* i2c bus adapter no */
	int i2c_adapter_id;
	/* i2c subdevice board info */
	struct i2c_board_info board_info;
	/* ppi board info */
	const struct ppi_info *ppi_info;
	/* ppi control */
	unsigned long ppi_control;
	/* ppi interrupt mask */
	u32 int_mask;
	/* horizontal blanking pixels */
	int blank_pixels;
};

#endif
