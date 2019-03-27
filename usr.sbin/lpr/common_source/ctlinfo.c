/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 * Copyright (c) 2001,2011  - Garance Alistair Drosehn <gad@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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
 *
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the FreeBSD Project.
 *
 * ------+---------+---------+---------+---------+---------+---------+---------*
 */

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

/*
 * ctlinfo - This collection of routines will know everything there is to
 * know about the information inside a control file ('cf*') which is used
 * to describe a print job in lpr & friends.  The eventual goal is that it
 * will be the ONLY source file to know what's inside these control-files.
 */

/*
 * Some define's useful for debuging.
 * TRIGGERTEST_FNAME and DEBUGREADCF_FNAME, allow us to do testing on
 * a per-spool-directory basis.
 */
/* #define TRIGGERTEST_FNAME "LpdTestRenameTF" */
/* #define DEBUGREADCF_FNAME "LpdDebugReadCF" */
/* #define LEAVE_TMPCF_FILES 1 */

#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include "ctlinfo.h"

struct cjprivate {
	struct cjobinfo pub;
	char	*cji_buff;		/* buffer for getline */
	char	*cji_eobuff;		/* last byte IN the buffer */
	FILE	*cji_fstream;
	int	 cji_buffsize;		/* # bytes in the buffer */
	int	 cji_dumpit;
};

/*
 * All the following take a parameter of 'int', but expect values in the
 * range of unsigned char.  Define wrappers which take values of type 'char',
 * whether signed or unsigned, and ensure they end up in the right range.
 */
#define	isdigitch(Anychar) isdigit((u_char)(Anychar))
#define	islowerch(Anychar) islower((u_char)(Anychar))
#define	isupperch(Anychar) isupper((u_char)(Anychar))
#define	tolowerch(Anychar) tolower((u_char)(Anychar))

#define	OTHER_USERID_CHARS  "-_"	/* special chars valid in a userid */

#define roundup(x, y)   ((((x)+((y)-1))/(y))*(y))

/*
 * This has to be large enough to fit the maximum length of a single line
 * in a control-file, including the leading 'command id', a trailing '\n'
 * and ending '\0'.  The max size of an 'U'nlink line, for instance, is
 * 1 ('U') + PATH_MAX (filename) + 2 ('\n\0').  The maximum 'H'ost line is
 * 1 ('H') + NI_MAXHOST (remote hostname) + 2 ('\n\0').  Other lines can be
 * even longer than those.  So, pick some nice, large, arbitrary value.
 */
#define CTI_LINEMAX  PATH_MAX+NI_MAXHOST+5

extern const char	*from_host;	/* client's machine name */
extern const char	*from_ip;	/* client machine's IP address */

__BEGIN_DECLS
void		 ctl_dumpcji(FILE *_dbg_stream, const char *_heading,
		    struct cjobinfo *_cjinf);
static char	*ctl_getline(struct cjobinfo *_cjinf);
static void	 ctl_rewindcf(struct cjobinfo *_cjinf);
char		*ctl_rmjob(const char *_ptrname, const char *_cfname);
__END_DECLS

/*
 * Here are some things which might be needed when compiling this under
 * platforms other than FreeBSD.
 */
#ifndef __FreeBSD__
#   ifndef NAME_MAX
#	define NAME_MAX	255
#   endif
#   ifndef NI_MAXHOST
#	define NI_MAXHOST	1025
#   endif
#   ifndef PATH_MAX
#	define PATH_MAX	1024
#   endif
__BEGIN_DECLS
char		*strdup(const char *_src);
size_t		 strlcpy(char *_dst, const char *_src, size_t _siz);
__END_DECLS
#endif

