/*	$OpenBSD: answer.c,v 1.22 2017/01/21 08:22:57 krw Exp $	*/
/*	$NetBSD: answer.c,v 1.3 1997/10/10 16:32:50 lukem Exp $	*/
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

#include <arpa/inet.h>
#include <netinet/in.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "conf.h"
#include "hunt.h"
#include "server.h"

/* Exported symbols for hosts_access(): */
int allow_severity	= LOG_INFO;
int deny_severity	= LOG_WARNING;


/* List of spawning connections: */
struct spawn		*Spawn = NULL;

static void	stplayer(PLAYER *, int);
static void	stmonitor(PLAYER *);
static IDENT *	get_ident(struct sockaddr *, int, u_long, char *, char);

void
answer_first(void)
{
	struct sockaddr		sockstruct;
	int			newsock;
	socklen_t		socklen;
	struct spawn *sp;

	/*
	 * Answer the call to hunt, turning off blocking I/O, so a slow
	 * or dead terminal won't stop the game.  All subsequent reads
	 * check how many bytes they read.
	 */
	socklen = sizeof sockstruct;
	newsock = accept4(Socket, (struct sockaddr *) &sockstruct, &socklen,
	    SOCK_NONBLOCK);
	if (newsock < 0) {
		logit(LOG_ERR, "accept");
		return;
	}

	/* Remember this spawning connection: */
	sp = calloc(1, sizeof *sp);
	if (sp == NULL) {
		logit(LOG_ERR, "calloc");
		close(newsock);
		return;
	}

	/* Keep the calling machine's source addr for ident purposes: */
	memcpy(&sp->source, &sockstruct, sizeof sp->source);
	sp->sourcelen = socklen;

	/* Warn if we lose connection info: */
	if (socklen > sizeof Spawn->source)
		logx(LOG_WARNING,
		    "struct sockaddr is not big enough! (%d > %zu)",
		    socklen, sizeof Spawn->source);

	/* Start listening to the spawning connection */
	sp->fd = newsock;
	FD_SET(sp->fd, &Fds_mask);
	if (sp->fd >= Num_fds)
		Num_fds = sp->fd + 1;

	sp->reading_msg = 0;
	sp->inlen = 0;

	/* Add to the spawning list */
	if ((sp->next = Spawn) != NULL)
		Spawn->prevnext = &sp->next;
	sp->prevnext = &Spawn;
	Spawn = sp;
}

