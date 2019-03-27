/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002-2005 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by Network Associates
 * Laboratories, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
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
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/jail.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/mount.h>

#include <security/mac_bsdextended/mac_bsdextended.h>

#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ugidfw.h"

/*
 * Text format for rules: rules contain subject and object elements, mode.
 * The total form is "subject [s_element] object [o_element] mode [mode]".
 * At least * one of a uid or gid entry must be present; both may also be
 * present.
 */

#define	MIB	"security.mac.bsdextended"

int
bsde_rule_to_string(struct mac_bsdextended_rule *rule, char *buf, size_t buflen)
{
	struct group *grp;
	struct passwd *pwd;
	struct statfs *mntbuf;
	char *cur, type[sizeof(rule->mbr_object.mbo_type) * CHAR_BIT + 1];
	size_t left, len;
	int anymode, unknownmode, numfs, i, notdone;

	cur = buf;
	left = buflen;

	len = snprintf(cur, left, "subject ");
	if (len < 0 || len > left)
		goto truncated;
	left -= len;
	cur += len;
	if (rule->mbr_subject.mbs_flags) {
		if (rule->mbr_subject.mbs_neg == MBS_ALL_FLAGS) {
			len = snprintf(cur, left, "not ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
			notdone = 1;
		} else {
			notdone = 0;
		}

		if (!notdone && (rule->mbr_subject.mbs_neg & MBO_UID_DEFINED)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_subject.mbs_flags & MBO_UID_DEFINED) {
			pwd = getpwuid(rule->mbr_subject.mbs_uid_min);
			if (pwd != NULL) {
				len = snprintf(cur, left, "uid %s",
				    pwd->pw_name);
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			} else {
				len = snprintf(cur, left, "uid %u",
				    rule->mbr_subject.mbs_uid_min);
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			}
			if (rule->mbr_subject.mbs_uid_min !=
			    rule->mbr_subject.mbs_uid_max) {
				pwd = getpwuid(rule->mbr_subject.mbs_uid_max);
				if (pwd != NULL) {
					len = snprintf(cur, left, ":%s ",
					    pwd->pw_name);
					if (len < 0 || len > left)
						goto truncated;
					left -= len;
					cur += len;
				} else {
					len = snprintf(cur, left, ":%u ",
					    rule->mbr_subject.mbs_uid_max);
					if (len < 0 || len > left)
						goto truncated;
					left -= len;
					cur += len;
				}
			} else {
				len = snprintf(cur, left, " ");
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			}
		}
		if (!notdone && (rule->mbr_subject.mbs_neg & MBO_GID_DEFINED)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_subject.mbs_flags & MBO_GID_DEFINED) {
			grp = getgrgid(rule->mbr_subject.mbs_gid_min);
			if (grp != NULL) {
				len = snprintf(cur, left, "gid %s",
				    grp->gr_name);
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			} else {
				len = snprintf(cur, left, "gid %u",
				    rule->mbr_subject.mbs_gid_min);
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			}
			if (rule->mbr_subject.mbs_gid_min !=
			    rule->mbr_subject.mbs_gid_max) {
				grp = getgrgid(rule->mbr_subject.mbs_gid_max);
				if (grp != NULL) {
					len = snprintf(cur, left, ":%s ",
					    grp->gr_name);
					if (len < 0 || len > left)
						goto truncated;
					left -= len;
					cur += len;
				} else {
					len = snprintf(cur, left, ":%u ",
					    rule->mbr_subject.mbs_gid_max);
					if (len < 0 || len > left)
						goto truncated;
					left -= len;
					cur += len;
				}
			} else {
				len = snprintf(cur, left, " ");
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			}
		}
		if (!notdone && (rule->mbr_subject.mbs_neg & MBS_PRISON_DEFINED)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_subject.mbs_flags & MBS_PRISON_DEFINED) {
			len = snprintf(cur, left, "jailid %d ",
			    rule->mbr_subject.mbs_prison);
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
	}

	len = snprintf(cur, left, "object ");
	if (len < 0 || len > left)
		goto truncated;
	left -= len;
	cur += len;
	if (rule->mbr_object.mbo_flags) {
		if (rule->mbr_object.mbo_neg == MBO_ALL_FLAGS) {
			len = snprintf(cur, left, "not ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
			notdone = 1;
		} else {
			notdone = 0;
		}

		if (!notdone && (rule->mbr_object.mbo_neg & MBO_UID_DEFINED)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_object.mbo_flags & MBO_UID_DEFINED) {
			pwd = getpwuid(rule->mbr_object.mbo_uid_min);
			if (pwd != NULL) {
				len = snprintf(cur, left, "uid %s",
				    pwd->pw_name);
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			} else {
				len = snprintf(cur, left, "uid %u",
				    rule->mbr_object.mbo_uid_min);
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			}
			if (rule->mbr_object.mbo_uid_min !=
			    rule->mbr_object.mbo_uid_max) {
				pwd = getpwuid(rule->mbr_object.mbo_uid_max);
				if (pwd != NULL) {
					len = snprintf(cur, left, ":%s ",
					    pwd->pw_name);
					if (len < 0 || len > left)
						goto truncated;
					left -= len;
					cur += len;
				} else {
					len = snprintf(cur, left, ":%u ",
					    rule->mbr_object.mbo_uid_max);
					if (len < 0 || len > left)
						goto truncated;
					left -= len;
					cur += len;
				}
			} else {
				len = snprintf(cur, left, " ");
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			}
		}
		if (!notdone && (rule->mbr_object.mbo_neg & MBO_GID_DEFINED)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_object.mbo_flags & MBO_GID_DEFINED) {
			grp = getgrgid(rule->mbr_object.mbo_gid_min);
			if (grp != NULL) {
				len = snprintf(cur, left, "gid %s",
				    grp->gr_name);
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			} else {
				len = snprintf(cur, left, "gid %u",
				    rule->mbr_object.mbo_gid_min);
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			}
			if (rule->mbr_object.mbo_gid_min !=
			    rule->mbr_object.mbo_gid_max) {
				grp = getgrgid(rule->mbr_object.mbo_gid_max);
				if (grp != NULL) {
					len = snprintf(cur, left, ":%s ",
					    grp->gr_name);
					if (len < 0 || len > left)
						goto truncated;
					left -= len;
					cur += len;
				} else {
					len = snprintf(cur, left, ":%u ",
					    rule->mbr_object.mbo_gid_max);
					if (len < 0 || len > left)
						goto truncated;
					left -= len;
					cur += len;
				}
			} else {
				len = snprintf(cur, left, " ");
				if (len < 0 || len > left)
					goto truncated;
				left -= len;
				cur += len;
			}
		}
		if (!notdone && (rule->mbr_object.mbo_neg & MBO_FSID_DEFINED)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_object.mbo_flags & MBO_FSID_DEFINED) {
			numfs = getmntinfo(&mntbuf, MNT_NOWAIT);
			for (i = 0; i < numfs; i++)
				if (memcmp(&(rule->mbr_object.mbo_fsid),
				    &(mntbuf[i].f_fsid),
				    sizeof(mntbuf[i].f_fsid)) == 0)
					break;
			len = snprintf(cur, left, "filesys %s ",
			    i == numfs ? "???" : mntbuf[i].f_mntonname);
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (!notdone && (rule->mbr_object.mbo_neg & MBO_SUID)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_object.mbo_flags & MBO_SUID) {
			len = snprintf(cur, left, "suid ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (!notdone && (rule->mbr_object.mbo_neg & MBO_SGID)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_object.mbo_flags & MBO_SGID) {
			len = snprintf(cur, left, "sgid ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (!notdone && (rule->mbr_object.mbo_neg & MBO_UID_SUBJECT)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_object.mbo_flags & MBO_UID_SUBJECT) {
			len = snprintf(cur, left, "uid_of_subject ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (!notdone && (rule->mbr_object.mbo_neg & MBO_GID_SUBJECT)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_object.mbo_flags & MBO_GID_SUBJECT) {
			len = snprintf(cur, left, "gid_of_subject ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (!notdone && (rule->mbr_object.mbo_neg & MBO_TYPE_DEFINED)) {
			len = snprintf(cur, left, "! ");
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
		if (rule->mbr_object.mbo_flags & MBO_TYPE_DEFINED) {
			i = 0;
			if (rule->mbr_object.mbo_type & MBO_TYPE_REG)
				type[i++] = 'r';
			if (rule->mbr_object.mbo_type & MBO_TYPE_DIR)
				type[i++] = 'd';
			if (rule->mbr_object.mbo_type & MBO_TYPE_BLK)
				type[i++] = 'b';
			if (rule->mbr_object.mbo_type & MBO_TYPE_CHR)
				type[i++] = 'c';
			if (rule->mbr_object.mbo_type & MBO_TYPE_LNK)
				type[i++] = 'l';
			if (rule->mbr_object.mbo_type & MBO_TYPE_SOCK)
				type[i++] = 's';
			if (rule->mbr_object.mbo_type & MBO_TYPE_FIFO)
				type[i++] = 'p';
			if (rule->mbr_object.mbo_type == MBO_ALL_TYPE) {
				i = 0;
				type[i++] = 'a';
			}
			type[i++] = '\0';
			len = snprintf(cur, left, "type %s ", type);
			if (len < 0 || len > left)
				goto truncated;
			left -= len;
			cur += len;
		}
	}

	len = snprintf(cur, left, "mode ");
	if (len < 0 || len > left)
		goto truncated;
	left -= len;
	cur += len;

	anymode = (rule->mbr_mode & MBI_ALLPERM);
	unknownmode = (rule->mbr_mode & ~MBI_ALLPERM);

	if (rule->mbr_mode & MBI_ADMIN) {
		len = snprintf(cur, left, "a");
		if (len < 0 || len > left)
			goto truncated;

		left -= len;
		cur += len;
	}
	if (rule->mbr_mode & MBI_READ) {
		len = snprintf(cur, left, "r");
		if (len < 0 || len > left)
			goto truncated;

		left -= len;
		cur += len;
	}
	if (rule->mbr_mode & MBI_STAT) {
		len = snprintf(cur, left, "s");
		if (len < 0 || len > left)
			goto truncated;

		left -= len;
		cur += len;
	}
	if (rule->mbr_mode & MBI_WRITE) {
		len = snprintf(cur, left, "w");
		if (len < 0 || len > left)
			goto truncated;

		left -= len;
		cur += len;
	}
	if (rule->mbr_mode & MBI_EXEC) {
		len = snprintf(cur, left, "x");
		if (len < 0 || len > left)
			goto truncated;

		left -= len;
		cur += len;
	}
	if (!anymode) {
		len = snprintf(cur, left, "n");
		if (len < 0 || len > left)
			goto truncated;

		left -= len;
		cur += len;
	}
	if (unknownmode) {
		len = snprintf(cur, left, "?");
		if (len < 0 || len > left)
			goto truncated;

		left -= len;
		cur += len;
	}

	return (0);

truncated:
	return (-1);
}

static int
bsde_parse_uidrange(char *spec, uid_t *min, uid_t *max,
    size_t buflen, char *errstr){
	struct passwd *pwd;
	uid_t uid1, uid2;
	char *spec1, *spec2, *endp;
	unsigned long value;

	spec2 = spec;
	spec1 = strsep(&spec2, ":");

	pwd = getpwnam(spec1);
	if (pwd != NULL)
		uid1 = pwd->pw_uid;
	else {
		value = strtoul(spec1, &endp, 10);
		if (*endp != '\0') {
			snprintf(errstr, buflen, "invalid uid: '%s'", spec1);
			return (-1);
		}
		uid1 = value;
	}

	if (spec2 == NULL) {
		*max = *min = uid1;
		return (0);
	}

	pwd = getpwnam(spec2);
	if (pwd != NULL)
		uid2 = pwd->pw_uid;
	else {
		value = strtoul(spec2, &endp, 10);
		if (*endp != '\0') {
			snprintf(errstr, buflen, "invalid uid: '%s'", spec2);
			return (-1);
		}
		uid2 = value;
	}

	*min = uid1;
	*max = uid2;

	return (0);
}

static int
bsde_parse_gidrange(char *spec, gid_t *min, gid_t *max,
    size_t buflen, char *errstr){
	struct group *grp;
	gid_t gid1, gid2;
	char *spec1, *spec2, *endp;
	unsigned long value;

	spec2 = spec;
	spec1 = strsep(&spec2, ":");

	grp = getgrnam(spec1);
	if (grp != NULL)
		gid1 = grp->gr_gid;
	else {
		value = strtoul(spec1, &endp, 10);
		if (*endp != '\0') {
			snprintf(errstr, buflen, "invalid gid: '%s'", spec1);
			return (-1);
		}
		gid1 = value;
	}

	if (spec2 == NULL) {
		*max = *min = gid1;
		return (0);
	}

	grp = getgrnam(spec2);
	if (grp != NULL)
		gid2 = grp->gr_gid;
	else {
		value = strtoul(spec2, &endp, 10);
		if (*endp != '\0') {
			snprintf(errstr, buflen, "invalid gid: '%s'", spec2);
			return (-1);
		}
		gid2 = value;
	}

	*min = gid1;
	*max = gid2;

	return (0);
}

static int
bsde_get_jailid(const char *name, size_t buflen, char *errstr)
{
	char *ep;
	int jid;
	struct iovec jiov[4];

	/* Copy jail_getid(3) instead of messing with library dependancies */
	jid = strtoul(name, &ep, 10);
	if (*name && !*ep)
		return jid;
	jiov[0].iov_base = __DECONST(char *, "name");
	jiov[0].iov_len = sizeof("name");
	jiov[1].iov_len = strlen(name) + 1;
	jiov[1].iov_base = alloca(jiov[1].iov_len);
	strcpy(jiov[1].iov_base, name);
	if (errstr && buflen) {
		jiov[2].iov_base = __DECONST(char *, "errmsg");
		jiov[2].iov_len = sizeof("errmsg");
		jiov[3].iov_base = errstr;
		jiov[3].iov_len = buflen;
		errstr[0] = 0;
		jid = jail_get(jiov, 4, 0);
		if (jid < 0 && !errstr[0])
			snprintf(errstr, buflen, "jail_get: %s",
			    strerror(errno));
	} else
		jid = jail_get(jiov, 2, 0);
	return jid;
}

static int
bsde_parse_subject(int argc, char *argv[],
    struct mac_bsdextended_subject *subject, size_t buflen, char *errstr)
{
	int not_seen, flags;
	int current, neg, nextnot;
	uid_t uid_min, uid_max;
	gid_t gid_min, gid_max;
	int jid = 0;

	current = 0;
	flags = 0;
	neg = 0;
	nextnot = 0;

	if (strcmp("not", argv[current]) == 0) {
		not_seen = 1;
		current++;
	} else
		not_seen = 0;

	while (current < argc) {
		if (strcmp(argv[current], "uid") == 0) {
			if (current + 2 > argc) {
				snprintf(errstr, buflen, "uid short");
				return (-1);
			}
			if (flags & MBS_UID_DEFINED) {
				snprintf(errstr, buflen, "one uid only");
				return (-1);
			}
			if (bsde_parse_uidrange(argv[current+1],
			    &uid_min, &uid_max, buflen, errstr) < 0)
				return (-1);
			flags |= MBS_UID_DEFINED;
			if (nextnot) {
				neg ^= MBS_UID_DEFINED;
				nextnot = 0;
			}
			current += 2;
		} else if (strcmp(argv[current], "gid") == 0) {
			if (current + 2 > argc) {
				snprintf(errstr, buflen, "gid short");
				return (-1);
			}
			if (flags & MBS_GID_DEFINED) {
				snprintf(errstr, buflen, "one gid only");
				return (-1);
			}
			if (bsde_parse_gidrange(argv[current+1],
			    &gid_min, &gid_max, buflen, errstr) < 0)
				return (-1);
			flags |= MBS_GID_DEFINED;
			if (nextnot) {
				neg ^= MBS_GID_DEFINED;
				nextnot = 0;
			}
			current += 2;
		} else if (strcmp(argv[current], "jailid") == 0) {
			if (current + 2 > argc) {
				snprintf(errstr, buflen, "prison short");
				return (-1);
			}
			if (flags & MBS_PRISON_DEFINED) {
				snprintf(errstr, buflen, "one jail only");
				return (-1);
			}
			jid = bsde_get_jailid(argv[current+1], buflen, errstr);
			if (jid < 0)
				return (-1);
			flags |= MBS_PRISON_DEFINED;
			if (nextnot) {
				neg ^= MBS_PRISON_DEFINED;
				nextnot = 0;
			}
			current += 2;
		} else if (strcmp(argv[current], "!") == 0) {
			if (nextnot) {
				snprintf(errstr, buflen, "double negative");
				return (-1);
			}
			nextnot = 1;
			current += 1;
		} else {
			snprintf(errstr, buflen, "'%s' not expected",
			    argv[current]);
			return (-1);
		}
	}

	subject->mbs_flags = flags;
	if (not_seen)
		subject->mbs_neg = MBS_ALL_FLAGS ^ neg;
	else
		subject->mbs_neg = neg;
	if (flags & MBS_UID_DEFINED) {
		subject->mbs_uid_min = uid_min;
		subject->mbs_uid_max = uid_max;
	}
	if (flags & MBS_GID_DEFINED) {
		subject->mbs_gid_min = gid_min;
		subject->mbs_gid_max = gid_max;
	}
	if (flags & MBS_PRISON_DEFINED)
		subject->mbs_prison = jid;

	return (0);
}

static int
bsde_parse_type(char *spec, int *type, size_t buflen, char *errstr)
{
	int i;

	*type = 0;
	for (i = 0; i < strlen(spec); i++) {
		switch (spec[i]) {
		case 'r':
		case '-':
			*type |= MBO_TYPE_REG;
			break;
		case 'd':
			*type |= MBO_TYPE_DIR;
			break;
		case 'b':
			*type |= MBO_TYPE_BLK;
			break;
		case 'c':
			*type |= MBO_TYPE_CHR;
			break;
		case 'l':
			*type |= MBO_TYPE_LNK;
			break;
		case 's':
			*type |= MBO_TYPE_SOCK;
			break;
		case 'p':
			*type |= MBO_TYPE_FIFO;
			break;
		case 'a':
			*type |= MBO_ALL_TYPE;
			break;
		default:
			snprintf(errstr, buflen, "Unknown type code: %c",
			    spec[i]);
			return (-1);
		}
	}

	return (0);
}

static int
bsde_parse_fsid(char *spec, struct fsid *fsid, size_t buflen, char *errstr)
{
	struct statfs buf;

	if (statfs(spec, &buf) < 0) {
		snprintf(errstr, buflen, "Unable to get id for %s: %s",
		    spec, strerror(errno));
		return (-1);
	}

	*fsid = buf.f_fsid;

	return (0);
}

static int
bsde_parse_object(int argc, char *argv[],
    struct mac_bsdextended_object *object, size_t buflen, char *errstr)
{
	int not_seen, flags;
	int current, neg, nextnot;
	int type;
	uid_t uid_min, uid_max;
	gid_t gid_min, gid_max;
	struct fsid fsid;

	current = 0;
	flags = 0;
	neg = 0;
	nextnot = 0;
	type = 0;

	if (strcmp("not", argv[current]) == 0) {
		not_seen = 1;
		current++;
	} else
		not_seen = 0;

	while (current < argc) {
		if (strcmp(argv[current], "uid") == 0) {
			if (current + 2 > argc) {
				snprintf(errstr, buflen, "uid short");
				return (-1);
			}
			if (flags & MBO_UID_DEFINED) {
				snprintf(errstr, buflen, "one uid only");
				return (-1);
			}
			if (bsde_parse_uidrange(argv[current+1],
			    &uid_min, &uid_max, buflen, errstr) < 0)
				return (-1);
			flags |= MBO_UID_DEFINED;
			if (nextnot) {
				neg ^= MBO_UID_DEFINED;
				nextnot = 0;
			}
			current += 2;
		} else if (strcmp(argv[current], "gid") == 0) {
			if (current + 2 > argc) {
				snprintf(errstr, buflen, "gid short");
				return (-1);
			}
			if (flags & MBO_GID_DEFINED) {
				snprintf(errstr, buflen, "one gid only");
				return (-1);
			}
			if (bsde_parse_gidrange(argv[current+1],
			    &gid_min, &gid_max, buflen, errstr) < 0)
				return (-1);
			flags |= MBO_GID_DEFINED;
			if (nextnot) {
				neg ^= MBO_GID_DEFINED;
				nextnot = 0;
			}
			current += 2;
		} else if (strcmp(argv[current], "filesys") == 0) {
			if (current + 2 > argc) {
				snprintf(errstr, buflen, "filesys short");
				return (-1);
			}
			if (flags & MBO_FSID_DEFINED) {
				snprintf(errstr, buflen, "one fsid only");
				return (-1);
			}
			if (bsde_parse_fsid(argv[current+1], &fsid,
			    buflen, errstr) < 0)
				return (-1);
			flags |= MBO_FSID_DEFINED;
			if (nextnot) {
				neg ^= MBO_FSID_DEFINED;
				nextnot = 0;
			}
			current += 2;
		} else if (strcmp(argv[current], "suid") == 0) {
			flags |= MBO_SUID;
			if (nextnot) {
				neg ^= MBO_SUID;
				nextnot = 0;
			}
			current += 1;
		} else if (strcmp(argv[current], "sgid") == 0) {
			flags |= MBO_SGID;
			if (nextnot) {
				neg ^= MBO_SGID;
				nextnot = 0;
			}
			current += 1;
		} else if (strcmp(argv[current], "uid_of_subject") == 0) {
			flags |= MBO_UID_SUBJECT;
			if (nextnot) {
				neg ^= MBO_UID_SUBJECT;
				nextnot = 0;
			}
			current += 1;
		} else if (strcmp(argv[current], "gid_of_subject") == 0) {
			flags |= MBO_GID_SUBJECT;
			if (nextnot) {
				neg ^= MBO_GID_SUBJECT;
				nextnot = 0;
			}
			current += 1;
		} else if (strcmp(argv[current], "type") == 0) {
			if (current + 2 > argc) {
				snprintf(errstr, buflen, "type short");
				return (-1);
			}
			if (flags & MBO_TYPE_DEFINED) {
				snprintf(errstr, buflen, "one type only");
				return (-1);
			}
			if (bsde_parse_type(argv[current+1], &type,
			    buflen, errstr) < 0)
				return (-1);
			flags |= MBO_TYPE_DEFINED;
			if (nextnot) {
				neg ^= MBO_TYPE_DEFINED;
				nextnot = 0;
			}
			current += 2;
		} else if (strcmp(argv[current], "!") == 0) {
			if (nextnot) {
				snprintf(errstr, buflen,
				    "double negative'");
				return (-1);
			}
			nextnot = 1;
			current += 1;
		} else {
			snprintf(errstr, buflen, "'%s' not expected",
			    argv[current]);
			return (-1);
		}
	}

	object->mbo_flags = flags;
	if (not_seen)
		object->mbo_neg = MBO_ALL_FLAGS ^ neg;
	else
		object->mbo_neg = neg;
	if (flags & MBO_UID_DEFINED) {
		object->mbo_uid_min = uid_min;
		object->mbo_uid_max = uid_max;
	}
	if (flags & MBO_GID_DEFINED) {
		object->mbo_gid_min = gid_min;
		object->mbo_gid_max = gid_max;
	}
	if (flags & MBO_FSID_DEFINED)
		object->mbo_fsid = fsid;
	if (flags & MBO_TYPE_DEFINED)
		object->mbo_type = type;

	return (0);
}

int
bsde_parse_mode(int argc, char *argv[], mode_t *mode, size_t buflen,
    char *errstr)
{
	int i;

	if (argc == 0) {
		snprintf(errstr, buflen, "mode expects mode value");
		return (-1);
	}

	if (argc != 1) {
		snprintf(errstr, buflen, "'%s' unexpected", argv[1]);
		return (-1);
	}

	*mode = 0;
	for (i = 0; i < strlen(argv[0]); i++) {
		switch (argv[0][i]) {
		case 'a':
			*mode |= MBI_ADMIN;
			break;
		case 'r':
			*mode |= MBI_READ;
			break;
		case 's':
			*mode |= MBI_STAT;
			break;
		case 'w':
			*mode |= MBI_WRITE;
			break;
		case 'x':
			*mode |= MBI_EXEC;
			break;
		case 'n':
			/* ignore */
			break;
		default:
			snprintf(errstr, buflen, "Unknown mode letter: %c",
			    argv[0][i]);
			return (-1);
		}
	}

	return (0);
}

int
bsde_parse_rule(int argc, char *argv[], struct mac_bsdextended_rule *rule,
    size_t buflen, char *errstr)
{
	int subject, subject_elements, subject_elements_length;
	int object, object_elements, object_elements_length;
	int mode, mode_elements, mode_elements_length;
	int error, i;

	bzero(rule, sizeof(*rule));

	if (argc < 1) {
		snprintf(errstr, buflen, "Rule must begin with subject");
		return (-1);
	}

	if (strcmp(argv[0], "subject") != 0) {
		snprintf(errstr, buflen, "Rule must begin with subject");
		return (-1);
	}
	subject = 0;
	subject_elements = 1;

	/* Search forward for object. */

	object = -1;
	for (i = 1; i < argc; i++)
		if (strcmp(argv[i], "object") == 0)
			object = i;

	if (object == -1) {
		snprintf(errstr, buflen, "Rule must contain an object");
		return (-1);
	}

	/* Search forward for mode. */
	mode = -1;
	for (i = object; i < argc; i++)
		if (strcmp(argv[i], "mode") == 0)
			mode = i;

	if (mode == -1) {
		snprintf(errstr, buflen, "Rule must contain mode");
		return (-1);
	}

	subject_elements_length = object - subject - 1;
	object_elements = object + 1;
	object_elements_length = mode - object_elements;
	mode_elements = mode + 1;
	mode_elements_length = argc - mode_elements;

	error = bsde_parse_subject(subject_elements_length,
	    argv + subject_elements, &rule->mbr_subject, buflen, errstr);
	if (error)
		return (-1);

	error = bsde_parse_object(object_elements_length,
	    argv + object_elements, &rule->mbr_object, buflen, errstr);
	if (error)
		return (-1);

	error = bsde_parse_mode(mode_elements_length, argv + mode_elements,
	    &rule->mbr_mode, buflen, errstr);
	if (error)
		return (-1);

	return (0);
}

int
bsde_parse_rule_string(const char *string, struct mac_bsdextended_rule *rule,
    size_t buflen, char *errstr)
{
	char *stringdup, *stringp, *argv[100], **ap;
	int argc, error;

	stringp = stringdup = strdup(string);
	while (*stringp == ' ' || *stringp == '\t')
		stringp++;

	argc = 0;
	for (ap = argv; (*ap = strsep(&stringp, " \t")) != NULL;) {
		argc++;
		if (**ap != '\0')
			if (++ap >= &argv[100])
				break;
	}

	error = bsde_parse_rule(argc, argv, rule, buflen, errstr);

	free(stringdup);

	return (error);
}

int
bsde_get_mib(const char *string, int *name, size_t *namelen)
{
	size_t len;
	int error;

	len = *namelen;
	error = sysctlnametomib(string, name, &len);
	if (error)
		return (error);

	*namelen = len;
	return (0);
}

static int
bsde_check_version(size_t buflen, char *errstr)
{
	size_t len;
	int error;
	int version;

	len = sizeof(version);
	error = sysctlbyname(MIB ".rule_version", &version, &len, NULL, 0);
	if (error) {
		snprintf(errstr, buflen, "version check failed: %s",
		    strerror(errno));
		return (-1);
	}
	if (version != MB_VERSION) {
		snprintf(errstr, buflen, "module v%d != library v%d",
		    version, MB_VERSION);
		return (-1);
	}
	return (0);
}

int
bsde_get_rule_count(size_t buflen, char *errstr)
{
	size_t len;
	int error;
	int rule_count;

	len = sizeof(rule_count);
	error = sysctlbyname(MIB ".rule_count", &rule_count, &len, NULL, 0);
	if (error) {
		snprintf(errstr, buflen, "%s", strerror(errno));
		return (-1);
	}
	if (len != sizeof(rule_count)) {
		snprintf(errstr, buflen, "Data error in %s.rule_count",
		    MIB);
		return (-1);
	}

	return (rule_count);
}

int
bsde_get_rule_slots(size_t buflen, char *errstr)
{
	size_t len;
	int error;
	int rule_slots;

	len = sizeof(rule_slots);
	error = sysctlbyname(MIB ".rule_slots", &rule_slots, &len, NULL, 0);
	if (error) {
		snprintf(errstr, buflen, "%s", strerror(errno));
		return (-1);
	}
	if (len != sizeof(rule_slots)) {
		snprintf(errstr, buflen, "Data error in %s.rule_slots", MIB);
		return (-1);
	}

	return (rule_slots);
}

/*
 * Returns 0 for success;
 * Returns -1 for failure;
 * Returns -2 for not present
 */
int
bsde_get_rule(int rulenum, struct mac_bsdextended_rule *rule, size_t errlen,
    char *errstr)
{
	int name[10];
	size_t len, size;
	int error;

	if (bsde_check_version(errlen, errstr) != 0)
		return (-1);

	len = 10;
	error = bsde_get_mib(MIB ".rules", name, &len);
	if (error) {
		snprintf(errstr, errlen, "%s: %s", MIB ".rules",
		    strerror(errno));
		return (-1);
	}

	size = sizeof(*rule);
	name[len] = rulenum;
	len++;
	error = sysctl(name, len, rule, &size, NULL, 0);
	if (error  == -1 && errno == ENOENT)
		return (-2);
	if (error) {
		snprintf(errstr, errlen, "%s.%d: %s", MIB ".rules",
		    rulenum, strerror(errno));
		return (-1);
	} else if (size != sizeof(*rule)) {
		snprintf(errstr, errlen, "Data error in %s.%d: %s",
		    MIB ".rules", rulenum, strerror(errno));
		return (-1);
	}

	return (0);
}

int
bsde_delete_rule(int rulenum, size_t buflen, char *errstr)
{
	struct mac_bsdextended_rule rule;
	int name[10];
	size_t len;
	int error;

	if (bsde_check_version(buflen, errstr) != 0)
		return (-1);

	len = 10;
	error = bsde_get_mib(MIB ".rules", name, &len);
	if (error) {
		snprintf(errstr, buflen, "%s: %s", MIB ".rules",
		    strerror(errno));
		return (-1);
	}

	name[len] = rulenum;
	len++;

	error = sysctl(name, len, NULL, NULL, &rule, 0);
	if (error) {
		snprintf(errstr, buflen, "%s.%d: %s", MIB ".rules",
		    rulenum, strerror(errno));
		return (-1);
	}

	return (0);
}

int
bsde_set_rule(int rulenum, struct mac_bsdextended_rule *rule, size_t buflen,
    char *errstr)
{
	int name[10];
	size_t len;
	int error;

	if (bsde_check_version(buflen, errstr) != 0)
		return (-1);

	len = 10;
	error = bsde_get_mib(MIB ".rules", name, &len);
	if (error) {
		snprintf(errstr, buflen, "%s: %s", MIB ".rules",
		    strerror(errno));
		return (-1);
	}

	name[len] = rulenum;
	len++;

	error = sysctl(name, len, NULL, NULL, rule, sizeof(*rule));
	if (error) {
		snprintf(errstr, buflen, "%s.%d: %s", MIB ".rules",
		    rulenum, strerror(errno));
		return (-1);
	}

	return (0);
}

int
bsde_add_rule(int *rulenum, struct mac_bsdextended_rule *rule, size_t buflen,
    char *errstr)
{
	char charstr[BUFSIZ];
	int name[10];
	size_t len;
	int error, rule_slots;

	if (bsde_check_version(buflen, errstr) != 0)
		return (-1);

	len = 10;
	error = bsde_get_mib(MIB ".rules", name, &len);
	if (error) {
		snprintf(errstr, buflen, "%s: %s", MIB ".rules",
		    strerror(errno));
		return (-1);
	}

	rule_slots = bsde_get_rule_slots(BUFSIZ, charstr);
	if (rule_slots == -1) {
		snprintf(errstr, buflen, "unable to get rule slots: %s",
		    strerror(errno));
		return (-1);
	}

	name[len] = rule_slots;
	len++;

	error = sysctl(name, len, NULL, NULL, rule, sizeof(*rule));
	if (error) {
		snprintf(errstr, buflen, "%s.%d: %s", MIB ".rules",
		    rule_slots, strerror(errno));
		return (-1);
	}

	if (rulenum != NULL)
		*rulenum = rule_slots;

	return (0);
}
