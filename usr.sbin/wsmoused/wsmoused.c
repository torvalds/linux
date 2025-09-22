/* $OpenBSD: wsmoused.c,v 1.38 2021/10/24 21:24:19 deraadt Exp $ */

/*
 * Copyright (c) 2001 Jean-Baptiste Marchand, Julien Montagne and Jerome Verdon
 *
 * Copyright (c) 1998 by Kazutaka Yokota
 *
 * Copyright (c) 1995 Michael Smith
 *
 * Copyright (c) 1993 by David Dawes <dawes@xfree86.org>
 *
 * Copyright (c) 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 *
 * All rights reserved.
 *
 * Most of this code was taken from the FreeBSD moused daemon, written by
 * Michael Smith. The FreeBSD moused daemon already contained code from the
 * Xfree Project, written by David Dawes and Thomas Roell and Kazutaka Yokota.
 *
 * Adaptation to OpenBSD was done by Jean-Baptiste Marchand, Julien Montagne
 * and Jerome Verdon.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by
 *      David Dawes, Jean-Baptiste Marchand, Julien Montagne, Thomas Roell,
 *      Michael Smith, Jerome Verdon and Kazutaka Yokota.
 * 4. The name authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/tty.h>
#include <dev/wscons/wsconsio.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#include "mouse_protocols.h"
#include "wsmoused.h"

#define	DEFAULT_TTY	"/dev/ttyCcfg"

extern char *__progname;
extern char *mouse_names[];

int debug = 0;
int background = FALSE;
int nodaemon = FALSE;
int identify = FALSE;

mouse_t mouse = {
	.flags = 0,
	.portname = NULL,
	.ttyname = NULL,
	.proto = P_UNKNOWN,
	.rate = MOUSE_RATE_UNKNOWN,
	.resolution = MOUSE_RES_UNKNOWN,
	.mfd = -1,
	.clickthreshold = 500,	/* 0.5 sec */
};

/* identify the type of a wsmouse supported mouse */
void
wsmouse_identify(void)
{
	unsigned int type;

	if (mouse.mfd != -1) {
		if (ioctl(mouse.mfd, WSMOUSEIO_GTYPE, &type) == -1)
			err(1, "can't detect mouse type");

		printf("wsmouse supported mouse: ");
		switch (type) {
		case WSMOUSE_TYPE_VSXXX:
			printf("DEC serial\n");
			break;
		case WSMOUSE_TYPE_PS2:
			printf("PS/2 compatible\n");
			break;
		case WSMOUSE_TYPE_USB:
			printf("USB\n");
			break;
		case WSMOUSE_TYPE_LMS:
			printf("Logitech busmouse\n");
			break;
		case WSMOUSE_TYPE_MMS:
			printf("Microsoft InPort mouse\n");
			break;
		case WSMOUSE_TYPE_TPANEL:
			printf("Generic Touch Panel\n");
			break;
		case WSMOUSE_TYPE_NEXT:
			printf("NeXT\n");
			break;
		case WSMOUSE_TYPE_ARCHIMEDES:
			printf("Archimedes\n");
			break;
		case WSMOUSE_TYPE_ADB:
			printf("ADB\n");
			break;
		case WSMOUSE_TYPE_HIL:
			printf("HP-HIL\n");
			break;
		case WSMOUSE_TYPE_LUNA:
			printf("Omron Luna\n");
			break;
		case WSMOUSE_TYPE_DOMAIN:
			printf("Apollo Domain\n");
			break;
		case WSMOUSE_TYPE_SUN:
			printf("Sun\n");
			break;
		default:
			printf("Unknown\n");
			break;
		}
	} else
		warnx("unable to open %s", mouse.portname);
}

/* wsmouse_init : init a wsmouse compatible mouse */
void
wsmouse_init(void)
{
	unsigned int res = WSMOUSE_RES_MIN;

	ioctl(mouse.mfd, WSMOUSEIO_SRES, &res);
}

/*
 * Buttons remapping
 */

