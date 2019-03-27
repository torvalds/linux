/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1991 Keith Muller.
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
static const char copyright[] =
"@(#) Copyright (c) 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif /* not lint */

#if 0
#ifndef lint
static char sccsid[] = "@(#)pr.c	8.2 (Berkeley) 4/16/94";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <langinfo.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pr.h"
#include "extern.h"

/*
 * pr:	a printing and pagination filter. If multiple input files
 *	are specified, each is read, formatted, and written to standard
 *	output. By default, input is separated into 66-line pages, each
 *	with a header that includes the page number, date, time and the
 *	files pathname.
 *
 *	Complies with posix P1003.2/D11
 */

/*
 * parameter variables
 */
static int	pgnm;		/* starting page number */
static int	clcnt;		/* number of columns */
static int	colwd;		/* column data width - multiple columns */
static int	across;		/* mult col flag; write across page */
static int	dspace;		/* double space flag */
static char	inchar;		/* expand input char */
static int	ingap;		/* expand input gap */
static int	pausefst;	/* Pause before first page */
static int	pauseall;	/* Pause before each page */
static int	formfeed;	/* use formfeed as trailer */
static char	*header;	/* header name instead of file name */
static char	ochar;		/* contract output char */
static int	ogap;		/* contract output gap */
static int	lines;		/* number of lines per page */
static int	merge;		/* merge multiple files in output */
static char	nmchar;		/* line numbering append char */
static int	nmwd;		/* width of line number field */
static int	offst;		/* number of page offset spaces */
static int	nodiag;		/* do not report file open errors */
static char	schar;		/* text column separation character */
static int	sflag;		/* -s option for multiple columns */
static int	nohead;		/* do not write head and trailer */
static int	pgwd;		/* page width with multiple col output */
static char	*timefrmt;	/* time conversion string */

/*
 * misc globals
 */
static FILE	*err;		/* error message file pointer */
static int	addone;		/* page length is odd with double space */
static int	errcnt;		/* error count on file processing */
static char	digs[] = "0123456789"; /* page number translation map */

static char	fnamedefault[] = FNAME;

int
main(int argc, char *argv[])
{
	int ret_val;

	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		(void)signal(SIGINT, terminate);
	ret_val = setup(argc, argv);
	if (!ret_val) {
		/*
		 * select the output format based on options
		 */
		if (merge)
			ret_val = mulfile(argc, argv);
		else if (clcnt == 1)
			ret_val = onecol(argc, argv);
		else if (across)
			ret_val = horzcol(argc, argv);
		else
			ret_val = vertcol(argc, argv);
		free(timefrmt);
	} else
		usage();
	flsh_errs();
	if (errcnt || ret_val)
		exit(1);
	return(0);
}

/*
 * Check if we should pause and write an alert character and wait for a
 * carriage return on /dev/tty.
 */
static void
ttypause(int pagecnt)
{
	int pch;
	FILE *ttyfp;

	if ((pauseall || (pausefst && pagecnt == 1)) &&
	    isatty(STDOUT_FILENO)) {
		if ((ttyfp = fopen("/dev/tty", "r")) != NULL) {
			(void)putc('\a', stderr);
			while ((pch = getc(ttyfp)) != '\n' && pch != EOF)
				;
			(void)fclose(ttyfp);
		}
	}
}

/*
 * onecol:	print files with only one column of output.
 *		Line length is unlimited.
 */
int
onecol(int argc, char *argv[])
{
	int cnt = -1;
	int off;
	int lrgln;
	int linecnt;
	int num;
	int lncnt;
	int pagecnt;
	int ips;
	int ops;
	int cps;
	char *obuf;
	char *lbuf;
	char *nbuf;
	char *hbuf;
	char *ohbuf;
	FILE *inf;
	const char *fname;
	int mor;

	if (nmwd)
		num = nmwd + 1;
	else
		num = 0;
	off = num + offst;

	/*
	 * allocate line buffer
	 */
	if ((obuf = malloc((unsigned)(LBUF + off)*sizeof(char))) == NULL) {
		mfail();
		return(1);
	}
	/*
	 * allocate header buffer
	 */
	if ((hbuf = malloc((unsigned)(HDBUF + offst)*sizeof(char))) == NULL) {
		free(obuf);
		mfail();
		return(1);
	}

	ohbuf = hbuf + offst;
	nbuf = obuf + offst;
	lbuf = nbuf + num;
	if (num)
		nbuf[--num] = nmchar;
	if (offst) {
		(void)memset(obuf, (int)' ', offst);
		(void)memset(hbuf, (int)' ', offst);
	}

	/*
	 * loop by file
	 */
	while ((inf = nxtfile(argc, argv, &fname, ohbuf, 0)) != NULL) {
		if (pgnm) {
			/*
			 * skip to specified page
			 */
			if (inskip(inf, pgnm, lines))
				continue;
			pagecnt = pgnm;
		} else
			pagecnt = 1;
		lncnt = 0;

		/*
		 * loop by page
		 */
		for(;;) {
			linecnt = 0;
			lrgln = 0;
			ops = 0;
			ips = 0;
			cps = 0;

			ttypause(pagecnt);

			/*
			 * loop by line
			 */
			while (linecnt < lines) {
				/*
				 * input next line
				 */
				if ((cnt = inln(inf,lbuf,LBUF,&cps,0,&mor)) < 0)
					break;
				if (!linecnt && !nohead &&
					prhead(hbuf, fname, pagecnt))
					goto err;

				/*
				 * start of new line.
				 */
				if (!lrgln) {
					if (num)
						addnum(nbuf, num, ++lncnt);
					if (otln(obuf,cnt+off, &ips, &ops, mor))
						goto err;
				} else if (otln(lbuf, cnt, &ips, &ops, mor))
					goto err;

				/*
				 * if line bigger than buffer, get more
				 */
				if (mor) {
					lrgln = 1;
					continue;
				}

				/*
				 * whole line rcvd. reset tab proc. state
				 */
				++linecnt;
				lrgln = 0;
				ops = 0;
				ips = 0;
			}

			/*
			 * fill to end of page
			 */
			if (linecnt && prtail(lines-linecnt-lrgln, lrgln))
				goto err;

			/*
			 * On EOF go to next file
			 */
			if (cnt < 0)
				break;
			++pagecnt;
		}
		if (inf != stdin)
			(void)fclose(inf);
	}
	if (eoptind < argc)
		goto err;
	free(hbuf);
	free(obuf);
	return(0);
err:
	free(hbuf);
	free(obuf);
	return(1);
}

