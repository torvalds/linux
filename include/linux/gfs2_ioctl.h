/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#ifndef __GFS2_IOCTL_DOT_H__
#define __GFS2_IOCTL_DOT_H__

#define _GFS2C_(x)               (('G' << 16) | ('2' << 8) | (x))

/* Ioctls implemented */

#define GFS2_IOCTL_SETFLAGS      _GFS2C_(3)
#define GFS2_IOCTL_GETFLAGS      _GFS2C_(4)

#endif /* ___GFS2_IOCTL_DOT_H__ */

