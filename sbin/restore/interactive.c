/*	$OpenBSD: interactive.c,v 1.30 2015/01/20 18:22:21 deraadt Exp $	*/
/*	$NetBSD: interactive.c,v 1.10 1997/03/19 08:42:52 lukem Exp $	*/

/*
 * Copyright (c) 1985, 1993
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

#include <sys/time.h>
#include <sys/stat.h>

#include <ufs/ffs/fs.h>
#include <ufs/ufs/dinode.h>
#include <ufs/ufs/dir.h>
#include <protocols/dumprestore.h>

#include <setjmp.h>
#include <glob.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "restore.h"
#include "extern.h"

#define round(a, b) (((a) + (b) - 1) / (b) * (b))

/*
 * Things to handle interruptions.
 */
static int runshell;
static jmp_buf reset;
static char *nextarg = NULL;

/*
 * Structure and routines associated with listing directories.
 */
struct afile {
	ino_t	fnum;		/* inode number of file */
	char	*fname;		/* file name */
	short	len;		/* name length */
	char	prefix;		/* prefix character */
	char	postfix;	/* postfix character */
};
struct arglist {
	int	freeglob;	/* glob structure needs to be freed */
	int	argcnt;		/* next globbed argument to return */
	glob_t	glob;		/* globbing information */
	char	*cmd;		/* the current command */
};

static char	*copynext(char *, char *);
static int	 fcmp(const void *, const void *);
static void	 formatf(struct afile *, int);
static void	 getcmd(char *, char *, size_t, char *, size_t, struct arglist *);
struct dirent	*glob_readdir(RST_DIR *dirp);
static int	 glob_stat(const char *, struct stat *);
static void	 mkentry(char *, struct direct *, struct afile *);
static void	 printlist(char *, char *);

/*
 * Read and execute commands from the terminal.
 */