int
answer_next(struct spawn *sp)
{
	PLAYER			*pp;
	char			*cp1, *cp2;
	u_int32_t		version;
	FILE			*conn;
	int			len;
	char			teamstr[] = "[x]";

	if (sp->reading_msg) {
		/* Receive a message from a player */
		len = read(sp->fd, sp->msg + sp->msglen,
		    sizeof sp->msg - sp->msglen);
		if (len < 0)
			goto error;
		sp->msglen += len;
		if (len && sp->msglen < sizeof sp->msg)
			return FALSE;

		teamstr[1] = sp->team;
		outyx(ALL_PLAYERS, HEIGHT, 0, "%s%s: %.*s",
			sp->name,
			sp->team == ' ' ? "": teamstr,
			sp->msglen,
			sp->msg);
		ce(ALL_PLAYERS);
		sendcom(ALL_PLAYERS, REFRESH);
		sendcom(ALL_PLAYERS, READY, 0);
		flush(ALL_PLAYERS);
		goto close_it;
	}

	/* Fill the buffer */
	len = read(sp->fd, sp->inbuf + sp->inlen,
	    sizeof sp->inbuf - sp->inlen);
	if (len <= 0)
		goto error;
	sp->inlen += len;
	if (sp->inlen < sizeof sp->inbuf)
		return FALSE;

	/* Extract values from the buffer */
	cp1 = sp->inbuf;
	memcpy(&sp->uid, cp1, sizeof (u_int32_t));
	cp1+= sizeof(u_int32_t);
	memcpy(sp->name, cp1, NAMELEN);
	cp1+= NAMELEN;
	memcpy(&sp->team, cp1, sizeof (u_int8_t));
	cp1+= sizeof(u_int8_t);
	memcpy(&sp->enter_status, cp1, sizeof (u_int32_t));
	cp1+= sizeof(u_int32_t);
	memcpy(sp->ttyname, cp1, NAMELEN);
	cp1+= NAMELEN;
	memcpy(&sp->mode, cp1, sizeof (u_int32_t));
	cp1+= sizeof(u_int32_t);

	/* Convert data from network byte order: */
	sp->uid = ntohl(sp->uid);
	sp->enter_status = ntohl(sp->enter_status);
	sp->mode = ntohl(sp->mode);

	/*
	 * Make sure the name contains only printable characters
	 * since we use control characters for cursor control
	 * between driver and player processes
	 */
	sp->name[NAMELEN] = '\0';
	for (cp1 = cp2 = sp->name; *cp1 != '\0'; cp1++)
		if (isprint((unsigned char)*cp1) || *cp1 == ' ')
			*cp2++ = *cp1;
	*cp2 = '\0';

	/* Make sure team name is valid */
	if (sp->team < '1' || sp->team > '9')
		sp->team = ' ';

	/* Tell the other end this server's hunt driver version: */
	version = htonl((u_int32_t) HUNT_VERSION);
	(void) write(sp->fd, &version, sizeof version);

	if (sp->mode == C_MESSAGE) {
		/* The clients only wants to send a message: */
		sp->msglen = 0;
		sp->reading_msg = 1;
		return FALSE;
	}

	/* Use a stdio file descriptor from now on: */
	conn = fdopen(sp->fd, "w");

	/* The player is a monitor: */
	if (sp->mode == C_MONITOR) {
		if (conf_monitor && End_monitor < &Monitor[MAXMON]) {
			pp = End_monitor++;
			if (sp->team == ' ')
				sp->team = '*';
		} else {
			/* Too many monitors */
			fprintf(conn, "Too many monitors\n");
			fflush(conn);
			logx(LOG_NOTICE, "too many monitors");
			goto close_it;
		}

	/* The player is a normal hunter: */
	} else {
		if (End_player < &Player[MAXPL])
			pp = End_player++;
		else {
			fprintf(conn, "Too many players\n");
			fflush(conn);
			/* Too many players */
			logx(LOG_NOTICE, "too many players");
			goto close_it;
		}
	}

	/* Find the player's running scorecard */
	pp->p_ident = get_ident(&sp->source, sp->sourcelen, sp->uid,
	    sp->name, sp->team);
	pp->p_output = conn;
	pp->p_death[0] = '\0';
	pp->p_fd = sp->fd;

	/* No idea where the player starts: */
	pp->p_y = 0;
	pp->p_x = 0;

	/* Mode-specific initialisation: */
	if (sp->mode == C_MONITOR)
		stmonitor(pp);
	else
		stplayer(pp, sp->enter_status);

	/* And, they're off! Caller should remove and free sp. */
	return TRUE;

error:
	if (len < 0)
		logit(LOG_WARNING, "read");
	else
		logx(LOG_WARNING, "lost connection to new client");

close_it:
	/* Destroy the spawn */
	*sp->prevnext = sp->next;
	if (sp->next) sp->next->prevnext = sp->prevnext;
	FD_CLR(sp->fd, &Fds_mask);
	close(sp->fd);
	free(sp);
	return FALSE;
}

/* Start a monitor: */
static void
stmonitor(PLAYER *pp)
{

	/* Monitors get to see the entire maze: */
	memcpy(pp->p_maze, Maze, sizeof pp->p_maze);
	drawmaze(pp);

	/* Put the monitor's name near the bottom right on all screens: */
	outyx(ALL_PLAYERS,
		STAT_MON_ROW + 1 + (pp - Monitor), STAT_NAME_COL,
		"%5.5s%c%-10.10s %c", " ",
		stat_char(pp), pp->p_ident->i_name, pp->p_ident->i_team);

	/* Ready the monitor: */
	sendcom(pp, REFRESH);
	sendcom(pp, READY, 0);
	flush(pp);
}

