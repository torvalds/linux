/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995, 1996
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <ctype.h>
#include <db.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libgen.h>
#include <libutil.h>

#include <rpc/rpc.h>
#include <rpcsvc/yp.h>
struct dom_binding;
#include <rpcsvc/ypclnt.h>
#include "yppasswdd_extern.h"
#include "yppasswd.h"
#include "yppasswd_private.h"
#include "ypxfr_extern.h"
#include "yp_extern.h"

static struct passwd yp_password;

static void
xlate_passwd(struct x_master_passwd *xpwd, struct passwd *pwd)
{
	pwd->pw_name = xpwd->pw_name;
	pwd->pw_passwd = xpwd->pw_passwd;
	pwd->pw_uid = xpwd->pw_uid;
	pwd->pw_gid = xpwd->pw_gid;
	pwd->pw_change = xpwd->pw_change;
	pwd->pw_class = xpwd->pw_class;
	pwd->pw_gecos = xpwd->pw_gecos;
	pwd->pw_dir = xpwd->pw_dir;
	pwd->pw_shell = xpwd->pw_shell;
	pwd->pw_expire = xpwd->pw_expire;
	pwd->pw_fields = xpwd->pw_fields;
}

static void
copy_yp_pass(char *p, int x, int m)
{
	char *t, *s = p;
	static char *buf;

	yp_password.pw_fields = 0;

	buf = realloc(buf, m + 10);
	bzero(buf, m + 10);

	/* Turn all colons into NULLs */
	while (strchr(s, ':')) {
		s = (strchr(s, ':') + 1);
		*(s - 1)= '\0';
	}

	t = buf;
#define EXPAND(e) do { \
	e = t; \
	while ((*t++ = *p++)); \
} while (0)
        EXPAND(yp_password.pw_name);
	yp_password.pw_fields |= _PWF_NAME;
        EXPAND(yp_password.pw_passwd);
	yp_password.pw_fields |= _PWF_PASSWD;
	yp_password.pw_uid = atoi(p);
        p += (strlen(p) + 1);
	yp_password.pw_fields |= _PWF_UID;
	yp_password.pw_gid = atoi(p);
        p += (strlen(p) + 1);
	yp_password.pw_fields |= _PWF_GID;
	if (x) {
		EXPAND(yp_password.pw_class);
		yp_password.pw_fields |= _PWF_CLASS;
		yp_password.pw_change = atol(p);
		p += (strlen(p) + 1);
		yp_password.pw_fields |= _PWF_CHANGE;
		yp_password.pw_expire = atol(p);
		p += (strlen(p) + 1);
		yp_password.pw_fields |= _PWF_EXPIRE;
	}
        EXPAND(yp_password.pw_gecos);
	yp_password.pw_fields |= _PWF_GECOS;
        EXPAND(yp_password.pw_dir);
	yp_password.pw_fields |= _PWF_DIR;
        EXPAND(yp_password.pw_shell);
	yp_password.pw_fields |= _PWF_SHELL;

	return;
}

static int
validchars(char *arg)
{
	size_t i;

	for (i = 0; i < strlen(arg); i++) {
		if (iscntrl(arg[i])) {
			yp_error("string contains a control character");
			return(1);
		}
		if (arg[i] == ':') {
			yp_error("string contains a colon");
			return(1);
		}
		/* Be evil: truncate strings with \n in them silently. */
		if (arg[i] == '\n') {
			arg[i] = '\0';
			return(0);
		}
	}
	return(0);
}

static int
validate_master(struct passwd *opw __unused, struct x_master_passwd *npw)
{

	if (npw->pw_name[0] == '+' || npw->pw_name[0] == '-') {
		yp_error("client tried to modify an NIS entry");
		return(1);
	}

	if (validchars(npw->pw_shell)) {
		yp_error("specified shell contains invalid characters");
		return(1);
	}

	if (validchars(npw->pw_gecos)) {
		yp_error("specified gecos field contains invalid characters");
		return(1);
	}

	if (validchars(npw->pw_passwd)) {
		yp_error("specified password contains invalid characters");
		return(1);
	}
	return(0);
}

