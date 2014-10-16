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

#ifndef PTSSERV_H
#define PTSSERV_H

#include <mach/cpu.h>

enum {
    PTS_TYPE_VIDEO = 0,
    PTS_TYPE_AUDIO = 1,
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    PTS_TYPE_HEVC  = 2,
    PTS_TYPE_MAX   = 3
#else
    PTS_TYPE_MAX   = 2
#endif
};

#define apts_checkin(x) pts_checkin(PTS_TYPE_AUDIO, (x))
#define vpts_checkin(x) pts_checkin(PTS_TYPE_VIDEO, (x))

#ifndef CALC_CACHED_TIME
#define CALC_CACHED_TIME
#endif
extern int pts_checkin(u8 type, u32 val);

extern int pts_checkin_wrptr(u8 type, u32 ptr, u32 val);

extern int pts_checkin_offset(u8 type, u32 offset, u32 val);

extern int pts_checkin_offset_us64(u8 type, u32 offset, u64 us);

extern int get_last_checkin_pts(u8 type);

extern int get_last_checkout_pts(u8 type);

extern int pts_lookup(u8 type, u32 *val, u32 pts_margin);

extern int pts_lookup_offset(u8 type, u32 offset,u32 *val, u32 pts_margin);

extern int pts_lookup_offset_us64(u8 type, u32 offset, u32 *val, u32 pts_margin, u64 *uS64);

extern int pts_set_resolution(u8 type, u32 level);

extern int pts_set_rec_size(u8 type, u32 val);

extern int pts_start(u8 type);

extern int pts_stop(u8 type);

extern int first_lookup_pts_failed(u8 type);

extern int first_pts_checkin_complete(u8 type);
extern int calculation_stream_delayed_ms(u8 type,u32 *latestbirate,u32*avg_bitare);

extern int calculation_vcached_delayed(void);

extern int calculation_acached_delayed(void);
#endif /* PTSSERV_H */
