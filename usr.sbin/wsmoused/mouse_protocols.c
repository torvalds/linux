/* $OpenBSD: mouse_protocols.c,v 1.19 2022/12/28 21:30:19 jmc Exp $ */

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

/* Support for non-wsmouse (i.e. serial mice) mice */

/*
 * Most of this code comes from the Xfree Project and are derived from two files
 * of Xfree86 3.3.6 with the following CVS tags :
 $XFree86: xc/programs/Xserver/hw/xfree86/common/xf86_Mouse.c,v 3.21.2.24
 1999/12/11 19:00:42 hohndel Exp $
 and
 $XFree86: xc/programs/Xserver/hw/xfree86/common/xf86_PnPMouse.c,v 1.1.2.6
 1999/07/29 09:22:51 hohndel Exp $
 */

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/tty.h>

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

#include "wsmoused.h"
#include "mouse_protocols.h"

extern int	debug;
extern int	nodaemon;
extern int	background;
extern mouse_t	mouse;

/* Cflags of each mouse protocol, ordered by P_XXX */
static const unsigned short mousecflags[] = {
	(CS7 | CREAD | CLOCAL | HUPCL),	/* Microsoft */
	(CS8 | CSTOPB | CREAD | CLOCAL | HUPCL),	/* MouseSystems */
	(CS8 | CSTOPB | CREAD | CLOCAL | HUPCL),	/* Logitech */
	(CS8 | PARENB | PARODD | CREAD | CLOCAL | HUPCL),	/* MMSeries */
	(CS7 | CREAD | CLOCAL | HUPCL),	/* MouseMan */
	(CS8 | CREAD | CLOCAL | HUPCL),	/* MM HitTablet */
	(CS7 | CREAD | CLOCAL | HUPCL),	/* GlidePoint */
	(CS7 | CREAD | CLOCAL | HUPCL),	/* IntelliMouse */
	(CS7 | CREAD | CLOCAL | HUPCL),	/* Thinking Mouse */
};

/* array ordered by P_XXX giving protocol properties */
static const unsigned char proto[][7] = {
	/* mask hd_id   dp_mask dp_id   bytes b4_mask b4_id */
	{0x40, 0x40, 0x40, 0x00, 3, ~0x23, 0x00},	/* Microsoft */
	{0xf8, 0x80, 0x00, 0x00, 5, 0x00, 0xff},	/* MouseSystems */
	{0xe0, 0x80, 0x80, 0x00, 3, 0x00, 0xff},	/* Logitech */
	{0xe0, 0x80, 0x80, 0x00, 3, 0x00, 0xff},	/* MMSeries */
	{0x40, 0x40, 0x40, 0x00, 3, ~0x33, 0x00},	/* MouseMan */
	{0xe0, 0x80, 0x80, 0x00, 3, 0x00, 0xff},	/* MM HitTablet */
	{0x40, 0x40, 0x40, 0x00, 3, ~0x33, 0x00},	/* GlidePoint */
	{0x40, 0x40, 0x40, 0x00, 3, ~0x3f, 0x00},	/* IntelliMouse */
	{0x40, 0x40, 0x40, 0x00, 3, ~0x33, 0x00},	/* ThinkingMouse */
};

/*
 * array ordered by P_XXX (mouse protocols) giving the protocol
 * corresponding to the name of a mouse
 */
const char *mouse_names[] = {
	"microsoft",
	"mousesystems",
	"logitech",
	"mmseries",
	"mouseman",
	"mmhitab",
	"glidepoint",
	"intellimouse",
	"thinkingmouse",
	NULL
};

/* protocol currently used */
static unsigned char cur_proto[7];

/* PnP EISA/product IDs */
static const symtab_t pnpprod[] = {
	{"KML0001", P_THINKING},/* Kensignton ThinkingMouse */
	{"MSH0001", P_IMSERIAL},/* MS IntelliMouse */
	{"MSH0004", P_IMSERIAL},/* MS IntelliMouse TrackBall */
	{"KYEEZ00", P_MS},	/* Genius EZScroll */
	{"KYE0001", P_MS},	/* Genius PnP Mouse */
	{"KYE0003", P_IMSERIAL},/* Genius NetMouse */
	{"LGI800C", P_IMSERIAL},/* Logitech MouseMan (4 button model) */
	{"LGI8050", P_IMSERIAL},/* Logitech MouseMan+ */
	{"LGI8051", P_IMSERIAL},/* Logitech FirstMouse+ */
	{"LGI8001", P_LOGIMAN},	/* Logitech serial */
	{"PNP0F01", P_MS},	/* MS serial */
	/*
	 * XXX EzScroll returns PNP0F04 in the compatible device field; but it
	 * doesn't look compatible...
	 */
	{"PNP0F04", P_MSC},	/* MouseSystems */
	{"PNP0F05", P_MSC},	/* MouseSystems */
	{"PNP0F08", P_LOGIMAN},	/* Logitech serial */
	{"PNP0F09", P_MS},	/* MS BallPoint serial */
	{"PNP0F0A", P_MS},	/* MS PnP serial */
	{"PNP0F0B", P_MS},	/* MS PnP BallPoint serial */
	{"PNP0F0C", P_MS},	/* MS serial comatible */
	{"PNP0F0F", P_MS},	/* MS BallPoint comatible */
	{"PNP0F17", P_LOGIMAN},	/* Logitech serial compat */
	{NULL, -1},
};