/* physical to logical button mapping */
static int p2l[MOUSE_MAXBUTTON] = {
	MOUSE_BUTTON1,	MOUSE_BUTTON2,	MOUSE_BUTTON3,	MOUSE_BUTTON4,
	MOUSE_BUTTON5,	MOUSE_BUTTON6,	MOUSE_BUTTON7,	MOUSE_BUTTON8,
};

static char *
skipspace(char *s)
{
	while (isspace((unsigned char)*s))
		++s;
	return s;
}

/* mouse_installmap : install a map between physical and logical buttons */
static int
mouse_installmap(char *arg)
{
	int pbutton;
	int lbutton;
	char *s;

	while (*arg) {
		arg = skipspace(arg);
		s = arg;
		while (isdigit((unsigned char)*arg))
			++arg;
		arg = skipspace(arg);
		if ((arg <= s) || (*arg != '='))
			return FALSE;
		lbutton = atoi(s);

		arg = skipspace(++arg);
		s = arg;
		while (isdigit((unsigned char)*arg))
			++arg;
		if (arg <= s || (!isspace((unsigned char)*arg) && *arg != '\0'))
			return FALSE;
		pbutton = atoi(s);

		if (lbutton <= 0 || lbutton > MOUSE_MAXBUTTON)
			return FALSE;
		if (pbutton <= 0 || pbutton > MOUSE_MAXBUTTON)
			return FALSE;
		p2l[pbutton - 1] = lbutton - 1;
	}
	return TRUE;
}

/* terminate signals handler */
static void
terminate(int sig)
{
	struct wscons_event event;
	unsigned int res;

	if (mouse.mfd != -1) {
		event.type = WSCONS_EVENT_WSMOUSED_OFF;
		ioctl(mouse.cfd, WSDISPLAYIO_WSMOUSED, &event);
		res = WSMOUSE_RES_DEFAULT;
		ioctl(mouse.mfd, WSMOUSEIO_SRES, &res);
		close(mouse.mfd);
		mouse.mfd = -1;
	}
	_exit(0);
}

/* buttons status (for multiple click detection) */
static struct {
	int count;		/* 0: up, 1: single click, 2: double click,... */
	struct timeval tv;	/* timestamp on the last `up' event */
} buttonstate[MOUSE_MAXBUTTON];

/*
 * handle button click
 * Note that an ioctl is sent for each button
 */
static void
mouse_click(struct wscons_event *event)
{
	struct timeval max_date;
	struct timeval now;
	struct timeval delay;
	int i; /* button number */
	
	i = event->value = p2l[event->value];

	gettimeofday(&now, NULL);
	delay.tv_sec = mouse.clickthreshold / 1000;
	delay.tv_usec = (mouse.clickthreshold % 1000) * 1000;
	timersub(&now, &delay, &max_date);

	if (event->type == WSCONS_EVENT_MOUSE_DOWN) {
		if (timercmp(&max_date, &buttonstate[i].tv, >)) {
			timerclear(&buttonstate[i].tv);
			buttonstate[i].count = 1;
		} else {
			buttonstate[i].count++;
		}
	} else {
		/* button is up */
		buttonstate[i].tv.tv_sec = now.tv_sec;
		buttonstate[i].tv.tv_usec = now.tv_usec;
	}

	/*
	 * we use the time field of wscons_event structure to put the number
	 * of multiple clicks
	 */
	if (event->type == WSCONS_EVENT_MOUSE_DOWN) {
		event->time.tv_sec = buttonstate[i].count;
		event->time.tv_nsec = 0;
	} else {
		/* button is up */
		event->time.tv_sec = 0;
		event->time.tv_nsec = 0;
	}
	ioctl(mouse.cfd, WSDISPLAYIO_WSMOUSED, event);
}

