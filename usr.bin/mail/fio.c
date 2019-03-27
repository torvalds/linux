/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
#if 0
static char sccsid[] = "@(#)fio.c	8.2 (Berkeley) 4/20/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "rcv.h"
#include <sys/file.h>
#include <sys/wait.h>

#include <unistd.h>
#include <paths.h>
#include <errno.h>
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * File I/O.
 */

extern int wait_status;

/*
 * Set up the input pointers while copying the mail file into /tmp.
 */
void
setptr(FILE *ibuf, off_t offset)
{
	int c, count;
	char *cp, *cp2;
	struct message this;
	FILE *mestmp;
	int maybe, inhead;
	char linebuf[LINESIZE], pathbuf[PATHSIZE];
	int omsgCount;

	/* Get temporary file. */
	(void)snprintf(pathbuf, sizeof(pathbuf), "%s/mail.XXXXXXXXXX", tmpdir);
	if ((c = mkstemp(pathbuf)) == -1 || (mestmp = Fdopen(c, "r+")) == NULL)
		err(1, "can't open %s", pathbuf);
	(void)rm(pathbuf);

	if (offset == 0) {
		 msgCount = 0;
	} else {
		/* Seek into the file to get to the new messages */
		(void)fseeko(ibuf, offset, SEEK_SET);
		/*
		 * We need to make "offset" a pointer to the end of
		 * the temp file that has the copy of the mail file.
		 * If any messages have been edited, this will be
		 * different from the offset into the mail file.
		 */
		(void)fseeko(otf, (off_t)0, SEEK_END);
		offset = ftello(otf);
	}
	omsgCount = msgCount;
	maybe = 1;
	inhead = 0;
	this.m_flag = MUSED|MNEW;
	this.m_size = 0;
	this.m_lines = 0;
	this.m_block = 0;
	this.m_offset = 0;
	for (;;) {
		if (fgets(linebuf, sizeof(linebuf), ibuf) == NULL) {
			if (append(&this, mestmp))
				errx(1, "temporary file");
			makemessage(mestmp, omsgCount);
			return;
		}
		count = strlen(linebuf);
		/*
		 * Transforms lines ending in <CR><LF> to just <LF>.
		 * This allows mail to be able to read Eudora mailboxes.
		 */
		if (count >= 2 && linebuf[count - 1] == '\n' &&
		    linebuf[count - 2] == '\r') {
			count--;
			linebuf[count - 1] = '\n';
		}

		(void)fwrite(linebuf, sizeof(*linebuf), count, otf);
		if (ferror(otf))
			errx(1, "/tmp");
		if (count)
			linebuf[count - 1] = '\0';
		if (maybe && linebuf[0] == 'F' && ishead(linebuf)) {
			msgCount++;
			if (append(&this, mestmp))
				errx(1, "temporary file");
			this.m_flag = MUSED|MNEW;
			this.m_size = 0;
			this.m_lines = 0;
			this.m_block = blockof(offset);
			this.m_offset = boffsetof(offset);
			inhead = 1;
		} else if (linebuf[0] == 0) {
			inhead = 0;
		} else if (inhead) {
			for (cp = linebuf, cp2 = "status";; cp++) {
				if ((c = *cp2++) == '\0') {
					while (isspace((unsigned char)*cp++))
						;
					if (cp[-1] != ':')
						break;
					while ((c = *cp++) != '\0')
						if (c == 'R')
							this.m_flag |= MREAD;
						else if (c == 'O')
							this.m_flag &= ~MNEW;
					inhead = 0;
					break;
				}
				if (*cp != c && *cp != toupper((unsigned char)c))
					break;
			}
		}
		offset += count;
		this.m_size += count;
		this.m_lines++;
		maybe = linebuf[0] == 0;
	}
}

/*
 * Drop the passed line onto the passed output buffer.
 * If a write error occurs, return -1, else the count of
 * characters written, including the newline if requested.
 */
