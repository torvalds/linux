/*	$OpenBSD: cmd.c,v 1.70 2023/02/23 19:48:22 miod Exp $	*/

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

#include <libsa.h>
#include <lib/libkern/funcs.h>

#include "cmd.h"

#define CTRL(c)	((c)&0x1f)

static int Xboot(void);
static int Xecho(void);
static int Xhelp(void);
static int Xhexdump(void);
static int Xls(void);
static int Xnop(void);
static int Xreboot(void);
#ifdef BOOT_STTY
static int Xstty(void);
#endif
static int Xtime(void);
#ifdef MACHINE_CMD
static int Xmachine(void);
extern const struct cmd_table MACHINE_CMD[];
#endif
extern int Xset(void);
extern int Xenv(void);

#ifdef CHECK_SKIP_CONF
extern int CHECK_SKIP_CONF(void);
#endif

extern const struct cmd_table cmd_set[];
const struct cmd_table cmd_table[] = {
	{"#",      CMDT_CMD, Xnop},  /* XXX must be first */
	{"boot",   CMDT_CMD, Xboot},
	{"echo",   CMDT_CMD, Xecho},
	{"env",    CMDT_CMD, Xenv},
	{"help",   CMDT_CMD, Xhelp},
	{"hexdump",CMDT_CMD, Xhexdump},
	{"ls",     CMDT_CMD, Xls},
#ifdef MACHINE_CMD
	{"machine",CMDT_MDC, Xmachine},
#endif
	{"reboot", CMDT_CMD, Xreboot},
	{"set",    CMDT_SET, Xset},
#ifdef BOOT_STTY
	{"stty",   CMDT_CMD, Xstty},
#endif
	{"time",   CMDT_CMD, Xtime},
	{NULL, 0},
};

static void ls(char *, struct stat *);
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
#ifndef INSECURE
	struct stat sb;
#endif
	int fd, rc = 0;

#ifdef CHECK_SKIP_CONF
	if (CHECK_SKIP_CONF()) {
		printf("boot.conf processing skipped at operator request\n");
		cmd.timeout = 0;
		return -1;		/* Pretend file wasn't found */
	}
#endif

	if ((fd = open(qualify(cmd.conf), O_RDONLY)) < 0) {
		if (errno != ENOENT && errno != ENXIO) {
			printf("open(%s): %s\n", cmd.path, strerror(errno));
			return 0;
		}
		return -1;
	}

#ifndef INSECURE
	(void) fstat(fd, &sb);
	if (sb.st_uid || (sb.st_mode & 2)) {
		printf("non-secure %s, will not proceed\n", cmd.path);
		close(fd);
		return -1;
	}
#endif

	do {
		char *p = cmd_buf;

		cmd.cmd = NULL;
		do {
			rc = read(fd, p, 1);
		} while (rc > 0 && *p++ != '\n' &&
		    (p-cmd_buf) < sizeof(cmd_buf));

		if (rc < 0) {			/* Error from read() */
			printf("%s: %s\n", cmd.path, strerror(errno));
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
				printf("%s: line too long\n", cmd.path);

				/* Don't want to run the truncated command */
				rc = -1;
			}
			*p = '\0';
		}
	} while (rc > 0 && !(rc = docmd()));

	close(fd);
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
#ifdef DEBUG
	extern int debug;
#endif
	char *p = buf, ch;

	/* Only do timeout if greater than 0 */
	if (to > 0) {
		time_t tt = getsecs() + to;
#ifdef DEBUG
		if (debug > 2)
			printf ("readline: timeout(%d) at %u\n", to, tt);
#endif
		while (!cnischar() && getsecs() < tt)
			continue;

		if (!cnischar()) {
			strlcpy(buf, "boot", 5);
			putchar('\n');
			return strlen(buf);
		}
	} else
		while (!cnischar())
			;

	/* User has typed something.  Turn off timeouts. */
	cmd.timeout = 0;

	while (1) {
		switch ((ch = getchar())) {
		case CTRL('u'):
			while (p > buf) {
				putchar('\177');
				p--;
			}
			continue;
		case '\n':
		case '\r':
			*p = '\0';
			break;
		case '\b':
		case '\177':
			if (p > buf) {
				putchar('\177');
				p--;
			}
			continue;
		default:
			if (ch >= ' ' && ch < '\177') {
				if (p - buf < n-1)
					*p++ = ch;
				else {
					putchar('\007');
					putchar('\177');
				}
			}
			continue;
		}
		break;
	}

	return p - buf;
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

static int
Xhexdump(void)
{
	long long val[2];
	char *ep;
	int i;

	if (cmd.argc != 3) {
		printf("hexdump addr size\n");
		return 0;
	}

	for (i = 1; i < cmd.argc; i++) {
		val[i-1] = strtoll(cmd.argv[i], &ep, 0);
		if (cmd.argv[i][0] == '\0' || *ep != '\0') {
			printf("bad '%c' in \"%s\"\n", *ep, cmd.argv[i]);
			return 0;
		}
	}
	hexdump((void *)(unsigned long)val[0], val[1]);
	return 0;
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

#ifdef BOOT_STTY
static int
Xstty(void)
{
	int sp;
	char *cp;
	dev_t dev;

	if (cmd.argc == 1) {
		printf("%s speed is %d\n", ttyname(0), cnspeed(0, -1));
		return 0;
	}
	dev = ttydev(cmd.argv[1]);
	if (dev == NODEV) {
		printf("%s not a console device\n", cmd.argv[1]);
		return 0;
	}

	if (cmd.argc == 2)
		printf("%s speed is %d\n", cmd.argv[1],
		    cnspeed(dev, -1));
	else {
		sp = 0;
		for (cp = cmd.argv[2]; isdigit(*cp); cp++)
			sp = sp * 10 + (*cp - '0');
		cnspeed(dev, sp);
	}
	return 0;
}
#endif

static int
Xtime(void)
{
	time_t tt = getsecs();

	if (cmd.argc == 1)
		printf(ctime(&tt));

	return 0;
}

static int
Xls(void)
{
	struct stat sb;
	char *p;
	int fd;

	if (stat(qualify((cmd.argv[1]? cmd.argv[1]: "/.")), &sb) < 0) {
		printf("stat(%s): %s\n", cmd.path, strerror(errno));
		return 0;
	}

	if ((sb.st_mode & S_IFMT) != S_IFDIR)
		ls(cmd.path, &sb);
	else {
		if ((fd = opendir(cmd.path)) < 0) {
			printf("opendir(%s): %s\n", cmd.path,
			    strerror(errno));
			return 0;
		}

		/* no strlen in lib !!! */
		for (p = cmd.path; *p; p++)
			;
		*p++ = '/';
		*p = '\0';

		while (readdir(fd, p) >= 0) {
			if (stat(cmd.path, &sb) < 0)
				printf("stat(%s): %s\n", cmd.path,
				    strerror(errno));
			else
				ls(p, &sb);
		}
		closedir (fd);
	}
	return 0;
}

#define lsrwx(mode,s) \
	putchar ((mode) & S_IROTH? 'r' : '-'); \
	putchar ((mode) & S_IWOTH? 'w' : '-'); \
	putchar ((mode) & S_IXOTH? *(s): (s)[1]);

static void
ls(char *name, struct stat *sb)
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
	exit();
	return 0; /* just in case */
}

int
upgrade(void)
{
	struct stat sb;

	if (stat(qualify(("/bsd.upgrade")), &sb) < 0)
		return 0;
	if ((sb.st_mode & S_IXUSR) == 0) {
		printf("/bsd.upgrade is not u+x\n");
		return 0;
	}
	return 1;
}