/*
 *	Control-files (cf*) have the following format.
 *
 *	Each control-file describes a single job.  It will list one or more
 *	"datafiles" (df*) which should be copied to some printer.  Usually
 *	there is only one datafile per job.  For the curious, RFC 1179 is an
 *	informal and out-of-date description of lpr/lpd circa 1990.
 *
 *	Each line in the file gives an attribute of the job as a whole, or one
 *	of the datafiles in the job, or a "command" indicating something to do
 *	with one of the datafiles.  Each line starts with an 'id' that indicates
 *	what that line is there for.  The 'id' is historically a single byte,
 *	but may be multiple bytes (obviously it would be best if multi-byte ids
 *	started with some letter not already used as a single-byte id!).
 *	After the 'id', the remainder of the line will be the value of the
 *	indicated attribute, or a name of the datafile to be operated on.
 *
 *	In the following lists of ids, the ids with a '!' in front of them are
 *	NOT explicitly supported by this version of lpd, or at least "not yet
 *	supported".  They are only listed for reference purposes, so people
 *	won't be tempted to reuse the same id for a different purpose.
 *
 *	The following are attributes of the job which should not appear more
 *	than once in a control file.  Only the 'H' and 'P' lines are required
 *	by the RFC, but some implementations of lpr won't even get that right.
 *
 *	! A   - [used by lprNG]
 *	  B   - As far as I know, this is never used as a single-byte id.
 *		Therefore, I intend to use it for multi-byte id codes.
 *	  C   - "class name" to display on banner page (this is sometimes
 *		used to hold options for print filters)
 *	! D   - [in lprNG, "timestamp" of when the job was submitted]
 *	! E   - "environment variables" to set [some versions of linux]
 *	  H   - "host name" of machine where the original 'lpr' was done
 *	  I   - "indent", the amount to indent output
 *	  J   - "job name" to display on banner page
 *	  L   - "literal" user's name as it should be displayed on the
 *		banner page (it is the existence of an 'L' line which
 *		indicates that a job should have a banner page).
 *	  M   - "mail", userid to mail to when done printing (with email
 *		going to 'M'@'H', so to speak).
 *	  P   - "person", the user's login name (e.g. for accounting)
 *	! Q   - [used by lprNG for queue-name]
 *	  R   - "resolution" in dpi, for some laser printer queues
 *	  T   - "title" for files sent thru 'pr'
 *	  W   - "width" to use for printing plain-text files
 *	  Z   - In BSD, "locale" to use for datafiles sent thru 'pr'.
 *		(this BSD usage should move to a different id...)
 *		[in lprNG - this line holds the "Z options"]
 *	  1   - "R font file" for files sent thru troff
 *	  2   - "I font file" for files sent thru troff
 *	  3   - "B font file" for files sent thru troff
 *	  4   - "S font file" for files sent thru troff
 *
 *	The following are attributes attached to a datafile, and thus may
 *	appear multiple times in a control file (once per datafile):
 *
 *	  N   - "name" of file (for display purposes, used by 'lpq')
 *	  S   - "stat() info" used for symbolic link ('lpr -s')
 *		security checks.
 *
 *	The following indicate actions to take on a given datafile.  The same
 *	datafile may appear on more than one "print this file" command in the
 *	control file.  Note that ALL ids with lowercase letters are expected
 *	to be actions to "print this file":
 *
 *	  c   - "file name", cifplot file to print.  This action appears
 *		when the user has requested 'lpr -c'.
 *	  d   - "file name", dvi file to print, user requested 'lpr -d'
 *	  f   - "file name", a plain-text file to print = "standard"
 *	  g   - "file name", plot(1G) file to print, ie 'lpr -g'
 *	  l   - "file name", text file with control chars which should
 *		be printed literally, ie 'lpr -l'  (note: some printers
 *		take this id as a request to print a postscript file,
 *		and because of *that* some OS's use 'l' to indicate
 *		that a datafile is a postscript file)
 *	  n   - "file name", ditroff(1) file to print, ie 'lpr -n'
 *	  o   - "file name", a postscript file to print.  This id is
 *		described in the original RFC, but not much has been
 *		done with it.  This 'lpr' does not generate control
 *		lines with 'o'-actions, but lpd's printjob processing
 *		will treat it the same as 'l'.
 *	  p   - "file name", text file to print with pr(1), ie 'lpr -p'
 *	  t   - "file name", troff(1) file to print, ie 'lpr -t'
 *	  v   - "file name", plain raster file to print
 *
 *	  U   - "file name" of datafile to unlink (ie, remove file
 *		from spool directory.  To be done in a 'Pass 2',
 *		AFTER having processed all datafiles in the job).
 *
 */