int
putline(FILE *obuf, char *linebuf, int outlf)
{
	int c;

	c = strlen(linebuf);
	(void)fwrite(linebuf, sizeof(*linebuf), c, obuf);
	if (outlf) {
		fprintf(obuf, "\n");
		c++;
	}
	if (ferror(obuf))
		return (-1);
	return (c);
}

/*
 * Read up a line from the specified input into the line
 * buffer.  Return the number of characters read.  Do not
 * include the newline (or carriage return) at the end.
 */
int
readline(FILE *ibuf, char *linebuf, int linesize)
{
	int n;

	clearerr(ibuf);
	if (fgets(linebuf, linesize, ibuf) == NULL)
		return (-1);
	n = strlen(linebuf);
	if (n > 0 && linebuf[n - 1] == '\n')
		linebuf[--n] = '\0';
	if (n > 0 && linebuf[n - 1] == '\r')
		linebuf[--n] = '\0';
	return (n);
}

/*
 * Return a file buffer all ready to read up the
 * passed message pointer.
 */
FILE *
setinput(struct message *mp)
{

	(void)fflush(otf);
	if (fseeko(itf,
		   positionof(mp->m_block, mp->m_offset), SEEK_SET) < 0)
		err(1, "fseeko");
	return (itf);
}

/*
 * Take the data out of the passed ghost file and toss it into
 * a dynamically allocated message structure.
 */
void
makemessage(FILE *f, int omsgCount)
{
	size_t size;
	struct message *nmessage;

	size = (msgCount + 1) * sizeof(struct message);
	nmessage = (struct message *)realloc(message, size);
	if (nmessage == NULL)
		errx(1, "Insufficient memory for %d messages\n",
		    msgCount);
	if (omsgCount == 0 || message == NULL)
		dot = nmessage;
	else
		dot = nmessage + (dot - message);
	message = nmessage;
	size -= (omsgCount + 1) * sizeof(struct message);
	(void)fflush(f);
	(void)lseek(fileno(f), (off_t)sizeof(*message), 0);
	if (read(fileno(f), (void *)&message[omsgCount], size) != size)
		errx(1, "Message temporary file corrupted");
	message[msgCount].m_size = 0;
	message[msgCount].m_lines = 0;
	(void)Fclose(f);
}

/*
 * Append the passed message descriptor onto the temp file.
 * If the write fails, return 1, else 0
 */
int
append(struct message *mp, FILE *f)
{
	return (fwrite((char *)mp, sizeof(*mp), 1, f) != 1);
}

/*
 * Delete a file, but only if the file is a plain file.
 */
int
rm(char *name)
{
	struct stat sb;

	if (stat(name, &sb) < 0)
		return (-1);
	if (!S_ISREG(sb.st_mode)) {
		errno = EISDIR;
		return (-1);
	}
	return (unlink(name));
}

static int sigdepth;		/* depth of holdsigs() */
static sigset_t nset, oset;
/*
 * Hold signals SIGHUP, SIGINT, and SIGQUIT.
 */
void
holdsigs(void)
{

	if (sigdepth++ == 0) {
		(void)sigemptyset(&nset);
		(void)sigaddset(&nset, SIGHUP);
		(void)sigaddset(&nset, SIGINT);
		(void)sigaddset(&nset, SIGQUIT);
		(void)sigprocmask(SIG_BLOCK, &nset, &oset);
	}
}

/*
 * Release signals SIGHUP, SIGINT, and SIGQUIT.
 */
void
relsesigs(void)
{

	if (--sigdepth == 0)
		(void)sigprocmask(SIG_SETMASK, &oset, NULL);
}

/*
 * Determine the size of the file possessed by
 * the passed buffer.
 */
off_t
fsize(FILE *iob)
{
	struct stat sbuf;

	if (fstat(fileno(iob), &sbuf) < 0)
		return (0);
	return (sbuf.st_size);
}

/*
 * Evaluate the string given as a new mailbox name.
 * Supported meta characters:
 *	%	for my system mail box
 *	%user	for user's system mail box
 *	#	for previous file
 *	&	invoker's mbox file
 *	+file	file in folder directory
 *	any shell meta character
 * Return the file name as a dynamic string.
 */
