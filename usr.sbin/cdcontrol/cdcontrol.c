/*
 * Compact Disc Control Utility by Serge V. Vakulenko <vak@cronyx.ru>.
 * Based on the non-X based CD player by Jean-Marc Zucconi and
 * Andrey A. Chernov.
 *
 * Fixed and further modified on 5-Sep-1995 by Jukka Ukkonen <jau@funet.fi>.
 *
 * 11-Sep-1995: Jukka A. Ukkonen <jau@funet.fi>
 *              A couple of further fixes to my own earlier "fixes".
 *
 * 18-Sep-1995: Jukka A. Ukkonen <jau@funet.fi>
 *              Added an ability to specify addresses relative to the
 *              beginning of a track. This is in fact a variation of
 *              doing the simple play_msf() call.
 *
 * 11-Oct-1995: Serge V.Vakulenko <vak@cronyx.ru>
 *              New eject algorithm.
 *              Some code style reformatting.
 * 
 * 13-Dec-1999: Knut A. Syed <kas@kas.no>
 * 		Volume-command modified.  If used with only one
 * 		parameter it now sets both channels.  If used without
 * 		parameters it will print volume-info.
 * 		Version 2.0.1
 *
 * 27-Jun-2008  Pietro Cerutti <gahr@FreeBSD.org>
 * 		Further enhancement to volume. Values not in range 0-255
 * 		are now reduced to be in range. This prevents overflow in
 * 		the uchar storing the volume (256 -> 0, -20 -> 236, ...).
 * 		Version 2.0.2
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/cdio.h>
#include <sys/cdrio.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <histedit.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vis.h>

#define VERSION "2.0.2"

#define ASTS_INVALID	0x00  /* Audio status byte not valid */
#define ASTS_PLAYING	0x11  /* Audio play operation in progress */
#define ASTS_PAUSED	0x12  /* Audio play operation paused */
#define ASTS_COMPLETED	0x13  /* Audio play operation successfully completed */
#define ASTS_ERROR	0x14  /* Audio play operation stopped due to error */
#define ASTS_VOID	0x15  /* No current audio status to return */

#ifdef DEFAULT_CD_DRIVE
#  error "Setting DEFAULT_CD_DRIVE is no longer supported"
#endif

#define CMD_DEBUG	1
#define CMD_EJECT	2
#define CMD_HELP	3
#define CMD_INFO	4
#define CMD_PAUSE	5
#define CMD_PLAY	6
#define CMD_QUIT	7
#define CMD_RESUME	8
#define CMD_STOP	9
#define CMD_VOLUME	10
#define CMD_CLOSE	11
#define CMD_RESET	12
#define CMD_SET		13
#define CMD_STATUS	14
#define CMD_CDID	15
#define CMD_NEXT	16
#define CMD_PREVIOUS	17
#define CMD_SPEED	18
#define STATUS_AUDIO	0x1
#define STATUS_MEDIA	0x2
#define STATUS_VOLUME	0x4

static struct cmdtab {
	int command;
	const char *name;
	unsigned min;
	const char *args;
} cmdtab[] = {
{ CMD_CLOSE,	"close",	1, "" },
{ CMD_DEBUG,	"debug",	1, "on | off" },
{ CMD_EJECT,	"eject",	1, "" },
{ CMD_HELP,	"?",		1, 0 },
{ CMD_HELP,	"help",		1, "" },
{ CMD_INFO,	"info",		1, "" },
{ CMD_NEXT,	"next",		1, "" },
{ CMD_PAUSE,	"pause",	2, "" },
{ CMD_PLAY,	"play",		1, "min1:sec1[.fram1] [min2:sec2[.fram2]]" },
{ CMD_PLAY,	"play",		1, "track1[.index1] [track2[.index2]]" },
{ CMD_PLAY,	"play",		1, "tr1 m1:s1[.f1] [[tr2] [m2:s2[.f2]]]" },
{ CMD_PLAY,	"play",		1, "[#block [len]]" },
{ CMD_PREVIOUS,	"previous",	2, "" },
{ CMD_QUIT,	"quit",		1, "" },
{ CMD_RESET,	"reset",	4, "" },
{ CMD_RESUME,	"resume",	1, "" },
{ CMD_SET,	"set",		2, "msf | lba" },
{ CMD_STATUS,	"status",	1, "[audio | media | volume]" },
{ CMD_STOP,	"stop",		3, "" },
{ CMD_VOLUME,	"volume",	1,
      "<l&r> <l> <r> | left | right | mute | mono | stereo" },
{ CMD_CDID,	"cdid",		2, "" },
{ CMD_SPEED,	"speed",	2, "speed" },
{ 0,		NULL,		0, NULL }
};

static struct cd_toc_entry toc_buffer[100];

