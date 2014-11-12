/*
 * AMLOGIC PTS Manager Driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef TIMESTAMP_H
#define TIMESTAMP_H

extern u32 timestamp_vpts_get(void);

extern void timestamp_vpts_set(u32 pts);

extern void timestamp_vpts_inc(s32 val);

extern u32 timestamp_apts_get(void);

extern void timestamp_apts_set(u32 pts);

extern void timestamp_apts_inc(s32 val);

extern u32 timestamp_pcrscr_get(void);

extern void timestamp_pcrscr_set(u32 pts);

extern void timestamp_pcrscr_inc(s32 val);

extern void timestamp_pcrscr_inc_scale(s32 inc, u32 base);

extern void timestamp_pcrscr_enable(u32 enable);

extern u32 timestamp_pcrscr_enable_state(void);

extern void timestamp_pcrscr_set_adj(s32 inc);

extern void timestamp_apts_enable(u32 enable);

extern void  timestamp_apts_start(u32 enable);

extern u32 timestamp_apts_started(void);

extern void timestamp_firstvpts_set(u32 pts);

extern u32 timestamp_firstvpts_get(void);

extern void timestamp_firstapts_set(u32 pts);

extern u32 timestamp_firstapts_get(void);

#endif /* TIMESTAMP_H */