static const symtab_t *
gettoken(const symtab_t * tab, char *s, int len)
{
	int	i;

	for (i = 0; tab[i].name != NULL; ++i) {
		if (strncmp(tab[i].name, s, len) == 0)
			break;
	}
	return &tab[i];
}

const char *
mouse_name(int type)
{
	return (type < 0 ||
	    (uint)type >= sizeof(mouse_names) / sizeof(mouse_names[0])) ?
	    "unknown" : mouse_names[type];
}

void
SetMouseSpeed(int old, unsigned int cflag)
{
	struct termios tty;
	char	*c;

	if (tcgetattr(mouse.mfd, &tty) == -1) {
		debug("Warning: %s unable to get status of mouse fd (%s)\n",
		    mouse.portname, strerror(errno));
		return;
	}
	tty.c_iflag = IGNBRK | IGNPAR;
	tty.c_oflag = 0;
	tty.c_lflag = 0;
	tty.c_cflag = (tcflag_t) cflag;
	tty.c_cc[VTIME] = 0;
	tty.c_cc[VMIN] = 1;

	switch (old) {
	case 9600:
		cfsetispeed(&tty, B9600);
		cfsetospeed(&tty, B9600);
		break;
	case 4800:
		cfsetispeed(&tty, B4800);
		cfsetospeed(&tty, B4800);
		break;
	case 2400:
		cfsetispeed(&tty, B2400);
		cfsetospeed(&tty, B2400);
		break;
	case 1200:
	default:
		cfsetispeed(&tty, B1200);
		cfsetospeed(&tty, B1200);
	}

	if (tcsetattr(mouse.mfd, TCSADRAIN, &tty) == -1)
		logerr(1, "unable to get mouse status. Exiting...\n");

	c = "*n";
	cfsetispeed(&tty, B1200);
	cfsetospeed(&tty, B1200);

	if (mouse.proto == P_LOGIMAN || mouse.proto == P_LOGI) {
		if (write(mouse.mfd, c, 2) != 2)
			logerr(1, "unable to write to mouse. Exiting...\n");
	}
	usleep(100000);

	if (tcsetattr(mouse.mfd, TCSADRAIN, &tty) == -1)
		logerr(1, "unable to get mouse status. Exiting...\n");
}

int
FlushInput(int fd)
{
	struct pollfd pfd[1];
	char	c[4];

	if (tcflush(fd, TCIFLUSH) == 0)
		return 0;

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;

	while (poll(pfd, 1, 0) > 0)
		read(fd, &c, sizeof(c));
	return 0;
}

/*
 * Try to elicit a PnP ID as described in
 * Microsoft, Hayes: "Plug and Play External COM Device Specification,
 * rev 1.00", 1995.
 *
 * The routine does not fully implement the COM Enumerator as par Section
 * 2.1 of the document.  In particular, we don't have idle state in which
 * the driver software monitors the com port for dynamic connection or
 * removal of a device at the port, because `moused' simply quits if no
 * device is found.
 *
 * In addition, as PnP COM device enumeration procedure slightly has
 * changed since its first publication, devices which follow earlier
 * revisions of the above spec. may fail to respond if the rev 1.0
 * procedure is used. XXX
 */
