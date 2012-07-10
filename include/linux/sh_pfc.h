/*
 * SuperH Pin Function Controller Support
 *
 * Copyright (c) 2008 Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef __SH_PFC_H
#define __SH_PFC_H

#include <asm-generic/gpio.h>

typedef unsigned short pinmux_enum_t;
typedef unsigned short pinmux_flag_t;

enum {
	PINMUX_TYPE_NONE,

	PINMUX_TYPE_FUNCTION,
	PINMUX_TYPE_GPIO,
	PINMUX_TYPE_OUTPUT,
	PINMUX_TYPE_INPUT,
	PINMUX_TYPE_INPUT_PULLUP,
	PINMUX_TYPE_INPUT_PULLDOWN,

	PINMUX_FLAG_TYPE,	/* must be last */
};

#define PINMUX_FLAG_DBIT_SHIFT      5
#define PINMUX_FLAG_DBIT            (0x1f << PINMUX_FLAG_DBIT_SHIFT)
#define PINMUX_FLAG_DREG_SHIFT      10
#define PINMUX_FLAG_DREG            (0x3f << PINMUX_FLAG_DREG_SHIFT)

struct pinmux_gpio {
	pinmux_enum_t enum_id;
	pinmux_flag_t flags;
};

#define PINMUX_GPIO(gpio, data_or_mark) \
	[gpio] = { .enum_id = data_or_mark, .flags = PINMUX_TYPE_NONE }

#define PINMUX_DATA(data_or_mark, ids...) data_or_mark, ids, 0

struct pinmux_cfg_reg {
	unsigned long reg, reg_width, field_width;
	unsigned long *cnt;
	pinmux_enum_t *enum_ids;
	unsigned long *var_field_width;
};

#define PINMUX_CFG_REG(name, r, r_width, f_width) \
	.reg = r, .reg_width = r_width, .field_width = f_width,		\
	.cnt = (unsigned long [r_width / f_width]) {}, \
	.enum_ids = (pinmux_enum_t [(r_width / f_width) * (1 << f_width)])

#define PINMUX_CFG_REG_VAR(name, r, r_width, var_fw0, var_fwn...) \
	.reg = r, .reg_width = r_width,	\
	.cnt = (unsigned long [r_width]) {}, \
	.var_field_width = (unsigned long [r_width]) { var_fw0, var_fwn, 0 }, \
	.enum_ids = (pinmux_enum_t [])

struct pinmux_data_reg {
	unsigned long reg, reg_width, reg_shadow;
	pinmux_enum_t *enum_ids;
	void __iomem *mapped_reg;
};

#define PINMUX_DATA_REG(name, r, r_width) \
	.reg = r, .reg_width = r_width,	\
	.enum_ids = (pinmux_enum_t [r_width]) \

struct pinmux_irq {
	int irq;
	pinmux_enum_t *enum_ids;
};

#define PINMUX_IRQ(irq_nr, ids...)			   \
	{ .irq = irq_nr, .enum_ids = (pinmux_enum_t []) { ids, 0 } }	\

struct pinmux_range {
	pinmux_enum_t begin;
	pinmux_enum_t end;
	pinmux_enum_t force;
};

struct pfc_window {
	phys_addr_t phys;
	void __iomem *virt;
	unsigned long size;
};

struct sh_pfc {
	char *name;
	pinmux_enum_t reserved_id;
	struct pinmux_range data;
	struct pinmux_range input;
	struct pinmux_range input_pd;
	struct pinmux_range input_pu;
	struct pinmux_range output;
	struct pinmux_range mark;
	struct pinmux_range function;

	unsigned first_gpio, last_gpio;

	struct pinmux_gpio *gpios;
	struct pinmux_cfg_reg *cfg_regs;
	struct pinmux_data_reg *data_regs;

	pinmux_enum_t *gpio_data;
	unsigned int gpio_data_size;

	struct pinmux_irq *gpio_irq;
	unsigned int gpio_irq_size;

	spinlock_t lock;

	struct resource *resource;
	unsigned int num_resources;
	struct pfc_window *window;

	unsigned long unlock_reg;
};

/* XXX compat for now */
#define pinmux_info sh_pfc

/* drivers/sh/pfc/gpio.c */
int sh_pfc_register_gpiochip(struct sh_pfc *pfc);

/* drivers/sh/pfc/core.c */
int register_sh_pfc(struct sh_pfc *pfc);

int sh_pfc_read_bit(struct pinmux_data_reg *dr, unsigned long in_pos);
void sh_pfc_write_bit(struct pinmux_data_reg *dr, unsigned long in_pos,
		      unsigned long value);
