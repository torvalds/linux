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

#ifndef TSYNC_H
#define TSYNC_H

#define TIME_UNIT90K    (90000)
#define VIDEO_HOLD_THRESHOLD        (TIME_UNIT90K * 3)
#define VIDEO_HOLD_SLOWSYNC_THRESHOLD        (TIME_UNIT90K / 10)
#define AV_DISCONTINUE_THREDHOLD_MIN    (TIME_UNIT90K * 3)
#define AV_DISCONTINUE_THREDHOLD_MAX    (TIME_UNIT90K * 60)

typedef enum {
    VIDEO_START,
    VIDEO_PAUSE,
    VIDEO_STOP,
    VIDEO_TSTAMP_DISCONTINUITY,
    AUDIO_START,
    AUDIO_PAUSE,
    AUDIO_RESUME,
    AUDIO_STOP,
    AUDIO_TSTAMP_DISCONTINUITY,
    AUDIO_PRE_START
} avevent_t;

typedef enum {
    TSYNC_MODE_VMASTER,
    TSYNC_MODE_AMASTER,
    TSYNC_MODE_PCRMASTER,
} tsync_mode_t;

extern void tsync_avevent_locked(avevent_t event, u32 param);

extern void tsync_mode_reinit(void);

extern void tsync_avevent(avevent_t event, u32 param);

extern void tsync_audio_break(int audio_break);

extern void tsync_trick_mode(int trick_mode);

extern void tsync_set_avthresh(unsigned int av_thresh);

extern void tsync_set_syncthresh(unsigned int sync_thresh);

extern void tsync_set_dec_reset(void);

extern void tsync_set_enable(int enable);

extern int tsync_get_mode(void);

extern int tsync_get_sync_adiscont(void);

extern int tsync_get_sync_vdiscont(void);

extern void tsync_set_sync_adiscont(int syncdiscont);

extern void tsync_set_sync_vdiscont(int syncdiscont);

extern u32 tsync_get_sync_adiscont_diff(void);

extern u32 tsync_get_sync_vdiscont_diff(void);

extern void tsync_set_sync_adiscont_diff(u32 discontinue_diff);

extern void tsync_set_sync_vdiscont_diff(u32 discontinue_diff);
extern int tsync_set_apts(unsigned pts);



extern void tsync_set_automute_on(int automute_on);

extern int tsync_get_debug_pts_checkin(void);

extern int tsync_get_debug_pts_checkout(void);

extern int tsync_get_debug_vpts(void);

extern int tsync_get_debug_apts(void);
extern int tsync_get_av_threshold_min(void);

extern int tsync_get_av_threshold_max(void);

extern int tsync_set_av_threshold_min(int min);

extern int tsync_set_av_threshold_max(int max);

static inline u32 tsync_vpts_discontinuity_margin(void)
{
    return tsync_get_av_threshold_min();
}

#endif /* TSYNC_H */
