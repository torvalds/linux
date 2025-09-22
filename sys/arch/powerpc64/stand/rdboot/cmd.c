/*	$OpenBSD: cmd.c,v 1.4 2025/08/22 20:05:31 gkoehler Exp $	*/

/*
 * Copyright (c) 1997-1999 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/select.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "cmd.h"
#include "disk.h"

static int Xboot(void);
static int Xecho(void);
static int Xhelp(void);
static int Xls(void);
static int Xnop(void);
static int Xreboot(void);
#ifdef MACHINE_CMD
static int Xmachine(void);
extern const struct cmd_table MACHINE_CMD[];
#endif
extern int Xset(void);

#ifdef CHECK_SKIP_CONF
extern int CHECK_SKIP_CONF(void);
#endif

extern const struct cmd_table cmd_set[];
const struct cmd_table cmd_table[] = {
	{"#",      CMDT_CMD, Xnop},  /* XXX must be first */
	{"boot",   CMDT_CMD, Xboot},
	{"echo",   CMDT_CMD, Xecho},
	{"help",   CMDT_CMD, Xhelp},
	{"ls",     CMDT_CMD, Xls},
#ifdef MACHINE_CMD
	{"machine",CMDT_MDC, Xmachine},
#endif
	{"reboot", CMDT_CMD, Xreboot},
	{"set",    CMDT_SET, Xset},
	{NULL, 0},
};

static void ls(const char *, struct stat *);
static int readline(char *, size_t, int);
char *nextword(char *);
static char *whatcmd(const struct cmd_table **ct, char *);
static char *qualify(char *);

char cmd_buf[CMD_BUFF_SIZE];

int
getcmd(void)
{
	cmd.cmd = NULL;

	if (!readline(cmd_buf, sizeof(cmd_buf), cmd.timeout))
		cmd.cmd = cmd_table;

	return docmd();
}

int
read_conf(void)
{
	struct stat sb;
	const char *path;
	int fd, rc = 0;

#ifdef CHECK_SKIP_CONF
	if (CHECK_SKIP_CONF()) {
		printf("boot.conf processing skipped at operator request\n");
		cmd.timeout = 0;
		return -1;		/* Pretend file wasn't found */
	}
#endif

	path = disk_open(qualify(cmd.conf));
	if (path == NULL) {
		fprintf(stderr, "cannot open device for reading %s: %s\n",
		    cmd.conf, strerror(errno));
		return -1;
	}
	if ((fd = open(path, O_RDONLY)) == -1) {
		if (errno != ENOENT && errno != ENXIO) {
			fprintf(stderr, "%s: open(%s): %s\n", __func__,
			    cmd.path, strerror(errno));
			rc = 0;
		} else
			rc = -1;
		goto out;
	}

	(void) fstat(fd, &sb);
	if (sb.st_uid || (sb.st_mode & 2)) {
		fprintf(stderr, "non-secure %s, will not proceed\n", cmd.path);
		rc = -1;
		goto out;
	}

	do {
		char *p = cmd_buf;

		cmd.cmd = NULL;
		do {
			rc = read(fd, p, 1);
		} while (rc > 0 && *p++ != '\n' &&
		    (p-cmd_buf) < sizeof(cmd_buf));

		if (rc < 0) {			/* Error from read() */
			fprintf(stderr, "%s: %s\n", cmd.path, strerror(errno));
			break;
		}

		if (rc == 0) {			/* eof from read() */
			if (p != cmd_buf) {	/* Line w/o trailing \n */
				*p = '\0';
				rc = docmd();
				break;
			}
		} else {			/* rc > 0, read a char */
			p--;			/* Get back to last character */

			if (*p != '\n') {	/* Line was too long */
				fprintf(stderr, "%s: line too long\n",
				    cmd.path);

				/* Don't want to run the truncated command */
				rc = -1;
			}
			*p = '\0';
		}
	} while (rc > 0 && !(rc = docmd()));

out:
	if (fd != -1)
		close(fd);
	disk_close();
	return rc;
}