/*
 * vertcol:	print files with more than one column of output down a page
 */
int
vertcol(int argc, char *argv[])
{
	char *ptbf;
	char **lstdat = NULL;
	int i;
	int j;
	int cnt = -1;
	int pln;
	int *indy = NULL;
	int cvc;
	int *lindy = NULL;
	int lncnt;
	int stp;
	int pagecnt;
	int col = colwd + 1;
	int mxlen = pgwd + offst + 1;
	int mclcnt = clcnt - 1;
	struct vcol *vc = NULL;
	int mvc;
	int tvc;
	int cw = nmwd + 1;
	int fullcol;
	char *buf = NULL;
	char *hbuf = NULL;
	char *ohbuf;
	const char *fname;
	FILE *inf;
	int ips = 0;
	int cps = 0;
	int ops = 0;
	int mor = 0;
	int retval = 1;

	/*
	 * allocate page buffer
	 */
	if ((buf = malloc((unsigned)lines*mxlen*sizeof(char))) == NULL) {
		mfail();
		return(1);
	}

	/*
	 * allocate page header
	 */
	if ((hbuf = malloc((unsigned)(HDBUF + offst)*sizeof(char))) == NULL) {
		mfail();
		goto out;
	}
	ohbuf = hbuf + offst;
	if (offst)
		(void)memset(hbuf, (int)' ', offst);

	/*
	 * col pointers when no headers
	 */
	mvc = lines * clcnt;
	if ((vc =
	    (struct vcol *)malloc((unsigned)mvc*sizeof(struct vcol))) == NULL) {
		mfail();
		goto out;
	}

	/*
	 * pointer into page where last data per line is located
	 */
	if ((lstdat = (char **)malloc((unsigned)lines*sizeof(char *))) == NULL){
		mfail();
		goto out;
	}

	/*
	 * fast index lookups to locate start of lines
	 */
	if ((indy = (int *)malloc((unsigned)lines*sizeof(int))) == NULL) {
		mfail();
		goto out;
	}
	if ((lindy = (int *)malloc((unsigned)lines*sizeof(int))) == NULL) {
		mfail();
		goto out;
	}

	if (nmwd)
		fullcol = col + cw;
	else
		fullcol = col;

	/*
	 * initialize buffer lookup indexes and offset area
	 */
	for (j = 0; j < lines; ++j) {
		lindy[j] = j * mxlen;
		indy[j] = lindy[j] + offst;
		if (offst) {
			ptbf = buf + lindy[j];
			(void)memset(ptbf, (int)' ', offst);
			ptbf += offst;
		} else
			ptbf = buf + indy[j];
		lstdat[j] = ptbf;
	}

	/*
	 * loop by file
	 */
	while ((inf = nxtfile(argc, argv, &fname, ohbuf, 0)) != NULL) {
		if (pgnm) {
			/*
			 * skip to requested page
			 */
			if (inskip(inf, pgnm, lines))
				continue;
			pagecnt = pgnm;
		} else
			pagecnt = 1;
		lncnt = 0;

		/*
		 * loop by page
		 */
		for(;;) {
			ttypause(pagecnt);

			/*
			 * loop by column
			 */
			cvc = 0;
			for (i = 0; i < clcnt; ++i) {
				j = 0;
				/*
				 * if last column, do not pad
				 */
				if (i == mclcnt)
					stp = 1;
				else
					stp = 0;
				/*
				 * loop by line
				 */
				for(;;) {
					/*
					 * is this first column
					 */
					if (!i) {
						ptbf = buf + indy[j];
						lstdat[j] = ptbf;
					} else
						ptbf = lstdat[j];
					vc[cvc].pt = ptbf;

					/*
					 * add number
					 */
					if (nmwd) {
						addnum(ptbf, nmwd, ++lncnt);
						ptbf += nmwd;
						*ptbf++ = nmchar;
					}

					/*
					 * input next line
					 */
					cnt = inln(inf,ptbf,colwd,&cps,1,&mor);
					vc[cvc++].cnt = cnt;
					if (cnt < 0)
						break;
					ptbf += cnt;

					/*
					 * pad all but last column on page
					 */
					if (!stp) {
						/*
						 * pad to end of column
						 */
						if (sflag)
							*ptbf++ = schar;
						else if ((pln = col-cnt) > 0) {
							(void)memset(ptbf,
								(int)' ',pln);
							ptbf += pln;
						}
					}
					/*
					 * remember last char in line
					 */
					lstdat[j] = ptbf;
					if (++j >= lines)
						break;
				}
				if (cnt < 0)
					break;
			}

			/*
			 * when -t (no header) is specified the spec requires
			 * the min number of lines. The last page may not have
			 * balanced length columns. To fix this we must reorder
			 * the columns. This is a very slow technique so it is
			 * only used under limited conditions. Without -t, the
			 * balancing of text columns is unspecified. To NOT
			 * balance the last page, add the global variable
			 * nohead to the if statement below e.g.
			 *
			 * if ((cnt < 0) && nohead && cvc ......
			 */
			--cvc;

			/*
			 * check to see if last page needs to be reordered
			 */
			if ((cnt < 0) && cvc && ((mvc-cvc) >= clcnt)){
				pln = cvc/clcnt;
				if (cvc % clcnt)
					++pln;

				/*
				 * print header
				 */
				if (!nohead && prhead(hbuf, fname, pagecnt))
					goto out;
				for (i = 0; i < pln; ++i) {
					ips = 0;
					ops = 0;
					if (offst &&
					    otln(buf,offst,&ips,&ops,1))
						goto out;
					tvc = i;

					for (j = 0; j < clcnt; ++j) {
						/*
						 * determine column length
						 */
						if (j == mclcnt) {
							/*
							 * last column
							 */
							cnt = vc[tvc].cnt;
							if (nmwd)
								cnt += cw;
						} else if (sflag) {
							/*
							 * single ch between
							 */
							cnt = vc[tvc].cnt + 1;
							if (nmwd)
								cnt += cw;
						} else
							cnt = fullcol;
						if (otln(vc[tvc].pt, cnt, &ips,
								&ops, 1))
							goto out;
						tvc += pln;
						if (tvc >= cvc)
							break;
					}
					/*
					 * terminate line
					 */
					if (otln(buf, 0, &ips, &ops, 0))
						goto out;
				}
				/*
				 * pad to end of page
				 */
				if (prtail((lines - pln), 0))
					goto out;
				/*
				 * done with output, go to next file
				 */
				break;
			}

			/*
			 * determine how many lines to output
			 */
			if (i > 0)
				pln = lines;
			else
				pln = j;

			/*
			 * print header
			 */
			if (pln && !nohead && prhead(hbuf, fname, pagecnt))
				goto out;

			/*
			 * output each line
			 */
			for (i = 0; i < pln; ++i) {
				ptbf = buf + lindy[i];
				if ((j = lstdat[i] - ptbf) <= offst)
					break;
				if (otln(ptbf, j, &ips, &ops, 0))
					goto out;
			}

			/*
			 * pad to end of page
			 */
			if (pln && prtail((lines - pln), 0))
				goto out;

			/*
			 * if EOF go to next file
			 */
			if (cnt < 0)
				break;
			++pagecnt;
		}
		if (inf != stdin)
			(void)fclose(inf);
	}
	if (eoptind < argc)
		goto out;
	retval = 0;
out:
	free(lindy);
	free(indy);
	free(lstdat);
	free(vc);
	free(hbuf);
	free(buf);
	return(retval);
}