/* Start a player: */
static void
stplayer(PLAYER *newpp, int enter_status)
{
	int	x, y;
	PLAYER	*pp;
	int len;

	Nplayer++;

	for (y = 0; y < UBOUND; y++)
		for (x = 0; x < WIDTH; x++)
			newpp->p_maze[y][x] = Maze[y][x];
	for (     ; y < DBOUND; y++) {
		for (x = 0; x < LBOUND; x++)
			newpp->p_maze[y][x] = Maze[y][x];
		for (     ; x < RBOUND; x++)
			newpp->p_maze[y][x] = SPACE;
		for (     ; x < WIDTH;  x++)
			newpp->p_maze[y][x] = Maze[y][x];
	}
	for (     ; y < HEIGHT; y++)
		for (x = 0; x < WIDTH; x++)
			newpp->p_maze[y][x] = Maze[y][x];

	/* Drop the new player somewhere in the maze: */
	do {
		x = rand_num(WIDTH - 1) + 1;
		y = rand_num(HEIGHT - 1) + 1;
	} while (Maze[y][x] != SPACE);
	newpp->p_over = SPACE;
	newpp->p_x = x;
	newpp->p_y = y;
	newpp->p_undershot = FALSE;

	/* Send them flying if needed */
	if (enter_status == Q_FLY && conf_fly) {
		newpp->p_flying = rand_num(conf_flytime);
		newpp->p_flyx = 2 * rand_num(conf_flystep + 1) - conf_flystep;
		newpp->p_flyy = 2 * rand_num(conf_flystep + 1) - conf_flystep;
		newpp->p_face = FLYER;
	} else {
		newpp->p_flying = -1;
		newpp->p_face = rand_dir();
	}

	/* Initialize the new player's attributes: */
	newpp->p_damage = 0;
	newpp->p_damcap = conf_maxdam;
	newpp->p_nchar = 0;
	newpp->p_ncount = 0;
	newpp->p_nexec = 0;
	newpp->p_ammo = conf_ishots;
	newpp->p_nboots = 0;

	/* Decide on what cloak/scan status to enter with */
	if (enter_status == Q_SCAN && conf_scan) {
		newpp->p_scan = conf_scanlen * Nplayer;
		newpp->p_cloak = 0;
	} else if (conf_cloak) {
		newpp->p_scan = 0;
		newpp->p_cloak = conf_cloaklen;
	} else {
		newpp->p_scan = 0;
		newpp->p_cloak = 0;
	}
	newpp->p_ncshot = 0;

	/*
	 * For each new player, place a large mine and
	 * a small mine somewhere in the maze:
	 */
	do {
		x = rand_num(WIDTH - 1) + 1;
		y = rand_num(HEIGHT - 1) + 1;
	} while (Maze[y][x] != SPACE);
	Maze[y][x] = GMINE;
	for (pp = Monitor; pp < End_monitor; pp++)
		check(pp, y, x);

	do {
		x = rand_num(WIDTH - 1) + 1;
		y = rand_num(HEIGHT - 1) + 1;
	} while (Maze[y][x] != SPACE);
	Maze[y][x] = MINE;
	for (pp = Monitor; pp < End_monitor; pp++)
		check(pp, y, x);

	/* Create a score line for the new player: */
	(void) snprintf(Buf, sizeof Buf, "%5.2f%c%-10.10s %c",
		newpp->p_ident->i_score, stat_char(newpp),
		newpp->p_ident->i_name, newpp->p_ident->i_team);
	len = strlen(Buf);
	y = STAT_PLAY_ROW + 1 + (newpp - Player);
	for (pp = Player; pp < End_player; pp++) {
		if (pp != newpp) {
			/* Give everyone a few more shots: */
			pp->p_ammo += conf_nshots;
			newpp->p_ammo += conf_nshots;
			outyx(pp, y, STAT_NAME_COL, Buf, len);
			ammo_update(pp);
		}
	}
	for (pp = Monitor; pp < End_monitor; pp++)
		outyx(pp, y, STAT_NAME_COL, Buf, len);

	/* Show the new player what they can see and where they are: */
	drawmaze(newpp);
	drawplayer(newpp, TRUE);
	look(newpp);

	/* Make sure that the position they enter in will be erased: */
	if (enter_status == Q_FLY && conf_fly)
		showexpl(newpp->p_y, newpp->p_x, FLYER);

	/* Ready the new player: */
	sendcom(newpp, REFRESH);
	sendcom(newpp, READY, 0);
	flush(newpp);
}

