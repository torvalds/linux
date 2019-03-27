/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <string.h>

#include "bootstrap.h"

const char	*command_errmsg;
/* XXX should have procedural interface for setting, size limit? */
char		command_errbuf[COMMAND_ERRBUFSZ];

static int page_file(char *filename);

/*
 * Help is read from a formatted text file.
 *
 * Entries in the file are formatted as 

# Ttopic [Ssubtopic] Ddescription
help
text
here
#

 *
 * Note that for code simplicity's sake, the above format must be followed
 * exactly.
 *
 * Subtopic entries must immediately follow the topic (this is used to
 * produce the listing of subtopics).
 *
 * If no argument(s) are supplied by the user, the help for 'help' is displayed.
 */
COMMAND_SET(help, "help", "detailed help", command_help);

static int
help_getnext(int fd, char **topic, char **subtopic, char **desc)
{
	char	line[81], *cp, *ep;

	/* Make sure we provide sane values. */
	*topic = *subtopic = *desc = NULL;
	for (;;) {
		if (fgetstr(line, 80, fd) < 0)
			return (0);

		if (strlen(line) < 3 || line[0] != '#' || line[1] != ' ')
			continue;

		cp = line + 2;
		while (cp != NULL && *cp != 0) {
			ep = strchr(cp, ' ');
			if (*cp == 'T' && *topic == NULL) {
				if (ep != NULL)
					*ep++ = 0;
				*topic = strdup(cp + 1);
			} else if (*cp == 'S' && *subtopic == NULL) {
				if (ep != NULL)
					*ep++ = 0;
				*subtopic = strdup(cp + 1);
			} else if (*cp == 'D') {
				*desc = strdup(cp + 1);
				ep = NULL;
			}
			cp = ep;
		}
		if (*topic == NULL) {
			free(*subtopic);
			free(*desc);
			*subtopic = *desc = NULL;
			continue;
		}
		return (1);
	}
}

static int
help_emitsummary(char *topic, char *subtopic, char *desc)
{
	int	i;

	pager_output("    ");
	pager_output(topic);
	i = strlen(topic);
	if (subtopic != NULL) {
		pager_output(" ");
		pager_output(subtopic);
		i += strlen(subtopic) + 1;
	}
	if (desc != NULL) {
		do {
			pager_output(" ");
		} while (i++ < 30);
		pager_output(desc);
	}
	return (pager_output("\n"));
}


static int
command_help(int argc, char *argv[])
{
	char	buf[81];	/* XXX buffer size? */
	int	hfd, matched, doindex;
	char	*topic, *subtopic, *t, *s, *d;

	/* page the help text from our load path */
	snprintf(buf, sizeof(buf), "%s/boot/loader.help", getenv("loaddev"));
	if ((hfd = open(buf, O_RDONLY)) < 0) {
		printf("Verbose help not available, "
		    "use '?' to list commands\n");
		return (CMD_OK);
	}

	/* pick up request from arguments */
	topic = subtopic = NULL;
	switch (argc) {
	case 3:
		subtopic = strdup(argv[2]);
		/* FALLTHROUGH */
	case 2:
		topic = strdup(argv[1]);
		break;
	case 1:
		topic = strdup("help");
		break;
	default:
		command_errmsg = "usage is 'help <topic> [<subtopic>]";
		close(hfd);
		return(CMD_ERROR);
	}

	/* magic "index" keyword */
	doindex = strcmp(topic, "index") == 0? 1 : 0;
	matched = doindex;

	/* Scan the helpfile looking for help matching the request */
	pager_open();
	while (help_getnext(hfd, &t, &s, &d)) {

		if (doindex) {		/* dink around formatting */
			if (help_emitsummary(t, s, d))
				break;

		} else if (strcmp(topic, t)) {
			/* topic mismatch */
			if (matched) {
				/* nothing more on this topic, stop scanning */
				break;
			}
		} else {
			/* topic matched */
			matched = 1;
			if ((subtopic == NULL && s == NULL) ||
			    (subtopic != NULL && s != NULL &&
			    strcmp(subtopic, s) == 0)) {
				/* exact match, print text */
				while (fgetstr(buf, 80, hfd) >= 0 &&
				    buf[0] != '#') {
					if (pager_output(buf))
						break;
					if (pager_output("\n"))
						break;
				}
			} else if (subtopic == NULL && s != NULL) {
				/* topic match, list subtopics */
				if (help_emitsummary(t, s, d))
					break;
			}
		}
		free(t);
		free(s);
		free(d);
		t = s = d = NULL;
	}
	free(t);
	free(s);
	free(d);
	pager_close();
	close(hfd);
	if (!matched) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "no help available for '%s'", topic);
		free(topic);
		free(subtopic);
		return (CMD_ERROR);
	}
	free(topic);
	free(subtopic);
	return (CMD_OK);
}

