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

/*
 * Simple commandline interpreter, toplevel and misc.
 */

#include <stand.h>
#include <string.h>
#include "bootstrap.h"

INTERP_DEFINE("simp");

void
interp_init(void)
{

	setenv("script.lang", "simple", 1);
	/* Read our default configuration. */
	interp_include("/boot/loader.rc");
}

int
interp_run(const char *input)
{
	int			argc;
	char			**argv;

	if (parse(&argc, &argv, input)) {
		printf("parse error\n");
		return CMD_ERROR;
	}

	if (interp_builtin_cmd(argc, argv)) {
		printf("%s: %s\n", argv[0], command_errmsg);
		free(argv);
		return CMD_ERROR;
	}
	free(argv);
	return CMD_OK;
}

/*
 * Header prepended to each line. The text immediately follows the header.
 * We try to make this short in order to save memory -- the loader has
 * limited memory available, and some of the forth files are very long.
 */
struct includeline
{
	struct includeline	*next;
	int			flags;
	int			line;
#define SL_QUIET	(1<<0)
#define SL_IGNOREERR	(1<<1)
	char			text[0];
};

int
interp_include(const char *filename)
{
	struct includeline	*script, *se, *sp;
	char			input[256];			/* big enough? */
	int			argc,res;
	char			**argv, *cp;
	int			fd, flags, line;

	if (((fd = open(filename, O_RDONLY)) == -1)) {
		snprintf(command_errbuf, sizeof(command_errbuf),
		    "can't open '%s': %s", filename, strerror(errno));
		return(CMD_ERROR);
	}

#ifdef LOADER_VERIEXEC
	if (verify_file(fd, filename, 0, VE_GUESS) < 0) {
		close(fd);
		sprintf(command_errbuf,"can't verify '%s'", filename);
		return(CMD_ERROR);
	}
#endif

	/*
	 * Read the script into memory.
	 */
	script = se = NULL;
	line = 0;
	
	while (fgetstr(input, sizeof(input), fd) >= 0) {
		line++;
		flags = 0;
		/* Discard comments */
		if (strncmp(input+strspn(input, " "), "\\ ", 2) == 0)
			continue;
		cp = input;
		/* Echo? */
		if (input[0] == '@') {
			cp++;
			flags |= SL_QUIET;
		}
		/* Error OK? */
		if (input[0] == '-') {
			cp++;
			flags |= SL_IGNOREERR;
		}

		/* Allocate script line structure and copy line, flags */
		if (*cp == '\0')
			continue;	/* ignore empty line, save memory */
		sp = malloc(sizeof(struct includeline) + strlen(cp) + 1);
		/* On malloc failure (it happens!), free as much as possible and exit */
		if (sp == NULL) {
			while (script != NULL) {
				se = script;
				script = script->next;
				free(se);
			}
			snprintf(command_errbuf, sizeof(command_errbuf),
			    "file '%s' line %d: memory allocation failure - aborting",
			    filename, line);
			close(fd);
			return (CMD_ERROR);
		}
		strcpy(sp->text, cp);
		sp->flags = flags;
		sp->line = line;
		sp->next = NULL;

		if (script == NULL) {
			script = sp;
		} else {
			se->next = sp;
		}
		se = sp;
	}
	close(fd);

	/*
	 * Execute the script
	 */
	argv = NULL;
	res = CMD_OK;
	for (sp = script; sp != NULL; sp = sp->next) {
	
		/* print if not being quiet */
		if (!(sp->flags & SL_QUIET)) {
			interp_emit_prompt();
			printf("%s\n", sp->text);
		}

		/* Parse the command */
		if (!parse(&argc, &argv, sp->text)) {
			if ((argc > 0) && (interp_builtin_cmd(argc, argv) != 0)) {
				/* normal command */
				printf("%s: %s\n", argv[0], command_errmsg);
				if (!(sp->flags & SL_IGNOREERR)) {
					res=CMD_ERROR;
					break;
				}
			}
			free(argv);
			argv = NULL;
		} else {
			printf("%s line %d: parse error\n", filename, sp->line);
			res=CMD_ERROR;
			break;
		}
	}
	if (argv != NULL)
		free(argv);

	while (script != NULL) {
		se = script;
		script = script->next;
		free(se);
	}
	return(res);
}
