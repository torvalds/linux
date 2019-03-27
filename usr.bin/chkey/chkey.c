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
 * WARRANTIES OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
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

#if 0
#ifndef lint
static char sccsid[] = "@(#)chkey.c 1.7 91/03/11 Copyr 1986 Sun Micro";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Copyright (C) 1986, Sun Microsystems, Inc.
 */

/*
 * Command to change one's public key in the public key database
 */
#include <rpc/rpc.h>
#include <rpc/key_prot.h>
#ifdef YP
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#else
#define	YPOP_STORE	4
#endif
#include <sys/fcntl.h>
#include <err.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

#ifdef YPPASSWD
struct passwd *ypgetpwuid(uid_t);
#endif

#ifdef YP
static char *domain;
static char PKMAP[] = "publickey.byname";
#else
static char PKFILE[] = "/etc/publickey";
#endif	/* YP */
static char ROOTKEY[] = "/etc/.rootkey";

static void usage(void);
extern int yp_update(char *, char *, int, char *, size_t, char *, size_t);

int
main(int argc, char **argv)
{
	char name[MAXNETNAMELEN+1];
	char public[HEXKEYBYTES + 1];
	char secret[HEXKEYBYTES + 1];
	char crypt1[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];
	char crypt2[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];
	int status;	
	char *pass;
	struct passwd *pw;
	uid_t uid;
	int force = 0;
	int ch;
#ifdef YP
	char *master;
#endif
#ifdef YPPASSWD
	char *cryptpw;
#endif

	while ((ch = getopt(argc, argv, "f")) != -1)
		switch(ch) {
		case 'f':
			force = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();

#ifdef YP
	(void)yp_get_default_domain(&domain);
	if (yp_master(domain, PKMAP, &master) != 0)
		errx(1, "can't find master of publickey database");
#endif
	uid = getuid() /*geteuid()*/;
	if (uid == 0) {
		if (host2netname(name, NULL, NULL) == 0)
			errx(1, "cannot convert hostname to netname");
	} else {
		if (user2netname(name, uid, NULL) == 0)
			errx(1, "cannot convert username to netname");
	}
	(void)printf("Generating new key for %s.\n", name);

	if (!force) {
		if (uid != 0) {
#ifdef YPPASSWD
			pw = ypgetpwuid(uid);
#else
			pw = getpwuid(uid);
#endif
			if (pw == NULL) {
#ifdef YPPASSWD
				errx(1,
			"no NIS password entry found: can't change key");
#else
				errx(1,
			"no password entry found: can't change key");
#endif
			}
		} else {
			pw = getpwuid(0);
			if (pw == NULL)
			  errx(1, "no password entry found: can't change key");
		}
	}
	pass = getpass("Password:");
#ifdef YPPASSWD
	if (!force) {
		cryptpw = crypt(pass, pw->pw_passwd);
		if (cryptpw == NULL || strcmp(cryptpw, pw->pw_passwd) != 0)
			errx(1, "invalid password");
	}
#else
	force = 1;	/* Make this mandatory */
#endif
	genkeys(public, secret, pass);	

	memcpy(crypt1, secret, HEXKEYBYTES);
	memcpy(crypt1 + HEXKEYBYTES, secret, KEYCHECKSUMSIZE);
	crypt1[HEXKEYBYTES + KEYCHECKSUMSIZE] = 0;
	xencrypt(crypt1, pass);

	if (force) {
		memcpy(crypt2, crypt1, HEXKEYBYTES + KEYCHECKSUMSIZE + 1);	
		xdecrypt(crypt2, getpass("Retype password:"));
		if (memcmp(crypt2, crypt2 + HEXKEYBYTES, KEYCHECKSUMSIZE) != 0
			|| memcmp(crypt2, secret, HEXKEYBYTES) != 0)
			errx(1, "password incorrect");
	}

#ifdef YP
	(void)printf("Sending key change request to %s...\n", master);
#endif
	status = setpublicmap(name, public, crypt1);
	if (status != 0) {
#ifdef YP
		errx(1, "unable to update NIS database (%u): %s",
				status, yperr_string(status));
#else
		errx(1, "unable to update publickey database");
#endif
	}

	if (uid == 0) {
		/*
		 * Root users store their key in /etc/$ROOTKEY so
		 * that they can auto reboot without having to be
		 * around to type a password. Storing this in a file
		 * is rather dubious: it should really be in the EEPROM
		 * so it does not go over the net.
		 */
		int fd;

		fd = open(ROOTKEY, O_WRONLY|O_TRUNC|O_CREAT, 0);
		if (fd < 0) {
			warn("%s", ROOTKEY);
		} else {
			char newline = '\n';

			if (write(fd, secret, strlen(secret)) < 0 ||
			    write(fd, &newline, sizeof(newline)) < 0)
				warn("%s: write", ROOTKEY);
		}
		close(fd);
	}

	if (key_setsecret(secret) < 0)
		errx(1, "unable to login with new secret key");
	(void)printf("Done.\n");
	exit(0);
	/* NOTREACHED */
}

static void
usage(void)
{
	(void)fprintf(stderr, "usage: chkey [-f]\n");
	exit(1);
	/* NOTREACHED */
}


/*
 * Set the entry in the public key file
 */
int
setpublicmap(char *name, char *public, char *secret)
{
	char pkent[1024];
	
	(void)sprintf(pkent,"%s:%s", public, secret);
#ifdef YP
	return (yp_update(domain, PKMAP, YPOP_STORE,
		name, strlen(name), pkent, strlen(pkent)));
#else
	return (localupdate(name, PKFILE, YPOP_STORE,
		strlen(name), name, strlen(pkent), pkent));
#endif
}

#ifdef YPPASSWD
struct passwd *
ypgetpwuid(uid_t uid)
{
	char uidstr[10];
	char *val;
	int vallen;
	static struct passwd pw;
	char *p;

	(void)sprintf(uidstr, "%d", uid);
	if (yp_match(domain, "passwd.byuid", uidstr, strlen(uidstr), 
			&val, &vallen) != 0) {
		return (NULL);
	}
	p = strchr(val, ':');
	if (p == NULL) {	
		return (NULL);
	}
	pw.pw_passwd = p + 1;
	p = strchr(pw.pw_passwd, ':');
	if (p == NULL) {
		return (NULL);
	}
	*p = 0;
	return (&pw);
}
#endif	/* YPPASSWD */