static int
pnpgets(int mouse_fd, char *buf)
{
	struct pollfd   pfd[1];
	int	i;
	char	c;

	pfd[0].fd = mouse_fd;
	pfd[0].events = POLLIN;

#if 0
	/*
	 * This is the procedure described in rev 1.0 of PnP COM device spec.
	 * Unfortunately, some devices which comform to earlier revisions of
	 * the spec gets confused and do not return the ID string...
	 */

	/* port initialization (2.1.2) */
	ioctl(mouse_fd, TIOCMGET, &i);
	i |= TIOCM_DTR;		/* DTR = 1 */
	i &= ~TIOCM_RTS;	/* RTS = 0 */
	ioctl(mouse_fd, TIOCMSET, &i);
	usleep(200000);
	if ((ioctl(mouse_fd, TIOCMGET, &i) == -1) || ((i & TIOCM_DSR) == 0))
		goto disconnect_idle;

	/* port setup, 1st phase (2.1.3) */
	SetMouseSpeed(1200, (CS7 | CREAD | CLOCAL | HUPCL));
	i = TIOCM_DTR | TIOCM_RTS;	/* DTR = 0, RTS = 0 */
	ioctl(mouse_fd, TIOCMBIC, &i);
	usleep(200000);
	i = TIOCM_DTR;		/* DTR = 1, RTS = 0 */
	ioctl(mouse_fd, TIOCMBIS, &i);
	usleep(200000);

	/* wait for response, 1st phase (2.1.4) */
	FlushInput(mouse_fd);
	i = TIOCM_RTS;		/* DTR = 1, RTS = 1 */
	ioctl(mouse_fd, TIOCMBIS, &i);

	/* try to read something */
	if (poll(pfd, 1, 200000 / 1000) <= 0) {
		/* port setup, 2nd phase (2.1.5) */
		i = TIOCM_DTR | TIOCM_RTS;	/* DTR = 0, RTS = 0 */
		ioctl(mouse_fd, TIOCMBIC, &i);
		usleep(200000);

		/* wait for response, 2nd phase (2.1.6) */
		FlushInput(mouse_fd);
		i = TIOCM_DTR | TIOCM_RTS;	/* DTR = 1, RTS = 1 */
		ioctl(mouse_fd, TIOCMBIS, &i);

		/* try to read something */
		if (poll(pfd, 1, 200000 / 1000) <= 0)
			goto connect_idle;
	}
#else

	/*
	 * This is a simplified procedure; it simply toggles RTS.
	 */
	SetMouseSpeed(1200, (CS7 | CREAD | CLOCAL | HUPCL));

	ioctl(mouse_fd, TIOCMGET, &i);
	i |= TIOCM_DTR;		/* DTR = 1 */
	i &= ~TIOCM_RTS;	/* RTS = 0 */
	ioctl(mouse_fd, TIOCMSET, &i);
	usleep(200000);

	/* wait for response */
	FlushInput(mouse_fd);
	i = TIOCM_DTR | TIOCM_RTS;	/* DTR = 1, RTS = 1 */
	ioctl(mouse_fd, TIOCMBIS, &i);

	/* try to read something */
	if (poll(pfd, 1, 200000 / 1000) <= 0)
		goto connect_idle;
#endif

	/* collect PnP COM device ID (2.1.7) */
	i = 0;
	usleep(200000);		/* the mouse must send `Begin ID' within
				 * 200msec */
	while (read(mouse_fd, &c, 1) == 1) {
		/* we may see "M", or "M3..." before `Begin ID' */
		if ((c == 0x08) || (c == 0x28)) {	/* Begin ID */
			buf[i++] = c;
			break;
		}
	}
	if (i <= 0) {
		/* we haven't seen `Begin ID' in time... */
		goto connect_idle;
	}
	++c;			/* make it `End ID' */
	for (;;) {
		if (poll(pfd, 1, 200000 / 1000) <= 0)
			break;

		read(mouse_fd, &buf[i], 1);
		if (buf[i++] == c)	/* End ID */
			break;
		if (i >= 256)
			break;
	}
	if (buf[i - 1] != c)
		goto connect_idle;
	return i;

#if 0
	/*
	 * According to PnP spec, we should set DTR = 1 and RTS = 0 while
	 * in idle state.  But, `moused' shall set DTR = RTS = 1 and proceed,
	 * assuming there is something at the port even if it didn't
	 * respond to the PnP enumeration procedure.
	 */
disconnect_idle:
	i = TIOCM_DTR | TIOCM_RTS;	/* DTR = 1, RTS = 1 */
	ioctl(mouse_fd, TIOCMBIS, &i);
#endif

connect_idle:
	return 0;
}

