#ifndef _IR_I2C
#define _IR_I2C

#include <media/ir-common.h>

struct IR_i2c;

struct IR_i2c {
	IR_KEYTAB_TYPE         *ir_codes;
	struct i2c_client      c;
	struct input_dev       *input;
	struct ir_input_state  ir;

	/* Used to avoid fast repeating */
	unsigned char          old;

	struct work_struct     work;
	struct timer_list      timer;
	char                   phys[32];
	int                    (*get_key)(struct IR_i2c*, u32*, u32*);
};

int get_key_pinnacle_grey(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw);
int get_key_pinnacle_color(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw);
#endif