int
docmd(void)
{
	char *p = NULL;
	const struct cmd_table *ct = cmd_table, *cs;

	cmd.argc = 1;
	if (cmd.cmd == NULL) {

		/* command */
		for (p = cmd_buf; *p == ' ' || *p == '\t'; p++)
			;
		if (*p == '#' || *p == '\0') { /* comment or empty string */
#ifdef DEBUG
			printf("rem\n");
#endif
			return 0;
		}
		ct = cmd_table;
		cs = NULL;
		cmd.argv[cmd.argc] = p; /* in case it's shortcut boot */
		p = whatcmd(&ct, p);
		if (ct == NULL) {
			cmd.argc++;
			ct = cmd_table;
		} else if (ct->cmd_type == CMDT_SET && p != NULL) {
			cs = cmd_set;
#ifdef MACHINE_CMD
		} else if (ct->cmd_type == CMDT_MDC && p != NULL) {
			cs = MACHINE_CMD;
#endif
		}

		if (cs != NULL) {
			p = whatcmd(&cs, p);
			if (cs == NULL) {
				printf("%s: syntax error\n", ct->cmd_name);
				return 0;
			}
			ct = cs;
		}
		cmd.cmd = ct;
	}

	cmd.argv[0] = ct->cmd_name;
	while (p && cmd.argc+1 < sizeof(cmd.argv) / sizeof(cmd.argv[0])) {
		cmd.argv[cmd.argc++] = p;
		p = nextword(p);
	}
	cmd.argv[cmd.argc] = NULL;

	return (*cmd.cmd->cmd_exec)();
}

static char *
whatcmd(const struct cmd_table **ct, char *p)
{
	char *q;
	int l;

	q = nextword(p);

	for (l = 0; p[l]; l++)
		;

	while ((*ct)->cmd_name != NULL && strncmp(p, (*ct)->cmd_name, l))
		(*ct)++;

	if ((*ct)->cmd_name == NULL)
		*ct = NULL;

	return q;
}

static int
readline(char *buf, size_t n, int to)
{
	struct termios saved_tio, tio;
	struct timeval tv;
	fd_set fdset;
	char *p;
	int c, timed_out = 0;
#ifdef DEBUG
	extern int debug;
#endif

	/* Only do timeout if greater than 0 */
	if (to > 0) {
		/* Switch to non-canonical mode for timeout detection. */
		tcgetattr(STDIN_FILENO, &saved_tio);
		tio = saved_tio;
		tio.c_lflag &= ~(ECHO | ICANON);
		tcsetattr(STDIN_FILENO, TCSANOW, &tio);

		FD_ZERO(&fdset);
		FD_SET(STDIN_FILENO, &fdset);
		tv.tv_sec = to;
		tv.tv_usec = 0;
		if (select(STDIN_FILENO + 1, &fdset, NULL, NULL, &tv) == 0)
			timed_out = 1;
		else if ((c = getchar()) != EOF) {
			putchar(c);		/* Echo. */
			ungetc(c, stdin);
		}

		/* Restore canonical mode. */
		tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);

		if (timed_out) {
			strlcpy(buf, "boot", 5);
			putchar('\n');
			return strlen(buf);
		}
	}

	/* User has typed something.  Turn off timeouts. */
	cmd.timeout = 0;

	if (fgets(buf, n, stdin) == NULL)
		return 0;

	/* Strip trailing newline. */
	p = strchr(buf, '\n');
	if (p != NULL)
		*p = '\0';

	return strlen(buf);
}

/*
 * Search for spaces/tabs after the current word. If found, \0 the
 * first one.  Then pass a pointer to the first character of the
 * next word, or NULL if there is no next word.
 */
char *
nextword(char *p)
{
	/* skip blanks */
	while (*p && *p != '\t' && *p != ' ')
		p++;
	if (*p) {
		*p++ = '\0';
		while (*p == '\t' || *p == ' ')
			p++;
	}
	if (*p == '\0')
		p = NULL;
	return p;
}

static void
print_help(const struct cmd_table *ct)
{
	for (; ct->cmd_name != NULL; ct++)
		printf(" %s", ct->cmd_name);
	putchar('\n');
}

static int
Xhelp(void)
{
	printf("commands:");
	print_help(cmd_table);
#ifdef MACHINE_CMD
	return Xmachine();
#else
	return 0;
#endif
}

#ifdef MACHINE_CMD
static int
Xmachine(void)
{
	printf("machine:");
	print_help(MACHINE_CMD);
	return 0;
}
#endif