static int
validate(struct passwd *opw, struct x_passwd *npw)
{

	if (npw->pw_name[0] == '+' || npw->pw_name[0] == '-') {
		yp_error("client tried to modify an NIS entry");
		return(1);
	}

	if ((uid_t)npw->pw_uid != opw->pw_uid) {
		yp_error("UID mismatch: client says user %s has UID %d",
			 npw->pw_name, npw->pw_uid);
		yp_error("database says user %s has UID %d", opw->pw_name,
			 opw->pw_uid);
		return(1);
	}

	if ((gid_t)npw->pw_gid != opw->pw_gid) {
		yp_error("GID mismatch: client says user %s has GID %d",
			 npw->pw_name, npw->pw_gid);
		yp_error("database says user %s has GID %d", opw->pw_name,
			 opw->pw_gid);
		return(1);
	}

	/*
	 * Don't allow the user to shoot himself in the foot,
	 * even on purpose.
	 */
	if (!no_chsh && !ok_shell(npw->pw_shell)) {
		yp_error("%s is not a valid shell", npw->pw_shell);
		return(1);
	}

	if (!no_chsh && validchars(npw->pw_shell)) {
		yp_error("specified shell contains invalid characters");
		return(1);
	}

	if (validchars(npw->pw_gecos)) {
		yp_error("specified gecos field contains invalid characters");
		return(1);
	}

	if (validchars(npw->pw_passwd)) {
		yp_error("specified password contains invalid characters");
		return(1);
	}
	return(0);
}

/*
 * Kludge alert:
 * In order to have one rpc.yppasswdd support multiple domains,
 * we have to cheat: we search each directory under /var/yp
 * and try to match the user in each master.passwd.byname
 * map that we find. If the user matches (username, uid and gid
 * all agree), then we use that domain. If we match the user in
 * more than one database, we must abort.
 */
static char *
find_domain(struct x_passwd *pw)
{
	struct stat statbuf;
	struct dirent *dirp;
	DIR *dird;
	char yp_mapdir[MAXPATHLEN + 2];
	static char domain[YPMAXDOMAIN];
	char *tmp = NULL;
	DBT key, data;
	int hit = 0;

	yp_error("performing multidomain lookup");

	if ((dird = opendir(yp_dir)) == NULL) {
		yp_error("opendir(%s) failed: %s", yp_dir, strerror(errno));
		return(NULL);
	}

	while ((dirp = readdir(dird)) != NULL) {
		snprintf(yp_mapdir, sizeof yp_mapdir, "%s/%s",
							yp_dir, dirp->d_name);
		if (stat(yp_mapdir, &statbuf) < 0) {
			yp_error("stat(%s) failed: %s", yp_mapdir,
							strerror(errno));
			closedir(dird);
			return(NULL);
		}
		if (S_ISDIR(statbuf.st_mode)) {
			tmp = (char *)dirp->d_name;
			key.data = pw->pw_name;
			key.size = strlen(pw->pw_name);

			if (yp_get_record(tmp,"master.passwd.byname",
			  		&key, &data, 0) != YP_TRUE) {
				continue;
			}
			*((char *)data.data + data.size) = '\0';
			copy_yp_pass(data.data, 1, data.size);
			if (yp_password.pw_uid == (uid_t)pw->pw_uid &&
			    yp_password.pw_gid == (gid_t)pw->pw_gid) {
				hit++;
				snprintf(domain, YPMAXDOMAIN, "%s", tmp);
			}
		}
	}

	closedir(dird);
	if (hit > 1) {
		yp_error("found same user in two different domains");
		return(NULL);
	} else
		return((char *)&domain);
}

static const char *maps[] = {
	"master.passwd.byname",
	"master.passwd.byuid",
	"passwd.byname",
	"passwd.byuid"
};