void
ctl_freeinf(struct cjobinfo *cjinf)
{
#define FREESTR(xStr) \
	if (xStr != NULL) { \
		free(xStr); \
		xStr = NULL;\
	}

	struct cjprivate *cpriv;

	if (cjinf == NULL)
		return;
	cpriv = cjinf->cji_priv;
	if ((cpriv == NULL) || (cpriv != cpriv->pub.cji_priv)) {
		syslog(LOG_ERR, "in ctl_freeinf(%p): invalid cjinf (cpriv %p)",
		    (void *)cjinf, (void *)cpriv);
		return;
	}

	FREESTR(cpriv->pub.cji_accthost);
	FREESTR(cpriv->pub.cji_acctuser);
	FREESTR(cpriv->pub.cji_class);
	FREESTR(cpriv->pub.cji_curqueue);
	/* [cpriv->pub.cji_fname is part of cpriv-malloced area] */
	FREESTR(cpriv->pub.cji_jobname);
	FREESTR(cpriv->pub.cji_mailto);
	FREESTR(cpriv->pub.cji_headruser);

	if (cpriv->cji_fstream != NULL) {
		fclose(cpriv->cji_fstream);
		cpriv->cji_fstream = NULL;
	}

	cjinf->cji_priv = NULL;
	free(cpriv);
#undef FREESTR
}

#ifdef DEBUGREADCF_FNAME
static FILE *ctl_dbgfile = NULL;
static struct stat ctl_dbgstat;
#endif
static int ctl_dbgline = 0;

struct cjobinfo *
ctl_readcf(const char *ptrname, const char *cfname)
{
	int id;
	char *lbuff;
	void *cstart;
	FILE *cfile;
	struct cjprivate *cpriv;
	struct cjobinfo *cjinf;
	size_t msize, sroom, sroom2;

	cfile = fopen(cfname, "r");
	if (cfile == NULL) {
		syslog(LOG_ERR, "%s: ctl_readcf error fopen(%s): %s",
		    ptrname, cfname, strerror(errno));
		return NULL;
	}

	sroom = roundup(sizeof(struct cjprivate), 8);
	sroom2 = sroom + strlen(cfname) + 1;
	sroom2 = roundup(sroom2, 8);
	msize = sroom2 + CTI_LINEMAX;
	msize = roundup(msize, 8);
	cstart = malloc(msize);
	if (cstart == NULL) {
		fclose(cfile);
		return NULL;
	}
	memset(cstart, 0, msize);
	cpriv = (struct cjprivate *)cstart;
	cpriv->pub.cji_priv = cpriv;

	cpriv->pub.cji_fname = (char *)cstart + sroom;
	strcpy(cpriv->pub.cji_fname, cfname);
	cpriv->cji_buff = (char *)cstart + sroom2;
	cpriv->cji_buffsize = (int)(msize - sroom2);
	cpriv->cji_eobuff = (char *)cstart + msize - 1;

	cpriv->cji_fstream = cfile;
	cpriv->pub.cji_curqueue = strdup(ptrname);

	ctl_dbgline = 0;
#ifdef DEBUGREADCF_FNAME
	ctl_dbgfile = NULL;
	id = stat(DEBUGREADCF_FNAME, &ctl_dbgstat);
	if (id != -1) {
		/* the file exists in this spool directory, write some simple
		 * debugging info to it */
		ctl_dbgfile = fopen(DEBUGREADCF_FNAME, "a");
		if (ctl_dbgfile != NULL) {
			fprintf(ctl_dbgfile, "%s: s=%p r=%ld e=%p %p->%s\n",
			    ptrname, (void *)cpriv, (long)sroom,
			    cpriv->cji_eobuff, cpriv->pub.cji_fname,
			    cpriv->pub.cji_fname);
		}
	}
#endif
	/*
	 * Copy job-attribute values from control file to the struct of
	 * "public" information.  In some cases, it is invalid for the
	 * value to be a null-string, so that is ignored.
	 */
	cjinf = &(cpriv->pub);
	lbuff = ctl_getline(cjinf);
	while (lbuff != NULL) {
		id = *lbuff++;
		switch (id) {
		case 'C':
			cpriv->pub.cji_class = strdup(lbuff);
			break;
		case 'H':
			if (*lbuff == '\0')
				break;
			cpriv->pub.cji_accthost = strdup(lbuff);
			break;
		case 'J':
			cpriv->pub.cji_jobname = strdup(lbuff);
			break;
		case 'L':
			cpriv->pub.cji_headruser = strdup(lbuff);
			break;
		case 'M':
			/*
			 * No valid mail-to address would start with a minus.
			 * If this one does, it is probably some trickster who
			 * is trying to trigger options on sendmail.  Ignore.
			 */
			if (*lbuff == '-')
				break;
			if (*lbuff == '\0')
				break;
			cpriv->pub.cji_mailto = strdup(lbuff);
			break;
		case 'P':
			if (*lbuff == '\0')
				break;
			/* The userid must not start with a minus sign */
			if (*lbuff == '-')
				*lbuff = '_';
			cpriv->pub.cji_acctuser = strdup(lbuff);
			break;
		default:
			if (islower(id)) {
				cpriv->pub.cji_dfcount++;
			}
			break;
		}
		lbuff = ctl_getline(cjinf);
	}

	/* the 'H'ost and 'P'erson fields are *always* supposed to be there */
	if (cpriv->pub.cji_accthost == NULL)
		cpriv->pub.cji_accthost = strdup(".na.");
	if (cpriv->pub.cji_acctuser == NULL)
		cpriv->pub.cji_acctuser = strdup(".na.");

#ifdef DEBUGREADCF_FNAME
	if (ctl_dbgfile != NULL) {
		if (cpriv->cji_dumpit)
			ctl_dumpcji(ctl_dbgfile, "end readcf", &(cpriv->pub));
		fclose(ctl_dbgfile);
		ctl_dbgfile = NULL;
	}
#endif
	return &(cpriv->pub);
}