void
runcmdshell(void)
{
	struct entry *np;
	ino_t ino;
	struct arglist arglist;
	char curdir[PATH_MAX];
	char name[PATH_MAX];
	char cmd[BUFSIZ];

	arglist.freeglob = 0;
	arglist.argcnt = 0;
	arglist.glob.gl_flags = GLOB_ALTDIRFUNC;
	arglist.glob.gl_opendir = (void *)rst_opendir;
	arglist.glob.gl_readdir = (void *)glob_readdir;
	arglist.glob.gl_closedir = (void *)rst_closedir;
	arglist.glob.gl_lstat = glob_stat;
	arglist.glob.gl_stat = glob_stat;
	canon("/", curdir, sizeof curdir);
loop:
	if (setjmp(reset) != 0) {
		if (arglist.freeglob != 0) {
			arglist.freeglob = 0;
			arglist.argcnt = 0;
			globfree(&arglist.glob);
		}
		nextarg = NULL;
		volno = 0;
	}
	runshell = 1;
	getcmd(curdir, cmd, sizeof cmd, name, sizeof name, &arglist);
	switch (cmd[0]) {
	/*
	 * Add elements to the extraction list.
	 */
	case 'a':
		if (strncmp(cmd, "add", strlen(cmd)) != 0)
			goto bad;
		ino = dirlookup(name);
		if (ino == 0)
			break;
		if (mflag)
			pathcheck(name);
		treescan(name, ino, addfile);
		break;
	/*
	 * Change working directory.
	 */
	case 'c':
		if (strncmp(cmd, "cd", strlen(cmd)) != 0)
			goto bad;
		ino = dirlookup(name);
		if (ino == 0)
			break;
		if (inodetype(ino) == LEAF) {
			fprintf(stderr, "%s: not a directory\n", name);
			break;
		}
		(void)strlcpy(curdir, name, sizeof curdir);
		break;
	/*
	 * Delete elements from the extraction list.
	 */
	case 'd':
		if (strncmp(cmd, "delete", strlen(cmd)) != 0)
			goto bad;
		np = lookupname(name);
		if (np == NULL || (np->e_flags & NEW) == 0) {
			fprintf(stderr, "%s: not on extraction list\n", name);
			break;
		}
		treescan(name, np->e_ino, deletefile);
		break;
	/*
	 * Extract the requested list.
	 */
	case 'e':
		if (strncmp(cmd, "extract", strlen(cmd)) != 0)
			goto bad;
		createfiles();
		createlinks();
		setdirmodes(0);
		if (dflag)
			checkrestore();
		volno = 0;
		break;
	/*
	 * List available commands.
	 */
	case 'h':
		if (strncmp(cmd, "help", strlen(cmd)) != 0)
			goto bad;
	case '?':
		fprintf(stderr, "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
			"Available commands are:\n",
			"\tls [arg] - list directory\n",
			"\tcd arg - change directory\n",
			"\tpwd - print current directory\n",
			"\tadd [arg] - add `arg' to list of",
			" files to be extracted\n",
			"\tdelete [arg] - delete `arg' from",
			" list of files to be extracted\n",
			"\textract - extract requested files\n",
			"\tsetmodes - set modes of requested directories\n",
			"\tquit - immediately exit program\n",
			"\twhat - list dump header information\n",
			"\tverbose - toggle verbose flag",
			" (useful with ``ls'')\n",
			"\thelp or `?' - print this list\n",
			"If no `arg' is supplied, the current",
			" directory is used\n");
		break;
	/*
	 * List a directory.
	 */
	case 'l':
		if (strncmp(cmd, "ls", strlen(cmd)) != 0)
			goto bad;
		printlist(name, curdir);
		break;
	/*
	 * Print current directory.
	 */
	case 'p':
		if (strncmp(cmd, "pwd", strlen(cmd)) != 0)
			goto bad;
		if (curdir[1] == '\0')
			fprintf(stderr, "/\n");
		else
			fprintf(stderr, "%s\n", &curdir[1]);
		break;
	/*
	 * Quit.
	 */
	case 'q':
		if (strncmp(cmd, "quit", strlen(cmd)) != 0)
			goto bad;
		return;
	case 'x':
		if (strncmp(cmd, "xit", strlen(cmd)) != 0)
			goto bad;
		return;
	/*
	 * Toggle verbose mode.
	 */
	case 'v':
		if (strncmp(cmd, "verbose", strlen(cmd)) != 0)
			goto bad;
		if (vflag) {
			fprintf(stderr, "verbose mode off\n");
			vflag = 0;
			break;
		}
		fprintf(stderr, "verbose mode on\n");
		vflag++;
		break;
	/*
	 * Just restore requested directory modes.
	 */
	case 's':
		if (strncmp(cmd, "setmodes", strlen(cmd)) != 0)
			goto bad;
		setdirmodes(FORCE);
		break;
	/*
	 * Print out dump header information.
	 */
	case 'w':
		if (strncmp(cmd, "what", strlen(cmd)) != 0)
			goto bad;
		printdumpinfo();
		break;
	/*
	 * Turn on debugging.
	 */
	case 'D':
		if (strncmp(cmd, "Debug", strlen(cmd)) != 0)
			goto bad;
		if (dflag) {
			fprintf(stderr, "debugging mode off\n");
			dflag = 0;
			break;
		}
		fprintf(stderr, "debugging mode on\n");
		dflag++;
		break;
	/*
	 * Unknown command.
	 */
	default:
	bad:
		fprintf(stderr, "%s: unknown command; type ? for help\n", cmd);
		break;
	}
	goto loop;
}

/*
 * Read and parse an interactive command.
 * The first word on the line is assigned to "cmd". If
 * there are no arguments on the command line, then "curdir"
 * is returned as the argument. If there are arguments
 * on the line they are returned one at a time on each
 * successive call to getcmd. Each argument is first assigned
 * to "name". If it does not start with "/" the pathname in
 * "curdir" is prepended to it. Finally "canon" is called to
 * eliminate any embedded ".." components.
 */
static void
getcmd(char *curdir, char *cmd, size_t cmdlen, char *name, size_t namelen,
       struct arglist *ap)
{
	char *cp;
	static char input[BUFSIZ];
	char output[BUFSIZ];
#	define rawname input	/* save space by reusing input buffer */
	int globretval;