COMMAND_SET(commandlist, "?", "list commands", command_commandlist);

/*
 * Please note: although we use the pager for the list of commands,
 * this routine is called from the ? FORTH function which then
 * unconditionally prints some commands. This will lead to anomalous
 * behavior. There's no 'pager_output' binding to FORTH to allow
 * things to work right, so I'm documenting the bug rather than
 * fixing it.
 */
static int
command_commandlist(int argc __unused, char *argv[] __unused)
{
	struct bootblk_command	**cmdp;
	int	res;
	char	name[20];

	res = 0;
	pager_open();
	res = pager_output("Available commands:\n");
	SET_FOREACH(cmdp, Xcommand_set) {
		if (res)
			break;
		if ((*cmdp)->c_name != NULL && (*cmdp)->c_desc != NULL) {
			snprintf(name, sizeof(name), "  %-15s  ",
			    (*cmdp)->c_name);
			pager_output(name);
			pager_output((*cmdp)->c_desc);
			res = pager_output("\n");
		}
	}
	pager_close();
	return (CMD_OK);
}

/*
 * XXX set/show should become set/echo if we have variable
 * substitution happening.
 */

COMMAND_SET(show, "show", "show variable(s)", command_show);

static int
command_show(int argc, char *argv[])
{
	struct env_var	*ev;
	char		*cp;

	if (argc < 2) {
		/*
		 * With no arguments, print everything.
		 */
		pager_open();
		for (ev = environ; ev != NULL; ev = ev->ev_next) {
			pager_output(ev->ev_name);
			cp = getenv(ev->ev_name);
			if (cp != NULL) {
				pager_output("=");
				pager_output(cp);
			}
			if (pager_output("\n"))
				break;
		}
		pager_close();
	} else {
		if ((cp = getenv(argv[1])) != NULL) {
			printf("%s\n", cp);
		} else {
			snprintf(command_errbuf, sizeof(command_errbuf),
			    "variable '%s' not found", argv[1]);
			return (CMD_ERROR);
		}
	}
	return (CMD_OK);
}

COMMAND_SET(set, "set", "set a variable", command_set);

static int
command_set(int argc, char *argv[])
{
	int	err;

	if (argc != 2) {
		command_errmsg = "wrong number of arguments";
		return (CMD_ERROR);
	} else {
		if ((err = putenv(argv[1])) != 0) {
			command_errmsg = strerror(err);
			return (CMD_ERROR);
		}
	}
	return (CMD_OK);
}

COMMAND_SET(unset, "unset", "unset a variable", command_unset);

static int
command_unset(int argc, char *argv[])
{
	int	err;

	if (argc != 2) {
		command_errmsg = "wrong number of arguments";
		return (CMD_ERROR);
	} else {
		if ((err = unsetenv(argv[1])) != 0) {
			command_errmsg = strerror(err);
			return (CMD_ERROR);
		}
	}
	return (CMD_OK);
}

COMMAND_SET(echo, "echo", "echo arguments", command_echo);