static const char *formats[] = {
	"%s:%s:%d:%d:%s:%ld:%ld:%s:%s:%s",
	"%s:%s:%d:%d:%s:%ld:%ld:%s:%s:%s",
	"%s:%s:%d:%d:%s:%s:%s",
	"%s:%s:%d:%d:%s:%s:%s"
};

static int
update_inplace(struct passwd *pw, char *domain)
{
	DB *dbp = NULL;
	DBT key = { NULL, 0 };
	DBT data = { NULL, 0 };
	char pwbuf[YPMAXRECORD];
	char keybuf[20];
	int i;
	char *ptr = NULL;
	static char yp_last[] = "YP_LAST_MODIFIED";
	char yplastbuf[YPMAXRECORD];

	snprintf(yplastbuf, sizeof yplastbuf, "%llu",
	    (unsigned long long)time(NULL));

	for (i = 0; i < 4; i++) {

		if (i % 2) {
			snprintf(keybuf, sizeof keybuf,
			    "%llu", (unsigned long long)pw->pw_uid);
			key.data = &keybuf;
			key.size = strlen(keybuf);
		} else {
			key.data = pw->pw_name;
			key.size = strlen(pw->pw_name);
		}

		/*
		 * XXX The passwd.byname and passwd.byuid maps come in
		 * two flavors: secure and insecure. The secure version
		 * has a '*' in the password field whereas the insecure one
		 * has a real crypted password. The maps will be insecure
		 * if they were built with 'unsecure = TRUE' enabled in
		 * /var/yp/Makefile, but we'd have no way of knowing if
		 * this has been done unless we were to try parsing the
		 * Makefile, which is a disgusting thought. Instead, we
		 * read the records from the maps, skip to the first ':'
		 * in them, and then look at the character immediately
		 * following it. If it's an '*' then the map is 'secure'
		 * and we must not insert a real password into the pw_passwd
		 * field. If it's not an '*', then we put the real crypted
		 * password in.
		 */
		if (yp_get_record(domain,maps[i],&key,&data,1) != YP_TRUE) {
			yp_error("couldn't read %s/%s: %s", domain,
						maps[i], strerror(errno));
			return(1);
		}

		if ((ptr = strchr(data.data, ':')) == NULL) {
			yp_error("no colon in passwd record?!");
			return(1);
		}

		/*
		 * XXX Supposing we have more than one user with the same
		 * UID? (Or more than one user with the same name?) We could
		 * end up modifying the wrong record if were not careful.
		 */
		if (i % 2) {
			if (strncmp(data.data, pw->pw_name,
							strlen(pw->pw_name))) {
				yp_error("warning: found entry for UID %d \
in map %s@%s with wrong name (%.*s)", pw->pw_uid, maps[i], domain,
				    (int)(ptr - (char *)data.data),
				    (char *)data.data);
				yp_error("there may be more than one user \
with the same UID - continuing");
				continue;
			}
		} else {
			/*
			 * We're really being ultra-paranoid here.
			 * This is generally a 'can't happen' condition.
			 */
			snprintf(pwbuf, sizeof pwbuf, ":%d:%d:", pw->pw_uid,
								  pw->pw_gid);
			if (!strstr(data.data, pwbuf)) {
				yp_error("warning: found entry for user %s \
in map %s@%s with wrong UID", pw->pw_name, maps[i], domain);
				yp_error("there may be more than one user \
with the same name - continuing");
				continue;
			}
		}

		if (i < 2) {
			snprintf(pwbuf, sizeof pwbuf, formats[i],
			   pw->pw_name, pw->pw_passwd, pw->pw_uid,
			   pw->pw_gid, pw->pw_class, pw->pw_change,
			   pw->pw_expire, pw->pw_gecos, pw->pw_dir,
			   pw->pw_shell);
		} else {
			snprintf(pwbuf, sizeof pwbuf, formats[i],
			   pw->pw_name, *(ptr+1) == '*' ? "*" : pw->pw_passwd,
			   pw->pw_uid, pw->pw_gid, pw->pw_gecos, pw->pw_dir,
			   pw->pw_shell);
		}

#define FLAGS O_RDWR|O_CREAT

		if ((dbp = yp_open_db_rw(domain, maps[i], FLAGS)) == NULL) {
			yp_error("couldn't open %s/%s r/w: %s",domain,
						maps[i],strerror(errno));
			return(1);
		}

		data.data = pwbuf;
		data.size = strlen(pwbuf);

		if (yp_put_record(dbp, &key, &data, 1) != YP_TRUE) {
			yp_error("failed to update record in %s/%s", domain,
								maps[i]);
			(void)(dbp->close)(dbp);
			return(1);
		}

		key.data = yp_last;
		key.size = strlen(yp_last);
		data.data = (char *)&yplastbuf;
		data.size = strlen(yplastbuf);

		if (yp_put_record(dbp, &key, &data, 1) != YP_TRUE) {
			yp_error("failed to update timestamp in %s/%s", domain,
								maps[i]);
			(void)(dbp->close)(dbp);
			return(1);
		}

		(void)(dbp->close)(dbp);
	}

	return(0);
}