	/*
	 * Check to see if still processing arguments.
	 */
	if (ap->argcnt > 0)
		goto retnext;
	if (nextarg != NULL)
		goto getnext;
	/*
	 * Read a command line and trim off trailing white space.
	 */
	do {
		(void)fprintf(stderr, "%s > ", __progname);
		(void)fflush(stderr);
		if (fgets(input, sizeof input, terminal) == NULL) {
			(void)strlcpy(cmd, "quit", cmdlen);
			return;
		}
	} while (input[0] == '\n' || input[0] == '\0');
	for (cp = &input[strlen(input) - 1];
	     cp >= input && (*cp == ' ' || *cp == '\t' || *cp == '\n'); cp--)
		/* trim off trailing white space and newline */;
	*++cp = '\0';
	/*
	 * Copy the command into "cmd".
	 */
	cp = copynext(input, cmd);
	ap->cmd = cmd;
	/*
	 * If no argument, use curdir as the default.
	 */
	if (*cp == '\0') {
		(void)strlcpy(name, curdir, PATH_MAX);
		return;
	}
	nextarg = cp;
	/*
	 * Find the next argument.
	 */
getnext:
	cp = copynext(nextarg, rawname);
	if (*cp == '\0')
		nextarg = NULL;
	else
		nextarg = cp;
	/*
	 * If it is an absolute pathname, canonicalize it and return it.
	 */
	if (rawname[0] == '/') {
		canon(rawname, name, namelen);
	} else {
		/*
		 * For relative pathnames, prepend the current directory to
		 * it then canonicalize and return it.
		 */
		snprintf(output, sizeof(output), "%s/%s", curdir, rawname);
		canon(output, name, namelen);
	}
	if ((globretval = glob(name, GLOB_ALTDIRFUNC | GLOB_NOESCAPE,
	    NULL, &ap->glob)) < 0) {
		fprintf(stderr, "%s: %s: ", ap->cmd, name);
		switch (globretval) {
		case GLOB_NOSPACE:
			fprintf(stderr, "out of memory\n");
			break;
		case GLOB_NOMATCH:
			fprintf(stderr, "no filename match.\n");
			break;
		case GLOB_ABORTED:
			fprintf(stderr, "glob() aborted.\n");
			break;
		default:
			fprintf(stderr, "unknown error!\n");
			break;
		}
	}

	if (ap->glob.gl_pathc == 0)
		return;
	ap->freeglob = 1;
	ap->argcnt = ap->glob.gl_pathc;

retnext:
	strlcpy(name, ap->glob.gl_pathv[ap->glob.gl_pathc - ap->argcnt],
	    PATH_MAX);
	if (--ap->argcnt == 0) {
		ap->freeglob = 0;
		globfree(&ap->glob);
	}
#	undef rawname
}

/*
 * Strip off the next token of the input.
 */
static char *
copynext(char *input, char *output)
{
	char *cp, *bp;
	char quote;

	for (cp = input; *cp == ' ' || *cp == '\t'; cp++)
		/* skip to argument */;
	bp = output;
	while (*cp != ' ' && *cp != '\t' && *cp != '\0') {
		/*
		 * Handle back slashes.
		 */
		if (*cp == '\\') {
			if (*++cp == '\0') {
				fprintf(stderr,
					"command lines cannot be continued\n");
				continue;
			}
			*bp++ = *cp++;
			continue;
		}
		/*
		 * The usual unquoted case.
		 */
		if (*cp != '\'' && *cp != '"') {
			*bp++ = *cp++;
			continue;
		}
		/*
		 * Handle single and double quotes.
		 */
		quote = *cp++;
		while (*cp != quote && *cp != '\0')
			*bp++ = *cp++;
		if (*cp++ == '\0') {
			fprintf(stderr, "missing %c\n", quote);
			cp--;
			continue;
		}
	}
	*bp = '\0';
	return (cp);
}

/*
 * Canonicalize file names to always start with ``./'' and
 * remove any imbedded "." and ".." components.
 */