/* workaround for cursor speed on serial mice */
static void
normalize_event(struct wscons_event *event)
{
	int dx, dy;
	int two_power = 1;

/* 2: normal speed, 3: slower cursor, 1: faster cursor */
#define NORMALIZE_DIVISOR 3

	switch (event->type) {
	case WSCONS_EVENT_MOUSE_DELTA_X:
		dx = abs(event->value);
		while (dx > 2) {
			two_power++;
			dx = dx / 2;
		}
		event->value = event->value / (NORMALIZE_DIVISOR * two_power);
		break;
	case WSCONS_EVENT_MOUSE_DELTA_Y:
		two_power = 1;
		dy = abs(event->value);
		while (dy > 2) {
			two_power++;
			dy = dy / 2;
		}
		event->value = event->value / (NORMALIZE_DIVISOR * two_power);
		break;
	}
}

/* send a wscons_event to the kernel */
static void
treat_event(struct wscons_event *event)
{
	if (IS_MOTION_EVENT(event->type)) {
		ioctl(mouse.cfd, WSDISPLAYIO_WSMOUSED, event);
	} else if (IS_BUTTON_EVENT(event->type) &&
	    (uint)event->value < MOUSE_MAXBUTTON) {
		mouse_click(event);
	}
}

/* split a full mouse event into multiples wscons events */
static void
split_event(mousestatus_t *act)
{
	struct wscons_event event;
	int button, i, mask;

	if (act->dx != 0) {
		event.type = WSCONS_EVENT_MOUSE_DELTA_X;
		event.value = act->dx;
		normalize_event(&event);
		treat_event(&event);
	}
	if (act->dy != 0) {
		event.type = WSCONS_EVENT_MOUSE_DELTA_Y;
		event.value = 0 - act->dy;
		normalize_event(&event);
		treat_event(&event);
	}
	if (act->dz != 0) {
		event.type = WSCONS_EVENT_MOUSE_DELTA_Z;
		event.value = act->dz;
		treat_event(&event);
	}
	if (act->dw != 0) {
		event.type = WSCONS_EVENT_MOUSE_DELTA_W;
		event.value = act->dw;
		treat_event(&event);
	}

	/* buttons state */
	mask = act->flags & MOUSE_BUTTONS;
	if (mask == 0)
		/* no button modified */
		return;

	button = MOUSE_BUTTON1DOWN;
	for (i = 0; (i < MOUSE_MAXBUTTON) && (mask != 0); i++) {
		if (mask & 1) {
			event.type = (act->button & button) ?
			    WSCONS_EVENT_MOUSE_DOWN : WSCONS_EVENT_MOUSE_UP;
			event.value = i;
			treat_event(&event);
		}
		button <<= 1;
		mask >>= 1;
	}
}

/* main function */
static void
wsmoused(void)
{
	mousestatus_t action;
	struct wscons_event event; /* original wscons_event */
	struct pollfd pfd[1];
	int res;
	u_char b;

	/* notify kernel the start of wsmoused */
	event.type = WSCONS_EVENT_WSMOUSED_ON;
	res = ioctl(mouse.cfd, WSDISPLAYIO_WSMOUSED, &event);
	if (res != 0) {
		/* the display driver has no getchar() method */
		logerr(1, "this display driver has no support for wsmoused(8)");
	}

	bzero(&action, sizeof(action));
	bzero(&event, sizeof(event));
	bzero(&buttonstate, sizeof(buttonstate));

	pfd[0].fd = mouse.mfd;
	pfd[0].events = POLLIN;

	/* process mouse data */
	for (;;) {
		if (poll(pfd, 1, INFTIM) <= 0)
			logwarn("failed to read from mouse");

		if (mouse.proto == P_WSCONS) {
			/* wsmouse supported mouse */
			read(mouse.mfd, &event, sizeof(event));
			treat_event(&event);
		} else {
			/* serial mouse (not supported by wsmouse) */
			res = read(mouse.mfd, &b, 1);

			/* if we have a full mouse event */
			if (mouse_protocol(b, &action))
				/* split it as multiple wscons_event */
				split_event(&action);
		}
	}
}