static const char *cdname;
static int	fd = -1;
static int	verbose = 1;
static int	msf = 1;

static int	 setvol(int, int);
static int	 read_toc_entrys(int);
static int	 play_msf(int, int, int, int, int, int);
static int	 play_track(int, int, int, int);
static int	 status(int *, int *, int *, int *);
static int	 open_cd(void);
static int	 next_prev(char *arg, int);
static int	 play(char *arg);
static int	 info(char *arg);
static int	 cdid(void);
static int	 pstatus(char *arg);
static char	*input(int *);
static void	 prtrack(struct cd_toc_entry *e, int lastflag);
static void	 lba2msf(unsigned long lba, u_char *m, u_char *s, u_char *f);
static unsigned int msf2lba(u_char m, u_char s, u_char f);
static int	 play_blocks(int blk, int len);
static int	 run(int cmd, char *arg);
static char	*parse(char *buf, int *cmd);
static void	 help(void);
static void	 usage(void);
static char	*use_cdrom_instead(const char *);
static const char *strstatus(int);
static u_int	 dbprog_discid(void);
static const char *cdcontrol_prompt(void);

static void
help(void)
{
	struct cmdtab *c;
	const char *s;
	char n;
	int i;

	for (c=cmdtab; c->name; ++c) {
		if (! c->args)
			continue;
		printf("\t");
		for (i = c->min, s = c->name; *s; s++, i--) {
			if (i > 0)
				n = toupper(*s);
			else
				n = *s;
			putchar(n);
		}
		if (*c->args)
			printf (" %s", c->args);
		printf ("\n");
	}
	printf ("\n\tThe word \"play\" is not required for the play commands.\n");
	printf ("\tThe plain target address is taken as a synonym for play.\n");
}

static void
usage(void)
{
	fprintf (stderr, "usage: cdcontrol [-sv] [-f device] [command ...]\n");
	exit (1);
}

static char *
use_cdrom_instead(const char *old_envvar)
{
	char *device;

	device = getenv(old_envvar);
	if (device)
		warnx("%s environment variable deprecated, "
		    "please use CDROM in the future.", old_envvar);
	return device;
}


int
main(int argc, char **argv)
{
	int cmd;
	char *arg;

	for (;;) {
		switch (getopt (argc, argv, "svhf:")) {
		case -1:
			break;
		case 's':
			verbose = 0;
			continue;
		case 'v':
			verbose = 2;
			continue;
		case 'f':
			cdname = optarg;
			continue;
		case 'h':
		default:
			usage ();
		}
		break;
	}
	argc -= optind;
	argv += optind;

	if (argc > 0 && ! strcasecmp (*argv, "help"))
		usage ();

	if (! cdname) {
		cdname = getenv("CDROM");
	}

	if (! cdname)
		cdname = use_cdrom_instead("MUSIC_CD");
	if (! cdname)
		cdname = use_cdrom_instead("CD_DRIVE");
	if (! cdname)
		cdname = use_cdrom_instead("DISC");
	if (! cdname)
		cdname = use_cdrom_instead("CDPLAY");

	if (argc > 0) {
		char buf[80], *p;
		int len, rc;

		for (p=buf; argc-->0; ++argv) {
			len = strlen (*argv);

			if (p + len >= buf + sizeof (buf) - 1)
				usage ();

			if (p > buf)
				*p++ = ' ';

			strcpy (p, *argv);
			p += len;
		}
		*p = 0;
		arg = parse (buf, &cmd);
		rc = run (cmd, arg);
		if (rc < 0 && verbose)
			warn(NULL);

		return (rc);
	}

	if (verbose == 1)
		verbose = isatty (0);

	if (verbose) {
		printf ("Compact Disc Control utility, version %s\n", VERSION);
		printf ("Type `?' for command list\n\n");
	}

	for (;;) {
		arg = input (&cmd);
		if (run (cmd, arg) < 0) {
			if (verbose)
				warn(NULL);
			close (fd);
			fd = -1;
		}
		fflush (stdout);
	}
}

