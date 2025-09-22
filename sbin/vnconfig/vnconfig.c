/*	$OpenBSD: vnconfig.c,v 1.14 2024/11/09 10:57:06 sobrado Exp $	*/
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
e */

#include <sys/param.h>	/* DEV_BSIZE */
#include <sys/ioctl.h>
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

#define DEFAULT_VND	"vnd0"

#define VND_CONFIG	1
#define VND_UNCONFIG	2
#define VND_GETINFO	3

int verbose = 0;

__dead void	 usage(void);
int		 config(char *, char *, struct disklabel *, char *, size_t);
int		 unconfig(char *);
int		 getinfo(const char *, int *availp);
char		*get_pkcs_key(char *, char *);

int
main(int argc, char **argv)
{
	int	 ch, rv, action, opt_k = 0, opt_K = 0, opt_l = 0, opt_u = 0;
	char	*key = NULL, *rounds = NULL, *saltopt = NULL;
	char	*file, *vnd;
	size_t	 keylen = 0;
	extern char *__progname;
	struct disklabel *dp = NULL;

	action = VND_CONFIG;

	while ((ch = getopt(argc, argv, "kK:lo:S:t:uv")) != -1) {
		switch (ch) {
		case 'k':
			opt_k = 1;
			break;
		case 'K':
			opt_K = 1;
			rounds = optarg;
			break;
		case 'l':
			opt_l = 1;
			break;
		case 'S':
			saltopt = optarg;
			break;
		case 't':
			dp = getdiskbyname(optarg);
			if (dp == NULL)
				errx(1, "unknown disk type: %s", optarg);
			break;
		case 'u':
			opt_u = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	if (opt_l + opt_u > 1)
		errx(1, "-l and -u are mutually exclusive options");

	if (opt_l)
		action = VND_GETINFO;
	else if (opt_u)
		action = VND_UNCONFIG;

	if (saltopt && (!opt_K))
		errx(1, "-S only makes sense when used with -K");

	if (action == VND_CONFIG) {
		if (argc == 1) {
			file = argv[0];
			vnd = NULL;
		} else if (argc == 2) {
			vnd = argv[0];
			file = argv[1];
		} else
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
		rv = config(file, vnd, dp, key, keylen);
	} else if (action == VND_UNCONFIG && argc == 1)
		rv = unconfig(argv[0]);
	else if (action == VND_GETINFO)
		rv = getinfo(argc ? argv[0] : NULL, NULL);
	else
		usage();

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
getinfo(const char *vname, int *availp)
{
	int vd, print_all = 0;
	struct vnd_user vnu;

	if (vname == NULL) {
		vname = DEFAULT_VND;
		print_all = 1;
	}

	vd = opendev(vname, O_RDONLY, OPENDEV_PART, NULL);
	if (vd == -1)
		err(1, "open: %s", vname);

	vnu.vnu_unit = -1;

query:
	if (ioctl(vd, VNDIOCGET, &vnu) == -1) {
		if (print_all && errno == ENXIO && vnu.vnu_unit > 0)
			goto end;
		err(1, "ioctl: %s", vname);
	}

	if (availp) {
		if (!vnu.vnu_ino) {
			*availp = vnu.vnu_unit;
			close(vd);
			return (0);
		}
		vnu.vnu_unit++;
		goto query;
	}

	fprintf(stdout, "vnd%d: ", vnu.vnu_unit);

	if (!vnu.vnu_ino)
		fprintf(stdout, "not in use\n");
	else
		fprintf(stdout, "covering %s on %s, inode %llu\n",
		    vnu.vnu_file, devname(vnu.vnu_dev, S_IFBLK),
		    (unsigned long long)vnu.vnu_ino);

	if (print_all) {
		vnu.vnu_unit++;
		goto query;
	}

end:
	close(vd);
	if (availp)
		return (-1);
	return (0);
}

int
config(char *file, char *dev, struct disklabel *dp, char *key, size_t keylen)
{
	struct vnd_ioctl vndio;
	char *rdev;
	int fd, rv = -1;
	int unit, print_dev = 0;

	if (dev == NULL) {
		if (getinfo(NULL, &unit) == -1)
			err(1, "no devices available");
		print_dev = 1;
		asprintf(&dev, "vnd%d", unit);
	}

	if ((fd = opendev(dev, O_RDONLY, OPENDEV_PART, &rdev)) == -1) {
		err(4, "%s", rdev);
		goto out;
	}

	memset(&vndio, 0, sizeof vndio);
	vndio.vnd_file = file;
	vndio.vnd_type = (dp && dp->d_type) ? dp->d_type : DTYPE_VND;
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
	else {
		if (print_dev)
			printf("%s\n", dev);
		if (verbose)
			fprintf(stderr, "%s: %llu bytes on %s\n", dev,
			    vndio.vnd_size, file);
	}

	close(fd);
	fflush(stdout);
 out:
	if (key)
		explicit_bzero(key, keylen);
	explicit_bzero(&vndio.vnd_keylen, sizeof vndio.vnd_keylen);
	return (rv == -1);
}

int
unconfig(char *vnd)
{
	struct vnd_ioctl vndio;
	int fd, rv = -1;
	char *rdev;

	if ((fd = opendev(vnd, O_RDONLY, OPENDEV_PART, &rdev)) == -1)
		err(4, "%s", rdev);

	memset(&vndio, 0, sizeof vndio);
	vndio.vnd_file = vnd;

	/*
	 * Clear (un-configure) the device
	 */
	rv = ioctl(fd, VNDIOCCLR, &vndio);
	if (rv)
		warn("VNDIOCCLR");
	else if (verbose)
		fprintf(stderr, "%s: cleared\n", vnd);

	close(fd);
	fflush(stdout);
	return (rv == -1);
}

__dead void
usage(void)
{
	fprintf(stderr,
	    "usage: vnconfig [-v] [-k | -K rounds [-S saltfile]] "
	    "[-t disktype] [vnd_dev]\n"
	    "                image\n"
	    "       vnconfig -l [vnd_dev]\n"
	    "       vnconfig -u [-v] vnd_dev\n");
	exit(1);
}
