
/*
 * main.c
 *
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * $Whistle: main.c,v 1.12 1999/11/29 19:17:46 archie Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#ifdef EDITLINE
#include <signal.h>
#include <histedit.h>
#include <pthread.h>
#endif

#include <netgraph.h>

#include "ngctl.h"

#define PROMPT			"+ "
#define MAX_ARGS		512
#define WHITESPACE		" \t\r\n\v\f"
#define DUMP_BYTES_PER_LINE	16

/* Internal functions */
static int	ReadFile(FILE *fp);
static void	ReadSockets(fd_set *);
static int	DoParseCommand(const char *line);
static int	DoCommand(int ac, char **av);
static int	DoInteractive(void);
static const	struct ngcmd *FindCommand(const char *string);
static int	MatchCommand(const struct ngcmd *cmd, const char *s);
static void	Usage(const char *msg);
static int	ReadCmd(int ac, char **av);
static int	HelpCmd(int ac, char **av);
static int	QuitCmd(int ac, char **av);
#ifdef EDITLINE
static volatile sig_atomic_t unblock;
static pthread_mutex_t	mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	cond = PTHREAD_COND_INITIALIZER;
#endif

/* List of commands */
static const struct ngcmd *const cmds[] = {
	&config_cmd,
	&connect_cmd,
	&debug_cmd,
	&dot_cmd,
	&help_cmd,
	&list_cmd,
	&mkpeer_cmd,
	&msg_cmd,
	&name_cmd,
	&read_cmd,
	&rmhook_cmd,
	&show_cmd,
	&shutdown_cmd,
	&status_cmd,
	&types_cmd,
	&write_cmd,
	&quit_cmd,
	NULL
};

/* Commands defined in this file */
const struct ngcmd read_cmd = {
	ReadCmd,
	"read <filename>",
	"Read and execute commands from a file",
	NULL,
	{ "source", "." }
};
const struct ngcmd help_cmd = {
	HelpCmd,
	"help [command]",
	"Show command summary or get more help on a specific command",
	NULL,
	{ "?" }
};
const struct ngcmd quit_cmd = {
	QuitCmd,
	"quit",
	"Exit program",
	NULL,
	{ "exit" }
};

/* Our control and data sockets */
int	csock, dsock;

/*
 * main()
 */
int
main(int ac, char *av[])
{
	char	name[NG_NODESIZ];
	int	interactive = isatty(0) && isatty(1);
	FILE	*fp = NULL;
	int	ch, rtn = 0;

	/* Set default node name */
	snprintf(name, sizeof(name), "ngctl%d", getpid());

	/* Parse command line */
	while ((ch = getopt(ac, av, "df:n:")) != -1) {
		switch (ch) {
		case 'd':
			NgSetDebug(NgSetDebug(-1) + 1);
			break;
		case 'f':
			if (strcmp(optarg, "-") == 0)
				fp = stdin;
			else if ((fp = fopen(optarg, "r")) == NULL)
				err(EX_NOINPUT, "%s", optarg);
			break;
		case 'n':
			snprintf(name, sizeof(name), "%s", optarg);
			break;
		case '?':
		default:
			Usage((char *)NULL);
			break;
		}
	}
	ac -= optind;
	av += optind;

	/* Create a new socket node */
	if (NgMkSockNode(name, &csock, &dsock) < 0)
		err(EX_OSERR, "can't create node");

	/* Do commands as requested */
	if (ac == 0) {
		if (fp != NULL) {
			rtn = ReadFile(fp);
		} else if (interactive) {
			rtn = DoInteractive();
		} else
			Usage("no command specified");
	} else {
		rtn = DoCommand(ac, av);
	}

	/* Convert command return code into system exit code */
	switch (rtn) {
	case CMDRTN_OK:
	case CMDRTN_QUIT:
		rtn = 0;
		break;
	case CMDRTN_USAGE:
		rtn = EX_USAGE;
		break;
	case CMDRTN_ERROR:
		rtn = EX_OSERR;
		break;
	}
	return (rtn);
}

/*
 * Process commands from a file
 */