/*
 * This routine renames the temporary control file as received from some
 * other (remote) host.  That file will almost always with `tfA*', because
 * recvjob.c creates the file by changing `c' to `t' in the original name
 * for the control file.  Now if you read the RFC, you would think that all
 * control filenames start with `cfA*'.  However, it seems there are some
 * implementations which send control filenames which start with `cf'
 * followed by *any* letter, so this routine can not assume what the third
 * letter will (or will not) be.  Sigh.
 *
 * So this will rewrite the temporary file to `rf*' (correcting any lines
 * which need correcting), rename that `rf*' file to `cf*', and then remove
 * the original `tf*' temporary file.
 *
 * The *main* purpose of this routine is to be paranoid about the contents
 * of that control file.  It is partially meant to protect against people
 * TRYING to cause trouble (perhaps after breaking into root of some host
 * that this host will accept print jobs from).  The fact that we're willing
 * to print jobs from some remote host does not mean that we should blindly
 * do anything that host tells us to do.
 *
 * This is also meant to protect us from errors in other implementations of
 * lpr, particularly since we may want to use some values from the control
 * file as environment variables when it comes time to print, or as parameters
 * to commands which will be exec'ed, or values in statistics records.
 *
 * This may also do some "conversions" between how different versions of
 * lpr or lprNG define the contents of various lines in a control file.
 *
 * If there is an error, it returns a pointer to a descriptive error message.
 * Error messages which are RETURNED (as opposed to syslog-ed) do not include
 * the printer-queue name.  Let the caller add that if it is wanted.
 */