void
canon(char *rawname, char *canonname, size_t canonnamelen)
{
	char *cp, *np;

	if (strcmp(rawname, ".") == 0 || strncmp(rawname, "./", 2) == 0)
		(void)strlcpy(canonname, "", canonnamelen);
	else if (rawname[0] == '/')
		(void)strlcpy(canonname, ".", canonnamelen);
	else
		(void)strlcpy(canonname, "./", canonnamelen);
	(void)strlcat(canonname, rawname, canonnamelen);
	/*
	 * Eliminate multiple and trailing '/'s
	 */
	for (cp = np = canonname; *np != '\0'; cp++) {
		*cp = *np++;
		while (*cp == '/' && *np == '/')
			np++;
	}
	*cp = '\0';
	if (*--cp == '/')
		*cp = '\0';
	/*
	 * Eliminate extraneous "." and ".." from pathnames.
	 */
	for (np = canonname; *np != '\0'; ) {
		np++;
		cp = np;
		while (*np != '/' && *np != '\0')
			np++;
		if (np - cp == 1 && *cp == '.') {
			cp--;
			(void)strlcpy(cp, np, canonname + canonnamelen - cp);
			np = cp;
		}
		if (np - cp == 2 && strncmp(cp, "..", 2) == 0) {
			cp--;
			while (cp > &canonname[1] && *--cp != '/')
				/* find beginning of name */;
			(void)strlcpy(cp, np, canonname + canonnamelen - cp);
			np = cp;
		}
	}
}

/*
 * Do an "ls" style listing of a directory
 */
static void
printlist(char *name, char *basename)
{
	struct afile *fp, *list, *listp = NULL;
	struct direct *dp;
	struct afile single;
	RST_DIR *dirp;
	size_t namelen;
	int entries, len;
	char locname[PATH_MAX];

	dp = pathsearch(name);
	if (dp == NULL || (!dflag && TSTINO(dp->d_ino, dumpmap) == 0))
		return;
	if ((dirp = rst_opendir(name)) == NULL) {
		entries = 1;
		list = &single;
		mkentry(name, dp, list);
		len = strlen(basename) + 1;
		if (strlen(name) - len > single.len) {
			freename(single.fname);
			single.fname = savename(&name[len]);
			single.len = strlen(single.fname);
		}
	} else {
		entries = 0;
		while ((dp = rst_readdir(dirp)))
			entries++;
		rst_closedir(dirp);
		list = calloc(entries, sizeof(struct afile));
		if (list == NULL) {
			fprintf(stderr, "ls: out of memory\n");
			return;
		}
		if ((dirp = rst_opendir(name)) == NULL)
			panic("directory reopen failed\n");
		fprintf(stderr, "%s:\n", name);
		entries = 0;
		listp = list;
		namelen = strlcpy(locname, name, sizeof(locname));
		if (namelen >= sizeof(locname) - 1)
			namelen = sizeof(locname) - 2;
		locname[namelen++] = '/';
		locname[namelen] = '\0';
		while ((dp = rst_readdir(dirp))) {
			if (dp == NULL)
				break;
			if (!dflag && TSTINO(dp->d_ino, dumpmap) == 0)
				continue;
			if (!vflag && (strcmp(dp->d_name, ".") == 0 ||
			     strcmp(dp->d_name, "..") == 0))
				continue;
			locname[namelen] = '\0';
			if (namelen + dp->d_namlen >= PATH_MAX) {
				fprintf(stderr, "%s%s: name exceeds %d char\n",
					locname, dp->d_name, PATH_MAX);
			} else {
				(void)strncat(locname, dp->d_name,
				    (int)dp->d_namlen);
				mkentry(locname, dp, listp++);
				entries++;
			}
		}
		rst_closedir(dirp);
		if (entries == 0) {
			fprintf(stderr, "\n");
			free(list);
			return;
		}
		qsort((char *)list, entries, sizeof(struct afile), fcmp);
	}
	formatf(list, entries);
	if (dirp != NULL) {
		for (fp = listp - 1; fp >= list; fp--)
			freename(fp->fname);
		fprintf(stderr, "\n");
		free(list);
	}
}

/*
 * Read the contents of a directory.
 */
