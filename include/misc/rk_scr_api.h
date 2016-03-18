/*
 * Driver for Rockchip Smart Card Reader Controller
 *
 * Copyright (C) 2012-2016 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __RK_SCR_API_H__
#define __RK_SCR_API_H__

int scr_open(void);
int scr_close(void);
int scr_check_card_insert(void);
int scr_reset(void);

int scr_get_atr_data(unsigned char *atr_buf, unsigned char *atr_len);
ssize_t scr_write(unsigned char *buf, unsigned int write_cnt,
		  unsigned int *to_read_cnt);
ssize_t scr_read(unsigned char *buf, unsigned int to_read_cnt,
		 unsigned int *have_read_cnt);

void scr_set_etu_duration(unsigned int F, unsigned int D);
void scr_set_work_waitingtime(unsigned char wi);

#endif	/* __RK_SCR_API_H__ */