static int
Xecho(void)
{
	int i;

	for (i = 1; i < cmd.argc; i++)
		printf("%s ", cmd.argv[i]);
	putchar('\n');
	return 0;
}

static int
Xls(void)
{
	struct stat sb;
	const char *path;
	DIR *dir;
	struct dirent *dent;
	int dirfd, oldcwd;

	path = disk_open(qualify(cmd.argv[1] ? cmd.argv[1] : "/."));
	if (path == NULL)
		return 0;

	if (stat(path, &sb) < 0) {
		printf("stat(%s): %s\n", cmd.path, strerror(errno));
		goto out;
	}

	if ((sb.st_mode & S_IFMT) != S_IFDIR)
		ls(path, &sb);
	else {
		oldcwd = open(".", O_RDONLY);

		dirfd = open(path, O_RDONLY);
		if (dirfd < 0) {
			printf("opendir(%s): %s\n", cmd.path, strerror(errno));
			close(oldcwd);
			goto out;
		}
		if ((dir = fdopendir(dirfd)) < 0) {
			printf("opendir(%s): %s\n", cmd.path, strerror(errno));
			close(dirfd);
			close(oldcwd);
			goto out;
		}
		fchdir(dirfd);
		while ((dent = readdir(dir)) != NULL) {
			if (fstatat(dirfd, dent->d_name, &sb,
			    AT_SYMLINK_NOFOLLOW) < 0)
				printf("stat(%s): %s\n", dent->d_name,
				    strerror(errno));
			else
				ls(dent->d_name, &sb);
		}
		closedir(dir);

		fchdir(oldcwd);
		close(oldcwd);
	}

out:
	disk_close();
	return 0;
}

#define lsrwx(mode,s) \
	putchar ((mode) & S_IROTH? 'r' : '-'); \
	putchar ((mode) & S_IWOTH? 'w' : '-'); \
	putchar ((mode) & S_IXOTH? *(s): (s)[1]);

static void
ls(const char *name, struct stat *sb)
{
	putchar("-fc-d-b---l-s-w-"[(sb->st_mode & S_IFMT) >> 12]);
	lsrwx(sb->st_mode >> 6, (sb->st_mode & S_ISUID? "sS" : "x-"));
	lsrwx(sb->st_mode >> 3, (sb->st_mode & S_ISGID? "sS" : "x-"));
	lsrwx(sb->st_mode     , (sb->st_mode & S_ISTXT? "tT" : "x-"));

	printf (" %u,%u\t%lu\t%s\n", sb->st_uid, sb->st_gid,
	    (u_long)sb->st_size, name);
}
#undef lsrwx

int doboot = 1;

static int
Xnop(void)
{
	if (doboot) {
		doboot = 0;
		return (Xboot());
	}

	return 0;
}

static int
Xboot(void)
{
	if (cmd.argc > 1 && cmd.argv[1][0] != '-') {
		qualify((cmd.argv[1]? cmd.argv[1]: cmd.image));
		if (bootparse(2))
			return 0;
	} else {
		if (bootparse(1))
			return 0;
		snprintf(cmd.path, sizeof cmd.path, "%s:%s",
		    cmd.bootdev, cmd.image);
	}

	return 1;
}

/*
 * Qualifies the path adding necessary dev
 */

static char *
qualify(char *name)
{
	char *p;

	for (p = name; *p; p++)
		if (*p == ':')
			break;
	if (*p == ':')
		strlcpy(cmd.path, name, sizeof(cmd.path));
	else
		snprintf(cmd.path, sizeof cmd.path, "%s:%s",
		    cmd.bootdev, name);
	return cmd.path;
}

static int
Xreboot(void)
{
	printf("Rebooting...\n");
	reboot(0);
	return 0; /* just in case */
}

int
upgrade(void)
{
	struct stat sb;
	const char *path;
	int ret = 0;

	path = disk_open(qualify("/bsd.upgrade"));
	if (path == NULL)
		return 0;
	if (stat(path, &sb) == 0 && S_ISREG(sb.st_mode)) {
		ret = 1;
		if ((sb.st_mode & S_IXUSR) == 0) {
			printf("/bsd.upgrade is not u+x\n");
			ret = 0;
		}
	}
	disk_close();

	return ret;
}