/*
 * horzcol:	print files with more than one column of output across a page
 */
int
horzcol(int argc, char *argv[])
{
	char *ptbf;
	int pln;
	int cnt = -1;
	char *lstdat;
	int col = colwd + 1;
	int j;
	int i;
	int lncnt;
	int pagecnt;
	char *buf;
	char *hbuf;
	char *ohbuf;
	const char *fname;
	FILE *inf;
	int ips = 0;
	int cps = 0;
	int ops = 0;
	int mor = 0;

	if ((buf = malloc((unsigned)(pgwd+offst+1)*sizeof(char))) == NULL) {
		mfail();
		return(1);
	}

	/*
	 * page header
	 */
	if ((hbuf = malloc((unsigned)(HDBUF + offst)*sizeof(char))) == NULL) {
		free(buf);
		mfail();
		return(1);
	}
	ohbuf = hbuf + offst;
	if (offst) {
		(void)memset(buf, (int)' ', offst);
		(void)memset(hbuf, (int)' ', offst);
	}

	/*
	 * loop by file
	 */
	while ((inf = nxtfile(argc, argv, &fname, ohbuf, 0)) != NULL) {
		if (pgnm) {
			if (inskip(inf, pgnm, lines))
				continue;
			pagecnt = pgnm;
		} else
			pagecnt = 1;
		lncnt = 0;

		/*
		 * loop by page
		 */
		for(;;) {
			ttypause(pagecnt);

			/*
			 * loop by line
			 */
			for (i = 0; i < lines; ++i) {
				ptbf = buf + offst;
				lstdat = ptbf;
				j = 0;
				/*
				 * loop by col
				 */
				for(;;) {
					if (nmwd) {
						/*
						 * add number to column
						 */
						addnum(ptbf, nmwd, ++lncnt);
						ptbf += nmwd;
						*ptbf++ = nmchar;
					}
					/*
					 * input line
					 */
					if ((cnt = inln(inf,ptbf,colwd,&cps,1,
							&mor)) < 0)
						break;
					ptbf += cnt;
					lstdat = ptbf;

					/*
					 * if last line skip padding
					 */
					if (++j >= clcnt)
						break;

					/*
					 * pad to end of column
					 */
					if (sflag)
						*ptbf++ = schar;
					else if ((pln = col - cnt) > 0) {
						(void)memset(ptbf,(int)' ',pln);
						ptbf += pln;
					}
				}

				/*
				 * determine line length
				 */
				if ((j = lstdat - buf) <= offst)
					break;
				if (!i && !nohead &&
					prhead(hbuf, fname, pagecnt))
					goto err;
				/*
				 * output line
				 */
				if (otln(buf, j, &ips, &ops, 0))
					goto err;
			}

			/*
			 * pad to end of page
			 */
			if (i && prtail(lines-i, 0))
				goto err;

			/*
			 * if EOF go to next file
			 */
			if (cnt < 0)
				break;
			++pagecnt;
		}
		if (inf != stdin)
			(void)fclose(inf);
	}
	if (eoptind < argc)
		goto err;
	free(hbuf);
	free(buf);
	return(0);
err:
	free(hbuf);
	free(buf);
	return(1);
}