static int
ReadFile(FILE *fp)
{
	char line[LINE_MAX];
	int num, rtn;

	for (num = 1; fgets(line, sizeof(line), fp) != NULL; num++) {
		if (*line == '#')
			continue;
		if ((rtn = DoParseCommand(line)) != 0) {
			warnx("line %d: error in file", num);
			return (rtn);
		}
	}
	return (CMDRTN_OK);
}

#ifdef EDITLINE
/* Signal handler for Monitor() thread. */
static void
Unblock(int signal __unused)
{

	unblock = 1;
}

/*
 * Thread that monitors csock and dsock while main thread
 * can be blocked in el_gets().
 */
static void *
Monitor(void *v __unused)
{
	struct sigaction act;
	const int maxfd = MAX(csock, dsock) + 1;

	act.sa_handler = Unblock;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGUSR1, &act, NULL);

	pthread_mutex_lock(&mutex);
	for (;;) {
		fd_set rfds;

		/* See if any data or control messages are arriving. */
		FD_ZERO(&rfds);
		FD_SET(csock, &rfds);
		FD_SET(dsock, &rfds);
		unblock = 0;
		if (select(maxfd, &rfds, NULL, NULL, NULL) <= 0) {
			if (errno == EINTR) {
				if (unblock == 1)
					pthread_cond_wait(&cond, &mutex);
				continue;
			}
			err(EX_OSERR, "select");
		}
		ReadSockets(&rfds);
	}

	return (NULL);
}

static char *
Prompt(EditLine *el __unused)
{

	return (PROMPT);
}

/*
 * Here we start a thread, that will monitor the netgraph
 * sockets and catch any unexpected messages or data on them,
 * that can arrive while user edits his/her commands.
 *
 * Whenever we expect data on netgraph sockets, we send signal
 * to monitoring thread. The signal forces it to exit select()
 * system call and sleep on condvar until we wake it. While
 * monitoring thread sleeps, we can do our work with netgraph
 * sockets.
 */
static int
DoInteractive(void)
{
	pthread_t monitor;
	EditLine *el;
	History *hist;
	HistEvent hev = { 0, "" };

	(*help_cmd.func)(0, NULL);
	pthread_create(&monitor, NULL, Monitor, NULL);
	el = el_init(getprogname(), stdin, stdout, stderr);
	if (el == NULL)
		return (CMDRTN_ERROR);
	el_set(el, EL_PROMPT, Prompt);
	el_set(el, EL_SIGNAL, 1);
	el_set(el, EL_EDITOR, "emacs");
	hist = history_init();
	if (hist == NULL)
		return (CMDRTN_ERROR);
	history(hist, &hev, H_SETSIZE, 100);
	history(hist, &hev, H_SETUNIQUE, 1);
	el_set(el, EL_HIST, history, (const char *)hist);
	el_source(el, NULL);

	for (;;) {
		const char *buf;
		int count;

		if ((buf = el_gets(el, &count)) == NULL) {
			printf("\n");
			break;
		}
		history(hist, &hev, H_ENTER, buf);
		pthread_kill(monitor, SIGUSR1);
		pthread_mutex_lock(&mutex);
		if (DoParseCommand(buf) == CMDRTN_QUIT) {
			pthread_mutex_unlock(&mutex);
			break;
		}
		pthread_cond_signal(&cond);
		pthread_mutex_unlock(&mutex);
	}

	history_end(hist);
	el_end(el);
	pthread_cancel(monitor);

	return (CMDRTN_QUIT);
}

#else /* !EDITLINE */

/*
 * Interactive mode w/o libedit functionality.
 */
