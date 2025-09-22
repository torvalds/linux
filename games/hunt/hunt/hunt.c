/*	$OpenBSD: hunt.c,v 1.23 2020/02/14 19:17:33 schwarze Exp $	*/
/*	$NetBSD: hunt.c,v 1.8 1998/09/13 15:27:28 hubertf Exp $	*/
/*
 * Copyright (c) 1983-2003, Regents of the University of California.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are 
 * met:
 * 
 * + Redistributions of source code must retain the above copyright 
 *   notice, this list of conditions and the following disclaimer.
 * + Redistributions in binary form must reproduce the above copyright 
 *   notice, this list of conditions and the following disclaimer in the 
 *   documentation and/or other materials provided with the distribution.
 * + Neither the name of the University of California, San Francisco nor 
 *   the names of its contributors may be used to endorse or promote 
 *   products derived from this software without specific prior written 
 *   permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS 
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED 
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A 
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/socket.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <curses.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "display.h"
#include "hunt.h"
#include "list.h"
#include "client.h"

#ifndef __GNUC__
#define __attribute__(x)
#endif

FLAG	Am_monitor = FALSE;
int	Socket;
char	map_key[256];			/* what to map keys to */
FLAG	no_beep = FALSE;
char	*Send_message = NULL;

static char	*Sock_host;
static char	*use_port;
static FLAG	Query_driver = FALSE;
static FLAG	Show_scores = FALSE;
static struct sockaddr	Daemon;


static char	name[NAMELEN];
static char	team = '-';

static int	in_visual;

static void	dump_scores(void);
static long	env_init(long);
static void	fill_in_blanks(void);
__dead static void	leave(int, char *);
static void	sigterm(int);
static int	find_driver(void);

/*
 * main:
 *	Main program for local process
 */
int
main(int ac, char **av)
{
	int		c;
	long		enter_status;
	int		option;
	struct servent	*se;

	enter_status = env_init((long) Q_CLOAK);
	while ((c = getopt(ac, av, "Sbcfh:l:mn:op:qst:w:")) != -1) {
		switch (c) {
		case 'l':	/* rsh compatibility */
		case 'n':
			(void) strlcpy(name, optarg, sizeof name);
			break;
		case 't':
			team = *optarg;
			if (!isdigit((unsigned char)team) && team != ' ') {
				warnx("Team names must be numeric or space");
				team = '-';
			}
			break;
		case 'o':
			Otto_mode = TRUE;
			break;
		case 'm':
			Am_monitor = TRUE;
			break;
		case 'S':
			Show_scores = TRUE;
			break;
		case 'q':	/* query whether hunt is running */
			Query_driver = TRUE;
			break;
		case 'w':
			Send_message = optarg;
			break;
		case 'h':
			Sock_host = optarg;
			break;
		case 'p':
			use_port = optarg;
			Server_port = atoi(use_port);
			break;
		case 'c':
			enter_status = Q_CLOAK;
			break;
		case 'f':
			enter_status = Q_FLY;
			break;
		case 's':
			enter_status = Q_SCAN;
			break;
		case 'b':
			no_beep = !no_beep;
			break;
		default:
		usage:
			fprintf(stderr, "usage: %s [-bcfmqSs] [-n name] "
			    "[-p port] [-t team] [-w message] [[-h] host]\n",
			    getprogname());
			return 1;
		}
	}
	if (optind + 1 < ac)
		goto usage;
	else if (optind + 1 == ac)
		Sock_host = av[ac - 1];

	if (Server_port == 0) {
		se = getservbyname("hunt", "udp");
		if (se != NULL)
			Server_port = ntohs(se->s_port);
		else
			Server_port = HUNT_PORT;
	}

	if (Show_scores) {
		dump_scores();
		return 0;
	}

	if (Query_driver) {
		struct driver		*driver;

		probe_drivers(C_MESSAGE, Sock_host);
		while ((driver = next_driver()) != NULL) {
			printf("%d player%s hunting on %s!\n",
			    driver->response,
			    (driver->response == 1) ? "" : "s",
			    driver_name(driver));
			if (Sock_host)
				break;
		}
		return 0;
	}
	if (Otto_mode) {
		if (Am_monitor)
			errx(1, "otto mode incompatible with monitor mode");
		(void) strlcpy(name, "otto", sizeof name);
		team = ' ';
	} else
		fill_in_blanks();

	(void) fflush(stdout);
	display_open();
	in_visual = TRUE;
	if (LINES < SCREEN_HEIGHT || COLS < SCREEN_WIDTH) {
		errno = 0;
		leave(1, "Need a larger window");
	}
	display_clear_the_screen();
	(void) signal(SIGINT, intr);
	(void) signal(SIGTERM, sigterm);
	/* (void) signal(SIGPIPE, SIG_IGN); */

	Daemon.sa_len = 0;
    ask_driver:
	while (!find_driver()) {
		if (Am_monitor) {
			errno = 0;
			leave(1, "No one playing");
		}

		if (Sock_host == NULL) {
			errno = 0;
			leave(1, "huntd not running");
		}

		sleep(3);
	}
	Socket = -1;

	for (;;) {
		if (Socket != -1)
			close(Socket);

		Socket = socket(Daemon.sa_family, SOCK_STREAM, 0);
		if (Socket < 0)
			leave(1, "socket");

		option = 1;
		if (setsockopt(Socket, SOL_SOCKET, SO_USELOOPBACK,
		    &option, sizeof option) < 0)
			warn("setsockopt loopback");

		errno = 0;
		if (connect(Socket, &Daemon, Daemon.sa_len) == -1)  {
			if (errno == ECONNREFUSED)
				goto ask_driver;
			leave(1, "connect");
		}

		do_connect(name, team, enter_status);
		if (Send_message != NULL) {
			do_message();
			if (enter_status == Q_MESSAGE)
				break;
			Send_message = NULL;
			continue;
		}
		playit();
		if ((enter_status = quit(enter_status)) == Q_QUIT)
			break;
	}
	leave(0, NULL);
	return 0;
}

