/************************************************************************
          Copyright 1988, 1991 by Carnegie Mellon University

                          All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted, provided
that the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation, and that the name of Carnegie Mellon University not be used
in advertising or publicity pertaining to distribution of the software
without specific, written prior permission.

CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

 $FreeBSD$

************************************************************************/

/*
 * bootpef - BOOTP Extension File generator
 *	Makes an "Extension File" for each host entry that
 *	defines an and Extension File. (See RFC1497, tag 18.)
 *
 * HISTORY
 *	See ./Changes
 *
 * BUGS
 *	See ./ToDo
 */



#include <stdarg.h>

#include <sys/types.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <arpa/inet.h>			/* inet_ntoa */

#ifndef	NO_UNISTD
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <syslog.h>

#ifndef	USE_BFUNCS
#include <memory.h>
/* Yes, memcpy is OK here (no overlapped copies). */
#define bcopy(a,b,c)    memcpy(b,a,c)
#define bzero(p,l)      memset(p,0,l)
#define bcmp(a,b,c)     memcmp(a,b,c)
#endif

#include "bootp.h"
#include "hash.h"
#include "hwaddr.h"
#include "bootpd.h"
#include "dovend.h"
#include "readfile.h"
#include "report.h"
#include "tzone.h"
#include "patchlevel.h"

#define	BUFFERSIZE   		0x4000

#ifndef CONFIG_FILE
#define CONFIG_FILE		"/etc/bootptab"
#endif



/*
 * Externals, forward declarations, and global variables
 */

static void mktagfile(struct host *);
static void usage(void);

/*
 * General
 */

char *progname;
char *chdir_path;
int debug = 0;					/* Debugging flag (level) */
byte *buffer;

/*
 * Globals below are associated with the bootp database file (bootptab).
 */

char *bootptab = CONFIG_FILE;


/*
 * Print "usage" message and exit
 */
static void
usage()
{
	fprintf(stderr,
	   "usage:  $s [ -c chdir ] [-d level] [-f configfile] [host...]\n");
	fprintf(stderr, "\t -c n\tset current directory\n");
	fprintf(stderr, "\t -d n\tset debug level\n");
	fprintf(stderr, "\t -f n\tconfig file name\n");
	exit(1);
}


/*
 * Initialization such as command-line processing is done and then the
 * main server loop is started.
 */
int
main(argc, argv)
	int argc;
	char **argv;
{
	struct host *hp;
	char *stmp;
	int n;

	progname = strrchr(argv[0], '/');
	if (progname) progname++;
	else progname = argv[0];

	/* Get work space for making tag 18 files. */
	buffer = (byte *) malloc(BUFFERSIZE);
	if (!buffer) {
		report(LOG_ERR, "malloc failed");
		exit(1);
	}
	/*
	 * Set defaults that might be changed by option switches.
	 */
	stmp = NULL;

	/*
	 * Read switches.
	 */
	for (argc--, argv++; argc > 0; argc--, argv++) {
		if (argv[0][0] != '-')
			break;
		switch (argv[0][1]) {

		case 'c':				/* chdir_path */
			if (argv[0][2]) {
				stmp = &(argv[0][2]);
			} else {
				argc--;
				argv++;
				stmp = argv[0];
			}
			if (!stmp || (stmp[0] != '/')) {
				fprintf(stderr,
						"bootpd: invalid chdir specification\n");
				break;
			}
			chdir_path = stmp;
			break;

		case 'd':				/* debug */
			if (argv[0][2]) {
				stmp = &(argv[0][2]);
			} else if (argv[1] && argv[1][0] == '-') {
				/*
				 * Backwards-compatible behavior:
				 * no parameter, so just increment the debug flag.
				 */
				debug++;
				break;
			} else {
				argc--;
				argv++;
				stmp = argv[0];
			}
			if (!stmp || (sscanf(stmp, "%d", &n) != 1) || (n < 0)) {
				fprintf(stderr,
						"bootpd: invalid debug level\n");
				break;
			}
			debug = n;
			break;

		case 'f':				/* config file */
			if (argv[0][2]) {
				stmp = &(argv[0][2]);
			} else {
				argc--;
				argv++;
				stmp = argv[0];
			}
			bootptab = stmp;
			break;

		default:
			fprintf(stderr, "bootpd: unknown switch: -%c\n",
					argv[0][1]);
			usage();
			break;
		}
	}

	/* Get the timezone. */
	tzone_init();

	/* Allocate hash tables. */
	rdtab_init();

	/*
	 * Read the bootptab file.
	 */
	readtab(1);					/* force read */

	/* Set the cwd (i.e. to /tftpboot) */
	if (chdir_path) {
		if (chdir(chdir_path) < 0)
			report(LOG_ERR, "%s: chdir failed", chdir_path);
	}
	/* If there are host names on the command line, do only those. */
	if (argc > 0) {
		unsigned int tlen, hashcode;

		while (argc) {
			tlen = strlen(argv[0]);
			hashcode = hash_HashFunction((u_char *)argv[0], tlen);
			hp = (struct host *) hash_Lookup(nmhashtable,
											 hashcode,
											 nmcmp, argv[0]);
			if (!hp) {
				printf("%s: no matching entry\n", argv[0]);
				exit(1);
			}
			if (!hp->flags.exten_file) {
				printf("%s: no extension file\n", argv[0]);
				exit(1);
			}
			mktagfile(hp);
			argv++;
			argc--;
		}
		exit(0);
	}
	/* No host names specified.  Do them all. */
	hp = (struct host *) hash_FirstEntry(nmhashtable);
	while (hp != NULL) {
		mktagfile(hp);
		hp = (struct host *) hash_NextEntry(nmhashtable);
	}
	return (0);
}



/*
 * Make a "TAG 18" file for this host.
 * (Insert the RFC1497 options.)
 */

static void
mktagfile(hp)
	struct host *hp;
{
	FILE *fp;
	int bytesleft, len;
	byte *vp;

	if (!hp->flags.exten_file)
		return;

	vp = buffer;
	bytesleft = BUFFERSIZE;
	bcopy(vm_rfc1048, vp, 4);	/* Copy in the magic cookie */
	vp += 4;
	bytesleft -= 4;

	/*
	 * The "extension file" options are appended by the following
	 * function (which is shared with bootpd.c).
	 */
	len = dovend_rfc1497(hp, vp, bytesleft);
	vp += len;
	bytesleft -= len;

	if (bytesleft < 1) {
		report(LOG_ERR, "%s: too much option data",
			   hp->exten_file->string);
		return;
	}
	*vp++ = TAG_END;
	bytesleft--;

	/* Write the buffer to the extension file. */
	printf("Updating \"%s\"\n", hp->exten_file->string);
	if ((fp = fopen(hp->exten_file->string, "w")) == NULL) {
		report(LOG_ERR, "error opening \"%s\": %s",
			   hp->exten_file->string, get_errmsg());
		return;
	}
	len = vp - buffer;
	if (len != fwrite(buffer, 1, len, fp)) {
		report(LOG_ERR, "write failed on \"%s\" : %s",
			   hp->exten_file->string, get_errmsg());
	}
	fclose(fp);

} /* mktagfile */

/*
 * Local Variables:
 * tab-width: 4
 * c-indent-level: 4
 * c-argdecl-indent: 4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: -4
 * c-label-offset: -4
 * c-brace-offset: 0
 * End:
 */