static int
DoInteractive(void)
{
	const int maxfd = MAX(csock, dsock) + 1;

	(*help_cmd.func)(0, NULL);
	while (1) {
		struct timeval tv;
		fd_set rfds;

		/* See if any data or control messages are arriving */
		FD_ZERO(&rfds);
		FD_SET(csock, &rfds);
		FD_SET(dsock, &rfds);
		memset(&tv, 0, sizeof(tv));
		if (select(maxfd, &rfds, NULL, NULL, &tv) <= 0) {

			/* Issue prompt and wait for anything to happen */
			printf("%s", PROMPT);
			fflush(stdout);
			FD_ZERO(&rfds);
			FD_SET(0, &rfds);
			FD_SET(csock, &rfds);
			FD_SET(dsock, &rfds);
			if (select(maxfd, &rfds, NULL, NULL, NULL) < 0)
				err(EX_OSERR, "select");

			/* If not user input, print a newline first */
			if (!FD_ISSET(0, &rfds))
				printf("\n");
		}

		ReadSockets(&rfds);

		/* Get any user input */
		if (FD_ISSET(0, &rfds)) {
			char buf[LINE_MAX];

			if (fgets(buf, sizeof(buf), stdin) == NULL) {
				printf("\n");
				break;
			}
			if (DoParseCommand(buf) == CMDRTN_QUIT)
				break;
		}
	}
	return (CMDRTN_QUIT);
}
#endif /* !EDITLINE */

/*
 * Read and process data on netgraph control and data sockets.
 */
static void
ReadSockets(fd_set *rfds)
{
	/* Display any incoming control message. */
	if (FD_ISSET(csock, rfds))
		MsgRead();

	/* Display any incoming data packet. */
	if (FD_ISSET(dsock, rfds)) {
		char hook[NG_HOOKSIZ];
		u_char *buf;
		int rl;

		/* Read packet from socket. */
		if ((rl = NgAllocRecvData(dsock, &buf, hook)) < 0)
			err(EX_OSERR, "reading hook \"%s\"", hook);
		if (rl == 0)
			errx(EX_OSERR, "EOF from hook \"%s\"?", hook);

		/* Write packet to stdout. */
		printf("Rec'd data packet on hook \"%s\":\n", hook);
		DumpAscii(buf, rl);
		free(buf);
	}
}

/*
 * Parse a command line and execute the command
 */
static int
DoParseCommand(const char *line)
{
	char *av[MAX_ARGS];
	int ac;

	/* Parse line */
	for (ac = 0, av[0] = strtok((char *)line, WHITESPACE);
	    ac < MAX_ARGS - 1 && av[ac];
	    av[++ac] = strtok(NULL, WHITESPACE));

	/* Do command */
	return (DoCommand(ac, av));
}

/*
 * Execute the command
 */
static int
DoCommand(int ac, char **av)
{
	const struct ngcmd *cmd;
	int rtn;

	if (ac == 0 || *av[0] == 0)
		return (CMDRTN_OK);
	if ((cmd = FindCommand(av[0])) == NULL)
		return (CMDRTN_ERROR);
	if ((rtn = (*cmd->func)(ac, av)) == CMDRTN_USAGE)
		warnx("usage: %s", cmd->cmd);
	return (rtn);
}

/*
 * Find a command
 */
static const struct ngcmd *
FindCommand(const char *string)
{
	int k, found = -1;

	for (k = 0; cmds[k] != NULL; k++) {
		if (MatchCommand(cmds[k], string)) {
			if (found != -1) {
				warnx("\"%s\": ambiguous command", string);
				return (NULL);
			}
			found = k;
		}
	}
	if (found == -1) {
		warnx("\"%s\": unknown command", string);
		return (NULL);
	}
	return (cmds[found]);
}

/*
 * See if string matches a prefix of "cmd" (or an alias) case insensitively
 */
static int
MatchCommand(const struct ngcmd *cmd, const char *s)
{
	int a;

	/* Try to match command, ignoring the usage stuff */
	if (strlen(s) <= strcspn(cmd->cmd, WHITESPACE)) {
		if (strncasecmp(s, cmd->cmd, strlen(s)) == 0)
			return (1);
	}

	/* Try to match aliases */
	for (a = 0; a < MAX_CMD_ALIAS && cmd->aliases[a] != NULL; a++) {
		if (strlen(cmd->aliases[a]) >= strlen(s)) {
			if (strncasecmp(s, cmd->aliases[a], strlen(s)) == 0)
				return (1);
		}
	}

	/* No match */
	return (0);
}

