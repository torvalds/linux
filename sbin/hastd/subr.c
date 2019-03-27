/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/jail.h>
#include <sys/stat.h>
#ifdef HAVE_CAPSICUM
#include <sys/capsicum.h>
#include <geom/gate/g_gate.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pjdlog.h>

#include "hast.h"
#include "subr.h"

int
vsnprlcat(char *str, size_t size, const char *fmt, va_list ap)
{
	size_t len;

	len = strlen(str);
	return (vsnprintf(str + len, size - len, fmt, ap));
}

int
snprlcat(char *str, size_t size, const char *fmt, ...)
{
	va_list ap;
	int result;

	va_start(ap, fmt);
	result = vsnprlcat(str, size, fmt, ap);
	va_end(ap);
	return (result);
}

int
provinfo(struct hast_resource *res, bool dowrite)
{
	struct stat sb;

	PJDLOG_ASSERT(res->hr_localpath != NULL &&
	    res->hr_localpath[0] != '\0');

	if (res->hr_localfd == -1) {
		res->hr_localfd = open(res->hr_localpath,
		    dowrite ? O_RDWR : O_RDONLY);
		if (res->hr_localfd == -1) {
			pjdlog_errno(LOG_ERR, "Unable to open %s",
			    res->hr_localpath);
			return (-1);
		}
	}
	if (fstat(res->hr_localfd, &sb) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to stat %s", res->hr_localpath);
		return (-1);
	}
	if (S_ISCHR(sb.st_mode)) {
		/*
		 * If this is character device, it is most likely GEOM provider.
		 */
		if (ioctl(res->hr_localfd, DIOCGMEDIASIZE,
		    &res->hr_local_mediasize) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable obtain provider %s mediasize",
			    res->hr_localpath);
			return (-1);
		}
		if (ioctl(res->hr_localfd, DIOCGSECTORSIZE,
		    &res->hr_local_sectorsize) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable obtain provider %s sectorsize",
			    res->hr_localpath);
			return (-1);
		}
	} else if (S_ISREG(sb.st_mode)) {
		/*
		 * We also support regular files for which we hardcode
		 * sector size of 512 bytes.
		 */
		res->hr_local_mediasize = sb.st_size;
		res->hr_local_sectorsize = 512;
	} else {
		/*
		 * We support no other file types.
		 */
		pjdlog_error("%s is neither GEOM provider nor regular file.",
		    res->hr_localpath);
		errno = EFTYPE;
		return (-1);
	}
	return (0);
}

const char *
role2str(int role)
{

	switch (role) {
	case HAST_ROLE_INIT:
		return ("init");
	case HAST_ROLE_PRIMARY:
		return ("primary");
	case HAST_ROLE_SECONDARY:
		return ("secondary");
	}
	return ("unknown");
}

int
drop_privs(const struct hast_resource *res)
{
	char jailhost[sizeof(res->hr_name) * 2];
	struct jail jailst;
	struct passwd *pw;
	uid_t ruid, euid, suid;
	gid_t rgid, egid, sgid;
	gid_t gidset[1];
	bool capsicum, jailed;

	/*
	 * According to getpwnam(3) we have to clear errno before calling the
	 * function to be able to distinguish between an error and missing
	 * entry (with is not treated as error by getpwnam(3)).
	 */
	errno = 0;
	pw = getpwnam(HAST_USER);
	if (pw == NULL) {
		if (errno != 0) {
			pjdlog_errno(LOG_ERR,
			    "Unable to find info about '%s' user", HAST_USER);
			return (-1);
		} else {
			pjdlog_error("'%s' user doesn't exist.", HAST_USER);
			errno = ENOENT;
			return (-1);
		}
	}

	bzero(&jailst, sizeof(jailst));
	jailst.version = JAIL_API_VERSION;
	jailst.path = pw->pw_dir;
	if (res == NULL) {
		(void)snprintf(jailhost, sizeof(jailhost), "hastctl");
	} else {
		(void)snprintf(jailhost, sizeof(jailhost), "hastd: %s (%s)",
		    res->hr_name, role2str(res->hr_role));
	}
	jailst.hostname = jailhost;
	jailst.jailname = NULL;
	jailst.ip4s = 0;
	jailst.ip4 = NULL;
	jailst.ip6s = 0;
	jailst.ip6 = NULL;
	if (jail(&jailst) >= 0) {
		jailed = true;
	} else {
		jailed = false;
		pjdlog_errno(LOG_WARNING,
		    "Unable to jail to directory to %s", pw->pw_dir);
		if (chroot(pw->pw_dir) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to change root directory to %s",
			    pw->pw_dir);
			return (-1);
		}
	}
	PJDLOG_VERIFY(chdir("/") == 0);
	gidset[0] = pw->pw_gid;
	if (setgroups(1, gidset) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to set groups to gid %u",
		    (unsigned int)pw->pw_gid);
		return (-1);
	}
	if (setgid(pw->pw_gid) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to set gid to %u",
		    (unsigned int)pw->pw_gid);
		return (-1);
	}
	if (setuid(pw->pw_uid) == -1) {
		pjdlog_errno(LOG_ERR, "Unable to set uid to %u",
		    (unsigned int)pw->pw_uid);
		return (-1);
	}