/*
 * mulfile:	print files with more than one column of output and
 *		more than one file concurrently
 */
int
mulfile(int argc, char *argv[])
{
	char *ptbf;
	int j;
	int pln;
	int cnt;
	char *lstdat;
	int i;
	FILE **fbuf = NULL;
	int actf;
	int lncnt;
	int col;
	int pagecnt;
	int fproc;
	char *buf = NULL;
	char *hbuf = NULL;
	char *ohbuf;
	const char *fname;
	int ips = 0;
	int cps = 0;
	int ops = 0;
	int mor = 0;
	int retval = 1;

	/*
	 * array of FILE *, one for each operand
	 */
	if ((fbuf = (FILE **)malloc((unsigned)clcnt*sizeof(FILE *))) == NULL) {
		mfail();
		goto out;
	}

	/*
	 * page header
	 */
	if ((hbuf = malloc((unsigned)(HDBUF + offst)*sizeof(char))) == NULL) {
		mfail();
		goto out;
	}
	ohbuf = hbuf + offst;

	/*
	 * do not know how many columns yet. The number of operands provide an
	 * upper bound on the number of columns. We use the number of files
	 * we can open successfully to set the number of columns. The operation
	 * of the merge operation (-m) in relation to unsuccessful file opens
	 * is unspecified by posix.
	 */
	j = 0;
	while (j < clcnt) {
		if ((fbuf[j] = nxtfile(argc, argv, &fname, ohbuf, 1)) == NULL)
			break;
		if (pgnm && (inskip(fbuf[j], pgnm, lines)))
			fbuf[j] = NULL;
		++j;
	}

	/*
	 * if no files, exit
	 */
	if (!j)
		goto out;

	/*
	 * calculate page boundaries based on open file count
	 */
	clcnt = j;
	if (nmwd) {
		colwd = (pgwd - clcnt - nmwd)/clcnt;
		pgwd = ((colwd + 1) * clcnt) - nmwd - 2;
	} else {
		colwd = (pgwd + 1 - clcnt)/clcnt;
		pgwd = ((colwd + 1) * clcnt) - 1;
	}
	if (colwd < 1) {
		(void)fprintf(err,
		  "pr: page width too small for %d columns\n", clcnt);
		goto out;
	}
	actf = clcnt;
	col = colwd + 1;

	/*
	 * line buffer
	 */
	if ((buf = malloc((unsigned)(pgwd+offst+1)*sizeof(char))) == NULL) {
		mfail();
		goto out;
	}
	if (offst) {
		(void)memset(buf, (int)' ', offst);
		(void)memset(hbuf, (int)' ', offst);
	}
	if (pgnm)
		pagecnt = pgnm;
	else
		pagecnt = 1;
	lncnt = 0;

	/*
	 * continue to loop while any file still has data
	 */
	while (actf > 0) {
		ttypause(pagecnt);

		/*
		 * loop by line
		 */
		for (i = 0; i < lines; ++i) {
			ptbf = buf + offst;
			lstdat = ptbf;
			if (nmwd) {
				/*
				 * add line number to line
				 */
				addnum(ptbf, nmwd, ++lncnt);
				ptbf += nmwd;
				*ptbf++ = nmchar;
			}
			j = 0;
			fproc = 0;

			/*
			 * loop by column
			 */
			for (j = 0; j < clcnt; ++j) {
				if (fbuf[j] == NULL) {
					/*
					 * empty column; EOF
					 */
					cnt = 0;
				} else if ((cnt = inln(fbuf[j], ptbf, colwd,
							&cps, 1, &mor)) < 0) {
					/*
					 * EOF hit; no data
					 */
					if (fbuf[j] != stdin)
						(void)fclose(fbuf[j]);
					fbuf[j] = NULL;
					--actf;
					cnt = 0;
				} else {
					/*
					 * process file data
					 */
					ptbf += cnt;
					lstdat = ptbf;
					fproc++;
				}

				/*
				 * if last ACTIVE column, done with line
				 */
				if (fproc >= actf)
					break;

				/*
				 * pad to end of column
				 */
				if (sflag) {
					*ptbf++ = schar;
				} else if ((pln = col - cnt) > 0) {
					(void)memset(ptbf, (int)' ', pln);
					ptbf += pln;
				}
			}

			/*
			 * calculate data in line
			 */
			if ((j = lstdat - buf) <= offst)
				break;

			if (!i && !nohead && prhead(hbuf, fname, pagecnt))
				goto out;

			/*
			 * output line
			 */
			if (otln(buf, j, &ips, &ops, 0))
				goto out;

			/*
			 * if no more active files, done
			 */
			if (actf <= 0) {
				++i;
				break;
			}
		}

		/*
		 * pad to end of page
		 */
		if (i && prtail(lines-i, 0))
			goto out;
		++pagecnt;
	}
	if (eoptind < argc)
		goto out;
	retval = 0;
out:
	free(buf);
	free(hbuf);
	free(fbuf);
	return(retval);
}