char *
expand(char *name)
{
	char xname[PATHSIZE];
	char cmdbuf[PATHSIZE];		/* also used for file names */
	int pid, l;
	char *cp, *sh;
	int pivec[2];
	struct stat sbuf;

	/*
	 * The order of evaluation is "%" and "#" expand into constants.
	 * "&" can expand into "+".  "+" can expand into shell meta characters.
	 * Shell meta characters expand into constants.
	 * This way, we make no recursive expansion.
	 */
	switch (*name) {
	case '%':
		findmail(name[1] ? name + 1 : myname, xname, sizeof(xname));
		return (savestr(xname));
	case '#':
		if (name[1] != 0)
			break;
		if (prevfile[0] == 0) {
			printf("No previous file\n");
			return (NULL);
		}
		return (savestr(prevfile));
	case '&':
		if (name[1] == 0 && (name = value("MBOX")) == NULL)
			name = "~/mbox";
		/* fall through */
	}
	if (name[0] == '+' && getfold(cmdbuf, sizeof(cmdbuf)) >= 0) {
		(void)snprintf(xname, sizeof(xname), "%s/%s", cmdbuf, name + 1);
		name = savestr(xname);
	}
	/* catch the most common shell meta character */
	if (name[0] == '~' && homedir != NULL &&
	    (name[1] == '/' || name[1] == '\0')) {
		(void)snprintf(xname, sizeof(xname), "%s%s", homedir, name + 1);
		name = savestr(xname);
	}
	if (!strpbrk(name, "~{[*?$`'\"\\"))
		return (savestr(name));
	if (pipe(pivec) < 0) {
		warn("pipe");
		return (NULL);
	}
	(void)snprintf(cmdbuf, sizeof(cmdbuf), "echo %s", name);
	if ((sh = value("SHELL")) == NULL)
		sh = _PATH_CSHELL;
	pid = start_command(sh, 0, -1, pivec[1], "-c", cmdbuf, NULL);
	if (pid < 0) {
		(void)close(pivec[0]);
		(void)close(pivec[1]);
		return (NULL);
	}
	(void)close(pivec[1]);
	l = read(pivec[0], xname, BUFSIZ);
	(void)close(pivec[0]);
	if (wait_child(pid) < 0 && WIFSIGNALED(wait_status) &&
	    WTERMSIG(wait_status) != SIGPIPE) {
		fprintf(stderr, "\"%s\": Expansion failed.\n", name);
		return (NULL);
	}
	if (l < 0) {
		warn("read");
		return (NULL);
	}
	if (l == 0) {
		fprintf(stderr, "\"%s\": No match.\n", name);
		return (NULL);
	}
	if (l == BUFSIZ) {
		fprintf(stderr, "\"%s\": Expansion buffer overflow.\n", name);
		return (NULL);
	}
	xname[l] = '\0';
	for (cp = &xname[l-1]; *cp == '\n' && cp > xname; cp--)
		;
	cp[1] = '\0';
	if (strchr(xname, ' ') && stat(xname, &sbuf) < 0) {
		fprintf(stderr, "\"%s\": Ambiguous.\n", name);
		return (NULL);
	}
	return (savestr(xname));
}

/*
 * Determine the current folder directory name.
 */
int
getfold(char *name, int namelen)
{
	char *folder;
	int copylen;

	if ((folder = value("folder")) == NULL)
		return (-1);
	if (*folder == '/')
		copylen = strlcpy(name, folder, namelen);
	else
		copylen = snprintf(name, namelen, "%s/%s",
		    homedir ? homedir : ".", folder);
	return (copylen < 0 || copylen >= namelen ? (-1) : (0));
}

/*
 * Return the name of the dead.letter file.
 */
char *
getdeadletter(void)
{
	char *cp;

	if ((cp = value("DEAD")) == NULL || (cp = expand(cp)) == NULL)
		cp = expand("~/dead.letter");
	else if (*cp != '/') {
		char buf[PATHSIZE];

		(void)snprintf(buf, sizeof(buf), "~/%s", cp);
		cp = expand(buf);
	}
	return (cp);
}