/*
 * ReadCmd()
 */
static int
ReadCmd(int ac, char **av)
{
	FILE *fp;
	int rtn;

	/* Open file */
	switch (ac) {
	case 2:
		if ((fp = fopen(av[1], "r")) == NULL) {
			warn("%s", av[1]);
			return (CMDRTN_ERROR);
		}
		break;
	default:
		return (CMDRTN_USAGE);
	}

	/* Process it */
	rtn = ReadFile(fp);
	fclose(fp);
	return (rtn);
}

/*
 * HelpCmd()
 */
static int
HelpCmd(int ac, char **av)
{
	const struct ngcmd *cmd;
	int k;

	switch (ac) {
	case 0:
	case 1:
		/* Show all commands */
		printf("Available commands:\n");
		for (k = 0; cmds[k] != NULL; k++) {
			char *s, buf[100];

			cmd = cmds[k];
			snprintf(buf, sizeof(buf), "%s", cmd->cmd);
			for (s = buf; *s != '\0' && !isspace(*s); s++);
			*s = '\0';
			printf("  %-10s %s\n", buf, cmd->desc);
		}
		return (CMDRTN_OK);
	default:
		/* Show help on a specific command */
		if ((cmd = FindCommand(av[1])) != NULL) {
			printf("usage:    %s\n", cmd->cmd);
			if (cmd->aliases[0] != NULL) {
				int a = 0;

				printf("Aliases:  ");
				while (1) {
					printf("%s", cmd->aliases[a++]);
					if (a == MAX_CMD_ALIAS
					    || cmd->aliases[a] == NULL) {
						printf("\n");
						break;
					}
					printf(", ");
				}
			}
			printf("Summary:  %s\n", cmd->desc);
			if (cmd->help != NULL) {
				const char *s;
				char buf[65];
				int tot, len, done;

				printf("Description:\n");
				for (s = cmd->help; *s != '\0'; s += len) {
					while (isspace(*s))
						s++;
					tot = snprintf(buf,
					    sizeof(buf), "%s", s);
					len = strlen(buf);
					done = len == tot;
					if (!done) {
						while (len > 0
						    && !isspace(buf[len-1]))
							buf[--len] = '\0';
					}
					printf("  %s\n", buf);
				}
			}
		}
	}
	return (CMDRTN_OK);
}

/*
 * QuitCmd()
 */
static int
QuitCmd(int ac __unused, char **av __unused)
{
	return (CMDRTN_QUIT);
}

/*
 * Dump data in hex and ASCII form
 */
void
DumpAscii(const u_char *buf, int len)
{
	char ch, sbuf[100];
	int k, count;

	for (count = 0; count < len; count += DUMP_BYTES_PER_LINE) {
		snprintf(sbuf, sizeof(sbuf), "%04x:  ", count);
		for (k = 0; k < DUMP_BYTES_PER_LINE; k++) {
			if (count + k < len) {
				snprintf(sbuf + strlen(sbuf),
				    sizeof(sbuf) - strlen(sbuf),
				    "%02x ", buf[count + k]);
			} else {
				snprintf(sbuf + strlen(sbuf),
				    sizeof(sbuf) - strlen(sbuf), "   ");
			}
		}
		snprintf(sbuf + strlen(sbuf), sizeof(sbuf) - strlen(sbuf), " ");
		for (k = 0; k < DUMP_BYTES_PER_LINE; k++) {
			if (count + k < len) {
				ch = isprint(buf[count + k]) ?
				    buf[count + k] : '.';
				snprintf(sbuf + strlen(sbuf),
				    sizeof(sbuf) - strlen(sbuf), "%c", ch);
			} else {
				snprintf(sbuf + strlen(sbuf),
				    sizeof(sbuf) - strlen(sbuf), " ");
			}
		}
		printf("%s\n", sbuf);
	}
}

/*
 * Usage()
 */
static void
Usage(const char *msg)
{
	if (msg)
		warnx("%s", msg);
	fprintf(stderr,
		"usage: ngctl [-d] [-f file] [-n name] [command ...]\n");
	exit(EX_USAGE);
}
