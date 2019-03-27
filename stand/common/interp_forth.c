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

#include <sys/param.h>		/* to pick up __FreeBSD_version */
#include <string.h>
#include <stand.h>
#include "bootstrap.h"
#include "ficl.h"

extern unsigned bootprog_rev;
INTERP_DEFINE("4th");

/* #define BFORTH_DEBUG */

#ifdef BFORTH_DEBUG
#define	DPRINTF(fmt, args...)	printf("%s: " fmt "\n" , __func__ , ## args)
#else
#define	DPRINTF(fmt, args...)
#endif

/*
 * Eventually, all builtin commands throw codes must be defined
 * elsewhere, possibly bootstrap.h. For now, just this code, used
 * just in this file, it is getting defined.
 */
#define BF_PARSE 100

/*
 * FreeBSD loader default dictionary cells
 */
#ifndef	BF_DICTSIZE
#define	BF_DICTSIZE	10000
#endif

/*
 * BootForth   Interface to Ficl Forth interpreter.
 */

FICL_SYSTEM *bf_sys;
FICL_VM	*bf_vm;

/*
 * Shim for taking commands from BF and passing them out to 'standard'
 * argv/argc command functions.
 */
static void
bf_command(FICL_VM *vm)
{
	char			*name, *line, *tail, *cp;
	size_t			len;
	struct bootblk_command	**cmdp;
	bootblk_cmd_t		*cmd;
	int			nstrings, i;
	int			argc, result;
	char			**argv;

	/* Get the name of the current word */
	name = vm->runningWord->name;

	/* Find our command structure */
	cmd = NULL;
	SET_FOREACH(cmdp, Xcommand_set) {
		if (((*cmdp)->c_name != NULL) && !strcmp(name, (*cmdp)->c_name))
			cmd = (*cmdp)->c_fn;
	}
	if (cmd == NULL)
		panic("callout for unknown command '%s'", name);

	/* Check whether we have been compiled or are being interpreted */
	if (stackPopINT(vm->pStack)) {
		/*
		 * Get parameters from stack, in the format:
		 * an un ... a2 u2 a1 u1 n --
		 * Where n is the number of strings, a/u are pairs of
		 * address/size for strings, and they will be concatenated
		 * in LIFO order.
		 */
		nstrings = stackPopINT(vm->pStack);
		for (i = 0, len = 0; i < nstrings; i++)
			len += stackFetch(vm->pStack, i * 2).i + 1;
		line = malloc(strlen(name) + len + 1);
		strcpy(line, name);

		if (nstrings)
			for (i = 0; i < nstrings; i++) {
				len = stackPopINT(vm->pStack);
				cp = stackPopPtr(vm->pStack);
				strcat(line, " ");
				strncat(line, cp, len);
			}
	} else {
		/* Get remainder of invocation */
		tail = vmGetInBuf(vm);
		for (cp = tail, len = 0; cp != vm->tib.end && *cp != 0 && *cp != '\n'; cp++, len++)
			;

		line = malloc(strlen(name) + len + 2);
		strcpy(line, name);
		if (len > 0) {
			strcat(line, " ");
			strncat(line, tail, len);
			vmUpdateTib(vm, tail + len);
		}
	}
	DPRINTF("cmd '%s'", line);

	command_errmsg = command_errbuf;
	command_errbuf[0] = 0;
	if (!parse(&argc, &argv, line)) {
		result = (cmd)(argc, argv);
		free(argv);
	} else {
		result=BF_PARSE;
	}

	switch (result) {
	case CMD_CRIT:
		printf("%s\n", command_errmsg);
		command_errmsg = NULL;
		break;
	case CMD_FATAL:
		panic("%s", command_errmsg);
	}

	free(line);
	/*
	 * If there was error during nested ficlExec(), we may no longer have
	 * valid environment to return.  Throw all exceptions from here.
	 */
	if (result != CMD_OK)
		vmThrow(vm, result);

	/* This is going to be thrown!!! */
	stackPushINT(vm->pStack,result);
}

