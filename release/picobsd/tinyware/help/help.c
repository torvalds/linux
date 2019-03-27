/*-
 * Copyright (c) 1998 Eric P. Scott <eps@sirius.com>
 * Copyright (c) 1998 Andrzej Bialecki <abial@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */


#include <stdio.h>
#include <string.h>
#include <ar.h>
#include <sys/ioctl.h>

int display(FILE *, const char *);

static int cnt, crt=-1;

int
main(int argc, char *argv[])
{
	register int i, s;
	FILE *fd;
	struct ttysize ts;

	if (!(fd=fopen("/help.a", "r"))) {
		(void)fputs("Couldn't open help archive.\n", stderr);
		exit(1);
	}
	cnt=0;
	if (ioctl(fileno(stdout), TIOCGWINSZ, &ts)>=0) {
		crt=ts.ts_lines-1;
	}
	if (crt<3) crt=23;
	s=display(fd, argc>1 ? argv[1] : "help");
	if (s<0) s=0;
	else for (i=2;i<argc;) {
		rewind(fd);
		s|=display(fd, argv[i++]);
		if (s<0) {
			s=0;
			break;
		}
	}
	(void)fclose(fd);
	exit(s);
}

int
more(void)
{
	char buf[8];

	(void)fflush(stdout);
	(void)fputs("\033[7mPress Enter to continue\033[m", stderr);
	(void)fflush(stderr);
	cnt=0;
	if (fgets(buf, sizeof buf, stdin)) return 0;
	(void)fputc('\n', stderr);
	return 1;
}

int
display(FILE *fd, const char *fname)
{
	register char *p;
	register int c, n, o;
	struct ar_hdr ar;
	char aname[20];

	if (!fgets(aname, sizeof aname, fd)) {
		return 1;
	}
	if (strncmp(aname, ARMAG, SARMAG)) return 1;
	(void)snprintf(aname, sizeof(aname), "%s/", fname);
	for (;;) {
		if (fread((void *)&ar, sizeof ar, 1, fd)!=1) return 1;
		if (strncmp(ar.ar_fmag, ARFMAG, 2)) return 1;
		n=0;
		p=ar.ar_size;
		do {
			if ((c=(int)(*p++-'0'))<0||c>9) break;
			n*=10; n+=c;
		} while (p<&ar.ar_size[sizeof ar.ar_size]);
		if (!strncmp(ar.ar_name, aname, strlen(aname))) break;
		if (fseek(fd, (long)n, SEEK_CUR)<0) return 1;
		if ((n&1)&&fgetc(fd)!='\n') return 1;
	}
	if (cnt>=crt&&more()) return -1;
	(void)fputc('\n', stdout);
	cnt++;
	o=0; while (o<n&&(c=fgetc(fd))!=EOF) {
	per:
		o++;
		(void)fputc(c, stdout);
		if (c!='\n') continue;
		if (++cnt<crt) continue;
		if (o>=n||(c=fgetc(fd))==EOF) break;
		if (more()) return -1;
		goto per;
	}
	if (cnt>=crt&&more()) return -1;
	(void)fputc('\n', stdout);
	cnt++;
	if (!strcmp(fname, "help")) {
		rewind(fd);
		(void)fgets(aname, sizeof aname, fd);
		if (cnt>=crt&&more()) return -1;
		(void)fputs("The following help items are available:\n",
			stdout);
		cnt++;
		o=0;
		while (fread((void *)&ar, sizeof ar, 1, fd)==1) {
			if (strncmp(ar.ar_fmag, ARFMAG, 2)) break;
			if ((o%6)==0) {
				(void)fputc('\n', stdout);
				if (++cnt>=crt&&more()) return -1;
			}
			*(index(ar.ar_name,'/'))=' ';
			(void)printf("%.13s", ar.ar_name);
			++o;
			n=0;
			p=ar.ar_size;
			do {
				if ((c=(int)(*p++-'0'))<0||c>9) break;
				n*=10; n+=c;
			} while (p<&ar.ar_size[sizeof ar.ar_size]);
			if (fseek(fd, (long)n, SEEK_CUR)<0) break;
			if ((n&1)&&fgetc(fd)!='\n') break;
		}
		if (cnt>=crt&&more()) return -1;
		(void)fputc('\n', stdout);
		cnt++;
	}
	return 0;
}
