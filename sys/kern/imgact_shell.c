/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1993, David Greenman
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

#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/kernel.h>

#if BYTE_ORDER == LITTLE_ENDIAN
#define SHELLMAGIC	0x2123 /* #! */
#else
#define SHELLMAGIC	0x2321
#endif

/*
 * At the time of this writing, MAXSHELLCMDLEN == PAGE_SIZE.  This is
 * significant because the caller has only mapped in one page of the
 * file we're reading.
 */
#if MAXSHELLCMDLEN > PAGE_SIZE
#error "MAXSHELLCMDLEN is larger than a single page!"
#endif

/*
 * MAXSHELLCMDLEN must be at least MAXINTERP plus the size of the `#!'
 * prefix and terminating newline.
 */
CTASSERT(MAXSHELLCMDLEN >= MAXINTERP + 3);

/**
 * Shell interpreter image activator. An interpreter name beginning at
 * imgp->args->begin_argv is the minimal successful exit requirement.
 *
 * If the given file is a shell-script, then the first line will start
 * with the two characters `#!' (aka SHELLMAGIC), followed by the name
 * of the shell-interpreter to run, followed by zero or more tokens.
 *
 * The interpreter is then started up such that it will see:
 *    arg[0] -> The name of interpreter as specified after `#!' in the
 *		first line of the script.  The interpreter name must
 *		not be longer than MAXSHELLCMDLEN bytes.
 *    arg[1] -> *If* there are any additional tokens on the first line,
 *		then we add a new arg[1], which is a copy of the rest of
 *		that line.  The copy starts at the first token after the
 *		interpreter name.  We leave it to the interpreter to
 *		parse the tokens in that value.
 *    arg[x] -> the full pathname of the script.  This will either be
 *		arg[2] or arg[1], depending on whether or not tokens
 *		were found after the interpreter name.
 *  arg[x+1] -> all the arguments that were specified on the original
 *		command line.
 *
 * This processing is described in the execve(2) man page.
 */

/*
 * HISTORICAL NOTE: From 1993 to mid-2005, FreeBSD parsed out the tokens as
 * found on the first line of the script, and setup each token as a separate
 * value in arg[].  This extra processing did not match the behavior of other
 * OS's, and caused a few subtle problems.  For one, it meant the kernel was
 * deciding how those values should be parsed (wrt characters for quoting or
 * comments, etc), while the interpreter might have other rules for parsing.
 * It also meant the interpreter had no way of knowing which arguments came
 * from the first line of the shell script, and which arguments were specified
 * by the user on the command line.  That extra processing was dropped in the
 * 6.x branch on May 28, 2005 (matching __FreeBSD_version 600029).
 */