/* pnpparse : parse a PnP string ID */
static int
pnpparse(pnpid_t * id, char *buf, int len)
{
	char	s[3];
	int	offset, sum = 0, i, j;

	id->revision = 0;
	id->eisaid = NULL;
	id->serial = NULL;
	id->class = NULL;
	id->compat = NULL;
	id->description = NULL;
	id->neisaid = 0;
	id->nserial = 0;
	id->nclass = 0;
	id->ncompat = 0;
	id->ndescription = 0;

	offset = 0x28 - buf[0];

	/* calculate checksum */
	for (i = 0; i < len - 3; ++i) {
		sum += buf[i];
		buf[i] += offset;
	}
	sum += buf[len - 1];
	for (; i < len; ++i)
		buf[i] += offset;
	debug("Mouse: PnP ID string: '%*.*s'\n", len, len, buf);

	/* revision */
	buf[1] -= offset;
	buf[2] -= offset;
	id->revision = ((buf[1] & 0x3f) << 6) | (buf[2] & 0x3f);
	debug("Mouse: PnP rev %d.%02d\n", id->revision / 100, id->revision % 100);

	/* EISA vendor and product ID */
	id->eisaid = &buf[3];
	id->neisaid = 7;

	/* option strings */
	i = 10;
	if (buf[i] == '\\') {
		/* device serial # */
		for (j = ++i; i < len; ++i) {
			if (buf[i] == '\\')
				break;
		}
		if (i >= len)
			i -= 3;
		if (i - j == 8) {
			id->serial = &buf[j];
			id->nserial = 8;
		}
	}
	if (buf[i] == '\\') {
		/* PnP class */
		for (j = ++i; i < len; ++i) {
			if (buf[i] == '\\')
				break;
		}
		if (i >= len)
			i -= 3;
		if (i > j + 1) {
			id->class = &buf[j];
			id->nclass = i - j;
		}
	}
	if (buf[i] == '\\') {
		/* compatible driver */
		for (j = ++i; i < len; ++i) {
			if (buf[i] == '\\')
				break;
		}
		/*
		 * PnP COM spec prior to v0.96 allowed '*' in this field,
		 * it's not allowed now; just ignore it.
		 */
		if (buf[j] == '*')
			++j;
		if (i >= len)
			i -= 3;
		if (i > j + 1) {
			id->compat = &buf[j];
			id->ncompat = i - j;
		}
	}
	if (buf[i] == '\\') {
		/* product description */
		for (j = ++i; i < len; ++i) {
			if (buf[i] == ';')
				break;
		}
		if (i >= len)
			i -= 3;
		if (i > j + 1) {
			id->description = &buf[j];
			id->ndescription = i - j;
		}
	}
	/* checksum exists if there are any optional fields */
	if ((id->nserial > 0) || (id->nclass > 0) ||
	    (id->ncompat > 0) || (id->ndescription > 0)) {
#if 0
		debug("Mouse: PnP checksum: 0x%02X\n", sum);
#endif
		snprintf(s, sizeof s, "%02X", sum & 0x0ff);
		if (strncmp(s, &buf[len - 3], 2) != 0) {
#if 0
			/*
			 * Checksum error!!
			 * I found some mice do not comply with the PnP COM device
			 * spec regarding checksum... XXX
			 */
			return FALSE;
#endif
		}
	}
	return 1;
}

/* pnpproto : return the prototype used, based on the PnP ID string */
static const symtab_t *
pnpproto(pnpid_t * id)
{
	const symtab_t *t;
	int i, j;

	if (id->nclass > 0)
		if (strncmp(id->class, "MOUSE", id->nclass) != 0)
			/* this is not a mouse! */
			return NULL;

	if (id->neisaid > 0) {
		t = gettoken(pnpprod, id->eisaid, id->neisaid);
		if (t->val != -1)
			return t;
	}
	/*
	 * The 'Compatible drivers' field may contain more than one
	 * ID separated by ','.
	 */
	if (id->ncompat <= 0)
		return NULL;
	for (i = 0; i < id->ncompat; ++i) {
		for (j = i; id->compat[i] != ','; ++i)
			if (i >= id->ncompat)
				break;
		if (i > j) {
			t = gettoken(pnpprod, id->compat + j, i - j);
			if (t->val != -1)
				return t;
		}
	}

	return NULL;
}