int *
yppasswdproc_update_1_svc(yppasswd *argp, struct svc_req *rqstp)
{
	static int  result;
	struct sockaddr_in *rqhost;
	DBT key, data;
	int rval = 0;
	int pfd, tfd;
	int pid;
	int passwd_changed = 0;
	int shell_changed = 0;
	int gecos_changed = 0;
	char *cryptpw;
	char *oldshell = NULL;
	char *oldgecos = NULL;
	char *passfile_hold;
	char passfile_buf[MAXPATHLEN + 2];
	char passfile_hold_buf[MAXPATHLEN + 2];
	char *domain = yppasswd_domain;
	static struct sockaddr_in clntaddr;
	static struct timeval t_saved, t_test;

	/*
	 * Normal user updates always use the 'default' master.passwd file.
	 */

	passfile = passfile_default;
	result = 1;

	rqhost = svc_getcaller(rqstp->rq_xprt);

	gettimeofday(&t_test, NULL);
	if (!bcmp(rqhost, &clntaddr, sizeof *rqhost) &&
		t_test.tv_sec > t_saved.tv_sec &&
		t_test.tv_sec - t_saved.tv_sec < 300) {

		bzero(&clntaddr, sizeof clntaddr);
		bzero(&t_saved, sizeof t_saved);
		return(NULL);
	}

	bcopy(rqhost, &clntaddr, sizeof clntaddr);
	gettimeofday(&t_saved, NULL);

	if (yp_access(resvport ? "master.passwd.byname" : NULL, rqstp)) {
		yp_error("rejected update request from unauthorized host");
		svcerr_auth(rqstp->rq_xprt, AUTH_BADCRED);
		return(&result);
	}

	/*
	 * Step one: find the user. (It's kinda pointless to
	 * proceed if the user doesn't exist.) We look for the
	 * user in the master.passwd.byname database, _NOT_ by
	 * using getpwent() and friends! We can't use getpwent()
	 * since the NIS master server is not guaranteed to be
	 * configured as an NIS client.
	 */

	if (multidomain) {
		if ((domain = find_domain(&argp->newpw)) == NULL) {
			yp_error("multidomain lookup failed - aborting update");
			return(&result);
		} else
			yp_error("updating user %s in domain %s",
					argp->newpw.pw_name, domain);
	}

	key.data = argp->newpw.pw_name;
	key.size = strlen(argp->newpw.pw_name);

	if ((rval = yp_get_record(domain,"master.passwd.byname",
		  	&key, &data, 0)) != YP_TRUE) {
		if (rval == YP_NOKEY) {
			yp_error("user %s not found in passwd database",
			 	argp->newpw.pw_name);
		} else {
			yp_error("database access error: %s",
			 	yperr_string(rval));
		}
		return(&result);
	}

	/* Nul terminate, please. */
	*((char *)data.data + data.size) = '\0';

	copy_yp_pass(data.data, 1, data.size);

	/* Step 2: check that the supplied oldpass is valid. */

	cryptpw = crypt(argp->oldpass, yp_password.pw_passwd);
	if (cryptpw == NULL || strcmp(cryptpw, yp_password.pw_passwd)) {
		yp_error("rejected change attempt -- bad password");
		yp_error("client address: %s username: %s",
			  inet_ntoa(rqhost->sin_addr),
			  argp->newpw.pw_name);
		return(&result);
	}

	/* Step 3: validate the arguments passed to us by the client. */

	if (validate(&yp_password, &argp->newpw)) {
		yp_error("rejecting change attempt: bad arguments");
		yp_error("client address: %s username: %s",
			 inet_ntoa(rqhost->sin_addr),
			 argp->newpw.pw_name);
		svcerr_decode(rqstp->rq_xprt);
		return(&result);
	}

	/* Step 4: update the user's passwd structure. */

	if (!no_chsh && strcmp(argp->newpw.pw_shell, yp_password.pw_shell)) {
		oldshell = yp_password.pw_shell;
		yp_password.pw_shell = argp->newpw.pw_shell;
		shell_changed++;
	}


	if (!no_chfn && strcmp(argp->newpw.pw_gecos, yp_password.pw_gecos)) {
		oldgecos = yp_password.pw_gecos;
		yp_password.pw_gecos = argp->newpw.pw_gecos;
		gecos_changed++;
	}

	if (strcmp(argp->newpw.pw_passwd, yp_password.pw_passwd)) {
		yp_password.pw_passwd = argp->newpw.pw_passwd;
		yp_password.pw_change = 0;
		passwd_changed++;
	}

	/*
	 * If the caller specified a domain other than our 'default'
	 * domain, change the path to master.passwd accordingly.
	 */

	if (strcmp(domain, yppasswd_domain)) {
		snprintf(passfile_buf, sizeof(passfile_buf),
			"%s/%s/master.passwd", yp_dir, domain);
		passfile = (char *)&passfile_buf;
	}

	/*
	 * Create a filename to hold the original master.passwd
	 * so if our call to yppwupdate fails we can roll back
	 */
	snprintf(passfile_hold_buf, sizeof(passfile_hold_buf),
	    "%s.hold", passfile);
	passfile_hold = (char *)&passfile_hold_buf;
	

	/* Step 5: make a new password file with the updated info. */

	if (pw_init(dirname(passfile), passfile)) {
		yp_error("pw_init() failed");
		return &result;
	}
	if ((pfd = pw_lock()) == -1) {
		pw_fini();
		yp_error("pw_lock() failed");
		return &result;
	}
	if ((tfd = pw_tmp(-1)) == -1) {
		pw_fini();
		yp_error("pw_tmp() failed");
		return &result;
	}
	if (pw_copy(pfd, tfd, &yp_password, NULL) == -1) {
		pw_fini();
		yp_error("pw_copy() failed");
		return &result;
	}
	if (rename(passfile, passfile_hold) == -1) {
		pw_fini();
		yp_error("rename of %s to %s failed", passfile,
		    passfile_hold);
		return &result;
	}

	if (strcmp(passfile, _PATH_MASTERPASSWD) == 0) { 
		/*
		 * NIS server is exporting the system's master.passwd.
		 * Call pw_mkdb to rebuild passwd and the .db files
		 */
		if (pw_mkdb(yp_password.pw_name) == -1) {
			pw_fini();
			yp_error("pw_mkdb() failed");
			rename(passfile_hold, passfile);
			return &result;
		}
	} else {
		/*
		 * NIS server is exporting a private master.passwd.
		 * Rename tempfile into final location
		 */
		if (rename(pw_tempname(), passfile) == -1) {
			pw_fini();
			yp_error("rename of %s to %s failed",
			    pw_tempname(), passfile);
			rename(passfile_hold, passfile);
			return &result;
		}
	}

	pw_fini();

	if (inplace) {
		if ((rval = update_inplace(&yp_password, domain))) {
			yp_error("inplace update failed -- rebuilding maps");
		}
	}

	switch ((pid = fork())) {
	case 0:
		if (inplace && !rval) {
    			execlp(MAP_UPDATE_PATH, MAP_UPDATE, passfile,
				yppasswd_domain, "pushpw", (char *)NULL);
		} else {
    			execlp(MAP_UPDATE_PATH, MAP_UPDATE, passfile,
				yppasswd_domain, (char *)NULL);
		}
    		yp_error("couldn't exec map update process: %s",
					strerror(errno));
		unlink(passfile);
		rename(passfile_hold, passfile);
    		exit(1);
		break;
	case -1:
		yp_error("fork() failed: %s", strerror(errno));
		unlink(passfile);
		rename(passfile_hold, passfile);
		return(&result);
		break;
	default:
		unlink(passfile_hold);
		break;
	}

	if (verbose) {
		yp_error("update completed for user %s (uid %d) in %s:",
		    argp->newpw.pw_name, argp->newpw.pw_uid, passfile);

		if (passwd_changed)
			yp_error("password changed");

		if (gecos_changed)
			yp_error("gecos changed ('%s' -> '%s')",
					oldgecos, argp->newpw.pw_gecos);

		if (shell_changed)
			yp_error("shell changed ('%s' -> '%s')",
					oldshell, argp->newpw.pw_shell);
	}

	result = 0;
	return (&result);
}

