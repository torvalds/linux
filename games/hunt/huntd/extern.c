/*	$OpenBSD: extern.c,v 1.7 2017/01/21 08:22:57 krw Exp $	*/
/*	$NetBSD: extern.c,v 1.2 1997/10/10 16:33:24 lukem Exp $	*/
/*
 * Copyright (c) 1983-2003, Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * + Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * + Neither the name of the University of California, San Francisco nor
 *   the names of its contributors may be used to endorse or promote
 *   products derived from this software without specific prior written
 *   permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/select.h>
#include "hunt.h"
#include "server.h"

FLAG	Am_monitor = FALSE;		/* current process is a monitor */

char	Buf[BUFSIZ];			/* general scribbling buffer */
char	Maze[HEIGHT][WIDTH2];		/* the maze */
char	Orig_maze[HEIGHT][WIDTH2];	/* the original maze */

fd_set	Fds_mask;			/* mask for the file descriptors */
fd_set	Have_inp;			/* which file descriptors have input */
int	Nplayer = 0;			/* number of players */
int	Num_fds;			/* number of maximum file descriptor */
int	Socket;				/* main socket */
int	Status;				/* stat socket */
int	See_over[NASCII];		/* lookup table for determining whether
					 * character represents "transparent"
					 * item */

BULLET	*Bullets = NULL;		/* linked list of bullets */

EXPL	*Expl[EXPLEN];			/* explosion lists */
EXPL	*Last_expl;			/* last explosion on Expl[0] */

PLAYER	Player[MAXPL];			/* all the players */
PLAYER	*End_player = Player;		/* last active player slot */
PLAYER	Boot[NBOOTS];			/* all the boots */
IDENT	*Scores;			/* score cache */
PLAYER	Monitor[MAXMON];		/* all the monitors */
PLAYER	*End_monitor = Monitor;		/* last active monitor slot */

int	volcano = 0;			/* Explosion size */

int	shot_req[MAXBOMB]	= {
				BULREQ, GRENREQ, SATREQ,
				BOMB7REQ, BOMB9REQ, BOMB11REQ,
				BOMB13REQ, BOMB15REQ, BOMB17REQ,
				BOMB19REQ, BOMB21REQ,
			};
int	shot_type[MAXBOMB]	= {
				SHOT, GRENADE, SATCHEL,
				BOMB, BOMB, BOMB,
				BOMB, BOMB, BOMB,
				BOMB, BOMB,
			};

int	slime_req[MAXSLIME]	= {
				SLIMEREQ, SSLIMEREQ, SLIME2REQ, SLIME3REQ,
			};