char *
ctl_renametf(const char *ptrname, const char *tfname)
{
	int chk3rd, has_uc, newfd, nogood, res;
	FILE *newcf;
	struct cjobinfo *cjinf;
	char *lbuff, *slash, *cp;
	char tfname2[NAME_MAX+1], cfname2[NAME_MAX+1];
	char errm[CTI_LINEMAX];

#ifdef TRIGGERTEST_FNAME
	struct stat tstat;
	res = stat(TRIGGERTEST_FNAME, &tstat);
	if (res == -1) {
		/*
		 * if the trigger file does NOT exist in this spool directory,
		 * then do the exact same steps that the pre-ctlinfo code had
		 * been doing.  Ie, very little.
		 */
		strlcpy(cfname2, tfname, sizeof(cfname2));
		cfname2[0] = 'c';
		res = link(tfname, cfname2);
		if (res < 0) {
			snprintf(errm, sizeof(errm),
			    "ctl_renametf error link(%s,%s): %s", tfname,
			    cfname2, strerror(errno));
			return strdup(errm);
		}
		unlink(tfname);
		return NULL;
	}
#endif
	cjinf = NULL;		/* in case of early jump to error_ret */
	newcf = NULL;		/* in case of early jump to error_ret */
	*errm = '\0';		/* in case of early jump to error_ret */

	chk3rd = tfname[2];
	if ((tfname[0] != 't') || (tfname[1] != 'f') || (!isalpha(chk3rd))) {
		snprintf(errm, sizeof(errm),
		    "ctl_renametf invalid filename: %s", tfname);
		goto error_ret;
	}

	cjinf = ctl_readcf(ptrname, tfname);
	if (cjinf == NULL) {
		snprintf(errm, sizeof(errm),
		    "ctl_renametf error cti_readcf(%s)", tfname);
		goto error_ret;
	}

	/*
	 * This uses open+fdopen instead of fopen because that combination
	 * gives us greater control over file-creation issues.
	 */
	strlcpy(tfname2, tfname, sizeof(tfname2));
	tfname2[0] = 'r';		/* rf<letter><job><hostname> */
	newfd = open(tfname2, O_WRONLY|O_CREAT|O_TRUNC, 0660);
	if (newfd == -1) {
		snprintf(errm, sizeof(errm),
		    "ctl_renametf error open(%s): %s", tfname2,
		    strerror(errno));
		goto error_ret;
	}
	newcf = fdopen(newfd, "w");
	if (newcf == NULL) {
		close(newfd);
		snprintf(errm, sizeof(errm),
		    "ctl_renametf error fopen(%s): %s", tfname2,
		    strerror(errno));
		goto error_ret;
	}

	/*
	 * Do extra sanity checks on some key job-attribute fields, and
	 * write them out first (thus making sure they are written in the
	 * order we generally expect them to be in).
	 */
	/*
	 * Some lpr implementations on PC's set a null-string for their
	 * hostname.  A MacOS 10 system which has not correctly setup
	 * /etc/hostconfig will claim a hostname of 'localhost'.  Anything
	 * with blanks in it would be an invalid value for hostname.  For
	 * any of these invalid hostname values, replace the given value
	 * with the name of the host that this job is coming from.
	 */
	nogood = 0;
	if (cjinf->cji_accthost == NULL)
		nogood = 1;
	else if (strcmp(cjinf->cji_accthost, ".na.") == 0)
		nogood = 1;
	else if (strcmp(cjinf->cji_accthost, "localhost") == 0)
		nogood = 1;
	else {
		for (cp = cjinf->cji_accthost; *cp != '\0'; cp++) {
			if (*cp <= ' ') {
				nogood = 1;
				break;
			}
		}
	}
	if (nogood)
		fprintf(newcf, "H%s\n", from_host);
	else
		fprintf(newcf, "H%s\n", cjinf->cji_accthost);

	/*
	 * Now do some sanity checks on the 'P' (original userid) value.  Note
	 * that the 'P'erson line is the second line which is ALWAYS supposed
	 * to be present in a control file.
	 *
	 * There is no particularly good value to use for replacements, but
	 * at least make sure the value is something reasonable to use in
	 * environment variables and statistics records.  Again, some PC
	 * implementations send a null-string for a value.  Various Mac
	 * implementations will set whatever string the user has set for
	 * their 'Owner Name', which usually includes blanks, etc.
	 */
	nogood = 0;
	if (cjinf->cji_acctuser == NULL)
		nogood = 1;
	else if (strcmp(cjinf->cji_acctuser, ".na.") == 0)
		;			/* No further checks needed... */
	else {
		has_uc = 0;
		cp = cjinf->cji_acctuser;
		if (*cp == '-')
			*cp++ = '_';
		for (; *cp != '\0'; cp++) {
			if (islowerch(*cp) || isdigitch(*cp))
				continue;	/* Standard valid characters */
			if (strchr(OTHER_USERID_CHARS, *cp) != NULL)
				continue;	/* Some more valid characters */
			if (isupperch(*cp)) {
				has_uc = 1;	/* These may be valid... */
				continue;
			}
			*cp = '_';
		}
		/*
		 * Some Windows hosts send print jobs where the correct userid
		 * has been converted to uppercase, and that can cause trouble
		 * for sites that expect the correct value (for something like
		 * accounting).  On the other hand, some sites do use uppercase
		 * in their userids, so we can't blindly convert to lowercase.
		 */
		if (has_uc && (getpwnam(cjinf->cji_acctuser) == NULL)) {
			for (cp = cjinf->cji_acctuser; *cp != '\0'; cp++) {
				if (isupperch(*cp))
					*cp = tolowerch(*cp);
			}
		}
	}
	if (nogood)
		fprintf(newcf, "P%s\n", ".na.");
	else
		fprintf(newcf, "P%s\n", cjinf->cji_acctuser);

	/* No need for sanity checks on class, jobname, "literal" user. */
	if (cjinf->cji_class != NULL)
		fprintf(newcf, "C%s\n", cjinf->cji_class);
	if (cjinf->cji_jobname != NULL)
		fprintf(newcf, "J%s\n", cjinf->cji_jobname);
	if (cjinf->cji_headruser != NULL)
		fprintf(newcf, "L%s\n", cjinf->cji_headruser);

	/*
	 * This should probably add more sanity checks on mailto value.
	 * Note that if the mailto value is "wrong", then there's no good
	 * way to know what the "correct" value would be, and we should not
	 * semd email to some random address.  At least for now, just ignore
	 * any invalid values.
	 */
	nogood = 0;
	if (cjinf->cji_mailto == NULL)
		nogood = 1;
	else {
		for (cp = cjinf->cji_mailto; *cp != '\0'; cp++) {
			if (*cp <= ' ') {
				nogood = 1;
				break;
			}
		}
	}
	if (!nogood)
		fprintf(newcf, "M%s\n", cjinf->cji_mailto);

	/*
	 * Now go thru the old control file, copying all information which
	 * hasn't already been written into the new file.
	 */
	ctl_rewindcf(cjinf);
	lbuff = ctl_getline(cjinf);
	while (lbuff != NULL) {
		switch (lbuff[0]) {
		case 'H':
		case 'P':
		case 'C':
		case 'J':
		case 'L':
		case 'M':
			/* already wrote values for these to the newcf */
			break;
		case 'N':
			/* see comments under 'U'... */
			if (cjinf->cji_dfcount == 0) {
				/* in this case, 'N's will be done in 'U' */
				break;
			}
			fprintf(newcf, "%s\n", lbuff);
			break;
		case 'U':
			/*
			 * check for the very common case where the remote
			 * host had to process 'lpr -s -r', but it did not
			 * remove the Unlink line from the control file.
			 * Such Unlink lines will legitimately have a '/' in
			 * them, but it is the original lpr host which would
			 * have done the unlink of such files, and not any
			 * host receiving that job.
			 */
			slash = strchr(lbuff, '/');
			if (slash != NULL) {
				break;		/* skip this line */
			}
			/*
			 * Okay, another kind of broken lpr implementation
			 * is one which send datafiles, and Unlink's those
			 * datafiles, but never includes any PRINT request
			 * for those files.  Experimentation shows that one
			 * copy of those datafiles should be printed with a
			 * format of 'f'.  If this is an example of such a
			 * screwed-up control file, fix it here.
			 */
			if (cjinf->cji_dfcount == 0) {
				lbuff++;
				if (strncmp(lbuff, "df", (size_t)2) == 0) {
					fprintf(newcf, "f%s\n", lbuff);
					fprintf(newcf, "U%s\n", lbuff);
					fprintf(newcf, "N%s\n", lbuff);
				}
				break;
			}
			fprintf(newcf, "%s\n", lbuff);
			break;
		default:
			fprintf(newcf, "%s\n", lbuff);
			break;
		}
		lbuff = ctl_getline(cjinf);
	}

	ctl_freeinf(cjinf);
	cjinf = NULL;

	res = fclose(newcf);
	newcf = NULL;
	if (res != 0) {
		snprintf(errm, sizeof(errm),
		    "ctl_renametf error fclose(%s): %s", tfname2,
		    strerror(errno));
		goto error_ret;
	}

	strlcpy(cfname2, tfname, sizeof(cfname2));
	cfname2[0] = 'c';		/* rename new file to 'cfA*' */
	res = link(tfname2, cfname2);
	if (res != 0) {
		snprintf(errm, sizeof(errm),
		    "ctl_renametf error link(%s,%s): %s", tfname2, cfname2,
		    strerror(errno));
		goto error_ret;
	}

	/* All the important work is done.  Now just remove temp files */
#ifdef LEAVE_TMPCF_FILES
	{
		struct stat tfstat;
		size_t size1;
		tfstat.st_size = 1;	/* certainly invalid value */
		res = stat(tfname, &tfstat);
		size1 = tfstat.st_size;
		tfstat.st_size = 2;	/* certainly invalid value */
		res = stat(tfname2, &tfstat);
		/*
		 * If the sizes do not match, or either stat call failed,
		 * then do not remove the temp files, but just move them
		 * out of the way.  This is so I can see what this routine
		 * had changed (and the files won't interfere with some
		 * later job coming in from the same host).  In this case,
		 * we don't care if we clobber some previous file.
		 */
		if (size1 != tfstat.st_size) {
			strlcpy(cfname2, tfname, sizeof(cfname2));
			strlcat(cfname2, "._T", sizeof(cfname2));
			rename(tfname, cfname2);
			strlcpy(cfname2, tfname2, sizeof(cfname2));
			strlcat(cfname2, "._T", sizeof(cfname2));
			rename(tfname2, cfname2);
			return NULL;
		}
	}
#endif
	unlink(tfname);
	unlink(tfname2);
    
	return NULL;

error_ret:
	if (cjinf != NULL)
		ctl_freeinf(cjinf);
	if (newcf != NULL)
		fclose(newcf);

	if (*errm != '\0')
		return strdup(errm);
	return strdup("ctl_renametf internal (missed) error");
}

