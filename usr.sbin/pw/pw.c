/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	David L. Nugent.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY DAVID L. NUGENT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL DAVID L. NUGENT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <err.h>
#include <fcntl.h>
#include <locale.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include "pw.h"

const char     *Modes[] = {
  "add", "del", "mod", "show", "next",
  NULL};
const char     *Which[] = {"user", "group", NULL};
static const char *Combo1[] = {
  "useradd", "userdel", "usermod", "usershow", "usernext",
  "lock", "unlock",
  "groupadd", "groupdel", "groupmod", "groupshow", "groupnext",
  NULL};
static const char *Combo2[] = {
  "adduser", "deluser", "moduser", "showuser", "nextuser",
  "lock", "unlock",
  "addgroup", "delgroup", "modgroup", "showgroup", "nextgroup",
  NULL};

struct pwf PWF =
{
	PWF_REGULAR,
	setpwent,
	endpwent,
	getpwent,
	getpwuid,
	getpwnam,
	setgrent,
	endgrent,
	getgrent,
	getgrgid,
	getgrnam,

};
struct pwf VPWF =
{
	PWF_ALT,
	vsetpwent,
	vendpwent,
	vgetpwent,
	vgetpwuid,
	vgetpwnam,
	vsetgrent,
	vendgrent,
	vgetgrent,
	vgetgrgid,
	vgetgrnam,
};

static int (*cmdfunc[W_NUM][M_NUM])(int argc, char **argv, char *_name) = {
	{ /* user */
		pw_user_add,
		pw_user_del,
		pw_user_mod,
		pw_user_show,
		pw_user_next,
		pw_user_lock,
		pw_user_unlock,
	},
	{ /* group */
		pw_group_add,
		pw_group_del,
		pw_group_mod,
		pw_group_show,
		pw_group_next,
	}
};

struct pwconf conf;

static int	getindex(const char *words[], const char *word);
static void	cmdhelp(int mode, int which);

int
main(int argc, char *argv[])
{
	int		mode = -1, which = -1, tmp;
	struct stat	st;
	char		arg, *arg1;
	bool		relocated, nis;

	arg1 = NULL;
	relocated = nis = false;
	memset(&conf, 0, sizeof(conf));
	strlcpy(conf.rootdir, "/", sizeof(conf.rootdir));
	strlcpy(conf.etcpath, _PATH_PWD, sizeof(conf.etcpath));
	conf.fd = -1;
	conf.checkduplicate = true;

	setlocale(LC_ALL, "");

	/*
	 * Break off the first couple of words to determine what exactly
	 * we're being asked to do
	 */
	while (argc > 1) {
		if (*argv[1] == '-') {
			/*
			 * Special case, allow pw -V<dir> <operation> [args] for scripts etc.
			 */
			arg = argv[1][1];
			if (arg == 'V' || arg == 'R') {
				if (relocated)
					errx(EXIT_FAILURE, "Both '-R' and '-V' "
					    "specified, only one accepted");
				relocated = true;
				optarg = &argv[1][2];
				if (*optarg == '\0') {
					if (stat(argv[2], &st) != 0)
						errx(EX_OSFILE, \
						    "no such directory `%s'",
						    argv[2]);
					if (!S_ISDIR(st.st_mode))
						errx(EX_OSFILE, "`%s' not a "
						    "directory", argv[2]);
					optarg = argv[2];
					++argv;
					--argc;
				}
				memcpy(&PWF, &VPWF, sizeof PWF);
				if (arg == 'R') {
					strlcpy(conf.rootdir, optarg,
					    sizeof(conf.rootdir));
					PWF._altdir = PWF_ROOTDIR;
				}
				snprintf(conf.etcpath, sizeof(conf.etcpath),
				    "%s%s", optarg, arg == 'R' ?
				    _PATH_PWD : "");
			} else
				break;
		}
		else if (mode == -1 && (tmp = getindex(Modes, argv[1])) != -1)
			mode = tmp;
		else if (which == -1 && (tmp = getindex(Which, argv[1])) != -1)
			which = tmp;
		else if ((mode == -1 && which == -1) &&
			 ((tmp = getindex(Combo1, argv[1])) != -1 ||
			  (tmp = getindex(Combo2, argv[1])) != -1)) {
			which = tmp / M_NUM;
			mode = tmp % M_NUM;
		} else if (strcmp(argv[1], "help") == 0 && argv[2] == NULL)
			cmdhelp(mode, which);
		else if (which != -1 && mode != -1)
				arg1 = argv[1];
		else
			errx(EX_USAGE, "unknown keyword `%s'", argv[1]);
		++argv;
		--argc;
	}

	/*
	 * Bail out unless the user is specific!
	 */
	if (mode == -1 || which == -1)
		cmdhelp(mode, which);

	conf.rootfd = open(conf.rootdir, O_DIRECTORY|O_CLOEXEC);
	if (conf.rootfd == -1)
		errx(EXIT_FAILURE, "Unable to open '%s'", conf.rootdir);

	return (cmdfunc[which][mode](argc, argv, arg1));
}


static int
getindex(const char *words[], const char *word)
{
	int	i = 0;

	while (words[i]) {
		if (strcmp(words[i], word) == 0)
			return (i);
		i++;
	}
	return (-1);
}


/*
 * This is probably an overkill for a cmdline help system, but it reflects
 * the complexity of the command line.
 */