/*
 * Set Daemon to be the address of a hunt driver, or return 0 on failure.
 *
 * We start quietly probing for drivers. As soon as one driver is found
 * we show it in the list. If we run out of drivers and we only have one
 * then we choose it. Otherwise we present a list of the found drivers.
 */
static int
find_driver(void)
{
	int last_driver, numdrivers, waiting, is_current;
	struct driver *driver;
	int c;
	char buf[80];
	const char *name;

	probe_drivers(Am_monitor ? C_MONITOR : C_PLAYER, Sock_host);

	last_driver = -1;
	numdrivers = 0;
	waiting = 1;
	for (;;) {
		if (numdrivers == 0) {
			/* Silently wait for at least one driver */
			driver = next_driver();
		} else if (!waiting || (driver = 
		    next_driver_fd(STDIN_FILENO)) == (struct driver *)-1) {
			/* We have a key waiting, or no drivers left */
			c = getchar();
			if (c == '\r' || c == '\n' || c == ' ') {
				if (numdrivers == 1)
					c = 'a';
				else if (last_driver != -1)
					c = 'a' + last_driver;
			}
			if (c < 'a' || c >= numdrivers + 'a') {
				display_beep();
				continue;
			}
			driver = &drivers[c - 'a'];
			break;
		}

		if (driver == NULL) {
			waiting = 0;
			if (numdrivers == 0) {
				probe_cleanup();
				return 0;	/* Failure */
			}
			if (numdrivers == 1) {
				driver = &drivers[0];
				break;
			}
			continue;
		}

		/* Use the preferred host straight away. */
		if (Sock_host)
			break;

		if (numdrivers == 0) {
			display_clear_the_screen();
			display_move(1, 0);
			display_put_str("Pick one:");
		}

		/* Mark the last driver we used with an asterisk */
		is_current = (last_driver == -1 && Daemon.sa_len != 0 && 
		    memcmp(&Daemon, &driver->addr, Daemon.sa_len) == 0);
		if (is_current)
			last_driver = numdrivers;

		/* Display it in the list if there is room */
		if (numdrivers < HEIGHT - 3) {
			name = driver_name(driver);
			display_move(3 + numdrivers, 0);
			snprintf(buf, sizeof buf, "%6c %c    %s", 
			    is_current ? '*' : ' ', 'a' + numdrivers, name);
			display_put_str(buf);
		}

		/* Clear the last 'Enter letter' line if any */
		display_move(4 + numdrivers, 0);
		display_clear_eol();

		if (last_driver != -1)
			snprintf(buf, sizeof buf, "Enter letter [%c]: ", 
			    'a' + last_driver);
		else
			snprintf(buf, sizeof buf, "Enter letter: ");

		display_move(5 + numdrivers, 0);
		display_put_str(buf);
		display_refresh();

		numdrivers++;
	}

	display_clear_the_screen();
	Daemon = driver->addr;

	probe_cleanup();
	return 1;		/* Success */
}