void
ctl_rewindcf(struct cjobinfo *cjinf)
{
	struct cjprivate *cpriv;

	if (cjinf == NULL)
		return;
	cpriv = cjinf->cji_priv;
	if ((cpriv == NULL) || (cpriv != cpriv->pub.cji_priv)) {
		syslog(LOG_ERR, "in ctl_rewindcf(%p): invalid cjinf (cpriv %p)",
		    (void *)cjinf, (void *)cpriv);
		return;
	}
	
	rewind(cpriv->cji_fstream);		/* assume no errors... :-) */
}

char *
ctl_rmjob(const char *ptrname, const char *cfname)
{
	struct cjobinfo	*cjinf;
	char *lbuff;
	char errm[CTI_LINEMAX];

	cjinf = ctl_readcf(ptrname, cfname);
	if (cjinf == NULL) {
		snprintf(errm, sizeof(errm),
		    "ctl_renametf error cti_readcf(%s)", cfname);
		return strdup(errm);
	}

	ctl_rewindcf(cjinf);
	lbuff = ctl_getline(cjinf);
	while (lbuff != NULL) {
		/* obviously we need to fill in the following... */
		switch (lbuff[0]) {
		case 'S':
			break;
		case 'U':
			break;
		default:
			break;
		}
		lbuff = ctl_getline(cjinf);
	}

	ctl_freeinf(cjinf);
	cjinf = NULL;

	return NULL;
}