static void
cmdhelp(int mode, int which)
{
	if (which == -1)
		fprintf(stderr, "usage:\n  pw [user|group|lock|unlock] [add|del|mod|show|next] [help|switches/values]\n");
	else if (mode == -1)
		fprintf(stderr, "usage:\n  pw %s [add|del|mod|show|next] [help|switches/values]\n", Which[which]);
	else {

		/*
		 * We need to give mode specific help
		 */
		static const char *help[W_NUM][M_NUM] =
		{
			{
				"usage: pw useradd [name] [switches]\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-C config      configuration file\n"
				"\t-q             quiet operation\n"
				"  Adding users:\n"
				"\t-n name        login name\n"
				"\t-u uid         user id\n"
				"\t-c comment     user name/comment\n"
				"\t-d directory   home directory\n"
				"\t-e date        account expiry date\n"
				"\t-p date        password expiry date\n"
				"\t-g grp         initial group\n"
				"\t-G grp1,grp2   additional groups\n"
				"\t-m [ -k dir ]  create and set up home\n"
				"\t-M mode        home directory permissions\n"
				"\t-s shell       name of login shell\n"
				"\t-o             duplicate uid ok\n"
				"\t-L class       user class\n"
				"\t-h fd          read password on fd\n"
				"\t-H fd          read encrypted password on fd\n"
				"\t-Y             update NIS maps\n"
				"\t-N             no update\n"
				"  Setting defaults:\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-D             set user defaults\n"
				"\t-b dir         default home root dir\n"
				"\t-e period      default expiry period\n"
				"\t-p period      default password change period\n"
				"\t-g group       default group\n"
				"\t-G grp1,grp2   additional groups\n"
				"\t-L class       default user class\n"
				"\t-k dir         default home skeleton\n"
				"\t-M mode        home directory permissions\n"
				"\t-u min,max     set min,max uids\n"
				"\t-i min,max     set min,max gids\n"
				"\t-w method      set default password method\n"
				"\t-s shell       default shell\n"
				"\t-y path        set NIS passwd file path\n",
				"usage: pw userdel [uid|name] [switches]\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-n name        login name\n"
				"\t-u uid         user id\n"
				"\t-Y             update NIS maps\n"
				"\t-y path        set NIS passwd file path\n"
				"\t-r             remove home & contents\n",
				"usage: pw usermod [uid|name] [switches]\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-C config      configuration file\n"
				"\t-q             quiet operation\n"
				"\t-F             force add if no user\n"
				"\t-n name        login name\n"
				"\t-u uid         user id\n"
				"\t-c comment     user name/comment\n"
				"\t-d directory   home directory\n"
				"\t-e date        account expiry date\n"
				"\t-p date        password expiry date\n"
				"\t-g grp         initial group\n"
				"\t-G grp1,grp2   additional groups\n"
				"\t-l name        new login name\n"
				"\t-L class       user class\n"
				"\t-m [ -k dir ]  create and set up home\n"
				"\t-M mode        home directory permissions\n"
				"\t-s shell       name of login shell\n"
				"\t-w method      set new password using method\n"
				"\t-h fd          read password on fd\n"
				"\t-H fd          read encrypted password on fd\n"
				"\t-Y             update NIS maps\n"
				"\t-y path        set NIS passwd file path\n"
				"\t-N             no update\n",
				"usage: pw usershow [uid|name] [switches]\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-n name        login name\n"
				"\t-u uid         user id\n"
				"\t-F             force print\n"
				"\t-P             prettier format\n"
				"\t-a             print all users\n"
				"\t-7             print in v7 format\n",
				"usage: pw usernext [switches]\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-C config      configuration file\n"
				"\t-q             quiet operation\n",
				"usage pw: lock [switches]\n"
				"\t-V etcdir      alternate /etc locations\n"
				"\t-C config      configuration file\n"
				"\t-q             quiet operation\n",
				"usage pw: unlock [switches]\n"
				"\t-V etcdir      alternate /etc locations\n"
				"\t-C config      configuration file\n"
				"\t-q             quiet operation\n"
			},
			{
				"usage: pw groupadd [group|gid] [switches]\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-C config      configuration file\n"
				"\t-q             quiet operation\n"
				"\t-n group       group name\n"
				"\t-g gid         group id\n"
				"\t-M usr1,usr2   add users as group members\n"
				"\t-o             duplicate gid ok\n"
				"\t-Y             update NIS maps\n"
				"\t-N             no update\n",
				"usage: pw groupdel [group|gid] [switches]\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-n name        group name\n"
				"\t-g gid         group id\n"
				"\t-Y             update NIS maps\n",
				"usage: pw groupmod [group|gid] [switches]\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-C config      configuration file\n"
				"\t-q             quiet operation\n"
				"\t-F             force add if not exists\n"
				"\t-n name        group name\n"
				"\t-g gid         group id\n"
				"\t-M usr1,usr2   replaces users as group members\n"
				"\t-m usr1,usr2   add users as group members\n"
				"\t-d usr1,usr2   delete users as group members\n"
				"\t-l name        new group name\n"
				"\t-Y             update NIS maps\n"
				"\t-N             no update\n",
				"usage: pw groupshow [group|gid] [switches]\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-n name        group name\n"
				"\t-g gid         group id\n"
				"\t-F             force print\n"
				"\t-P             prettier format\n"
				"\t-a             print all accounting groups\n",
				"usage: pw groupnext [switches]\n"
				"\t-V etcdir      alternate /etc location\n"
				"\t-R rootdir     alternate root directory\n"
				"\t-C config      configuration file\n"
				"\t-q             quiet operation\n"
			}
		};

		fprintf(stderr, "%s", help[which][mode]);
	}
	exit(EXIT_FAILURE);
}