/*
 * inln():	input a line of data (unlimited length lines supported)
 *		Input is optionally expanded to spaces
 *
 *	inf:	file
 *	buf:	buffer
 *	lim:	buffer length
 *	cps:	column position 1st char in buffer (large line support)
 *	trnc:	throw away data more than lim up to \n
 *	mor:	set if more data in line (not truncated)
 */
int
inln(FILE *inf, char *buf, int lim, int *cps, int trnc, int *mor)
{
	int col;
	int gap = ingap;
	int ch = EOF;
	char *ptbuf;
	int chk = (int)inchar;

	ptbuf = buf;

	if (gap) {
		/*
		 * expanding input option
		 */
		while ((--lim >= 0) && ((ch = getc(inf)) != EOF)) {
			/*
			 * is this the input "tab" char
			 */
			if (ch == chk) {
				/*
				 * expand to number of spaces
				 */
				col = (ptbuf - buf) + *cps;
				col = gap - (col % gap);

				/*
				 * if more than this line, push back
				 */
				if ((col > lim) && (ungetc(ch, inf) == EOF))
					return(1);

				/*
				 * expand to spaces
				 */
				while ((--col >= 0) && (--lim >= 0))
					*ptbuf++ = ' ';
				continue;
			}
			if (ch == '\n')
				break;
			*ptbuf++ = ch;
		}
	} else {
		/*
		 * no expansion
		 */
		while ((--lim >= 0) && ((ch = getc(inf)) != EOF)) {
			if (ch == '\n')
				break;
			*ptbuf++ = ch;
		}
	}
	col = ptbuf - buf;
	if (ch == EOF) {
		*mor = 0;
		*cps = 0;
		if (!col)
			return(-1);
		return(col);
	}
	if (ch == '\n') {
		/*
		 * entire line processed
		 */
		*mor = 0;
		*cps = 0;
		return(col);
	}

	/*
	 * line was larger than limit
	 */
	if (trnc) {
		/*
		 * throw away rest of line
		 */
		while ((ch = getc(inf)) != EOF) {
			if (ch == '\n')
				break;
		}
		*cps = 0;
		*mor = 0;
	} else {
		/*
		 * save column offset if not truncated
		 */
		*cps += col;
		*mor = 1;
	}

	return(col);
}

/*
 * otln():	output a line of data. (Supports unlimited length lines)
 *		output is optionally contracted to tabs
 *
 *	buf:	output buffer with data
 *	cnt:	number of chars of valid data in buf
 *	svips:	buffer input column position (for large lines)
 *	svops:	buffer output column position (for large lines)
 *	mor:	output line not complete in this buf; more data to come.
 *		1 is more, 0 is complete, -1 is no \n's
 */
int
otln(char *buf, int cnt, int *svips, int *svops, int mor)
{
	int ops;		/* last col output */
	int ips;		/* last col in buf examined */
	int gap = ogap;
	int tbps;
	char *endbuf;

	if (ogap) {
		/*
		 * contracting on output
		 */
		endbuf = buf + cnt;
		ops = *svops;
		ips = *svips;
		while (buf < endbuf) {
			/*
			 * count number of spaces and ochar in buffer
			 */
			if (*buf == ' ') {
				++ips;
				++buf;
				continue;
			}

			/*
			 * simulate ochar processing
			 */
			if (*buf == ochar) {
				ips += gap - (ips % gap);
				++buf;
				continue;
			}

			/*
			 * got a non space char; contract out spaces
			 */
			while (ips - ops > 1) {
				/*
				 * use as many ochar as will fit
				 */
				if ((tbps = ops + gap - (ops % gap)) > ips)
					break;
				if (putchar(ochar) == EOF) {
					pfail();
					return(1);
				}
				ops = tbps;
			}

			while (ops < ips) {
				/*
				 * finish off with spaces
				 */
				if (putchar(' ') == EOF) {
					pfail();
					return(1);
				}
				++ops;
			}

			/*
			 * output non space char
			 */
			if (putchar(*buf++) == EOF) {
				pfail();
				return(1);
			}
			++ips;
			++ops;
		}

		if (mor > 0) {
			/*
			 * if incomplete line, save position counts
			 */
			*svops = ops;
			*svips = ips;
			return(0);
		}

		if (mor < 0) {
			while (ips - ops > 1) {
				/*
				 * use as many ochar as will fit
				 */
				if ((tbps = ops + gap - (ops % gap)) > ips)
					break;
				if (putchar(ochar) == EOF) {
					pfail();
					return(1);
				}
				ops = tbps;
			}
			while (ops < ips) {
				/*
				 * finish off with spaces
				 */
				if (putchar(' ') == EOF) {
					pfail();
					return(1);
				}
				++ops;
			}
			return(0);
		}
	} else {
		/*
		 * output is not contracted
		 */
		if (cnt && (fwrite(buf, sizeof(char), cnt, stdout) <= 0)) {
			pfail();
			return(1);
		}
		if (mor != 0)
			return(0);
	}

	/*
	 * process line end and double space as required
	 */
	if ((putchar('\n') == EOF) || (dspace && (putchar('\n') == EOF))) {
		pfail();
		return(1);
	}
	return(0);
}