/*
 * The following routine was originally written to pin down a bug.  It is
 * no longer needed for that problem, but may be useful to keep around for
 * other debugging.
 */
void
ctl_dumpcji(FILE *dbg_stream, const char *heading, struct cjobinfo *cjinf)
{
#define PRINTSTR(xHdr,xStr) \
	astr = xStr; \
	ctl_dbgline++; \
	fprintf(dbg_stream, "%4d] %12s = ", ctl_dbgline, xHdr); \
	if (astr == NULL) \
		fprintf(dbg_stream, "NULL\n"); \
	else \
		fprintf(dbg_stream, "%p -> %s\n", astr, astr)

	struct cjprivate *cpriv;
	char *astr;

	if (cjinf == NULL) {
		fprintf(dbg_stream,
		    "ctl_dumpcji: ptr to cjobinfo for '%s' is NULL\n",
		    heading);
		return;
	}
	cpriv = cjinf->cji_priv;

	fprintf(dbg_stream, "ctl_dumpcji: Dump '%s' of cjobinfo at %p->%p\n",
	    heading, (void *)cjinf, cpriv->cji_buff);

	PRINTSTR("accthost.H", cpriv->pub.cji_accthost);
	PRINTSTR("acctuser.P", cpriv->pub.cji_acctuser);
	PRINTSTR("class.C", cpriv->pub.cji_class);
	PRINTSTR("cf-qname", cpriv->pub.cji_curqueue);
	PRINTSTR("cf-fname", cpriv->pub.cji_fname);
	PRINTSTR("jobname.J", cpriv->pub.cji_jobname);
	PRINTSTR("mailto.M", cpriv->pub.cji_mailto);
	PRINTSTR("headruser.L", cpriv->pub.cji_headruser);

	ctl_dbgline++;
	fprintf(dbg_stream, "%4d] %12s = ", ctl_dbgline, "*cjprivate");
	if (cpriv->pub.cji_priv == NULL)
		fprintf(dbg_stream, "NULL !!\n");
	else
		fprintf(dbg_stream, "%p\n", (void *)cpriv->pub.cji_priv);

	fprintf(dbg_stream, "|- - - - --> Dump '%s' complete\n", heading);

	/* flush output for the benefit of anyone doing a 'tail -f' */
	fflush(dbg_stream);

#undef PRINTSTR
}