/*
 * Note that this function performs a little less sanity checking
 * than the last one. Since only the superuser is allowed to use it,
 * it is assumed that the caller knows what he's doing.
 */
int *
yppasswdproc_update_master_1_svc(master_yppasswd *argp,
    struct svc_req *rqstp)
{
	static int result;
	int pfd, tfd;
	int pid;
	uid_t uid;
	int rval = 0;
	DBT key, data;
	char *passfile_hold;
	char passfile_buf[MAXPATHLEN + 2];
	char passfile_hold_buf[MAXPATHLEN + 2];
	struct sockaddr_in *rqhost;
	SVCXPRT	*transp;
	struct passwd newpasswd;

	result = 1;
	transp = rqstp->rq_xprt;

	/*
	 * NO AF_INET CONNETCIONS ALLOWED!
	 */
	rqhost = svc_getcaller(transp);
	if (rqhost->sin_family != AF_UNIX) {
		yp_error("Alert! %s/%d attempted to use superuser-only \
procedure!\n", inet_ntoa(rqhost->sin_addr), rqhost->sin_port);
		svcerr_auth(transp, AUTH_BADCRED);
		return(&result);
	}

	if (rqstp->rq_cred.oa_flavor != AUTH_SYS) {
		yp_error("caller didn't send proper credentials");
		svcerr_auth(transp, AUTH_BADCRED);
		return(&result);
	}