/* mouse_init : init the mouse by writing appropriate sequences */
void
mouse_init(void)
{
	struct pollfd   pfd[1];
	char           *s;
	char            c;
	int             i;

	pfd[0].fd = mouse.mfd;
	pfd[0].events = POLLIN;

	/**
	 ** This comment is a little out of context here, but it contains
	 ** some useful information...
	 ********************************************************************
	 **
	 ** The following lines take care of the Logitech MouseMan protocols.
	 **
	 ** NOTE: There are different versions of both MouseMan and TrackMan!
	 **       Hence I add another protocol P_LOGIMAN, which the user can
	 **       specify as MouseMan in his XF86Config file. This entry was
	 **       formerly handled as a special case of P_MS. However, people
	 **       who don't have the middle button problem, can still specify
	 **       Microsoft and use P_MS.
	 **
	 ** By default, these mice should use a 3 byte Microsoft protocol
	 ** plus a 4th byte for the middle button. However, the mouse might
	 ** have switched to a different protocol before we use it, so I send
	 ** the proper sequence just in case.
	 **
	 ** NOTE: - all commands to (at least the European) MouseMan have to
	 **	 be sent at 1200 Baud.
	 **       - each command starts with a '*'.
	 **       - whenever the MouseMan receives a '*', it will switch back
	 **	 to 1200 Baud. Hence I have to select the desired protocol
	 **	 first, then select the baud rate.
	 **
	 ** The protocols supported by the (European) MouseMan are:
	 **   -  5 byte packed binary protocol, as with the Mouse Systems
	 **      mouse. Selected by sequence "*U".
	 **   -  2 button 3 byte Microsoft compatible protocol. Selected
	 **      by sequence "*V".
	 **   -  3 button 3+1 byte Microsoft compatible protocol (default).
	 **      Selected by sequence "*X".
	 **
	 ** The following baud rates are supported:
	 **   -  1200 Baud (default). Selected by sequence "*n".
	 **   -  9600 Baud. Selected by sequence "*q".
	 **
	 ** Selecting a sample rate is no longer supported with the MouseMan!
	 ** Some additional lines in xf86Config.c take care of ill configured
	 ** baud rates and sample rates. (The user will get an error.)
	 */

	switch (mouse.proto) {

	case P_LOGI:
		/*
		 * The baud rate selection command must be sent at the current
		 * baud rate; try all likely settings
		 */
		SetMouseSpeed(9600, mousecflags[mouse.proto]);
		SetMouseSpeed(4800, mousecflags[mouse.proto]);
		SetMouseSpeed(2400, mousecflags[mouse.proto]);
#if 0
		SetMouseSpeed(1200, mousecflags[mouse.proto]);
#endif
		/* select MM series data format */
		write(mouse.mfd, "S", 1);
		SetMouseSpeed(1200, mousecflags[P_MM]);
		/* select report rate/frequency */
		if (mouse.rate <= 0)
			write(mouse.mfd, "O", 1);
		else if (mouse.rate <= 15)
			write(mouse.mfd, "J", 1);
		else if (mouse.rate <= 27)
			write(mouse.mfd, "K", 1);
		else if (mouse.rate <= 42)
			write(mouse.mfd, "L", 1);
		else if (mouse.rate <= 60)
			write(mouse.mfd, "R", 1);
		else if (mouse.rate <= 85)
			write(mouse.mfd, "M", 1);
		else if (mouse.rate <= 125)
			write(mouse.mfd, "Q", 1);
		else
			write(mouse.mfd, "N", 1);
		break;

	case P_LOGIMAN:
		/* The command must always be sent at 1200 baud */
		SetMouseSpeed(1200, mousecflags[mouse.proto]);
		write(mouse.mfd, "*X", 2);
		SetMouseSpeed(1200, mousecflags[mouse.proto]);
		break;

	case P_MMHIT:
		SetMouseSpeed(1200, mousecflags[mouse.proto]);

		/*
		 * Initialize Hitachi PUMA Plus - Model 1212E to desired settings.
		 * The tablet must be configured to be in MM mode, NO parity,
		 * Binary Format.  xf86Info.sampleRate controls the sensativity
		 * of the tablet.  We only use this tablet for its 4-button puck
		 * so we don't run in "Absolute Mode"
		 */
		write(mouse.mfd, "z8", 2);	/* Set Parity = "NONE" */
		usleep(50000);
		write(mouse.mfd, "zb", 2);	/* Set Format = "Binary" */
		usleep(50000);
		write(mouse.mfd, "@", 1);	/* Set Report Mode = "Stream" */
		usleep(50000);
		write(mouse.mfd, "R", 1);	/* Set Output Rate = "45 rps" */
		usleep(50000);
		write(mouse.mfd, "I\x20", 2);	/* Set Incrememtal Mode "20" */
		usleep(50000);
		write(mouse.mfd, "E", 1);	/* Set Data Type = "Relative */
		usleep(50000);

		/* Resolution is in 'lines per inch' on the Hitachi tablet */
		if (mouse.resolution == MOUSE_RES_LOW)
			c = 'g';
		else if (mouse.resolution == MOUSE_RES_MEDIUMLOW)
			c = 'e';
		else if (mouse.resolution == MOUSE_RES_MEDIUMHIGH)
			c = 'h';
		else if (mouse.resolution == MOUSE_RES_HIGH)
			c = 'd';
		else if (mouse.resolution <= 40)
			c = 'g';
		else if (mouse.resolution <= 100)
			c = 'd';
		else if (mouse.resolution <= 200)
			c = 'e';
		else if (mouse.resolution <= 500)
			c = 'h';
		else if (mouse.resolution <= 1000)
			c = 'j';
		else
			c = 'd';
		write(mouse.mfd, &c, 1);
		usleep(50000);

		write(mouse.mfd, "\021", 1);	/* Resume DATA output */
		break;

	case P_THINKING:
		SetMouseSpeed(1200, mousecflags[mouse.proto]);
		/* the PnP ID string may be sent again, discard it */
		usleep(200000);
		i = FREAD;
		ioctl(mouse.mfd, TIOCFLUSH, &i);
		/* send the command to initialize the beast */
		for (s = "E5E5"; *s; ++s) {
			write(mouse.mfd, s, 1);

			if (poll(pfd, 1, INFTIM) <= 0)
				break;
			read(mouse.mfd, &c, 1);
			debug("%c", c);
			if (c != *s)
				break;
		}
		break;

	case P_MSC:
		SetMouseSpeed(1200, mousecflags[mouse.proto]);
#if 0
		if (mouse.flags & ClearDTR) {
			i = TIOCM_DTR;
			ioctl(mouse.mfd, TIOCMBIC, &i);
		}
		if (mouse.flags & ClearRTS) {
			i = TIOCM_RTS;
			ioctl(mouse.mfd, TIOCMBIC, &i);
		}
#endif
		break;

	default:
		SetMouseSpeed(1200, mousecflags[mouse.proto]);
		break;
	}
}