int sh_pfc_get_data_reg(struct sh_pfc *pfc, unsigned gpio,
			struct pinmux_data_reg **drp, int *bitp);
int sh_pfc_gpio_to_enum(struct sh_pfc *pfc, unsigned gpio, int pos,
			pinmux_enum_t *enum_idp);
int sh_pfc_config_gpio(struct sh_pfc *pfc, unsigned gpio, int pinmux_type,
		       int cfg_mode);
int sh_pfc_set_direction(struct sh_pfc *pfc, unsigned gpio,
			 int new_pinmux_type);

/* xxx */
static inline int register_pinmux(struct pinmux_info *pip)
{
	struct sh_pfc *pfc = pip;
	return register_sh_pfc(pfc);
}

enum { GPIO_CFG_DRYRUN, GPIO_CFG_REQ, GPIO_CFG_FREE };

/* helper macro for port */
#define PORT_1(fn, pfx, sfx) fn(pfx, sfx)

#define PORT_10(fn, pfx, sfx) \
	PORT_1(fn, pfx##0, sfx), PORT_1(fn, pfx##1, sfx),	\
	PORT_1(fn, pfx##2, sfx), PORT_1(fn, pfx##3, sfx),	\
	PORT_1(fn, pfx##4, sfx), PORT_1(fn, pfx##5, sfx),	\
	PORT_1(fn, pfx##6, sfx), PORT_1(fn, pfx##7, sfx),	\
	PORT_1(fn, pfx##8, sfx), PORT_1(fn, pfx##9, sfx)

#define PORT_90(fn, pfx, sfx) \
	PORT_10(fn, pfx##1, sfx), PORT_10(fn, pfx##2, sfx),	\
	PORT_10(fn, pfx##3, sfx), PORT_10(fn, pfx##4, sfx),	\
	PORT_10(fn, pfx##5, sfx), PORT_10(fn, pfx##6, sfx),	\
	PORT_10(fn, pfx##7, sfx), PORT_10(fn, pfx##8, sfx),	\
	PORT_10(fn, pfx##9, sfx)

#define _PORT_ALL(pfx, sfx) pfx##_##sfx
#define _GPIO_PORT(pfx, sfx) PINMUX_GPIO(GPIO_PORT##pfx, PORT##pfx##_DATA)
#define PORT_ALL(str)	CPU_ALL_PORT(_PORT_ALL, PORT, str)
#define GPIO_PORT_ALL()	CPU_ALL_PORT(_GPIO_PORT, , unused)
#define GPIO_FN(str) PINMUX_GPIO(GPIO_FN_##str, str##_MARK)

/* helper macro for pinmux_enum_t */
#define PORT_DATA_I(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_IN)

#define PORT_DATA_I_PD(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0,	\
		    PORT##nr##_IN, PORT##nr##_IN_PD)

#define PORT_DATA_I_PU(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0,	\
		    PORT##nr##_IN, PORT##nr##_IN_PU)

#define PORT_DATA_I_PU_PD(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0,			\
		    PORT##nr##_IN, PORT##nr##_IN_PD, PORT##nr##_IN_PU)

#define PORT_DATA_O(nr)		\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_OUT)

#define PORT_DATA_IO(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_OUT,	\
		    PORT##nr##_IN)

#define PORT_DATA_IO_PD(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_OUT,	\
		    PORT##nr##_IN, PORT##nr##_IN_PD)

#define PORT_DATA_IO_PU(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_OUT,	\
		    PORT##nr##_IN, PORT##nr##_IN_PU)

#define PORT_DATA_IO_PU_PD(nr)	\
	PINMUX_DATA(PORT##nr##_DATA, PORT##nr##_FN0, PORT##nr##_OUT,	\
		    PORT##nr##_IN, PORT##nr##_IN_PD, PORT##nr##_IN_PU)

/* helper macro for top 4 bits in PORTnCR */
#define _PCRH(in, in_pd, in_pu, out)	\
	0, (out), (in), 0,		\
	0, 0, 0, 0,			\
	0, 0, (in_pd), 0,		\
	0, 0, (in_pu), 0

#define PORTCR(nr, reg)							\
	{								\
		PINMUX_CFG_REG("PORT" nr "CR", reg, 8, 4) {		\
			_PCRH(PORT##nr##_IN, PORT##nr##_IN_PD,		\
			      PORT##nr##_IN_PU, PORT##nr##_OUT),	\
				PORT##nr##_FN0, PORT##nr##_FN1,		\
				PORT##nr##_FN2, PORT##nr##_FN3,		\
				PORT##nr##_FN4, PORT##nr##_FN5,		\
				PORT##nr##_FN6, PORT##nr##_FN7 }	\
	}

#endif /* __SH_PFC_H */
