/*	$OpenBSD: mount_vnd.c,v 1.22 2019/06/28 13:32:45 deraadt Exp $	*/
/*
 * Copyright (c) 1993 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 */

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/disklabel.h>

#include <dev/vndioctl.h>

#include <blf.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <readpassphrase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <util.h>

__dead void	 usage(void);
int		 config(char *, char *, struct disklabel *, char *, size_t);
char		*get_pkcs_key(char *, char *);

int
main(int argc, char **argv)
{
	int	 ch, rv, opt_k = 0, opt_K = 0;
	char	*key = NULL, *mntopts = NULL, *rounds = NULL, *saltopt = NULL;
	size_t	 keylen = 0;
	struct disklabel *dp = NULL;

	while ((ch = getopt(argc, argv, "kK:o:S:t:")) != -1) {
		switch (ch) {
		case 'k':
			opt_k = 1;
			break;
		case 'K':
			opt_K = 1;
			rounds = optarg;
			break;
		case 'o':
			mntopts = optarg;
			break;
		case 'S':
			saltopt = optarg;
			break;
		case 't':
			dp = getdiskbyname(optarg);
			if (dp == NULL)
				errx(1, "unknown disk type: %s", optarg);
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (saltopt && !opt_K)
		errx(1, "-S only makes sense when used with -K");

	if (argc != 2)
		usage();

	if (opt_k || opt_K)
		fprintf(stderr, "WARNING: Consider using softraid crypto.\n");
	if (opt_k) {
		if (opt_K)
			errx(1, "-k and -K are mutually exclusive");
		key = getpass("Encryption key: ");
		if (key == NULL || (keylen = strlen(key)) == 0)
			errx(1, "Need an encryption key");
	} else if (opt_K) {
		key = get_pkcs_key(rounds, saltopt);
		keylen = BLF_MAXUTILIZED;
	}
	rv = config(argv[1], argv[0], dp, key, keylen);

	exit(rv);
}

char *
get_pkcs_key(char *arg, char *saltopt)
{
	char		 passphrase[128] = {'\0'};
	char		 saltbuf[128] = {'\0'}, saltfilebuf[PATH_MAX];
	char		*key = NULL;
	char		*saltfile;
	const char	*errstr;
	int		 rounds;

	rounds = strtonum(arg, 1000, INT_MAX, &errstr);
	if (errstr)
		err(1, "rounds: %s", errstr);
	if (readpassphrase("Encryption key: ", passphrase, sizeof(passphrase),
	    RPP_REQUIRE_TTY) == NULL)
		errx(1, "Unable to read passphrase");
	if (saltopt)
		saltfile = saltopt;
	else {
		printf("Salt file: ");
		fflush(stdout);
		saltfile = fgets(saltfilebuf, sizeof(saltfilebuf), stdin);
		if (saltfile)
			saltfile[strcspn(saltfile, "\n")] = '\0';
	}
	if (!saltfile || saltfile[0] == '\0')
		warnx("Skipping salt file, insecure");
	else {
		int fd;

		fd = open(saltfile, O_RDONLY);
		if (fd == -1) {
			int *s;

			fprintf(stderr, "Salt file not found, attempting to "
			    "create one\n");
			fd = open(saltfile, O_RDWR|O_CREAT|O_EXCL, 0600);
			if (fd == -1)
				err(1, "Unable to create salt file: '%s'",
				    saltfile);
			for (s = (int *)saltbuf;
			    s < (int *)(saltbuf + sizeof(saltbuf)); s++)
				*s = arc4random();
			if (write(fd, saltbuf, sizeof(saltbuf))
			    != sizeof(saltbuf))
				err(1, "Unable to write salt file: '%s'",
				    saltfile);
			fprintf(stderr, "Salt file created as '%s'\n",
			    saltfile);
		} else {
			if (read(fd, saltbuf, sizeof(saltbuf))
			    != sizeof(saltbuf))
				err(1, "Unable to read salt file: '%s'",
				    saltfile);
		}
		close(fd);
	}
	if ((key = calloc(1, BLF_MAXUTILIZED)) == NULL)
		err(1, NULL);
	if (pkcs5_pbkdf2(passphrase, sizeof(passphrase), saltbuf,
	    sizeof (saltbuf), key, BLF_MAXUTILIZED, rounds))
		errx(1, "pkcs5_pbkdf2 failed");
	explicit_bzero(passphrase, sizeof(passphrase));

	return (key);
}

int
config(char *dev, char *file, struct disklabel *dp, char *key, size_t keylen)
{
	struct vnd_ioctl vndio;
	char *rdev;
	int fd, rv = -1;

	if ((fd = opendev(dev, O_RDONLY, OPENDEV_PART, &rdev)) == -1) {
		err(4, "%s", rdev);
		goto out;
	}

	vndio.vnd_file = file;
	vndio.vnd_secsize = (dp && dp->d_secsize) ? dp->d_secsize : DEV_BSIZE;
	vndio.vnd_nsectors = (dp && dp->d_nsectors) ? dp->d_nsectors : 100;
	vndio.vnd_ntracks = (dp && dp->d_ntracks) ? dp->d_ntracks : 1;
	vndio.vnd_key = (u_char *)key;
	vndio.vnd_keylen = keylen;

	/*
	 * Configure the device
	 */
	rv = ioctl(fd, VNDIOCSET, &vndio);
	if (rv)
		warn("VNDIOCSET");

	close(fd);
	fflush(stdout);
 out:
	if (key)
		explicit_bzero(key, keylen);
	return (rv < 0);
}

__dead void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: mount_vnd [-k] [-K rounds] [-o options] "
	    "[-S saltfile] [-t disktype]\n"
	    "\t\t image vnd_dev\n");
	exit(1);
}