int
exec_shell_imgact(struct image_params *imgp)
{
	const char *image_header = imgp->image_header;
	const char *ihp, *interpb, *interpe, *maxp, *optb, *opte, *fname;
	int error, offset;
	size_t length;
	struct vattr vattr;
	struct sbuf *sname;

	/* a shell script? */
	if (((const short *)image_header)[0] != SHELLMAGIC)
		return (-1);

	/*
	 * Don't allow a shell script to be the shell for a shell
	 *	script. :-)
	 */
	if (imgp->interpreted & IMGACT_SHELL)
		return (ENOEXEC);

	imgp->interpreted |= IMGACT_SHELL;

	/*
	 * At this point we have the first page of the file mapped.
	 * However, we don't know how far into the page the contents are
	 * valid -- the actual file might be much shorter than the page.
	 * So find out the file size.
	 */
	error = VOP_GETATTR(imgp->vp, &vattr, imgp->proc->p_ucred);
	if (error)
		return (error);

	/*
	 * Copy shell name and arguments from image_header into a string
	 * buffer.
	 */
	maxp = &image_header[MIN(vattr.va_size, MAXSHELLCMDLEN)];
	ihp = &image_header[2];

	/*
	 * Find the beginning and end of the interpreter_name.  If the
	 * line does not include any interpreter, or if the name which
	 * was found is too long, we bail out.
	 */
	while (ihp < maxp && ((*ihp == ' ') || (*ihp == '\t')))
		ihp++;
	interpb = ihp;
	while (ihp < maxp && ((*ihp != ' ') && (*ihp != '\t') && (*ihp != '\n')
	    && (*ihp != '\0')))
		ihp++;
	interpe = ihp;
	if (interpb == interpe)
		return (ENOEXEC);
	if (interpe - interpb >= MAXINTERP)
		return (ENAMETOOLONG);

	/*
	 * Find the beginning of the options (if any), and the end-of-line.
	 * Then trim the trailing blanks off the value.  Note that some
	 * other operating systems do *not* trim the trailing whitespace...
	 */
	while (ihp < maxp && ((*ihp == ' ') || (*ihp == '\t')))
		ihp++;
	optb = ihp;
	while (ihp < maxp && ((*ihp != '\n') && (*ihp != '\0')))
		ihp++;
	opte = ihp;
	if (opte == maxp)
		return (ENOEXEC);
	while (--ihp > optb && ((*ihp == ' ') || (*ihp == '\t')))
		opte = ihp;

	if (imgp->args->fname != NULL) {
		fname = imgp->args->fname;
		sname = NULL;
	} else {
		sname = sbuf_new_auto();
		sbuf_printf(sname, "/dev/fd/%d", imgp->args->fd);
		sbuf_finish(sname);
		fname = sbuf_data(sname);
	}

	/*
	 * We need to "pop" (remove) the present value of arg[0], and "push"
	 * either two or three new values in the arg[] list.  To do this,
	 * we first shift all the other values in the `begin_argv' area to
	 * provide the exact amount of room for the values added.  Set up
	 * `offset' as the number of bytes to be added to the `begin_argv'
	 * area, and 'length' as the number of bytes being removed.
	 */
	offset = interpe - interpb + 1;			/* interpreter */
	if (opte > optb)				/* options (if any) */
		offset += opte - optb + 1;
	offset += strlen(fname) + 1;			/* fname of script */
	length = (imgp->args->argc == 0) ? 0 :
	    strlen(imgp->args->begin_argv) + 1;		/* bytes to delete */

	error = exec_args_adjust_args(imgp->args, length, offset);
	if (error != 0) {
		if (sname != NULL)
			sbuf_delete(sname);
		return (error);
	}

	/*
	 * If there was no arg[0] when we started, then the interpreter_name
	 * is adding an argument (instead of replacing the arg[0] we started
	 * with).  And we're always adding an argument when we include the
	 * full pathname of the original script.
	 */
	if (imgp->args->argc == 0)
		imgp->args->argc = 1;
	imgp->args->argc++;

	/*
	 * The original arg[] list has been shifted appropriately.  Copy in
	 * the interpreter name and options-string.
	 */
	length = interpe - interpb;
	bcopy(interpb, imgp->args->begin_argv, length);
	*(imgp->args->begin_argv + length) = '\0';
	offset = length + 1;
	if (opte > optb) {
		length = opte - optb;
		bcopy(optb, imgp->args->begin_argv + offset, length);
		*(imgp->args->begin_argv + offset + length) = '\0';
		offset += length + 1;
		imgp->args->argc++;
	}

	/*
	 * Finally, add the filename onto the end for the interpreter to
	 * use and copy the interpreter's name to imgp->interpreter_name
	 * for exec to use.
	 */
	error = copystr(fname, imgp->args->begin_argv + offset,
	    imgp->args->stringspace, NULL);

	if (error == 0)
		imgp->interpreter_name = imgp->args->begin_argv;

	if (sname != NULL)
		sbuf_delete(sname);
	return (error);
}

/*
 * Tell kern_execve.c about it, with a little help from the linker.
 */
static struct execsw shell_execsw = {
	.ex_imgact = exec_shell_imgact,
	.ex_name = "#!"
};
EXEC_SET(shell, shell_execsw);
