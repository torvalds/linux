/*	$OpenBSD: struct.h,v 1.6 2015/12/31 16:50:29 mestre Exp $	*/
/*	$NetBSD: struct.h,v 1.3 1995/03/21 15:04:31 cgd Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ed James.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)struct.h	8.1 (Berkeley) 5/31/93
 */

/*
 * Copyright (c) 1987 by Ed James, UC Berkeley.  All rights reserved.
 *
 * Copy permission is hereby granted provided that this notice is
 * retained on all partial or complete copies.
 *
 * For more info on this and all of my stuff, mail edjames@berkeley.edu.
 */

#include <limits.h>

typedef struct {
	int	x, y;
	int	dir;	/* used only sometimes */
} SCREEN_POS;

typedef struct {
	SCREEN_POS	p1, p2;
} LINE;

typedef SCREEN_POS	EXIT;
typedef SCREEN_POS	BEACON;
typedef SCREEN_POS	AIRPORT;

typedef struct {
	int	width, height;
	int	update_secs;
	int	newplane_time;
	int	num_exits;
	int	num_lines;
	int	num_beacons;
	int	num_airports;
	EXIT	*exit;
	LINE	*line;
	BEACON	*beacon;
	AIRPORT	*airport;
} C_SCREEN;

typedef struct plane {
	struct plane	*next, *prev;
	int		status;
	int		plane_no;
	int		plane_type;
	int		orig_no;
	int		orig_type;
	int		dest_no;
	int		dest_type;
	int		altitude;
	int		new_altitude;
	int		dir;
	int		new_dir;
	int		fuel;
	int		xpos;
	int		ypos;
	int		delayd;
	int		delayd_no;
} PLANE;

typedef struct {
	PLANE	*head, *tail;
} LIST;

typedef struct {
	char	name[LOGIN_NAME_MAX];
	char	game[256];
	int	planes;
	int	time;
	int	real_time;
} SCORE;

typedef struct displacement {
	int	dx;
	int	dy;
} DISPLACEMENT;