	if (__rpc_get_local_uid(transp, &uid) < 0) {
		yp_error("caller didn't send proper credentials");
		svcerr_auth(transp, AUTH_BADCRED);
		return(&result);
	}

	if (uid) {
		yp_error("caller euid is %d, expecting 0 -- rejecting request",
		    uid);
		svcerr_auth(rqstp->rq_xprt, AUTH_BADCRED);
		return(&result);
	}

	passfile = passfile_default;

	key.data = argp->newpw.pw_name;
	key.size = strlen(argp->newpw.pw_name);

	/*
	 * The superuser may add entries to the passwd maps if
	 * rpc.yppasswdd is started with the -a flag. Paranoia
	 * prevents me from allowing additions by default.
	 */
	if ((rval = yp_get_record(argp->domain, "master.passwd.byname",
			  &key, &data, 0)) != YP_TRUE) {
		if (rval == YP_NOKEY) {
			yp_error("user %s not found in passwd database",
				 argp->newpw.pw_name);
			if (allow_additions)
				yp_error("notice: adding user %s to \
master.passwd database for domain %s", argp->newpw.pw_name, argp->domain);
			else
				yp_error("restart rpc.yppasswdd with the -a flag to \
allow additions to be made to the password database");
		} else {
			yp_error("database access error: %s",
				 yperr_string(rval));
		}
		if (!allow_additions)
			return(&result);
	} else {

		/* Nul terminate, please. */
		*((char *)data.data + data.size) = '\0';

		copy_yp_pass(data.data, 1, data.size);
	}

