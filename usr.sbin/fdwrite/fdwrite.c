/*-
 * SPDX-License-Identifier: Beerware
 *
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD$
 *
 */

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/fdcio.h>

static int
format_track(int fd, int cyl, int secs, int head, int rate,
     int gaplen, int secsize, int fill, int interleave)
{
    struct fd_formb f;
    register int i,j;
    int il[100];

    memset(il,0,sizeof il);
    for(j = 0, i = 1; i <= secs; i++) {
	while(il[(j%secs)+1]) j++;
	il[(j%secs)+1] = i;
	j += interleave;
    }

    f.format_version = FD_FORMAT_VERSION;
    f.head = head;
    f.cyl = cyl;
    f.transfer_rate = rate;

    f.fd_formb_secshift = secsize;
    f.fd_formb_nsecs = secs;
    f.fd_formb_gaplen = gaplen;
    f.fd_formb_fillbyte = fill;
    for(i = 0; i < secs; i++) {
	f.fd_formb_cylno(i) = cyl;
	f.fd_formb_headno(i) = head;
	f.fd_formb_secno(i) = il[i+1];
	f.fd_formb_secsize(i) = secsize;
    }
    return ioctl(fd, FD_FORM, (caddr_t)&f);
}

static void
usage(void)
{
	fprintf(stderr, "usage: fdwrite [-v] [-y] [-f inputfile] [-d device]\n");
	exit(2);
}

int
main(int argc, char **argv)
{
    int inputfd = -1, c, fdn = 0, i,j,fd;
    int bpt, verbose=1, nbytes=0, track;
    int interactive = 1;
    const char *device= "/dev/fd0";
    char *trackbuf = 0,*vrfybuf = 0;
    struct fd_type fdt;
    FILE *tty;

    setbuf(stdout,0);
    while((c = getopt(argc, argv, "d:f:vy")) != -1)
	    switch(c) {
	    case 'd':	/* Which drive */
		    device = optarg;
		    break;

	    case 'f':	/* input file */
		    if (inputfd >= 0)
			    close(inputfd);
		    inputfd = open(optarg,O_RDONLY);
		    if (inputfd < 0)
			    err(1, "%s", optarg);
		    break;

	    case 'v':  /* Toggle verbosity */
		    verbose = !verbose;
		    break;

	    case 'y':  /* Don't confirm? */
		    interactive = 0;
		    break;

	    case '?': default:
		    usage();
	    }

    if (inputfd < 0)
	inputfd = 0;

    if (!isatty(1))
	interactive = 0;

    if(optind < argc)
	    usage();

    tty = fopen(_PATH_TTY,"r+");
    if(!tty)
	    err(1, _PATH_TTY);
    setbuf(tty,0);

    for(j=1;j > 0;) {
        fdn++;
	if (interactive) {
	    fprintf(tty,
		    "Please insert floppy #%d in drive %s and press return >",
		    fdn,device);
	    while(1) {
		i = getc(tty);
		if(i == '\n') break;
	    }
	}

	if((fd = open(device, O_RDWR)) < 0)
	    err(1, "%s", device);

	if(ioctl(fd, FD_GTYPE, &fdt) < 0)
	    errx(1, "not a floppy disk: %s", device);

	bpt = fdt.sectrac * (1<<fdt.secsize) * 128;
	if(!trackbuf) {
	    trackbuf = malloc(bpt);
	    if(!trackbuf) errx(1, "malloc");
	}
	if(!vrfybuf) {
	    vrfybuf = malloc(bpt);
	    if(!vrfybuf) errx(1, "malloc");
	}

	if(fdn == 1) {
	    if(verbose) {
		printf("Format: %d cylinders, %d heads, %d sectors, %d bytes = %dkb\n",
		fdt.tracks,fdt.heads,fdt.sectrac,(1<<fdt.secsize) * 128,
		fdt.tracks*bpt*fdt.heads/1024);

	    }
	    memset(trackbuf,0,bpt);
	    for(j=0;inputfd >= 0 && j<bpt;j+=i) {
		if(!(i = read(inputfd,trackbuf+j,bpt-j))) {
		    close(inputfd);
		    inputfd = -1;
		    break;
		}
		nbytes += i;
	    }
	}
	for (track = 0; track < fdt.tracks * fdt.heads; track++) {
	    if(verbose) printf("\r%3d ",fdt.tracks * fdt.heads-track);
	    if(verbose) putc((j ? 'I':'Z'),stdout);
	    format_track(fd, track / fdt.heads, fdt.sectrac, track % fdt.heads,
		    fdt.trans, fdt.f_gap, fdt.secsize, 0xe6,
		    fdt.f_inter);
	    if(verbose) putc('F',stdout);

	    if (lseek (fd, (long) track*bpt, 0) < 0) err(1, "lseek");
	    if (write (fd, trackbuf, bpt) != bpt) err(1, "write");
	    if(verbose) putc('W',stdout);

	    if (lseek (fd, (long) track*bpt, 0) < 0) err(1, "lseek");
	    if (read (fd, vrfybuf, bpt) != bpt) err(1, "read");
	    if(verbose) putc('R',stdout);

	    if (memcmp(trackbuf,vrfybuf,bpt)) err(1, "compare");
	    if(verbose) putc('C',stdout);

	    memset(trackbuf,0,bpt);
	    for(j=0;inputfd >= 0 && j<bpt;j+=i) {
		if(!(i = read(inputfd,trackbuf+j,bpt-j))) {
		    close(inputfd);
		    inputfd = -1;
		    break;
		}
		nbytes += i;
	    }
	}
	close(fd);
	putc('\r',stdout);
    }
    if(verbose)
	printf("%d bytes on %d flopp%s\n",nbytes,fdn,fdn==1?"y":"ies");
    exit(0);
}