static int
run(int cmd, char *arg)
{
	long speed;
	int l, r, rc, count;

	switch (cmd) {

	case CMD_QUIT:
		exit (0);

	case CMD_INFO:
		if (fd < 0 && ! open_cd ())
			return (0);

		return info (arg);

	case CMD_CDID:
		if (fd < 0 && ! open_cd ())
			return (0);

		return cdid ();

	case CMD_STATUS:
		if (fd < 0 && ! open_cd ())
			return (0);

		return pstatus (arg);

	case CMD_NEXT:
	case CMD_PREVIOUS:
		if (fd < 0 && ! open_cd ())
			return (0);

		while (isspace (*arg))
			arg++;

		return next_prev (arg, cmd);

	case CMD_PAUSE:
		if (fd < 0 && ! open_cd ())
			return (0);

		return ioctl (fd, CDIOCPAUSE);

	case CMD_RESUME:
		if (fd < 0 && ! open_cd ())
			return (0);

		return ioctl (fd, CDIOCRESUME);

	case CMD_STOP:
		if (fd < 0 && ! open_cd ())
			return (0);

		rc = ioctl (fd, CDIOCSTOP);

		(void) ioctl (fd, CDIOCALLOW);

		return (rc);

	case CMD_RESET:
		if (fd < 0 && ! open_cd ())
			return (0);

		rc = ioctl (fd, CDIOCRESET);
		if (rc < 0)
			return rc;
		close(fd);
		fd = -1;
		return (0);

	case CMD_DEBUG:
		if (fd < 0 && ! open_cd ())
			return (0);

		if (! strcasecmp (arg, "on"))
			return ioctl (fd, CDIOCSETDEBUG);

		if (! strcasecmp (arg, "off"))
			return ioctl (fd, CDIOCCLRDEBUG);

		warnx("invalid command arguments");

		return (0);

	case CMD_EJECT:
		if (fd < 0 && ! open_cd ())
			return (0);

		(void) ioctl (fd, CDIOCALLOW);
		rc = ioctl (fd, CDIOCEJECT);
		if (rc < 0)
			return (rc);
		return (0);

	case CMD_CLOSE:
		if (fd < 0 && ! open_cd ())
			return (0);

		(void) ioctl (fd, CDIOCALLOW);
		rc = ioctl (fd, CDIOCCLOSE);
		if (rc < 0)
			return (rc);
		close(fd);
		fd = -1;
		return (0);

	case CMD_PLAY:
		if (fd < 0 && ! open_cd ())
			return (0);

		while (isspace (*arg))
			arg++;

		return play (arg);

	case CMD_SET:
		if (! strcasecmp (arg, "msf"))
			msf = 1;
		else if (! strcasecmp (arg, "lba"))
			msf = 0;
		else
			warnx("invalid command arguments");
		return (0);

	case CMD_VOLUME:
		if (fd < 0 && !open_cd ())
			return (0);

		if (! strlen (arg)) {
			char volume[] = "volume";

		    	return pstatus (volume);
		}

		if (! strncasecmp (arg, "left", strlen(arg)))
			return ioctl (fd, CDIOCSETLEFT);

		if (! strncasecmp (arg, "right", strlen(arg)))
			return ioctl (fd, CDIOCSETRIGHT);

		if (! strncasecmp (arg, "mono", strlen(arg)))
			return ioctl (fd, CDIOCSETMONO);

		if (! strncasecmp (arg, "stereo", strlen(arg)))
			return ioctl (fd, CDIOCSETSTERIO);

		if (! strncasecmp (arg, "mute", strlen(arg)))
			return ioctl (fd, CDIOCSETMUTE);

		count = sscanf (arg, "%d %d", &l, &r);
		if (count == 1)
		    return setvol (l, l);
		if (count == 2)
		    return setvol (l, r);
		warnx("invalid command arguments");
		return (0);

	case CMD_SPEED:
		if (fd < 0 && ! open_cd ())
			return (0);

		errno = 0;
		if (strcasecmp("max", arg) == 0)
			speed = CDR_MAX_SPEED;
		else
			speed = strtol(arg, NULL, 10) * 177;
		if (speed <= 0 || speed > INT_MAX) {
			warnx("invalid command arguments %s", arg);
			return (0);
		}
		return ioctl(fd, CDRIOCREADSPEED, &speed);

	default:
	case CMD_HELP:
		help ();
		return (0);

	}
}

