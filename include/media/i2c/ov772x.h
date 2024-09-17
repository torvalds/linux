/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ov772x Camera
 *
 * Copyright (C) 2008 Renesas Solutions Corp.
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 */

#ifndef __OV772X_H__
#define __OV772X_H__

/* for flags */
#define OV772X_FLAG_VFLIP	(1 << 0) /* Vertical flip image */
#define OV772X_FLAG_HFLIP	(1 << 1) /* Horizontal flip image */

/*
 * for Edge ctrl
 *
 * strength also control Auto or Manual Edge Control Mode
 * see also OV772X_MANUAL_EDGE_CTRL
 */
struct ov772x_edge_ctrl {
	unsigned char strength;
	unsigned char threshold;
	unsigned char upper;
	unsigned char lower;
};

#define OV772X_MANUAL_EDGE_CTRL		0x80 /* un-used bit of strength */
#define OV772X_EDGE_STRENGTH_MASK	0x1F
#define OV772X_EDGE_THRESHOLD_MASK	0x0F
#define OV772X_EDGE_UPPER_MASK		0xFF
#define OV772X_EDGE_LOWER_MASK		0xFF

#define OV772X_AUTO_EDGECTRL(u, l)	\
{					\
	.upper = (u & OV772X_EDGE_UPPER_MASK),	\
	.lower = (l & OV772X_EDGE_LOWER_MASK),	\
}

#define OV772X_MANUAL_EDGECTRL(s, t)			\
{							\
	.strength  = (s & OV772X_EDGE_STRENGTH_MASK) |	\
			OV772X_MANUAL_EDGE_CTRL,	\
	.threshold = (t & OV772X_EDGE_THRESHOLD_MASK),	\
}

/**
 * struct ov772x_camera_info -	ov772x driver interface structure
 * @flags:		Sensor configuration flags
 * @edgectrl:		Sensor edge control
 */
struct ov772x_camera_info {
	unsigned long		flags;
	struct ov772x_edge_ctrl	edgectrl;
};

#endif /* __OV772X_H__ */