#ifdef HAVE_CAPSICUM
	capsicum = (cap_enter() == 0);
	if (!capsicum) {
		pjdlog_common(LOG_DEBUG, 1, errno,
		    "Unable to sandbox using capsicum");
	} else if (res != NULL) {
		cap_rights_t rights;
		static const unsigned long geomcmds[] = {
		    DIOCGDELETE,
		    DIOCGFLUSH
		};

		PJDLOG_ASSERT(res->hr_role == HAST_ROLE_PRIMARY ||
		    res->hr_role == HAST_ROLE_SECONDARY);

		cap_rights_init(&rights, CAP_FLOCK, CAP_IOCTL, CAP_PREAD,
		    CAP_PWRITE);
		if (cap_rights_limit(res->hr_localfd, &rights) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to limit capability rights on local descriptor");
		}
		if (cap_ioctls_limit(res->hr_localfd, geomcmds,
		    nitems(geomcmds)) == -1) {
			pjdlog_errno(LOG_ERR,
			    "Unable to limit allowed GEOM ioctls");
		}

		if (res->hr_role == HAST_ROLE_PRIMARY) {
			static const unsigned long ggatecmds[] = {
			    G_GATE_CMD_MODIFY,
			    G_GATE_CMD_START,
			    G_GATE_CMD_DONE,
			    G_GATE_CMD_DESTROY
			};

			cap_rights_init(&rights, CAP_IOCTL);
			if (cap_rights_limit(res->hr_ggatefd, &rights) == -1) {
				pjdlog_errno(LOG_ERR,
				    "Unable to limit capability rights to CAP_IOCTL on ggate descriptor");
			}
			if (cap_ioctls_limit(res->hr_ggatefd, ggatecmds,
			    nitems(ggatecmds)) == -1) {
				pjdlog_errno(LOG_ERR,
				    "Unable to limit allowed ggate ioctls");
			}
		}
	}
#else
	capsicum = false;
#endif

	/*
	 * Better be sure that everything succeeded.
	 */
	PJDLOG_VERIFY(getresuid(&ruid, &euid, &suid) == 0);
	PJDLOG_VERIFY(ruid == pw->pw_uid);
	PJDLOG_VERIFY(euid == pw->pw_uid);
	PJDLOG_VERIFY(suid == pw->pw_uid);
	PJDLOG_VERIFY(getresgid(&rgid, &egid, &sgid) == 0);
	PJDLOG_VERIFY(rgid == pw->pw_gid);
	PJDLOG_VERIFY(egid == pw->pw_gid);
	PJDLOG_VERIFY(sgid == pw->pw_gid);
	PJDLOG_VERIFY(getgroups(0, NULL) == 1);
	PJDLOG_VERIFY(getgroups(1, gidset) == 1);
	PJDLOG_VERIFY(gidset[0] == pw->pw_gid);

	pjdlog_debug(1,
	    "Privileges successfully dropped using %s%s+setgid+setuid.",
	    capsicum ? "capsicum+" : "", jailed ? "jail" : "chroot");

	return (0);
}