/*
 * inskip():	skip over pgcnt pages with lncnt lines per page
 *		file is closed at EOF (if not stdin).
 *
 *	inf	FILE * to read from
 *	pgcnt	number of pages to skip
 *	lncnt	number of lines per page
 */
int
inskip(FILE *inf, int pgcnt, int lncnt)
{
	int c;
	int cnt;

	while(--pgcnt > 0) {
		cnt = lncnt;
		while ((c = getc(inf)) != EOF) {
			if ((c == '\n') && (--cnt == 0))
				break;
		}
		if (c == EOF) {
			if (inf != stdin)
				(void)fclose(inf);
			return(1);
		}
	}
	return(0);
}

/*
 * nxtfile:	returns a FILE * to next file in arg list and sets the
 *		time field for this file (or current date).
 *
 *	buf	array to store proper date for the header.
 *	dt	if set skips the date processing (used with -m)
 */
FILE *
nxtfile(int argc, char **argv, const char **fname, char *buf, int dt)
{
	FILE *inf = NULL;
	time_t tv_sec;
	struct tm *timeptr = NULL;
	struct stat statbuf;
	static int twice = -1;

	++twice;
	if (eoptind >= argc) {
		/*
		 * no file listed; default, use standard input
		 */
		if (twice)
			return(NULL);
		clearerr(stdin);
		inf = stdin;
		if (header != NULL)
			*fname = header;
		else
			*fname = fnamedefault;
		if (nohead)
			return(inf);
		if ((tv_sec = time(NULL)) == -1) {
			++errcnt;
			(void)fprintf(err, "pr: cannot get time of day, %s\n",
				strerror(errno));
			eoptind = argc - 1;
			return(NULL);
		}
		timeptr = localtime(&tv_sec);
	}
	for (; eoptind < argc; ++eoptind) {
		if (strcmp(argv[eoptind], "-") == 0) {
			/*
			 * process a "-" for filename
			 */
			clearerr(stdin);
			inf = stdin;
			if (header != NULL)
				*fname = header;
			else
				*fname = fnamedefault;
			++eoptind;
			if (nohead || (dt && twice))
				return(inf);
			if ((tv_sec = time(NULL)) == -1) {
				++errcnt;
				(void)fprintf(err,
					"pr: cannot get time of day, %s\n",
					strerror(errno));
				return(NULL);
			}
			timeptr = localtime(&tv_sec);
		} else {
			/*
			 * normal file processing
			 */
			if ((inf = fopen(argv[eoptind], "r")) == NULL) {
				++errcnt;
				if (nodiag)
					continue;
				(void)fprintf(err, "pr: cannot open %s, %s\n",
					argv[eoptind], strerror(errno));
				continue;
			}
			if (header != NULL)
				*fname = header;
			else if (dt)
				*fname = fnamedefault;
			else
				*fname = argv[eoptind];
			++eoptind;
			if (nohead || (dt && twice))
				return(inf);

			if (dt) {
				if ((tv_sec = time(NULL)) == -1) {
					++errcnt;
					(void)fprintf(err,
					     "pr: cannot get time of day, %s\n",
					     strerror(errno));
					fclose(inf);
					return(NULL);
				}
				timeptr = localtime(&tv_sec);
			} else {
				if (fstat(fileno(inf), &statbuf) < 0) {
					++errcnt;
					(void)fclose(inf);
					(void)fprintf(err,
						"pr: cannot stat %s, %s\n",
						argv[eoptind], strerror(errno));
					return(NULL);
				}
				timeptr = localtime(&(statbuf.st_mtime));
			}
		}
		break;
	}
	if (inf == NULL)
		return(NULL);

	/*
	 * set up time field used in header
	 */
	if (strftime(buf, HDBUF, timefrmt, timeptr) <= 0) {
		++errcnt;
		if (inf != stdin)
			(void)fclose(inf);
		(void)fputs("pr: time conversion failed\n", err);
		return(NULL);
	}
	return(inf);
}

/*
 * addnum():	adds the line number to the column
 *		Truncates from the front or pads with spaces as required.
 *		Numbers are right justified.
 *
 *	buf	buffer to store the number
 *	wdth	width of buffer to fill
 *	line	line number
 *
 *		NOTE: numbers occupy part of the column. The posix
 *		spec does not specify if -i processing should or should not
 *		occur on number padding. The spec does say it occupies
 *		part of the column. The usage of addnum	currently treats
 *		numbers as part of the column so spaces may be replaced.
 */
void
addnum(char *buf, int wdth, int line)
{
	char *pt = buf + wdth;

	do {
		*--pt = digs[line % 10];
		line /= 10;
	} while (line && (pt > buf));

	/*
	 * pad with space as required
	 */
	while (pt > buf)
		*--pt = ' ';
}

