/*
 * Amlogic Apollo
 * frame buffer driver
 *
 * Copyright (C) 2009 Amlogic, Inc.
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
 * Author:	Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef VINFO_H
#define VINFO_H
//the MSB is represent vmode set by logo
#define	VMODE_LOGO_BIT_MASK	0x8000
#define	VMODE_MODE_BIT_MASK	0xff
typedef enum {
    VMODE_480I  = 0,
    VMODE_480I_RPT  ,
    VMODE_480CVBS,
    VMODE_480P  ,
    VMODE_480P_RPT  ,
    VMODE_576I   ,
    VMODE_576I_RPT  ,
    VMODE_576CVBS   ,
    VMODE_576P  ,
    VMODE_576P_RPT  ,
    VMODE_720P  ,
    VMODE_1080I ,
    VMODE_1080P ,
    VMODE_720P_50HZ ,
    VMODE_1080I_50HZ ,
    VMODE_1080P_50HZ ,
    VMODE_1080P_24HZ ,
    VMODE_4K2K_30HZ ,
    VMODE_4K2K_25HZ ,
    VMODE_4K2K_24HZ ,
    VMODE_4K2K_SMPTE,
    VMODE_1920x1200,
    VMODE_VGA,
    VMODE_SVGA,
    VMODE_XGA,
    VMODE_SXGA,
    VMODE_WSXGA,
    VMODE_FHDVGA,
    VMODE_LCD,
    VMODE_LVDS_1080P,
    VMODE_LVDS_1080P_50HZ,
    VMODE_LVDS_768P,
    VMODE_MAX,
    VMODE_INIT_NULL,
    VMODE_MASK = 0xFF,
} vmode_t;

typedef struct {
	char  		*name;
	vmode_t		mode;
	u32			width;
	u32			height;
	u32			field_height;
	u32			aspect_ratio_num;
	u32			aspect_ratio_den;
	u32			sync_duration_num;
	u32			sync_duration_den;
	u32         screen_real_width;
    u32         screen_real_height;
	u32			video_clk;
} vinfo_t;

#endif /* TVMODE_H */