static void
dump_scores(void)
{
	struct	driver *driver;
	int	s, cnt, i;
	char	buf[1024];

	probe_drivers(C_SCORES, Sock_host);
	while ((driver = next_driver()) != NULL) {
		printf("\n%s:\n", driver_name(driver));
		fflush(stdout);

		if ((s = socket(driver->addr.sa_family, SOCK_STREAM, 0)) < 0) {
			warn("socket");
			continue;
		}
		if (connect(s, &driver->addr, driver->addr.sa_len) < 0) {
			warn("connect");
			close(s);
			continue;
		}
		while ((cnt = read(s, buf, sizeof buf)) > 0) {
			/* Whittle out bad characters */
			for (i = 0; i < cnt; i++)
				if ((buf[i] < ' ' || buf[i] > '~') &&
				    buf[i] != '\n' && buf[i] != '\t')
					buf[i] = '?';
			fwrite(buf, cnt, 1, stdout);
		}
		if (cnt < 0)
			warn("read");
		(void)close(s);
		if (Sock_host)
			break;
	}
	probe_cleanup();
}


/*
 * bad_con:
 *	We had a bad connection.  For the moment we assume that this
 *	means the game is full.
 */
void
bad_con(void)
{
	leave(1, "lost connection to huntd");
}

/*
 * bad_ver:
 *	version number mismatch.
 */
void
bad_ver(void)
{
	errno = 0;
	leave(1, "Version number mismatch. No go.");
}

/*
 * sigterm:
 *	Handle a terminate signal
 */
static void
sigterm(int dummy)
{
	leave(0, NULL);
}

/*
 * rmnl:
 *	Remove a '\n' at the end of a string if there is one
 */
static void
rmnl(char *s)
{
	char	*cp;

	cp = strrchr(s, '\n');
	if (cp != NULL)
		*cp = '\0';
}

/*
 * intr:
 *	Handle a interrupt signal
 */
void
intr(int dummy)
{
	int	ch;
	int	explained;
	int	y, x;

	(void) signal(SIGINT, SIG_IGN);
	display_getyx(&y, &x);
	display_move(HEIGHT, 0);
	display_put_str("Really quit? ");
	display_clear_eol();
	display_refresh();
	explained = FALSE;
	for (;;) {
		ch = getchar();
		if (isupper(ch))
			ch = tolower(ch);
		if (ch == 'y') {
			if (Socket != 0) {
				(void) write(Socket, "q", 1);
				(void) close(Socket);
			}
			leave(0, NULL);
		}
		else if (ch == 'n') {
			(void) signal(SIGINT, intr);
			display_move(y, x);
			display_refresh();
			return;
		}
		if (!explained) {
			display_put_str("(Yes or No) ");
			display_refresh();
			explained = TRUE;
		}
		display_beep();
		display_refresh();
	}
}

/*
 * leave:
 *	Leave the game somewhat gracefully, restoring all current
 *	tty stats.
 */
static void
leave(int eval, char *mesg)
{
	int saved_errno;

	saved_errno = errno;
	if (in_visual) {
		display_move(HEIGHT, 0);
		display_refresh();
		display_end();
	}
	errno = saved_errno;

	if (errno == 0 && mesg != NULL)
		errx(eval, "%s", mesg);
	else if (mesg != NULL)
		err(eval, "%s", mesg);
	exit(eval);
}

/*
 * env_init:
 *	initialise game parameters from the HUNT envvar
 */