static void
usage(void)
{
	fprintf(stderr, "usage: %s [-2dfi] [-C thresh] [-D device]"
	    " [-M N=M]\n\t[-p device] [-t type]\n", __progname);
	exit(1);
}

int
main(int argc, char **argv)
{
	unsigned int type;
	int opt;
	int i;

#define GETOPT_STRING "2dfhip:t:C:D:M:"
	while ((opt = (getopt(argc, argv, GETOPT_STRING))) != -1) {
		switch (opt) {
		case '2':
			/* on two button mice, right button pastes */
			p2l[MOUSE_BUTTON3] = MOUSE_BUTTON2;
			break;
		case 'd':
			++debug;
			break;
		case 'f':
			nodaemon = TRUE;
			break;
		case 'h':
			usage();
			break;
		case 'i':
			identify = TRUE;
			nodaemon = TRUE;
			break;
		case 'p':
			if ((mouse.portname = strdup(optarg)) == NULL)
				logerr(1, "out of memory");
			break;
		case 't':
			if (strcmp(optarg, "auto") == 0) {
				mouse.proto = P_UNKNOWN;
				mouse.flags &= ~NoPnP;
				break;
			}
			for (i = 0; mouse_names[i] != NULL; i++)
				if (strcmp(optarg,mouse_names[i]) == 0) {
					mouse.proto = i;
					mouse.flags |= NoPnP;
					break;
				}
			if (mouse_names[i] != NULL)
				break;
			warnx("no such mouse protocol `%s'", optarg);
			usage();
			break;
		case 'C':
#define MAX_CLICKTHRESHOLD 2000 /* max delay for double click */
			mouse.clickthreshold = atoi(optarg);
			if (mouse.clickthreshold < 0 ||
			    mouse.clickthreshold > MAX_CLICKTHRESHOLD) {
				warnx("invalid threshold `%s': max value is %d",
				    optarg, MAX_CLICKTHRESHOLD);
				usage();
			}
			break;
		case 'D':
			if ((mouse.ttyname = strdup(optarg)) == NULL)
				logerr(1, "out of memory");
			break;
		case 'M':
			if (!mouse_installmap(optarg)) {
				warnx("invalid mapping `%s'", optarg);
				usage();
			}
			break;
		default:
			usage();
		}
	}

	/*
	 * Use defaults if unspecified
	 */
	if (mouse.portname == NULL)
		mouse.portname = WSMOUSE_DEV;
	if (mouse.ttyname == NULL)
		mouse.ttyname = DEFAULT_TTY;

	if (identify == FALSE) {
		if ((mouse.cfd = open(mouse.ttyname, O_RDWR)) == -1)
			logerr(1, "cannot open %s", mouse.ttyname);
	}

	if ((mouse.mfd = open(mouse.portname,
	    O_RDONLY | O_NONBLOCK)) == -1)
		logerr(1, "unable to open %s", mouse.portname);

	/*
	 * Find out whether the mouse device is a wsmouse device
	 * or a serial device.
	 */
	if (ioctl(mouse.mfd, WSMOUSEIO_GTYPE, &type) != -1)
		mouse.proto = P_WSCONS;
	else {
		if (mouse_identify() == P_UNKNOWN) {
			close(mouse.mfd);
			logerr(1, "cannot determine mouse type on %s",
			    mouse.portname);
		}
	}

	if (identify == TRUE) {
		if (mouse.proto == P_WSCONS)
			wsmouse_identify();
		else
			printf("serial mouse: %s type\n",
			    mouse_name(mouse.proto));
		exit(0);
	}

	signal(SIGINT, terminate);
	signal(SIGQUIT, terminate);
	signal(SIGTERM, terminate);

	if (mouse.proto == P_WSCONS)
		wsmouse_init();
	else
		mouse_init();

	if (!nodaemon) {
		openlog(__progname, LOG_PID, LOG_DAEMON);
		if (daemon(0, 0)) {
			logerr(1, "failed to become a daemon");
		} else {
			background = TRUE;
		}
	}

	wsmoused();
	exit(0);
}