/*
 * prhead():	prints the top of page header
 *
 *	buf	buffer with time field (and offset)
 *	cnt	number of chars in buf
 *	fname	fname field for header
 *	pagcnt	page number
 */
int
prhead(char *buf, const char *fname, int pagcnt)
{
	int ips = 0;
	int ops = 0;

	if ((putchar('\n') == EOF) || (putchar('\n') == EOF)) {
		pfail();
		return(1);
	}
	/*
	 * posix is not clear if the header is subject to line length
	 * restrictions. The specification for header line format
	 * in the spec clearly does not limit length. No pr currently
	 * restricts header length. However if we need to truncate in
	 * a reasonable way, adjust the length of the printf by
	 * changing HDFMT to allow a length max as an argument to printf.
	 * buf (which contains the offset spaces and time field could
	 * also be trimmed
	 *
	 * note only the offset (if any) is processed for tab expansion
	 */
	if (offst && otln(buf, offst, &ips, &ops, -1))
		return(1);
	(void)printf(HDFMT,buf+offst, fname, pagcnt);
	return(0);
}

/*
 * prtail():	pad page with empty lines (if required) and print page trailer
 *		if requested
 *
 *	cnt	number of lines of padding needed
 *	incomp	was a '\n' missing from last line output
 */
int
prtail(int cnt, int incomp)
{
	if (nohead) {
		/*
		 * only pad with no headers when incomplete last line
		 */
		if (incomp &&
		    ((dspace && (putchar('\n') == EOF)) ||
		     (putchar('\n') == EOF))) {
			pfail();
			return(1);
		}
		/*
		 * but honor the formfeed request
		 */
		if (formfeed) {
			if (putchar('\f') == EOF) {
				pfail();
				return(1);
			}
		}
		return(0);
	}
	/*
	 * if double space output two \n
	 */
	if (dspace)
		cnt *= 2;

	/*
	 * if an odd number of lines per page, add an extra \n
	 */
	if (addone)
		++cnt;

	/*
	 * pad page
	 */
	if (formfeed) {
		if ((incomp && (putchar('\n') == EOF)) ||
		    (putchar('\f') == EOF)) {
			pfail();
			return(1);
		}
		return(0);
	}
	cnt += TAILLEN;
	while (--cnt >= 0) {
		if (putchar('\n') == EOF) {
			pfail();
			return(1);
		}
	}
	return(0);
}

/*
 * terminate():	when a SIGINT is recvd
 */
void
terminate(int which_sig __unused)
{
	flsh_errs();
	exit(1);
}


/*
 * flsh_errs():	output saved up diagnostic messages after all normal
 *		processing has completed
 */
void
flsh_errs(void)
{
	char buf[BUFSIZ];

	(void)fflush(stdout);
	(void)fflush(err);
	if (err == stderr)
		return;
	rewind(err);
	while (fgets(buf, BUFSIZ, err) != NULL)
		(void)fputs(buf, stderr);
}

void
mfail(void)
{
	(void)fputs("pr: memory allocation failed\n", err);
}

void
pfail(void)
{
	(void)fprintf(err, "pr: write failure, %s\n", strerror(errno));
}

void
usage(void)
{
	(void)fputs(
	 "usage: pr [+page] [-col] [-adFfmprt] [-e[ch][gap]] [-h header]\n",
	 err);
	(void)fputs(
	 "          [-i[ch][gap]] [-l line] [-n[ch][width]] [-o offset]\n",err);
	(void)fputs(
	 "          [-L locale] [-s[ch]] [-w width] [-] [file ...]\n", err);
}

/*
 * setup:	Validate command args, initialize and perform sanity
 *		checks on options
 */