static void
mkentry(char *name, struct direct *dp, struct afile *fp)
{
	char *cp;
	struct entry *np;

	fp->fnum = dp->d_ino;
	fp->fname = savename(dp->d_name);
	for (cp = fp->fname; *cp; cp++)
		if (!vflag && (*cp < ' ' || *cp >= 0177))
			*cp = '?';
	fp->len = cp - fp->fname;
	if (dflag && TSTINO(fp->fnum, dumpmap) == 0)
		fp->prefix = '^';
	else if ((np = lookupname(name)) != NULL && (np->e_flags & NEW))
		fp->prefix = '*';
	else
		fp->prefix = ' ';
	switch(dp->d_type) {

	default:
		fprintf(stderr, "Warning: undefined file type %d\n",
		    dp->d_type);
		/* fall through */
	case DT_REG:
		fp->postfix = ' ';
		break;

	case DT_LNK:
		fp->postfix = '@';
		break;

	case DT_FIFO:
	case DT_SOCK:
		fp->postfix = '=';
		break;

	case DT_CHR:
	case DT_BLK:
		fp->postfix = '#';
		break;

	case DT_UNKNOWN:
	case DT_DIR:
		if (inodetype(dp->d_ino) == NODE)
			fp->postfix = '/';
		else
			fp->postfix = ' ';
		break;
	}
	return;
}

/*
 * Print out a pretty listing of a directory
 */
static void
formatf(struct afile *list, int nentry)
{
	struct afile *fp, *endlist;
	int width, bigino, haveprefix, havepostfix;
	int i, j, w, precision = 0, columns, lines;

	width = 0;
	haveprefix = 0;
	havepostfix = 0;
	bigino = ROOTINO;
	endlist = &list[nentry];
	for (fp = &list[0]; fp < endlist; fp++) {
		if (bigino < fp->fnum)
			bigino = fp->fnum;
		if (width < fp->len)
			width = fp->len;
		if (fp->prefix != ' ')
			haveprefix = 1;
		if (fp->postfix != ' ')
			havepostfix = 1;
	}
	if (haveprefix)
		width++;
	if (havepostfix)
		width++;
	if (vflag) {
		for (precision = 0, i = bigino; i > 0; i /= 10)
			precision++;
		width += precision + 1;
	}
	width++;
	columns = 81 / width;
	if (columns == 0)
		columns = 1;
	lines = (nentry + columns - 1) / columns;
	for (i = 0; i < lines; i++) {
		for (j = 0; j < columns; j++) {
			fp = &list[j * lines + i];
			if (vflag) {
				fprintf(stderr, "%*llu ", precision,
				(unsigned long long)fp->fnum);
				fp->len += precision + 1;
			}
			if (haveprefix) {
				putc(fp->prefix, stderr);
				fp->len++;
			}
			fprintf(stderr, "%s", fp->fname);
			if (havepostfix) {
				putc(fp->postfix, stderr);
				fp->len++;
			}
			if (fp + lines >= endlist) {
				fprintf(stderr, "\n");
				break;
			}
			for (w = fp->len; w < width; w++)
				putc(' ', stderr);
		}
	}
}

/*
 * Skip over directory entries that are not on the tape
 *
 * First have to get definition of a dirent.
 */
#undef DIRBLKSIZ
#include <dirent.h>
#undef d_ino

struct dirent *
glob_readdir(RST_DIR *dirp)
{
	struct direct *dp;
	static struct dirent adirent;

	while ((dp = rst_readdir(dirp)) != NULL) {
		if (dflag || TSTINO(dp->d_ino, dumpmap))
			break;
	}
	if (dp == NULL)
		return (NULL);
	adirent.d_fileno = dp->d_ino;
	adirent.d_namlen = dp->d_namlen;
	memcpy(adirent.d_name, dp->d_name, dp->d_namlen + 1);
	return (&adirent);
}

/*
 * Return st_mode information in response to stat or lstat calls
 */
static int
glob_stat(const char *name, struct stat *stp)
{
	struct direct *dp;

	dp = pathsearch(name);
	if (dp == NULL || (!dflag && TSTINO(dp->d_ino, dumpmap) == 0))
		return (-1);
	if (inodetype(dp->d_ino) == NODE)
		stp->st_mode = S_IFDIR;
	else
		stp->st_mode = S_IFREG;
	return (0);
}

/*
 * Comparison routine for qsort.
 */
static int
fcmp(const void *f1, const void *f2)
{
	return (strcmp(((struct afile *)f1)->fname,
	    ((struct afile *)f2)->fname));
}

/*
 * respond to interrupts
 */
void
onintr(int signo)
{
	int save_errno = errno;

	if (command == 'i' && runshell)
		longjmp(reset, 1);	/* XXX signal/longjmp reentrancy */
	if (reply("restore interrupted, continue") == FAIL)	/* XXX signal race */
		_exit(1);
	errno = save_errno;
}