	/*
	 * Perform a small bit of sanity checking.
	 */
	if (validate_master(rval == YP_TRUE ? &yp_password:NULL,&argp->newpw)){
		yp_error("rejecting update attempt for %s: bad arguments",
			 argp->newpw.pw_name);
		return(&result);
	}

	/*
	 * If the caller specified a domain other than our 'default'
	 * domain, change the path to master.passwd accordingly.
	 */

	if (strcmp(argp->domain, yppasswd_domain)) {
		snprintf(passfile_buf, sizeof(passfile_buf),
			"%s/%s/master.passwd", yp_dir, argp->domain);
		passfile = (char *)&passfile_buf;
	}

	/*
	 * Create a filename to hold the original master.passwd
	 * so if our call to yppwupdate fails we can roll back
	 */
	snprintf(passfile_hold_buf, sizeof(passfile_hold_buf),
	    "%s.hold", passfile);
	passfile_hold = (char *)&passfile_hold_buf;

	if (pw_init(dirname(passfile), passfile)) {
		yp_error("pw_init() failed");
		return &result;
	}
	if ((pfd = pw_lock()) == -1) {
		pw_fini();
		yp_error("pw_lock() failed");
		return &result;
	}
	if ((tfd = pw_tmp(-1)) == -1) {
		pw_fini();
		yp_error("pw_tmp() failed");
		return &result;
	}
	xlate_passwd(&argp->newpw, &newpasswd);
	if (pw_copy(pfd, tfd, &newpasswd, NULL) == -1) {
		pw_fini();
		yp_error("pw_copy() failed");
		return &result;
	}
	if (rename(passfile, passfile_hold) == -1) {
		pw_fini();
		yp_error("rename of %s to %s failed", passfile,
		    passfile_hold);
		return &result;
	}
	if (strcmp(passfile, _PATH_MASTERPASSWD) == 0) {
		/*
		 * NIS server is exporting the system's master.passwd.
		 * Call pw_mkdb to rebuild passwd and the .db files
		 */
		if (pw_mkdb(argp->newpw.pw_name) == -1) {
			pw_fini();
			yp_error("pw_mkdb() failed");
			rename(passfile_hold, passfile);
			return &result;
		}
	} else {
		/*
		 * NIS server is exporting a private master.passwd.
		 * Rename tempfile into final location
		 */
		if (rename(pw_tempname(), passfile) == -1) {
			pw_fini();
			yp_error("rename of %s to %s failed",
			    pw_tempname(), passfile);
			rename(passfile_hold, passfile);
			return &result;
		}
	}
	pw_fini();

	if (inplace) {
		xlate_passwd(&argp->newpw, &newpasswd);
		if ((rval = update_inplace(&newpasswd, argp->domain))) {
			yp_error("inplace update failed -- rebuilding maps");
		}
	}

	switch ((pid = fork())) {
	case 0:
		if (inplace && !rval) {
    			execlp(MAP_UPDATE_PATH, MAP_UPDATE, passfile,
				argp->domain, "pushpw", (char *)NULL);
    		} else {
			execlp(MAP_UPDATE_PATH, MAP_UPDATE, passfile,
				argp->domain, (char *)NULL);
		}
    		yp_error("couldn't exec map update process: %s",
					strerror(errno));
		unlink(passfile);
		rename(passfile_hold, passfile);
    		exit(1);
		break;
	case -1:
		yp_error("fork() failed: %s", strerror(errno));
		unlink(passfile);
		rename(passfile_hold, passfile);
		return(&result);
		break;
	default:
		unlink(passfile_hold);
		break;
	}

	yp_error("performed update of user %s (uid %d) domain %s",
						argp->newpw.pw_name,
						argp->newpw.pw_uid,
						argp->domain);

	result = 0;
	return(&result);
}