int
setup(int argc, char *argv[])
{
	int c;
	int d_first;
	int eflag = 0;
	int iflag = 0;
	int wflag = 0;
	int cflag = 0;
	char *Lflag = NULL;

	if (isatty(fileno(stdout))) {
		/*
		 * defer diagnostics until processing is done
		 */
		if ((err = tmpfile()) == NULL) {
		       err = stderr;
		       (void)fputs("Cannot defer diagnostic messages\n",stderr);
		       return(1);
		}
	} else
		err = stderr;
	while ((c = egetopt(argc, argv, "#adFfmrte?h:i?L:l:n?o:ps?w:")) != -1) {
		switch (c) {
		case '+':
			if ((pgnm = atoi(eoptarg)) < 1) {
			    (void)fputs("pr: +page number must be 1 or more\n",
				err);
			    return(1);
			}
			break;
		case '-':
			if ((clcnt = atoi(eoptarg)) < 1) {
			    (void)fputs("pr: -columns must be 1 or more\n",err);
			    return(1);
			}
			if (clcnt > 1)
				++cflag;
			break;
		case 'a':
			++across;
			break;
		case 'd':
			++dspace;
			break;
		case 'e':
			++eflag;
			if ((eoptarg != NULL) && !isdigit((unsigned char)*eoptarg))
				inchar = *eoptarg++;
			else
				inchar = INCHAR;
			if ((eoptarg != NULL) && isdigit((unsigned char)*eoptarg)) {
				if ((ingap = atoi(eoptarg)) < 0) {
					(void)fputs(
					"pr: -e gap must be 0 or more\n", err);
					return(1);
				}
				if (ingap == 0)
					ingap = INGAP;
			} else if ((eoptarg != NULL) && (*eoptarg != '\0')) {
				(void)fprintf(err,
				      "pr: invalid value for -e %s\n", eoptarg);
				return(1);
			} else
				ingap = INGAP;
			break;
		case 'f':
			++pausefst;
			/*FALLTHROUGH*/
		case 'F':
			++formfeed;
			break;
		case 'h':
			header = eoptarg;
			break;
		case 'i':
			++iflag;
			if ((eoptarg != NULL) && !isdigit((unsigned char)*eoptarg))
				ochar = *eoptarg++;
			else
				ochar = OCHAR;
			if ((eoptarg != NULL) && isdigit((unsigned char)*eoptarg)) {
				if ((ogap = atoi(eoptarg)) < 0) {
					(void)fputs(
					"pr: -i gap must be 0 or more\n", err);
					return(1);
				}
				if (ogap == 0)
					ogap = OGAP;
			} else if ((eoptarg != NULL) && (*eoptarg != '\0')) {
				(void)fprintf(err,
				      "pr: invalid value for -i %s\n", eoptarg);
				return(1);
			} else
				ogap = OGAP;
			break;
		case 'L':
			Lflag = eoptarg;
			break;
		case 'l':
			if (!isdigit((unsigned char)*eoptarg) || ((lines=atoi(eoptarg)) < 1)) {
				(void)fputs(
				 "pr: number of lines must be 1 or more\n",err);
				return(1);
			}
			break;
		case 'm':
			++merge;
			break;
		case 'n':
			if ((eoptarg != NULL) && !isdigit((unsigned char)*eoptarg))
				nmchar = *eoptarg++;
			else
				nmchar = NMCHAR;
			if ((eoptarg != NULL) && isdigit((unsigned char)*eoptarg)) {
				if ((nmwd = atoi(eoptarg)) < 1) {
					(void)fputs(
					"pr: -n width must be 1 or more\n",err);
					return(1);
				}
			} else if ((eoptarg != NULL) && (*eoptarg != '\0')) {
				(void)fprintf(err,
				      "pr: invalid value for -n %s\n", eoptarg);
				return(1);
			} else
				nmwd = NMWD;
			break;
		case 'o':
			if (!isdigit((unsigned char)*eoptarg) || ((offst = atoi(eoptarg))< 1)){
				(void)fputs("pr: -o offset must be 1 or more\n",
					err);
				return(1);
			}
			break;
		case 'p':
			++pauseall;
			break;
		case 'r':
			++nodiag;
			break;
		case 's':
			++sflag;
			if (eoptarg == NULL)
				schar = SCHAR;
			else {
				schar = *eoptarg++;
				if (*eoptarg != '\0') {
					(void)fprintf(err,
					    "pr: invalid value for -s %s\n",
					    eoptarg);
					return(1);
				}
			}
			break;
		case 't':
			++nohead;
			break;
		case 'w':
			++wflag;
			if ((eoptarg == NULL ) ||
			    !isdigit((unsigned char)*eoptarg) ||
			    ((pgwd = atoi(eoptarg)) < 1)){
				(void)fputs(
				   "pr: -w width must be 1 or more \n",err);
				return(1);
			}
			break;
		case '?':
		default:
			return(1);
		}
	}

	/*
	 * default and sanity checks
	 */
	if (!clcnt) {
		if (merge) {
			if ((clcnt = argc - eoptind) <= 1) {
				clcnt = CLCNT;
				merge = 0;
			}
		} else
			clcnt = CLCNT;
	}
	if (across) {
		if (clcnt == 1) {
			(void)fputs("pr: -a flag requires multiple columns\n",
				err);
			return(1);
		}
		if (merge) {
			(void)fputs("pr: -m cannot be used with -a\n", err);
			return(1);
		}
	}
	if (!wflag) {
		if (sflag)
			pgwd = SPGWD;
		else
			pgwd = PGWD;
	}
	if (cflag || merge) {
		if (!eflag) {
			inchar = INCHAR;
			ingap = INGAP;
		}
		if (!iflag) {
			ochar = OCHAR;
			ogap = OGAP;
		}
	}
	if (cflag) {
		if (merge) {
			(void)fputs(
			  "pr: -m cannot be used with multiple columns\n", err);
			return(1);
		}
		if (nmwd) {
			colwd = (pgwd + 1 - (clcnt * (nmwd + 2)))/clcnt;
			pgwd = ((colwd + nmwd + 2) * clcnt) - 1;
		} else {
			colwd = (pgwd + 1 - clcnt)/clcnt;
			pgwd = ((colwd + 1) * clcnt) - 1;
		}
		if (colwd < 1) {
			(void)fprintf(err,
			  "pr: page width is too small for %d columns\n",clcnt);
			return(1);
		}
	}
	if (!lines)
		lines = LINES;

	/*
	 * make sure long enough for headers. if not disable
	 */
	if (lines <= HEADLEN + TAILLEN)
		++nohead;
	else if (!nohead)
		lines -= HEADLEN + TAILLEN;

	/*
	 * adjust for double space on odd length pages
	 */
	if (dspace) {
		if (lines == 1)
			dspace = 0;
		else {
			if (lines & 1)
				++addone;
			lines /= 2;
		}
	}

	(void) setlocale(LC_TIME, (Lflag != NULL) ? Lflag : "");

	d_first = (*nl_langinfo(D_MD_ORDER) == 'd');
	timefrmt = strdup(d_first ? TIMEFMTD : TIMEFMTM);

	return(0);
}