/*
 * Replace a word definition (a builtin command) with another
 * one that:
 *
 *        - Throw error results instead of returning them on the stack
 *        - Pass a flag indicating whether the word was compiled or is
 *          being interpreted.
 *
 * There is one major problem with builtins that cannot be overcome
 * in anyway, except by outlawing it. We want builtins to behave
 * differently depending on whether they have been compiled or they
 * are being interpreted. Notice that this is *not* the interpreter's
 * current state. For example:
 *
 * : example ls ; immediate
 * : problem example ;		\ "ls" gets executed while compiling
 * example			\ "ls" gets executed while interpreting
 *
 * Notice that, though the current state is different in the two
 * invocations of "example", in both cases "ls" has been
 * *compiled in*, which is what we really want.
 *
 * The problem arises when you tick the builtin. For example:
 *
 * : example-1 ['] ls postpone literal ; immediate
 * : example-2 example-1 execute ; immediate
 * : problem example-2 ;
 * example-2
 *
 * We have no way, when we get EXECUTEd, of knowing what our behavior
 * should be. Thus, our only alternative is to "outlaw" this. See RFI
 * 0007, and ANS Forth Standard's appendix D, item 6.7 for a related
 * problem, concerning compile semantics.
 *
 * The problem is compounded by the fact that "' builtin CATCH" is valid
 * and desirable. The only solution is to create an intermediary word.
 * For example:
 *
 * : my-ls ls ;
 * : example ['] my-ls catch ;
 *
 * So, with the below implementation, here is a summary of the behavior
 * of builtins:
 *
 * ls -l				\ "interpret" behavior, ie,
 *					\ takes parameters from TIB
 * : ex-1 s" -l" 1 ls ;			\ "compile" behavior, ie,
 *					\ takes parameters from the stack
 * : ex-2 ['] ls catch ; immediate	\ undefined behavior
 * : ex-3 ['] ls catch ;		\ undefined behavior
 * ex-2 ex-3				\ "interpret" behavior,
 *					\ catch works
 * : ex-4 ex-2 ;			\ "compile" behavior,
 *					\ catch does not work
 * : ex-5 ex-3 ; immediate		\ same as ex-2
 * : ex-6 ex-3 ;			\ same as ex-3
 * : ex-7 ['] ex-1 catch ;		\ "compile" behavior,
 *					\ catch works
 * : ex-8 postpone ls ;	immediate	\ same as ex-2
 * : ex-9 postpone ls ;			\ same as ex-3
 *
 * As the definition below is particularly tricky, and it's side effects
 * must be well understood by those playing with it, I'll be heavy on
 * the comments.
 *
 * (if you edit this definition, pay attention to trailing spaces after
 *  each word -- I warned you! :-) )
 */
#define BUILTIN_CONSTRUCTOR						\
	": builtin: "							\
	">in @ "		/* save the tib index pointer */	\
	"' "			/* get next word's xt */		\
	"swap >in ! "		/* point again to next word */		\
	"create "		/* create a new definition of the next word */ \
	", "			/* save previous definition's xt */	\
	"immediate "		/* make the new definition an immediate word */ \
									\
	"does> "		/* Now, the *new* definition will: */	\
	"state @ if "		/* if in compiling state: */		\
	"1 postpone literal "	/* pass 1 flag to indicate compile */	\
	"@ compile, "		/* compile in previous definition */	\
	"postpone throw "		/* throw stack-returned result */ \
	"else "		/* if in interpreting state: */			\
	"0 swap "			/* pass 0 flag to indicate interpret */ \
	"@ execute "		/* call previous definition */		\
	"throw "			/* throw stack-returned result */ \
	"then ; "

/*
 * Initialise the Forth interpreter, create all our commands as words.
 */