static int
play(char *arg)
{
	struct ioc_toc_header h;
	unsigned int n;
	int rc, start, end = 0, istart = 1, iend = 1;

	rc = ioctl (fd, CDIOREADTOCHEADER, &h);

	if (rc < 0)
		return (rc);

	n = h.ending_track - h.starting_track + 1;
	rc = read_toc_entrys ((n + 1) * sizeof (struct cd_toc_entry));

	if (rc < 0)
		return (rc);

	if (! arg || ! *arg) {
		/* Play the whole disc */
		if (msf)
			return play_blocks (0, msf2lba (toc_buffer[n].addr.msf.minute,
							toc_buffer[n].addr.msf.second,
							toc_buffer[n].addr.msf.frame));
		else
			return play_blocks (0, ntohl(toc_buffer[n].addr.lba));
	}

	if (strchr (arg, '#')) {
		/* Play block #blk [ len ] */
		int blk, len = 0;

		if (2 != sscanf (arg, "#%d%d", &blk, &len) &&
		    1 != sscanf (arg, "#%d", &blk))
			goto Clean_up;

		if (len == 0) {
			if (msf)
				len = msf2lba (toc_buffer[n].addr.msf.minute,
					       toc_buffer[n].addr.msf.second,
					       toc_buffer[n].addr.msf.frame) - blk;
			else
				len = ntohl(toc_buffer[n].addr.lba) - blk;
		}
		return play_blocks (blk, len);
	}

	if (strchr (arg, ':')) {
		/*
		 * Play MSF m1:s1 [ .f1 ] [ m2:s2 [ .f2 ] ]
		 *
		 * Will now also undestand timed addresses relative
		 * to the beginning of a track in the form...
		 *
		 *      tr1 m1:s1[.f1] [[tr2] [m2:s2[.f2]]]
		 */
		unsigned tr1, tr2;
		unsigned m1, m2, s1, s2, f1, f2;
		unsigned char tm, ts, tf;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (8 == sscanf (arg, "%d %d:%d.%d %d %d:%d.%d",
		    &tr1, &m1, &s1, &f1, &tr2, &m2, &s2, &f2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (7 == sscanf (arg, "%d %d:%d %d %d:%d.%d",
		    &tr1, &m1, &s1, &tr2, &m2, &s2, &f2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (7 == sscanf (arg, "%d %d:%d.%d %d %d:%d",
		    &tr1, &m1, &s1, &f1, &tr2, &m2, &s2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (7 == sscanf (arg, "%d %d:%d.%d %d:%d.%d",
		    &tr1, &m1, &s1, &f1, &m2, &s2, &f2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (6 == sscanf (arg, "%d %d:%d.%d %d:%d",
		    &tr1, &m1, &s1, &f1, &m2, &s2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (6 == sscanf (arg, "%d %d:%d %d:%d.%d",
		    &tr1, &m1, &s1, &m2, &s2, &f2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (6 == sscanf (arg, "%d %d:%d.%d %d %d",
		    &tr1, &m1, &s1, &f1, &tr2, &m2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (5 == sscanf (arg, "%d %d:%d %d:%d", &tr1, &m1, &s1, &m2, &s2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (5 == sscanf (arg, "%d %d:%d %d %d",
		    &tr1, &m1, &s1, &tr2, &m2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (5 == sscanf (arg, "%d %d:%d.%d %d",
		    &tr1, &m1, &s1, &f1, &tr2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (4 == sscanf (arg, "%d %d:%d %d", &tr1, &m1, &s1, &tr2))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (4 == sscanf (arg, "%d %d:%d.%d", &tr1, &m1, &s1, &f1))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		if (3 == sscanf (arg, "%d %d:%d", &tr1, &m1, &s1))
			goto Play_Relative_Addresses;

		tr2 = m2 = s2 = f2 = f1 = 0;
		goto Try_Absolute_Timed_Addresses;

Play_Relative_Addresses:
		if (! tr1)
			tr1 = 1;
		else if (tr1 > n)
			tr1 = n;

		tr1--;

		if (msf) {
			tm = toc_buffer[tr1].addr.msf.minute;
			ts = toc_buffer[tr1].addr.msf.second;
			tf = toc_buffer[tr1].addr.msf.frame;
		} else
			lba2msf(ntohl(toc_buffer[tr1].addr.lba),
				&tm, &ts, &tf);
		if ((m1 > tm)
		    || ((m1 == tm)
		    && ((s1 > ts)
		    || ((s1 == ts)
		    && (f1 > tf))))) {
			printf ("Track %d is not that long.\n", tr1);
			return (0);
		}

		f1 += tf;
		if (f1 >= 75) {
			s1 += f1 / 75;
			f1 %= 75;
		}

		s1 += ts;
		if (s1 >= 60) {
			m1 += s1 / 60;
			s1 %= 60;
		}

		m1 += tm;

		if (! tr2) {
			if (m2 || s2 || f2) {
				tr2 = tr1;
				f2 += f1;
				if (f2 >= 75) {
					s2 += f2 / 75;
					f2 %= 75;
				}

				s2 += s1;
				if (s2 > 60) {
					m2 += s2 / 60;
					s2 %= 60;
				}

				m2 += m1;
			} else {
				tr2 = n;
				if (msf) {
					m2 = toc_buffer[n].addr.msf.minute;
					s2 = toc_buffer[n].addr.msf.second;
					f2 = toc_buffer[n].addr.msf.frame;
				} else {
					lba2msf(ntohl(toc_buffer[n].addr.lba),
						&tm, &ts, &tf);
					m2 = tm;
					s2 = ts;
					f2 = tf;
				}
			}
		} else if (tr2 > n) {
			tr2 = n;
			m2 = s2 = f2 = 0;
		} else {
			if (m2 || s2 || f2)
				tr2--;
			if (msf) {
				tm = toc_buffer[tr2].addr.msf.minute;
				ts = toc_buffer[tr2].addr.msf.second;
				tf = toc_buffer[tr2].addr.msf.frame;
			} else
				lba2msf(ntohl(toc_buffer[tr2].addr.lba),
					&tm, &ts, &tf);
			f2 += tf;
			if (f2 >= 75) {
				s2 += f2 / 75;
				f2 %= 75;
			}

			s2 += ts;
			if (s2 > 60) {
				m2 += s2 / 60;
				s2 %= 60;
			}

			m2 += tm;
		}

		if (msf) {
			tm = toc_buffer[n].addr.msf.minute;
			ts = toc_buffer[n].addr.msf.second;
			tf = toc_buffer[n].addr.msf.frame;
		} else
			lba2msf(ntohl(toc_buffer[n].addr.lba),
				&tm, &ts, &tf);
		if ((tr2 < n)
		    && ((m2 > tm)
		    || ((m2 == tm)
		    && ((s2 > ts)
		    || ((s2 == ts)
		    && (f2 > tf)))))) {
			printf ("The playing time of the disc is not that long.\n");
			return (0);
		}
		return (play_msf (m1, s1, f1, m2, s2, f2));

Try_Absolute_Timed_Addresses:
		if (6 != sscanf (arg, "%d:%d.%d%d:%d.%d",
			&m1, &s1, &f1, &m2, &s2, &f2) &&
		    5 != sscanf (arg, "%d:%d.%d%d:%d", &m1, &s1, &f1, &m2, &s2) &&
		    5 != sscanf (arg, "%d:%d%d:%d.%d", &m1, &s1, &m2, &s2, &f2) &&
		    3 != sscanf (arg, "%d:%d.%d", &m1, &s1, &f1) &&
		    4 != sscanf (arg, "%d:%d%d:%d", &m1, &s1, &m2, &s2) &&
		    2 != sscanf (arg, "%d:%d", &m1, &s1))
			goto Clean_up;

		if (m2 == 0) {
			if (msf) {
				m2 = toc_buffer[n].addr.msf.minute;
				s2 = toc_buffer[n].addr.msf.second;
				f2 = toc_buffer[n].addr.msf.frame;
			} else {
				lba2msf(ntohl(toc_buffer[n].addr.lba),
					&tm, &ts, &tf);
				m2 = tm;
				s2 = ts;
				f2 = tf;
			}
		}
		return play_msf (m1, s1, f1, m2, s2, f2);
	}

	/*
	 * Play track trk1 [ .idx1 ] [ trk2 [ .idx2 ] ]
	 */
	if (4 != sscanf (arg, "%d.%d%d.%d", &start, &istart, &end, &iend) &&
	    3 != sscanf (arg, "%d.%d%d", &start, &istart, &end) &&
	    3 != sscanf (arg, "%d%d.%d", &start, &end, &iend) &&
	    2 != sscanf (arg, "%d.%d", &start, &istart) &&
	    2 != sscanf (arg, "%d%d", &start, &end) &&
	    1 != sscanf (arg, "%d", &start))
		goto Clean_up;

	if (end == 0)
		end = n;
	return (play_track (start, istart, end, iend));

Clean_up:
	warnx("invalid command arguments");
	return (0);
}

static int
next_prev(char *arg, int cmd)
{
	struct ioc_toc_header h;
	int dir, junk, n, off, rc, trk;

	dir = (cmd == CMD_NEXT) ? 1 : -1;
	rc = ioctl (fd, CDIOREADTOCHEADER, &h);
	if (rc < 0)
		return (rc);

	n = h.ending_track - h.starting_track + 1;
	rc = status (&trk, &junk, &junk, &junk);
	if (rc < 0)
		return (-1);

	if (arg && *arg) {
		if (sscanf (arg, "%u", &off) != 1) {
		    warnx("invalid command argument");
		    return (0);
		} else
		    trk += off * dir;
	} else
		trk += dir;

	if (trk > h.ending_track)
		trk = 1;

	return (play_track (trk, 1, n, 1));
}

static const char *
strstatus(int sts)
{
	switch (sts) {
	case ASTS_INVALID:	return ("invalid");
	case ASTS_PLAYING:	return ("playing");
	case ASTS_PAUSED:	return ("paused");
	case ASTS_COMPLETED:	return ("completed");
	case ASTS_ERROR:	return ("error");
	case ASTS_VOID:		return ("void");
	default:		return ("??");
	}
}

static int
pstatus(char *arg)
{
	struct ioc_vol v;
	struct ioc_read_subchannel ss;
	struct cd_sub_channel_info data;
	int rc, trk, m, s, f;
	int what = 0;
	char *p, vmcn[(4 * 15) + 1];

	while ((p = strtok(arg, " \t"))) {
	    arg = 0;
	    if (!strncasecmp(p, "audio", strlen(p)))
		what |= STATUS_AUDIO;
	    else if (!strncasecmp(p, "media", strlen(p)))
		what |= STATUS_MEDIA;
	    else if (!strncasecmp(p, "volume", strlen(p)))
		what |= STATUS_VOLUME;
	    else {
		warnx("invalid command arguments");
		return 0;
	    }
	}
	if (!what)
	    what = STATUS_AUDIO|STATUS_MEDIA|STATUS_VOLUME;
	if (what & STATUS_AUDIO) {
	    rc = status (&trk, &m, &s, &f);
	    if (rc >= 0)
		if (verbose)
		    printf ("Audio status = %d<%s>, current track = %d, current position = %d:%02d.%02d\n",
			    rc, strstatus (rc), trk, m, s, f);
		else
		    printf ("%d %d %d:%02d.%02d\n", rc, trk, m, s, f);
	    else
		printf ("No current status info available\n");
	}
	if (what & STATUS_MEDIA) {
	    bzero (&ss, sizeof (ss));
	    ss.data = &data;
	    ss.data_len = sizeof (data);
	    ss.address_format = msf ? CD_MSF_FORMAT : CD_LBA_FORMAT;
	    ss.data_format = CD_MEDIA_CATALOG;
	    rc = ioctl (fd, CDIOCREADSUBCHANNEL, (char *) &ss);
	    if (rc >= 0) {
		printf("Media catalog is %sactive",
		    ss.data->what.media_catalog.mc_valid ? "": "in");
		if (ss.data->what.media_catalog.mc_valid &&
		    ss.data->what.media_catalog.mc_number[0])
		{
		    strvisx (vmcn, ss.data->what.media_catalog.mc_number,
			    (sizeof (vmcn) - 1) / 4, VIS_OCTAL | VIS_NL);
		    printf(", number \"%.*s\"", (int)sizeof (vmcn), vmcn);
		}
		putchar('\n');
	    } else
		printf("No media catalog info available\n");
	}
	if (what & STATUS_VOLUME) {
	    rc = ioctl (fd, CDIOCGETVOL, &v);
	    if (rc >= 0)
		if (verbose)
		    printf ("Left volume = %d, right volume = %d\n",
			    v.vol[0], v.vol[1]);
		else
		    printf ("%d %d\n", v.vol[0], v.vol[1]);
	    else
		printf ("No volume level info available\n");
	}
	return(0);
}

/*
 * dbprog_sum
 *	Convert an integer to its text string representation, and
 *	compute its checksum.  Used by dbprog_discid to derive the
 *	disc ID.
 *
 * Args:
 *	n - The integer value.
 *
 * Return:
 *	The integer checksum.
 */
static int
dbprog_sum(int n)
{
	char	buf[12],
		*p;
	int	ret = 0;

	/* For backward compatibility this algorithm must not change */
	sprintf(buf, "%u", n);
	for (p = buf; *p != '\0'; p++)
		ret += (*p - '0');

	return(ret);
}


/*
 * dbprog_discid
 *	Compute a magic disc ID based on the number of tracks,
 *	the length of each track, and a checksum of the string
 *	that represents the offset of each track.
 *
 * Args:
 *	s - Pointer to the curstat_t structure.
 *
 * Return:
 *	The integer disc ID.
 */
static u_int
dbprog_discid(void)
{
	struct	ioc_toc_header h;
	int	rc;
	int	i, ntr,
		t = 0,
		n = 0;

	rc = ioctl (fd, CDIOREADTOCHEADER, &h);
	if (rc < 0)
		return 0;
	ntr = h.ending_track - h.starting_track + 1;
	i = msf;
	msf = 1;
	rc = read_toc_entrys ((ntr + 1) * sizeof (struct cd_toc_entry));
	msf = i;
	if (rc < 0)
		return 0;
	/* For backward compatibility this algorithm must not change */
	for (i = 0; i < ntr; i++) {
#define TC_MM(a) toc_buffer[a].addr.msf.minute
#define TC_SS(a) toc_buffer[a].addr.msf.second
		n += dbprog_sum((TC_MM(i) * 60) + TC_SS(i));

		t += ((TC_MM(i+1) * 60) + TC_SS(i+1)) -
		    ((TC_MM(i) * 60) + TC_SS(i));
	}

	return((n % 0xff) << 24 | t << 8 | ntr);
}

static int
cdid(void)
{
	u_int	id;

	id = dbprog_discid();
	if (id)
	{
		if (verbose)
			printf ("CDID=");
		printf ("%08x\n",id);
	}
	return id ? 0 : 1;
}

static int
info(char *arg __unused)
{
	struct ioc_toc_header h;
	int rc, i, n;

	rc = ioctl (fd, CDIOREADTOCHEADER, &h);
	if (rc >= 0) {
		if (verbose)
			printf ("Starting track = %d, ending track = %d, TOC size = %d bytes\n",
				h.starting_track, h.ending_track, h.len);
		else
			printf ("%d %d %d\n", h.starting_track,
				h.ending_track, h.len);
	} else {
		warn("getting toc header");
		return (rc);
	}

	n = h.ending_track - h.starting_track + 1;
	rc = read_toc_entrys ((n + 1) * sizeof (struct cd_toc_entry));
	if (rc < 0)
		return (rc);

	if (verbose) {
		printf ("track     start  duration   block  length   type\n");
		printf ("-------------------------------------------------\n");
	}

	for (i = 0; i < n; i++) {
		printf ("%5d  ", toc_buffer[i].track);
		prtrack (toc_buffer + i, 0);
	}
	printf ("%5d  ", toc_buffer[n].track);
	prtrack (toc_buffer + n, 1);
	return (0);
}

static void
lba2msf(unsigned long lba, u_char *m, u_char *s, u_char *f)
{
	lba += 150;			/* block start offset */
	lba &= 0xffffff;		/* negative lbas use only 24 bits */
	*m = lba / (60 * 75);
	lba %= (60 * 75);
	*s = lba / 75;
	*f = lba % 75;
}

static unsigned int
msf2lba(u_char m, u_char s, u_char f)
{
	return (((m * 60) + s) * 75 + f) - 150;
}

static void
prtrack(struct cd_toc_entry *e, int lastflag)
{
	int block, next, len;
	u_char m, s, f;

	if (msf) {
		/* Print track start */
		printf ("%2d:%02d.%02d  ", e->addr.msf.minute,
			e->addr.msf.second, e->addr.msf.frame);

		block = msf2lba (e->addr.msf.minute, e->addr.msf.second,
			e->addr.msf.frame);
	} else {
		block = ntohl(e->addr.lba);
		lba2msf(block, &m, &s, &f);
		/* Print track start */
		printf ("%2d:%02d.%02d  ", m, s, f);
	}
	if (lastflag) {
		/* Last track -- print block */
		printf ("       -  %6d       -      -\n", block);
		return;
	}

	if (msf)
		next = msf2lba (e[1].addr.msf.minute, e[1].addr.msf.second,
			e[1].addr.msf.frame);
	else
		next = ntohl(e[1].addr.lba);
	len = next - block;
	/* Take into account a start offset time. */
	lba2msf (len - 150, &m, &s, &f);

	/* Print duration, block, length, type */
	printf ("%2d:%02d.%02d  %6d  %6d  %5s\n", m, s, f, block, len,
		(e->control & 4) ? "data" : "audio");
}

static int
play_track(int tstart, int istart, int tend, int iend)
{
	struct ioc_play_track t;

	t.start_track = tstart;
	t.start_index = istart;
	t.end_track = tend;
	t.end_index = iend;

	return ioctl (fd, CDIOCPLAYTRACKS, &t);
}

static int
play_blocks(int blk, int len)
{
	struct ioc_play_blocks  t;

	t.blk = blk;
	t.len = len;

	return ioctl (fd, CDIOCPLAYBLOCKS, &t);
}

static int
setvol(int left, int right)
{
	struct ioc_vol  v;

	left  = left  < 0 ? 0 : left  > 255 ? 255 : left;
	right = right < 0 ? 0 : right > 255 ? 255 : right;

	v.vol[0] = left;
	v.vol[1] = right;
	v.vol[2] = 0;
	v.vol[3] = 0;

	return ioctl (fd, CDIOCSETVOL, &v);
}

static int
read_toc_entrys(int len)
{
	struct ioc_read_toc_entry t;

	t.address_format = msf ? CD_MSF_FORMAT : CD_LBA_FORMAT;
	t.starting_track = 0;
	t.data_len = len;
	t.data = toc_buffer;

	return (ioctl (fd, CDIOREADTOCENTRYS, (char *) &t));
}

static int
play_msf(int start_m, int start_s, int start_f,
	int end_m, int end_s, int end_f)
{
	struct ioc_play_msf a;

	a.start_m = start_m;
	a.start_s = start_s;
	a.start_f = start_f;
	a.end_m = end_m;
	a.end_s = end_s;
	a.end_f = end_f;

	return ioctl (fd, CDIOCPLAYMSF, (char *) &a);
}

static int
status(int *trk, int *min, int *sec, int *frame)
{
	struct ioc_read_subchannel s;
	struct cd_sub_channel_info data;
	u_char mm, ss, ff;

	bzero (&s, sizeof (s));
	s.data = &data;
	s.data_len = sizeof (data);
	s.address_format = msf ? CD_MSF_FORMAT : CD_LBA_FORMAT;
	s.data_format = CD_CURRENT_POSITION;

	if (ioctl (fd, CDIOCREADSUBCHANNEL, (char *) &s) < 0)
		return -1;

	*trk = s.data->what.position.track_number;
	if (msf) {
		*min = s.data->what.position.reladdr.msf.minute;
		*sec = s.data->what.position.reladdr.msf.second;
		*frame = s.data->what.position.reladdr.msf.frame;
	} else {
		lba2msf(ntohl(s.data->what.position.reladdr.lba),
			&mm, &ss, &ff);
		*min = mm;
		*sec = ss;
		*frame = ff;
	}

	return s.data->header.audio_status;
}

static const char *
cdcontrol_prompt(void)
{
	return ("cdcontrol> ");
}

static char *
input(int *cmd)
{
#define MAXLINE 80
	static EditLine *el = NULL;
	static History *hist = NULL;
	HistEvent he;
	static char buf[MAXLINE];
	int num = 0;
	int len;
	const char *bp = NULL;
	char *p;

	do {
		if (verbose) {
			if (!el) {
				el = el_init("cdcontrol", stdin, stdout,
				    stderr);
				hist = history_init();
				history(hist, &he, H_SETSIZE, 100);
				el_set(el, EL_HIST, history, hist);
				el_set(el, EL_EDITOR, "emacs");
				el_set(el, EL_PROMPT, cdcontrol_prompt);
				el_set(el, EL_SIGNAL, 1);
				el_source(el, NULL);
			}
			if ((bp = el_gets(el, &num)) == NULL || num == 0) {
				*cmd = CMD_QUIT;
				fprintf (stderr, "\r\n");
				return (0);
			}

			len = (num > MAXLINE) ? MAXLINE : num;
			memcpy(buf, bp, len);
			buf[len] = 0;
			history(hist, &he, H_ENTER, bp);
#undef MAXLINE

		} else {
			if (! fgets (buf, sizeof (buf), stdin)) {
				*cmd = CMD_QUIT;
				fprintf (stderr, "\r\n");
				return (0);
			}
		}
		p = parse (buf, cmd);
	} while (! p);
	return (p);
}

static char *
parse(char *buf, int *cmd)
{
	struct cmdtab *c;
	char *p;
	unsigned int len;

	for (p=buf; isspace (*p); p++)
		continue;

	if (isdigit (*p) || (p[0] == '#' && isdigit (p[1]))) {
		*cmd = CMD_PLAY;
		return (p);
	} else if (*p == '+') {
		*cmd = CMD_NEXT;
		return (p + 1);
	} else if (*p == '-') {
		*cmd = CMD_PREVIOUS;
		return (p + 1);
	}

	for (buf = p; *p && ! isspace (*p); p++)
		continue;
 
	len = p - buf;
	if (! len)
		return (0);

	if (*p) {		/* It must be a spacing character! */
		char *q;

		*p++ = 0;
		for (q=p; *q && *q != '\n' && *q != '\r'; q++)
			continue;
		*q = 0;
	}

	*cmd = -1;
	for (c=cmdtab; c->name; ++c) {
		/* Is it an exact match? */
		if (! strcasecmp (buf, c->name)) {
  			*cmd = c->command;
  			break;
  		}

		/* Try short hand forms then... */
		if (len >= c->min && ! strncasecmp (buf, c->name, len)) {
			if (*cmd != -1 && *cmd != c->command) {
				warnx("ambiguous command");
				return (0);
			}
			*cmd = c->command;
  		}
	}

	if (*cmd == -1) {
		warnx("invalid command, enter ``help'' for commands");
		return (0);
	}

	while (isspace (*p))
		p++;
	return p;
}

static int
open_cd(void)
{
	char devbuf[MAXPATHLEN];
	const char *dev;

	if (fd > -1)
		return (1);

	if (cdname) {
	    if (*cdname == '/') {
		    snprintf (devbuf, MAXPATHLEN, "%s", cdname);
	    } else {
		    snprintf (devbuf, MAXPATHLEN, "%s%s", _PATH_DEV, cdname);
	    }
	    fd = open (dev = devbuf, O_RDONLY);
	} else {
	    fd = open(dev = "/dev/cdrom", O_RDONLY);
	    if (fd < 0 && errno == ENOENT)
		fd = open(dev = "/dev/cd0", O_RDONLY);
	}

	if (fd < 0) {
		if (errno == ENXIO) {
			/*  ENXIO has an overloaded meaning here.
			 *  The original "Device not configured" should
			 *  be interpreted as "No disc in drive %s". */
			warnx("no disc in drive %s", dev);
			return (0);
		}
		err(1, "%s", dev);
	}
	return (1);
}
