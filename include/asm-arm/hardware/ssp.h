/*
 *  ssp.h
 *
 *  Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef SSP_H
#define SSP_H

struct ssp_state {
	unsigned int	cr0;
	unsigned int	cr1;
};

int ssp_write_word(u16 data);
int ssp_read_word(void);
void ssp_flush(void);
void ssp_enable(void);
void ssp_disable(void);
void ssp_save_state(struct ssp_state *ssp);
void ssp_restore_state(struct ssp_state *ssp);
int ssp_init(void);
void ssp_exit(void);

#endif