void
bf_init(void)
{
	struct bootblk_command	**cmdp;
	char create_buf[41];	/* 31 characters-long builtins */
	int fd;

	bf_sys = ficlInitSystem(BF_DICTSIZE);
	bf_vm = ficlNewVM(bf_sys);

	/* Put all private definitions in a "builtins" vocabulary */
	ficlExec(bf_vm, "vocabulary builtins also builtins definitions");

	/* Builtin constructor word  */
	ficlExec(bf_vm, BUILTIN_CONSTRUCTOR);

	/* make all commands appear as Forth words */
	SET_FOREACH(cmdp, Xcommand_set) {
		ficlBuild(bf_sys, (char *)(*cmdp)->c_name, bf_command, FW_DEFAULT);
		ficlExec(bf_vm, "forth definitions builtins");
		sprintf(create_buf, "builtin: %s", (*cmdp)->c_name);
		ficlExec(bf_vm, create_buf);
		ficlExec(bf_vm, "builtins definitions");
	}
	ficlExec(bf_vm, "only forth definitions");

	/* Export some version numbers so that code can detect the loader/host version */
	ficlSetEnv(bf_sys, "FreeBSD_version", __FreeBSD_version);
	ficlSetEnv(bf_sys, "loader_version", bootprog_rev);

	/* try to load and run init file if present */
	if ((fd = open("/boot/boot.4th", O_RDONLY)) != -1) {
#ifdef LOADER_VERIEXEC
		if (verify_file(fd, "/boot/boot.4th", 0, VE_GUESS) < 0) {
			close(fd);
			return;
		}
#endif
		(void)ficlExecFD(bf_vm, fd);
		close(fd);
	}
}

/*
 * Feed a line of user input to the Forth interpreter
 */
static int
bf_run(const char *line)
{
	int		result;

	/*
	 * ficl would require extensive changes to accept a const char *
	 * interface. Instead, cast it away here and hope for the best.
	 * We know at the present time the caller for us in the boot
	 * forth loader can tolerate the string being modified because
	 * the string is passed in here and then not touched again.
	 */
	result = ficlExec(bf_vm, __DECONST(char *, line));

	DPRINTF("ficlExec '%s' = %d", line, result);
	switch (result) {
	case VM_OUTOFTEXT:
	case VM_ABORTQ:
	case VM_QUIT:
	case VM_ERREXIT:
		break;
	case VM_USEREXIT:
		printf("No where to leave to!\n");
		break;
	case VM_ABORT:
		printf("Aborted!\n");
		break;
	case BF_PARSE:
		printf("Parse error!\n");
		break;
	default:
		if (command_errmsg != NULL) {
			printf("%s\n", command_errmsg);
			command_errmsg = NULL;
		}
	}

	if (result == VM_USEREXIT)
		panic("interpreter exit");
	setenv("interpret", bf_vm->state ? "" : "OK", 1);

	return (result);
}

void
interp_init(void)
{

	setenv("script.lang", "forth", 1);
	bf_init();
	/* Read our default configuration. */
	interp_include("/boot/loader.rc");
}

int
interp_run(const char *input)
{

	bf_vm->sourceID.i = 0;
	return bf_run(input);
}

/*
 * Header prepended to each line. The text immediately follows the header.
 * We try to make this short in order to save memory -- the loader has
 * limited memory available, and some of the forth files are very long.
 */
struct includeline
{
	struct includeline	*next;
	char			text[0];
};

int
interp_include(const char *filename)
{
	struct includeline	*script, *se, *sp;
	char			input[256];			/* big enough? */
	int			res;
	char			*cp;
	int			prevsrcid, fd, line;

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
		cp = input;
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
	prevsrcid = bf_vm->sourceID.i;
	bf_vm->sourceID.i = fd;
	res = CMD_OK;
	for (sp = script; sp != NULL; sp = sp->next) {
		res = bf_run(sp->text);
		if (res != VM_OUTOFTEXT) {
			snprintf(command_errbuf, sizeof(command_errbuf),
			    "Error while including %s, in the line:\n%s",
			    filename, sp->text);
			res = CMD_ERROR;
			break;
		} else
			res = CMD_OK;
	}
	bf_vm->sourceID.i = prevsrcid;

	while (script != NULL) {
		se = script;
		script = script->next;
		free(se);
	}
	return(res);
}