/*
 * rand_dir:
 *	Return a random direction
 */
int
rand_dir(void)
{
	switch (rand_num(4)) {
	  case 0:
		return LEFTS;
	  case 1:
		return RIGHT;
	  case 2:
		return BELOW;
	  case 3:
		return ABOVE;
	}
	return(-1);
}

/*
 * get_ident:
 *	Get the score structure of a player
 */
static IDENT *
get_ident(struct sockaddr *sa, int salen, u_long uid, char *name, char team)
{
	IDENT		*ip;
	static IDENT	punt;
	u_int32_t	machine;

	if (sa->sa_family == AF_INET)
		machine = ntohl((u_long)((struct sockaddr_in *)sa)->sin_addr.s_addr);
	else
		machine = 0;

	for (ip = Scores; ip != NULL; ip = ip->i_next)
		if (ip->i_machine == machine
		&&  ip->i_uid == uid
		/* &&  ip->i_team == team */
		&&  strncmp(ip->i_name, name, NAMELEN) == 0)
			break;

	if (ip != NULL) {
		if (ip->i_team != team) {
			logx(LOG_INFO, "player %s %s team %c",
				name,
				team == ' ' ? "left" : ip->i_team == ' ' ?
					"joined" : "changed to",
				team == ' ' ? ip->i_team : team);
			ip->i_team = team;
		}
		if (ip->i_entries < conf_scoredecay)
			ip->i_entries++;
		else
			ip->i_kills = (ip->i_kills * (conf_scoredecay - 1))
				/ conf_scoredecay;
		ip->i_score = ip->i_kills / (double) ip->i_entries;
	}
	else {
		/* Alloc new entry -- it is released in clear_scores() */
		ip = malloc(sizeof (IDENT));
		if (ip == NULL) {
			logit(LOG_ERR, "malloc");
			/* Fourth down, time to punt */
			ip = &punt;
		}
		ip->i_machine = machine;
		ip->i_team = team;
		ip->i_uid = uid;
		strlcpy(ip->i_name, name, sizeof ip->i_name);
		ip->i_kills = 0;
		ip->i_entries = 1;
		ip->i_score = 0;
		ip->i_absorbed = 0;
		ip->i_faced = 0;
		ip->i_shot = 0;
		ip->i_robbed = 0;
		ip->i_slime = 0;
		ip->i_missed = 0;
		ip->i_ducked = 0;
		ip->i_gkills = ip->i_bkills = ip->i_deaths = 0;
		ip->i_stillb = ip->i_saved = 0;
		ip->i_next = Scores;
		Scores = ip;

		logx(LOG_INFO, "new player: %s%s%c%s",
			name,
			team == ' ' ? "" : " (team ",
			team,
			team == ' ' ? "" : ")");
	}

	return ip;
}

void
answer_info(FILE *fp)
{
	struct spawn *sp;
	char buf[128];
	const char *bf;
	struct sockaddr_in *sa;

	if (Spawn == NULL)
		return;
	fprintf(fp, "\nSpawning connections:\n");
	for (sp = Spawn; sp; sp = sp->next) {
		sa = (struct sockaddr_in *)&sp->source;
		bf = inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof buf);
		if (!bf)  {
			logit(LOG_WARNING, "inet_ntop");
			bf = "?";
		}
		fprintf(fp, "fd %d: state %d, from %s:%d\n",
			sp->fd, sp->inlen + (sp->reading_msg ? sp->msglen : 0),
			bf, sa->sin_port);
	}
}
