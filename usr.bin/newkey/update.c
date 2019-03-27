/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user or with the express written consent of
 * Sun Microsystems, Inc.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTIBILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 */

#ifndef lint
#if 0
static	char sccsid[] = "@(#)update.c 1.2 91/03/11 Copyr 1986 Sun Micro";
#endif
#endif

/*
 * Copyright (C) 1986, 1989, Sun Microsystems, Inc.
 */

/*
 * Administrative tool to add a new user to the publickey database
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <rpc/rpc.h>
#include <rpc/key_prot.h>

#ifdef YP
#include <sys/wait.h>
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#include <netdb.h>
#endif	/* YP */

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#ifdef YP
static char SHELL[] = "/bin/sh";
static char YPDBPATH[]="/var/yp";	/* This is defined but not used! */
static char UPDATEFILE[] = "updaters";

static int _openchild(char *, FILE **, FILE **);
static char *basename(char *path);

/*
 * Determine if requester is allowed to update the given map,
 * and update it if so. Returns the yp status, which is zero
 * if there is no access violation.
 */
int
mapupdate(char *requester, char *mapname, u_int op, u_int keylen,
    char *key, u_int datalen, char *data)
{
	char updater[MAXMAPNAMELEN + 40];
	FILE *childargs;
	FILE *childrslt;
#ifdef WEXITSTATUS
	int status;
#else
	union wait status;
#endif
	pid_t pid;
	u_int yperrno;


#ifdef DEBUG
	printf("%s %s\n", key, data);
#endif
	(void)sprintf(updater, "make -s -f %s/%s %s", YPDBPATH, /* !!! */
					UPDATEFILE, mapname);
	pid = _openchild(updater, &childargs, &childrslt);
	if (pid < 0) {
		return (YPERR_YPERR);
	}

	/*
	 * Write to child
	 */
	(void)fprintf(childargs, "%s\n", requester);
	(void)fprintf(childargs, "%u\n", op);
	(void)fprintf(childargs, "%u\n", keylen);
	(void)fwrite(key, (int)keylen, 1, childargs);
	(void)fprintf(childargs, "\n");
	(void)fprintf(childargs, "%u\n", datalen);
	(void)fwrite(data, (int)datalen, 1, childargs);
	(void)fprintf(childargs, "\n");
	(void)fclose(childargs);

	/*
	 * Read from child
	 */
	(void)fscanf(childrslt, "%d", &yperrno);
	(void)fclose(childrslt);

	(void)wait(&status);
#ifdef WEXITSTATUS
	if (WEXITSTATUS(status) != 0) {
#else
	if (status.w_retcode != 0) {
#endif
		return (YPERR_YPERR);
	}
	return (yperrno);
}

/*
 * returns pid, or -1 for failure
 */
static pid_t
_openchild(char *command, FILE **fto, FILE **ffrom)
{
	int i;
	pid_t pid;
	int pdto[2];
	int pdfrom[2];
	char *com;
	struct rlimit rl;

	if (pipe(pdto) < 0) {
		goto error1;
	}
	if (pipe(pdfrom) < 0) {
		goto error2;
	}
	switch (pid = fork()) {
	case -1:
		goto error3;

	case 0:
		/*
		 * child: read from pdto[0], write into pdfrom[1]
		 */
		(void)close(0);
		(void)dup(pdto[0]);
		(void)close(1);
		(void)dup(pdfrom[1]);
		getrlimit(RLIMIT_NOFILE, &rl);
		for (i = rl.rlim_max - 1; i >= 3; i--) {
			(void) close(i);
		}
		com = malloc((unsigned) strlen(command) + 6);
		if (com == NULL) {
			_exit(~0);
		}
		(void)sprintf(com, "exec %s", command);
		execl(SHELL, basename(SHELL), "-c", com, (char *)NULL);
		_exit(~0);

	default:
		/*
		 * parent: write into pdto[1], read from pdfrom[0]
		 */
		*fto = fdopen(pdto[1], "w");
		(void)close(pdto[0]);
		*ffrom = fdopen(pdfrom[0], "r");
		(void)close(pdfrom[1]);
		break;
	}
	return (pid);

	/*
	 * error cleanup and return
	 */
error3:
	(void)close(pdfrom[0]);
	(void)close(pdfrom[1]);
error2:
	(void)close(pdto[0]);
	(void)close(pdto[1]);
error1:
	return (-1);
}

static char *
basename(char *path)
{
	char *p;

	p = strrchr(path, '/');
	if (p == NULL) {
		return (path);
	} else {
		return (p + 1);
	}
}

#else /* YP */

#define	ERR_ACCESS	1
#define	ERR_MALLOC	2
#define	ERR_READ	3
#define	ERR_WRITE	4
#define	ERR_DBASE	5
#define	ERR_KEY		6

static int match(char *, char *);

/*
 * Determine if requester is allowed to update the given map,
 * and update it if so. Returns the status, which is zero
 * if there is no access violation. This function updates
 * the local file and then shuts up.
 */
int
localupdate(char *name, char *filename, u_int op, u_int keylen __unused,
    char *key, u_int datalen __unused, char *data)
{
	char line[256];
	FILE *rf;
	FILE *wf;
	char *tmpname;
	int err;

	/*
	 * Check permission
	 */
	if (strcmp(name, key) != 0) {
		return (ERR_ACCESS);
	}
	if (strcmp(name, "nobody") == 0) {
		/*
		 * Can't change "nobody"s key.
		 */
		return (ERR_ACCESS);
	}

	/*
	 * Open files
	 */
	tmpname = malloc(strlen(filename) + 4);
	if (tmpname == NULL) {
		return (ERR_MALLOC);
	}
	sprintf(tmpname, "%s.tmp", filename);
	rf = fopen(filename, "r");
	if (rf == NULL) {
		err = ERR_READ;
		goto cleanup;
	}
	wf = fopen(tmpname, "w");
	if (wf == NULL) {
		fclose(rf);
		err = ERR_WRITE;
		goto cleanup;
	}
	err = -1;
	while (fgets(line, sizeof (line), rf)) {
		if (err < 0 && match(line, name)) {
			switch (op) {
			case YPOP_INSERT:
				err = ERR_KEY;
				break;
			case YPOP_STORE:
			case YPOP_CHANGE:
				fprintf(wf, "%s %s\n", key, data);
				err = 0;
				break;
			case YPOP_DELETE:
				/* do nothing */
				err = 0;
				break;
			}
		} else {
			fputs(line, wf);
		}
	}
	if (err < 0) {
		switch (op) {
		case YPOP_CHANGE:
		case YPOP_DELETE:
			err = ERR_KEY;
			break;
		case YPOP_INSERT:
		case YPOP_STORE:
			err = 0;
			fprintf(wf, "%s %s\n", key, data);
			break;
		}
	}
	fclose(wf);
	fclose(rf);
	if (err == 0) {
		if (rename(tmpname, filename) < 0) {
			err = ERR_DBASE;
			goto cleanup;
		}
	} else {
		if (unlink(tmpname) < 0) {
			err = ERR_DBASE;
			goto cleanup;
		}
	}

cleanup:
	free(tmpname);
	return (err);
}

static int
match(char *line, char *name)
{
	int len;

	len = strlen(name);
	return (strncmp(line, name, len) == 0 &&
		(line[len] == ' ' || line[len] == '\t'));
}
#endif /* !YP */