/*
 * This routine reads in the next line from the control-file, and removes
 * the trailing newline character.
 *
 * Historical note: Earlier versions of this routine did tab-expansion for
 * ALL lines read in, which did not make any sense for most of the lines
 * in a control file.  For the lines where tab-expansion is useful, it will
 * now have to be done by the calling routine.
 */
static char *
ctl_getline(struct cjobinfo *cjinf)
{
	char *strp, *nl;
	struct cjprivate *cpriv;

	if (cjinf == NULL)
		return NULL;
	cpriv = cjinf->cji_priv;
	if ((cpriv == NULL) || (cpriv != cpriv->pub.cji_priv)) {
		syslog(LOG_ERR, "in ctl_getline(%p): invalid cjinf (cpriv %p)",
		    (void *)cjinf, (void *)cpriv);
		return NULL;
	}

	errno = 0;
	strp = fgets(cpriv->cji_buff, cpriv->cji_buffsize, cpriv->cji_fstream);
	if (strp == NULL) {
		if (errno != 0)
			syslog(LOG_ERR, "%s: ctl_getline error fgets(%s): %s",
			    cpriv->pub.cji_curqueue, cpriv->pub.cji_fname,
			    strerror(errno));
		return NULL;
	}
	nl = strchr(strp, '\n');
	if (nl != NULL)
		*nl = '\0';

#ifdef DEBUGREADCF_FNAME
	/* I'd like to find out if the previous work to expand tabs was ever
	 * really used, and if so, on what lines and for what reason.
	 * Yes, all this work probably means I'm obsessed about this 'tab'
	 * issue, but isn't programming a matter of obsession?
	 */
	{
		int tabcnt;
		char *ch;

		tabcnt = 0;
		ch = strp;
		for (ch = strp; *ch != '\0'; ch++) {
			if (*ch == '\t')
				tabcnt++;
		}

		if (tabcnt && (ctl_dbgfile != NULL)) {
			cpriv->cji_dumpit++;
			fprintf(ctl_dbgfile, "%s: tabs=%d '%s'\n",
			    cpriv->pub.cji_fname, tabcnt, cpriv->cji_buff);
		}
	}
#endif
	return strp;
}