/* mouse_identify : identify the protocol used by the mouse */
int
mouse_identify(void)
{
	char pnpbuf[256];	/* PnP identifier string may be up to
				 * 256 bytes long */
	pnpid_t pnpid;
	const symtab_t *t;
	int len;

	/* protocol has been specified with '-t' */
	if (mouse.proto != P_UNKNOWN)
		bcopy(proto[mouse.proto], cur_proto, sizeof(cur_proto));
	else {
		/* maybe this is an PnP mouse... */
		if (mouse.flags & NoPnP)
			return mouse.proto;
		if (((len = pnpgets(mouse.mfd, pnpbuf)) <= 0)
		    || !pnpparse(&pnpid, pnpbuf, len))
			return mouse.proto;

		debug("PnP serial mouse: '%*.*s' '%*.*s' '%*.*s'",
		      pnpid.neisaid, pnpid.neisaid,
		      pnpid.eisaid, pnpid.ncompat,
		      pnpid.ncompat, pnpid.compat,
		      pnpid.ndescription, pnpid.ndescription,
		      pnpid.description);

		/* we have a valid PnP serial device ID */
		t = pnpproto(&pnpid);
		if (t != NULL) {
			mouse.proto = t->val;
			bcopy(proto[mouse.proto], cur_proto, sizeof(cur_proto));
		} else
			mouse.proto = P_UNKNOWN;

	}

	debug("proto params: %02x %02x %02x %02x %d %02x %02x",
	      cur_proto[0], cur_proto[1], cur_proto[2], cur_proto[3],
	      cur_proto[4], cur_proto[5], cur_proto[6]);

	return mouse.proto;
}