static long
env_init(long enter_status)
{
	int	i;
	char	*envp, *envname, *s;

	/* Map all keys to themselves: */
	for (i = 0; i < 256; i++)
		map_key[i] = (char) i;

	envname = NULL;
	if ((envp = getenv("HUNT")) != NULL) {
		while ((s = strpbrk(envp, "=,")) != NULL) {
			if (strncmp(envp, "cloak,", s - envp + 1) == 0) {
				enter_status = Q_CLOAK;
				envp = s + 1;
			}
			else if (strncmp(envp, "scan,", s - envp + 1) == 0) {
				enter_status = Q_SCAN;
				envp = s + 1;
			}
			else if (strncmp(envp, "fly,", s - envp + 1) == 0) {
				enter_status = Q_FLY;
				envp = s + 1;
			}
			else if (strncmp(envp, "nobeep,", s - envp + 1) == 0) {
				no_beep = TRUE;
				envp = s + 1;
			}
			else if (strncmp(envp, "name=", s - envp + 1) == 0) {
				envname = s + 1;
				if ((s = strchr(envp, ',')) == NULL) {
					*envp = '\0';
					strlcpy(name, envname, sizeof name);
					break;
				}
				*s = '\0';
				strlcpy(name, envname, sizeof name);
				envp = s + 1;
			}
			else if (strncmp(envp, "port=", s - envp + 1) == 0) {
				use_port = s + 1;
				Server_port = atoi(use_port);
				if ((s = strchr(envp, ',')) == NULL) {
					*envp = '\0';
					break;
				}
				*s = '\0';
				envp = s + 1;
			}
			else if (strncmp(envp, "host=", s - envp + 1) == 0) {
				Sock_host = s + 1;
				if ((s = strchr(envp, ',')) == NULL) {
					*envp = '\0';
					break;
				}
				*s = '\0';
				envp = s + 1;
			}
			else if (strncmp(envp, "message=", s - envp + 1) == 0) {
				Send_message = s + 1;
				if ((s = strchr(envp, ',')) == NULL) {
					*envp = '\0';
					break;
				}
				*s = '\0';
				envp = s + 1;
			}
			else if (strncmp(envp, "team=", s - envp + 1) == 0) {
				team = *(s + 1);
				if (!isdigit((unsigned char)team))
					team = ' ';
				if ((s = strchr(envp, ',')) == NULL) {
					*envp = '\0';
					break;
				}
				*s = '\0';
				envp = s + 1;
			}			/* must be last option */
			else if (strncmp(envp, "mapkey=", s - envp + 1) == 0) {
				for (s = s + 1; *s != '\0'; s += 2) {
					map_key[(unsigned int) *s] = *(s + 1);
					if (*(s + 1) == '\0') {
						break;
					}
				}
				*envp = '\0';
				break;
			} else {
				*s = '\0';
				printf("unknown option %s\n", envp);
				if ((s = strchr(envp, ',')) == NULL) {
					*envp = '\0';
					break;
				}
				envp = s + 1;
			}
		}
		if (*envp != '\0') {
			if (envname == NULL)
				strlcpy(name, envp, sizeof name);
			else
				printf("unknown option %s\n", envp);
		}
	}
	return enter_status;
}

/*
 * fill_in_blanks:
 *	quiz the user for the information they didn't provide earlier
 */
static void
fill_in_blanks(void)
{
	int	i;
	char	*cp;

again:
	if (name[0] != '\0') {
		printf("Entering as '%s'", name);
		if (team != ' ' && team != '-')
			printf(" on team %c.\n", team);
		else
			putchar('\n');
	} else {
		printf("Enter your code name: ");
		if (fgets(name, sizeof name, stdin) == NULL)
			exit(1);
	}
	rmnl(name);
	if (name[0] == '\0') {
		printf("You have to have a code name!\n");
		goto again;
	}
	for (cp = name; *cp != '\0'; cp++)
		if (!isprint((unsigned char)*cp)) {
			name[0] = '\0';
			printf("Illegal character in your code name.\n");
			goto again;
		}
	if (team == '-') {
		printf("Enter your team (0-9 or nothing): ");
		i = getchar();
		if (isdigit(i))
			team = i;
		else if (i == '\n' || i == EOF || i == ' ')
			team = ' ';
		/* ignore trailing chars */
		while (i != '\n' && i != EOF)
			i = getchar();
		if (team == '-') {
			printf("Teams must be numeric.\n");
			goto again;
		}
	}
}