static int
command_echo(int argc, char *argv[])
{
	char	*s;
	int	nl, ch;

	nl = 0;
	optind = 1;
	optreset = 1;
	while ((ch = getopt(argc, argv, "n")) != -1) {
		switch (ch) {
		case 'n':
			nl = 1;
			break;
		case '?':
		default:
			/* getopt has already reported an error */
			return (CMD_OK);
		}
	}
	argv += (optind);
	argc -= (optind);

	s = unargv(argc, argv);
	if (s != NULL) {
		printf("%s", s);
		free(s);
	}
	if (!nl)
		printf("\n");
	return (CMD_OK);
}

/*
 * A passable emulation of the sh(1) command of the same name.
 */

COMMAND_SET(read, "read", "read input from the terminal", command_read);

static int
command_read(int argc, char *argv[])
{
	char	*prompt;
	int	timeout;
	time_t	when;
	char	*cp;
	char	*name;
	char	buf[256];		/* XXX size? */
	int	c;
    
	timeout = -1;
	prompt = NULL;
	optind = 1;
	optreset = 1;
	while ((c = getopt(argc, argv, "p:t:")) != -1) {
		switch (c) {
		case 'p':
			prompt = optarg;
			break;
		case 't':
			timeout = strtol(optarg, &cp, 0);
			if (cp == optarg) {
				snprintf(command_errbuf,
				    sizeof(command_errbuf),
				    "bad timeout '%s'", optarg);
				return (CMD_ERROR);
			}
			break;
		default:
			return (CMD_OK);
		}
	}

	argv += (optind);
	argc -= (optind);
	name = (argc > 0) ? argv[0]: NULL;

	if (prompt != NULL)
		printf("%s", prompt);
	if (timeout >= 0) {
		when = time(NULL) + timeout;
		while (!ischar())
			if (time(NULL) >= when)
				return (CMD_OK); /* is timeout an error? */
	}

	ngets(buf, sizeof(buf));

	if (name != NULL)
		setenv(name, buf, 1);
	return (CMD_OK);
}

/*
 * File pager
 */
COMMAND_SET(more, "more", "show contents of a file", command_more);

static int
command_more(int argc, char *argv[])
{
	int	i;
	int	res;
	char	line[80];

	res = 0;
	pager_open();
	for (i = 1; (i < argc) && (res == 0); i++) {
		snprintf(line, sizeof(line), "*** FILE %s BEGIN ***\n",
		    argv[i]);
		if (pager_output(line))
			break;
		res = page_file(argv[i]);
		if (!res) {
			snprintf(line, sizeof(line), "*** FILE %s END ***\n",
			    argv[i]);
			res = pager_output(line);
		}
	}
	pager_close();

	if (res == 0)
		return (CMD_OK);
	else
		return (CMD_ERROR);
}

static int
page_file(char *filename)
{
	int result;

	result = pager_file(filename);

	if (result == -1) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "error showing %s", filename);
	}

	return (result);
}

/*
 * List all disk-like devices
 */
COMMAND_SET(lsdev, "lsdev", "list all devices", command_lsdev);

static int
command_lsdev(int argc, char *argv[])
{
	int	verbose, ch, i;
	char	line[80];

	verbose = 0;
	optind = 1;
	optreset = 1;
	while ((ch = getopt(argc, argv, "v")) != -1) {
		switch (ch) {
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			/* getopt has already reported an error */
			return (CMD_OK);
		}
	}
	argv += (optind);
	argc -= (optind);

	pager_open();
	for (i = 0; devsw[i] != NULL; i++) {
		if (devsw[i]->dv_print != NULL) {
			if (devsw[i]->dv_print(verbose))
				break;
		} else {
			snprintf(line, sizeof(line), "%s: (unknown)\n",
			    devsw[i]->dv_name);
			if (pager_output(line))
				break;
		}
	}
	pager_close();
	return (CMD_OK);
}
