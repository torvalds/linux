/*
 *	This is an example of a mixer program for Linux
 *
 *	updated 1/1/93 to add stereo, level query, broken
 *      	devmask kludge - cmetz@thor.tjhsst.edu
 *
 * (C) Craig Metz and Hannu Savolainen 1993.
 *
 * You may do anything you wish with this program.
 *
 * ditto for my modifications (John-Mark Gurney, 1997)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/soundcard.h>

static const char *names[SOUND_MIXER_NRDEVICES] = SOUND_DEVICE_NAMES;

static void	usage(int devmask, int recmask) __dead2;
static int	res_name(const char *name, int mask);
static void	print_recsrc(int recsrc, int recmask, int sflag);

static void __dead2
usage(int devmask, int recmask)
{
	int	i, n;

	printf("usage: mixer [-f device] [-s | -S] [dev [+|-][voll[:[+|-]volr]] ...\n"
	    "       mixer [-f device] [-s | -S] recsrc ...\n"
	    "       mixer [-f device] [-s | -S] {^|+|-|=}rec rdev ...\n");
	if (devmask != 0) {
		printf(" devices: ");
		for (i = 0, n = 0; i < SOUND_MIXER_NRDEVICES; i++)
			if ((1 << i) & devmask)  {
				if (n)
					printf(", ");
				printf("%s", names[i]);
				n++;
			}
	}
	if (recmask != 0) {
		printf("\n rec devices: ");
		for (i = 0, n = 0; i < SOUND_MIXER_NRDEVICES; i++)
			if ((1 << i) & recmask)  {
				if (n)
					printf(", ");
				printf("%s", names[i]);
				n++;
			}
	}
	printf("\n");
	exit(1);
}

static int
res_name(const char *name, int mask)
{
	int	foo;

	for (foo = 0; foo < SOUND_MIXER_NRDEVICES; foo++)
		if ((1 << foo) & mask && strcmp(names[foo], name) == 0)
			break;

	return (foo == SOUND_MIXER_NRDEVICES ? -1 : foo);
}

static void
print_recsrc(int recsrc, int recmask, int sflag)
{
	int	i, n;

	if (recmask == 0)
		return;

	if (!sflag)
		printf("Recording source: ");

	for (i = 0, n = 0; i < SOUND_MIXER_NRDEVICES; i++)
		if ((1 << i) & recsrc) {
			if (sflag)
				printf("%srec ", n ? " +" : "=");
			else if (n)
				printf(", ");
			printf("%s", names[i]);
			n++;
		}
	if (!sflag)
		printf("\n");
}

int
main(int argc, char *argv[])
{
	char	mixer[PATH_MAX] = "/dev/mixer";
	char	lstr[8], rstr[8];
	char	*name, *eptr;
	int	devmask = 0, recmask = 0, recsrc = 0, orecsrc;
	int	dusage = 0, drecsrc = 0, sflag = 0, Sflag = 0;
	int	l, r, lrel, rrel;
	int	ch, foo, bar, baz, dev, m, n, t;

	if ((name = strdup(basename(argv[0]))) == NULL)
		err(1, "strdup()");
	if (strncmp(name, "mixer", 5) == 0 && name[5] != '\0') {
		n = strtol(name + 5, &eptr, 10) - 1;
		if (n > 0 && *eptr == '\0')
			snprintf(mixer, PATH_MAX - 1, "/dev/mixer%d", n);
	}
	free(name);
	name = mixer;

	n = 1;
	for (;;) {
		if (n >= argc || *argv[n] != '-')
			break;
		if (strlen(argv[n]) != 2) {
			if (strcmp(argv[n] + 1, "rec") != 0)
				dusage = 1;
			break;
		}
		ch = *(argv[n] + 1);
		if (ch == 'f' && n < argc - 1) {
			name = argv[n + 1];
			n += 2;
		} else if (ch == 's') {
			sflag = 1;
			n++;
		} else if (ch == 'S') {
			Sflag = 1;
			n++;
		} else {
			dusage = 1;
			break;
		}
	}
	if (sflag && Sflag)
		dusage = 1;

	argc -= n - 1;
	argv += n - 1;

	if ((baz = open(name, O_RDWR)) < 0)
		err(1, "%s", name);
	if (ioctl(baz, SOUND_MIXER_READ_DEVMASK, &devmask) == -1)
		err(1, "SOUND_MIXER_READ_DEVMASK");
	if (ioctl(baz, SOUND_MIXER_READ_RECMASK, &recmask) == -1)
		err(1, "SOUND_MIXER_READ_RECMASK");
	if (ioctl(baz, SOUND_MIXER_READ_RECSRC, &recsrc) == -1)
		err(1, "SOUND_MIXER_READ_RECSRC");
	orecsrc = recsrc;

	if (argc == 1 && dusage == 0) {
		for (foo = 0, n = 0; foo < SOUND_MIXER_NRDEVICES; foo++) {
			if (!((1 << foo) & devmask))
				continue;
			if (ioctl(baz, MIXER_READ(foo),&bar) == -1) {
			   	warn("MIXER_READ");
				continue;
			}
			if (Sflag || sflag) {
				printf("%s%s%c%d:%d", n ? " " : "",
				    names[foo], Sflag ? ':' : ' ',
				    bar & 0x7f, (bar >> 8) & 0x7f);
				n++;
			} else
				printf("Mixer %-8s is currently set to "
				    "%3d:%d\n", names[foo], bar & 0x7f,
				    (bar >> 8) & 0x7f);
		}
		if (n && recmask)
			printf(" ");
		print_recsrc(recsrc, recmask, Sflag || sflag);
		return (0);
	}

	argc--;
	argv++;

	n = 0;
	while (argc > 0 && dusage == 0) {
		if (strcmp("recsrc", *argv) == 0) {
			drecsrc = 1;
			argc--;
			argv++;
			continue;
		} else if (strcmp("rec", *argv + 1) == 0) {
			if (**argv != '+' && **argv != '-' &&
			    **argv != '=' && **argv != '^') {
				warnx("unknown modifier: %c", **argv);
				dusage = 1;
				break;
			}
			if (argc <= 1) {
				warnx("no recording device specified");
				dusage = 1;
				break;
			}
			if ((dev = res_name(argv[1], recmask)) == -1) {
				warnx("unknown recording device: %s", argv[1]);
				dusage = 1;
				break;
			}
			switch (**argv) {
			case '+':
				recsrc |= (1 << dev);
				break;
			case '-':
				recsrc &= ~(1 << dev);
				break;
			case '=':
				recsrc = (1 << dev);
				break;
			case '^':
				recsrc ^= (1 << dev);
				break;
			}
			drecsrc = 1;
			argc -= 2;
			argv += 2;
			continue;
		}

		if ((t = sscanf(*argv, "%d:%d", &l, &r)) > 0)
			dev = 0;
		else if ((dev = res_name(*argv, devmask)) == -1) {
			warnx("unknown device: %s", *argv);
			dusage = 1;
			break;
		}

		lrel = rrel = 0;
		if (argc > 1) {
			m = sscanf(argv[1], "%7[^:]:%7s", lstr, rstr);
			if (m > 0) {
				if (*lstr == '+' || *lstr == '-')
					lrel = rrel = 1;
				l = strtol(lstr, NULL, 10);
			}
			if (m > 1) {
				if (*rstr == '+' || *rstr == '-')
					rrel = 1;
				r = strtol(rstr, NULL, 10);
			}
		}

		switch (argc > 1 ? m : t) {
		case 0:
			if (ioctl(baz, MIXER_READ(dev), &bar) == -1) {
				warn("MIXER_READ");
				argc--;
				argv++;
				continue;
			}
			if (Sflag || sflag) {
				printf("%s%s%c%d:%d", n ? " " : "",
				    names[dev], Sflag ? ':' : ' ',
				    bar & 0x7f, (bar >> 8) & 0x7f);
				n++;
			} else
				printf("Mixer %-8s is currently set to "
				    "%3d:%d\n", names[dev], bar & 0x7f,
				    (bar >> 8) & 0x7f);

			argc--;
			argv++;
			break;
		case 1:
			r = l;
			/* FALLTHROUGH */
		case 2:
			if (ioctl(baz, MIXER_READ(dev), &bar) == -1) {
				warn("MIXER_READ");
				argc--;
				argv++;
				continue;
			}

			if (lrel)
				l = (bar & 0x7f) + l;
			if (rrel)
				r = ((bar >> 8) & 0x7f) + r;

			if (l < 0)
				l = 0;
			else if (l > 100)
				l = 100;
			if (r < 0)
				r = 0;
			else if (r > 100)
				r = 100;

			if (!Sflag)
				printf("Setting the mixer %s from %d:%d to "
				    "%d:%d.\n", names[dev], bar & 0x7f,
				    (bar >> 8) & 0x7f, l, r);

			l |= r << 8;
			if (ioctl(baz, MIXER_WRITE(dev), &l) == -1)
				warn("WRITE_MIXER");

			argc -= 2;
			argv += 2;
 			break;
		}
	}

	if (dusage) {
		close(baz);
		usage(devmask, recmask);
		/* NOTREACHED */
	}

	if (orecsrc != recsrc) {
		if (ioctl(baz, SOUND_MIXER_WRITE_RECSRC, &recsrc) == -1)
			err(1, "SOUND_MIXER_WRITE_RECSRC");
		if (ioctl(baz, SOUND_MIXER_READ_RECSRC, &recsrc) == -1)
			err(1, "SOUND_MIXER_READ_RECSRC");
	}

	if (drecsrc)
		print_recsrc(recsrc, recmask, Sflag || sflag);

	close(baz);

	return (0);
}
