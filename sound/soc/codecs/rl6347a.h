/*
 * rl6347a.h - RL6347A class device shared support
 *
 * Copyright 2015 Realtek Semiconductor Corp.
 *
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __RL6347A_H__
#define __RL6347A_H__

#define VERB_CMD(V, N, D) ((N << 20) | (V << 8) | D)

#define RL6347A_VENDOR_REGISTERS	0x20

#define RL6347A_COEF_INDEX\
	VERB_CMD(AC_VERB_SET_COEF_INDEX, RL6347A_VENDOR_REGISTERS, 0)
#define RL6347A_PROC_COEF\
	VERB_CMD(AC_VERB_SET_PROC_COEF, RL6347A_VENDOR_REGISTERS, 0)

struct rl6347a_priv {
	struct reg_default *index_cache;
	int index_cache_size;
};

int rl6347a_hw_write(void *context, unsigned int reg, unsigned int value);
int rl6347a_hw_read(void *context, unsigned int reg, unsigned int *value);

#endif /* __RL6347A_H__ */