/* mouse_protocol : decode bytes with the current mouse protocol */
int
mouse_protocol(u_char rBuf, mousestatus_t * act)
{
	/* MOUSE_MSS_BUTTON?DOWN -> MOUSE_BUTTON?DOWN */
	static int      butmapmss[4] = {	/* Microsoft, MouseMan,
						 * GlidePoint, IntelliMouse,
						 * Thinking Mouse */
		0,
		MOUSE_BUTTON3DOWN,
		MOUSE_BUTTON1DOWN,
		MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
	};
	static int      butmapmss2[4] = {	/* Microsoft, MouseMan,
						 * GlidePoint, Thinking Mouse */
		0,
		MOUSE_BUTTON4DOWN,
		MOUSE_BUTTON2DOWN,
		MOUSE_BUTTON2DOWN | MOUSE_BUTTON4DOWN,
	};
	/* MOUSE_INTELLI_BUTTON?DOWN -> MOUSE_BUTTON?DOWN */
	static int      butmapintelli[4] = {	/* IntelliMouse, NetMouse,
						 * Mie Mouse, MouseMan+ */
		0,
		MOUSE_BUTTON2DOWN,
		MOUSE_BUTTON4DOWN,
		MOUSE_BUTTON2DOWN | MOUSE_BUTTON4DOWN,
	};
	/* MOUSE_MSC_BUTTON?UP -> MOUSE_BUTTON?DOWN */
	static int      butmapmsc[8] = {	/* MouseSystems, MMSeries,
						 * Logitech, Bus, sysmouse */
		0,
		MOUSE_BUTTON3DOWN,
		MOUSE_BUTTON2DOWN,
		MOUSE_BUTTON2DOWN | MOUSE_BUTTON3DOWN,
		MOUSE_BUTTON1DOWN,
		MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN,
		MOUSE_BUTTON1DOWN | MOUSE_BUTTON2DOWN,
		MOUSE_BUTTON1DOWN | MOUSE_BUTTON2DOWN | MOUSE_BUTTON3DOWN
	};
	/* for Hitachi tablet */
	static int      butmaphit[8] = {	/* MM HitTablet */
		0,
		MOUSE_BUTTON3DOWN,
		MOUSE_BUTTON2DOWN,
		MOUSE_BUTTON1DOWN,
		MOUSE_BUTTON4DOWN,
		MOUSE_BUTTON5DOWN,
		MOUSE_BUTTON6DOWN,
		MOUSE_BUTTON7DOWN,
	};
	static int      pBufP = 0;
	static unsigned char pBuf[8];

	debug("received char 0x%x", (int) rBuf);

	/*
         * Hack for resyncing: We check here for a package that is:
         *  a) illegal (detected by wrong data-package header)
         *  b) invalid (0x80 == -128 and that might be wrong for MouseSystems)
         *  c) bad header-package
         *
         * NOTE: b) is a violation of the MouseSystems-Protocol, since values of
         *       -128 are allowed, but since they are very seldom we can easily
         *       use them as package-header with no button pressed.
         * NOTE/2: On a PS/2 mouse any byte is valid as a data byte. Furthermore,
         *         0x80 is not valid as a header byte. For a PS/2 mouse we skip
         *         checking data bytes.
         *         For resyncing a PS/2 mouse we require the two most significant
         *         bits in the header byte to be 0. These are the overflow bits,
         *         and in case of an overflow we actually lose sync. Overflows
         *         are very rare, however, and we quickly gain sync again after
         *         an overflow condition. This is the best we can do. (Actually,
         *         we could use bit 0x08 in the header byte for resyncing, since
         *         that bit is supposed to be always on, but nobody told
         *         Microsoft...)
         */

	if (pBufP != 0 && ((rBuf & cur_proto[2]) != cur_proto[3] || rBuf == 0x80)) {
		pBufP = 0;	/* skip package */
	}
	if (pBufP == 0 && (rBuf & cur_proto[0]) != cur_proto[1])
		return 0;

	/* is there an extra data byte? */
	if (pBufP >= cur_proto[4] && (rBuf & cur_proto[0]) != cur_proto[1]) {
		/*
		 * Hack for Logitech MouseMan Mouse - Middle button
		 *
		 * Unfortunately this mouse has variable length packets: the standard
		 * Microsoft 3 byte packet plus an optional 4th byte whenever the
		 * middle button status changes.
		 *
		 * We have already processed the standard packet with the movement
		 * and button info.  Now post an event message with the old status
		 * of the left and right buttons and the updated middle button.
		 */

		/*
		 * Even worse, different MouseMen and TrackMen differ in the 4th
		 * byte: some will send 0x00/0x20, others 0x01/0x21, or even
		 * 0x02/0x22, so I have to strip off the lower bits.
	         *
	         * [JCH-96/01/21]
	         * HACK for ALPS "fourth button". (It's bit 0x10 of the "fourth byte"
	         * and it is activated by tapping the glidepad with the finger! 8^)
	         * We map it to bit bit3, and the reverse map in xf86Events just has
	         * to be extended so that it is identified as Button 4. The lower
	         * half of the reverse-map may remain unchanged.
		 */

		/*
		 * [KY-97/08/03]
		 * Receive the fourth byte only when preceding three bytes have
		 * been detected (pBufP >= cur_proto[4]).  In the previous
		 * versions, the test was pBufP == 0; thus, we may have mistakingly
		 * received a byte even if we didn't see anything preceding
		 * the byte.
		 */

		if ((rBuf & cur_proto[5]) != cur_proto[6]) {
			pBufP = 0;
			return 0;
		}
		switch (mouse.proto) {

			/*
			 * IntelliMouse, NetMouse (including NetMouse Pro) and Mie Mouse
			 * always send the fourth byte, whereas the fourth byte is
			 * optional for GlidePoint and ThinkingMouse. The fourth byte
			 * is also optional for MouseMan+ and FirstMouse+ in their
			 * native mode. It is always sent if they are in the IntelliMouse
			 * compatible mode.
			 */
		case P_IMSERIAL:	/* IntelliMouse, NetMouse, Mie Mouse,
					 * MouseMan+ */
			act->dx = act->dy = 0;
			act->dz = (rBuf & 0x08) ? (rBuf & 0x0f) - 16 : (rBuf & 0x0f);
			act->obutton = act->button;
			act->button = butmapintelli[(rBuf & MOUSE_MSS_BUTTONS) >> 4]
				| (act->obutton & (MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN));
			break;

		default:
			act->dx = act->dy = act->dz = act->dw = 0;
			act->obutton = act->button;
			act->button = butmapmss2[(rBuf & MOUSE_MSS_BUTTONS) >> 4]
				| (act->obutton & (MOUSE_BUTTON1DOWN | MOUSE_BUTTON3DOWN));
			break;
		}

		act->flags = ((act->dx || act->dy || act->dz || act->dw) ?
		    MOUSE_POSCHANGED : 0) | (act->obutton ^ act->button);
		pBufP = 0;
		return act->flags;
	}
	if (pBufP >= cur_proto[4])
		pBufP = 0;
	pBuf[pBufP++] = rBuf;
	if (pBufP != cur_proto[4])
		return 0;

	/*
         * assembly full package
         */

	debug("assembled full packet (len %d) %x,%x,%x,%x,%x,%x,%x,%x",
	    cur_proto[4],
	    pBuf[0], pBuf[1], pBuf[2], pBuf[3],
	    pBuf[4], pBuf[5], pBuf[6], pBuf[7]);

	act->dz = 0;
	act->dw = 0;
	act->obutton = act->button;
	switch (mouse.proto) {
	case P_MS:		/* Microsoft */
	case P_LOGIMAN:	/* MouseMan/TrackMan */
	case P_GLIDEPOINT:	/* GlidePoint */
	case P_THINKING:	/* ThinkingMouse */
	case P_IMSERIAL:	/* IntelliMouse, NetMouse, Mie Mouse,
				 * MouseMan+ */
		act->button = (act->obutton & (MOUSE_BUTTON2DOWN | MOUSE_BUTTON4DOWN))
		    | butmapmss[(pBuf[0] & MOUSE_MSS_BUTTONS) >> 4];
		act->dx = (char) (((pBuf[0] & 0x03) << 6) | (pBuf[1] & 0x3F));
		act->dy = (char) (((pBuf[0] & 0x0C) << 4) | (pBuf[2] & 0x3F));
		break;

	case P_MSC:		/* MouseSystems Corp */
		act->button = butmapmsc[(~pBuf[0]) & MOUSE_MSC_BUTTONS];
		act->dx = (char) (pBuf[1]) + (char) (pBuf[3]);
		act->dy = -((char) (pBuf[2]) + (char) (pBuf[4]));
		break;

	case P_MMHIT:		/* MM HitTablet */
		act->button = butmaphit[pBuf[0] & 0x07];
		act->dx = (pBuf[0] & MOUSE_MM_XPOSITIVE) ? pBuf[1] : -pBuf[1];
		act->dy = (pBuf[0] & MOUSE_MM_YPOSITIVE) ? -pBuf[2] : pBuf[2];
		break;

	case P_MM:		/* MM Series */
	case P_LOGI:		/* Logitech Mice */
		act->button = butmapmsc[pBuf[0] & MOUSE_MSC_BUTTONS];
		act->dx = (pBuf[0] & MOUSE_MM_XPOSITIVE) ? pBuf[1] : -pBuf[1];
		act->dy = (pBuf[0] & MOUSE_MM_YPOSITIVE) ? -pBuf[2] : pBuf[2];
		break;

		/*
		 * XXX removed the code for BusMouse and PS/2 protocols which
		 * are now handled by wsmouse compatible mouse drivers XXX
		 */

	default:
		return 0;
	}

	/*
         * We don't reset pBufP here yet, as there may be an additional data
         * byte in some protocols. See above.
         */
	/* has something changed? */
	act->flags = ((act->dx || act->dy || act->dz || act->dw) ?
	    MOUSE_POSCHANGED : 0) | (act->obutton ^ act->button);
	return act->flags;
}
