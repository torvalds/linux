#define Extern extern
#include <sys/types.h>
#include <signal.h>
#define _NSIG NSIG
#include <errno.h>
#include <setjmp.h>
#include "sh.h"

/* -------- io.c -------- */
/* #include "sh.h" */

/*
 * shell IO
 */

static struct iobuf sharedbuf = {AFID_NOBUF};
static struct iobuf mainbuf = {AFID_NOBUF};
static unsigned bufid = AFID_ID;	/* buffer id counter */

struct ioarg temparg = {0, 0, 0, AFID_NOBUF, 0};

_PROTOTYPE(static void readhere, (char **name, char *s, int ec ));
_PROTOTYPE(void pushio, (struct ioarg *argp, int (*fn)()));
_PROTOTYPE(static int xxchar, (struct ioarg *ap ));
_PROTOTYPE(void tempname, (char *tname ));

int
getc(ec)
register int ec;
{
	register int c;

	if(e.linep > elinep) {
		while((c=readc()) != '\n' && c)
			;
		err("input line too long");
		gflg++;
		return(c);
	}
	c = readc();
 	if (ec != '\'' && e.iop->task != XGRAVE) {
		if(c == '\\') {
			c = readc();
			if (c == '\n' && ec != '\"')
				return(getc(ec));
			c |= QUOTE;
		}
	}
	return(c);
}

void
unget(c)
int c;
{
	if (e.iop >= e.iobase)
		e.iop->peekc = c;
}

int
eofc()

{
  return e.iop < e.iobase || (e.iop->peekc == 0 && e.iop->prev == 0);
}

int
readc()
{
	register c;

	for (; e.iop >= e.iobase; e.iop--)
		if ((c = e.iop->peekc) != '\0') {
			e.iop->peekc = 0;
			return(c);
		}
		else {
		    if (e.iop->prev != 0) {
		        if ((c = (*e.iop->iofn)(e.iop->argp, e.iop)) != '\0') {
			        if (c == -1) {
				        e.iop++;
				        continue;
			        }
			        if (e.iop == iostack)
				        ioecho(c);
			        return(e.iop->prev = c);
		        }
		        else if (e.iop->task == XIO && e.iop->prev != '\n') {
			        e.iop->prev = 0;
				if (e.iop == iostack)
					ioecho('\n');
			        return '\n';
		        }
		    }
		    if (e.iop->task == XIO) {
			if (multiline)
			    return e.iop->prev = 0;
			if (talking && e.iop == iostack+1)
			    prs(prompt->value);
		    }
		}
	if (e.iop >= iostack)
		return(0);
	leave();
	/* NOTREACHED */
}

void
ioecho(c)
char c;
{
	if (flag['v'])
		write(2, &c, sizeof c);
}

void
pushio(argp, fn)
struct ioarg *argp;
int (*fn)();
{
	if (++e.iop >= &iostack[NPUSH]) {
		e.iop--;
		err("Shell input nested too deeply");
		gflg++;
		return;
	}
	e.iop->iofn = fn;

	if (argp->afid != AFID_NOBUF)
	  e.iop->argp = argp;
	else {
	  e.iop->argp  = ioargstack + (e.iop - iostack);
	  *e.iop->argp = *argp;
	  e.iop->argp->afbuf = e.iop == &iostack[0] ? &mainbuf : &sharedbuf;
	  if (isatty(e.iop->argp->afile) == 0 &&
	      (e.iop == &iostack[0] ||
	       lseek(e.iop->argp->afile, 0L, 1) != -1)) {
	    if (++bufid == AFID_NOBUF)
	      bufid = AFID_ID;
	    e.iop->argp->afid  = bufid;
	  }
	}

	e.iop->prev  = ~'\n';
	e.iop->peekc = 0;
	e.iop->xchar = 0;
	e.iop->nlcount = 0;
	if (fn == filechar || fn == linechar)
		e.iop->task = XIO;
	else if (fn == gravechar || fn == qgravechar)
		e.iop->task = XGRAVE;
	else
		e.iop->task = XOTHER;
}

struct io *
setbase(ip)
struct io *ip;
{
	register struct io *xp;

	xp = e.iobase;
	e.iobase = ip;
	return(xp);
}

/*
 * Input generating functions
 */

/*
 * Produce the characters of a string, then a newline, then EOF.
 */
int
nlchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL)
		return(0);
	if ((c = *ap->aword++) == 0) {
		ap->aword = NULL;
		return('\n');
	}
	return(c);
}

/*
 * Given a list of words, produce the characters
 * in them, with a space after each word.
 */
int
wdchar(ap)
register struct ioarg *ap;
{
	register char c;
	register char **wl;

	if ((wl = ap->awordlist) == NULL)
		return(0);
	if (*wl != NULL) {
		if ((c = *(*wl)++) != 0)
			return(c & 0177);
		ap->awordlist++;
		return(' ');
	}
	ap->awordlist = NULL;
	return('\n');
}

/*
 * Return the characters of a list of words,
 * producing a space between them.
 */
int
dolchar(ap)
register struct ioarg *ap;
{
	register char *wp;

	if ((wp = *ap->awordlist++) != NULL) {
		PUSHIO(aword, wp, *ap->awordlist == NULL? strchar: xxchar);
		return(-1);
	}
	return(0);
}

static int
xxchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL)
		return(0);
	if ((c = *ap->aword++) == '\0') {
		ap->aword = NULL;
		return(' ');
	}
	return(c);
}

/*
 * Produce the characters from a single word (string).
 */
int
strchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL || (c = *ap->aword++) == 0)
		return(0);
	return(c);
}

/*
 * Produce quoted characters from a single word (string).
 */
int
qstrchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL || (c = *ap->aword++) == 0)
		return(0);
	return(c|QUOTE);
}

/*
 * Return the characters from a file.
 */
int
filechar(ap)
register struct ioarg *ap;
{
	register int i;
	char c;
	struct iobuf *bp = ap->afbuf;

	if (ap->afid != AFID_NOBUF) {
	  if ((i = ap->afid != bp->id) || bp->bufp == bp->ebufp) {
	    if (i)
	      lseek(ap->afile, ap->afpos, 0);
	    do {
	      i = read(ap->afile, bp->buf, sizeof(bp->buf));
	    } while (i < 0 && errno == EINTR);
	    if (i <= 0) {
	      closef(ap->afile);
	      return 0;
	    }
	    bp->id = ap->afid;
	    bp->ebufp = (bp->bufp  = bp->buf) + i;
	  }
	  ap->afpos++;
	  return *bp->bufp++ & 0177;
	}

	do {
		i = read(ap->afile, &c, sizeof(c));
	} while (i < 0 && errno == EINTR);
	return(i == sizeof(c)? c&0177: (closef(ap->afile), 0));
}

/*
 * Return the characters from a here temp file.
 */
int
herechar(ap)
register struct ioarg *ap;
{
	char c;


	if (read(ap->afile, &c, sizeof(c)) != sizeof(c)) {
		close(ap->afile);
		c = 0;
	}
	return (c);

}

/*
 * Return the characters produced by a process (`...`).
 * Quote them if required, and remove any trailing newline characters.
 */
int
gravechar(ap, iop)
struct ioarg *ap;
struct io *iop;
{
	register int c;

	if ((c = qgravechar(ap, iop)&~QUOTE) == '\n')
		c = ' ';
	return(c);
}

int
qgravechar(ap, iop)
register struct ioarg *ap;
struct io *iop;
{
	register int c;

	if (iop->xchar) {
		if (iop->nlcount) {
			iop->nlcount--;
			return('\n'|QUOTE);
		}
		c = iop->xchar;
		iop->xchar = 0;
	} else if ((c = filechar(ap)) == '\n') {
		iop->nlcount = 1;
		while ((c = filechar(ap)) == '\n')
			iop->nlcount++;
		iop->xchar = c;
		if (c == 0)
			return(c);
		iop->nlcount--;
		c = '\n';
	}
	return(c!=0? c|QUOTE: 0);
}

/*
 * Return a single command (usually the first line) from a file.
 */
int
linechar(ap)
register struct ioarg *ap;
{
	register int c;

	if ((c = filechar(ap)) == '\n') {
		if (!multiline) {
			closef(ap->afile);
			ap->afile = -1;	/* illegal value */
		}
	}
	return(c);
}

void
prs(s)
register char *s;
{
	if (*s)
		write(2, s, strlen(s));
}

void
putc(c)
char c;
{
	write(2, &c, sizeof c);
}

void
prn(u)
unsigned u;
{
	prs(itoa(u, 0));
}

void
closef(i)
register int i;
{
	if (i > 2)
		close(i);
}

void
closeall()
{
	register u;

	for (u=NUFILE; u<NOFILE;)
		close(u++);
}

/*
 * remap fd into Shell's fd space
 */
int
remap(fd)
register int fd;
{
	register int i;
	int map[NOFILE];

	if (fd < e.iofd) {
		for (i=0; i<NOFILE; i++)
			map[i] = 0;
		do {
			map[fd] = 1;
			fd = dup(fd);
		} while (fd >= 0 && fd < e.iofd);
		for (i=0; i<NOFILE; i++)
			if (map[i])
				close(i);
		if (fd < 0)
			err("too many files open in shell");
	}
	return(fd);
}

int
openpipe(pv)
register int *pv;
{
	register int i;

	if ((i = pipe(pv)) < 0)
		err("can't create pipe - try again");
	return(i);
}

void
closepipe(pv)
register int *pv;
{
	if (pv != NULL) {
		close(*pv++);
		close(*pv);
	}
}

/* -------- here.c -------- */
/* #include "sh.h" */

/*
 * here documents
 */

struct	here {
	char	*h_tag;
	int	h_dosub;
	struct	ioword *h_iop;
	struct	here	*h_next;
};

static	struct here *inhere;		/* list of hear docs while parsing */
static	struct here *acthere;		/* list of active here documents */

void
markhere(s, iop)
register char *s;
struct ioword *iop;
{
	register struct here *h, *lh;

	h = (struct here *) space(sizeof(struct here));
	if (h == 0)
		return;
	h->h_tag = evalstr(s, DOSUB);
	if (h->h_tag == 0)
		return;
	h->h_iop = iop;
	iop->io_name = 0;
	h->h_next = NULL;
	if (inhere == 0)
		inhere = h;
	else
		for (lh = inhere; lh!=NULL; lh = lh->h_next)
			if (lh->h_next == 0) {
				lh->h_next = h;
				break;
			}
	iop->io_flag |= IOHERE|IOXHERE;
	for (s = h->h_tag; *s; s++)
		if (*s & QUOTE) {
			iop->io_flag &= ~ IOXHERE;
			*s &= ~ QUOTE;
		}
	h->h_dosub = iop->io_flag & IOXHERE;
}

void
gethere()
{
	register struct here *h, *hp;

	/* Scan here files first leaving inhere list in place */
	for (hp = h = inhere; h != NULL; hp = h, h = h->h_next)
	  readhere(&h->h_iop->io_name, h->h_tag, h->h_dosub? 0: '\'');

	/* Make inhere list active - keep list intact for scraphere */
	if (hp != NULL) {
	  hp->h_next = acthere;
	  acthere    = inhere;
	  inhere     = NULL;
	}
}

static void
readhere(name, s, ec)
char **name;
register char *s;
int ec;
{
	int tf;
	char tname[30];
	register c;
	jmp_buf ev;
	char line [LINELIM+1];
	char *next;

	tempname(tname);
	*name = strsave(tname, areanum);
	tf = creat(tname, 0600);
	if (tf < 0)
		return;
	if (newenv(setjmp(errpt = ev)) != 0)
		unlink(tname);
	else {
		pushio(e.iop->argp, e.iop->iofn);
		e.iobase = e.iop;
		for (;;) {
			if (talking && e.iop <= iostack)
				prs(cprompt->value);
			next = line;
			while ((c = getc(ec)) != '\n' && c) {
				if (ec == '\'')
					c &= ~ QUOTE;
				if (next >= &line[LINELIM]) {
					c = 0;
					break;
				}
				*next++ = c;
			}
			*next = 0;
			if (strcmp(s, line) == 0 || c == 0)
				break;
			*next++ = '\n';
			write (tf, line, (int)(next-line));
		}
		if (c == 0) {
			prs("here document `"); prs(s); err("' unclosed");
		}
		quitenv();
	}
	close(tf);
}

/*
 * open here temp file.
 * if unquoted here, expand here temp file into second temp file.
 */
int
herein(hname, xdoll)
char *hname;
int xdoll;
{
	register hf, tf;

	if (hname == 0)
		return(-1);
	hf = open(hname, 0);
	if (hf < 0)
		return (-1);
	if (xdoll) {
		char c;
		char tname[30];
		jmp_buf ev;

		tempname(tname);
		if ((tf = creat(tname, 0600)) < 0)
			return (-1);
		if (newenv(setjmp(errpt = ev)) == 0) {
			PUSHIO(afile, hf, herechar);
			setbase(e.iop);
			while ((c = subgetc(0, 0)) != 0) {
				c &= ~ QUOTE;
				write(tf, &c, sizeof c);
			}
			quitenv();
		} else
			unlink(tname);
		close(tf);
		tf = open(tname, 0);
		unlink(tname);
		return (tf);
	} else
		return (hf);
}

void
scraphere()
{
	register struct here *h;

	for (h = inhere; h != NULL; h = h->h_next) {
		if (h->h_iop && h->h_iop->io_name)
		  unlink(h->h_iop->io_name);
	}
	inhere = NULL;
}

/* unlink here temp files before a freearea(area) */
void
freehere(area)
int area;
{
	register struct here *h, *hl;

	hl = NULL;
	for (h = acthere; h != NULL; h = h->h_next)
		if (getarea((char *) h) >= area) {
			if (h->h_iop->io_name != NULL)
				unlink(h->h_iop->io_name);
			if (hl == NULL)
				acthere = h->h_next;
			else
				hl->h_next = h->h_next;
		} else
			hl = h;
}

void
tempname(tname)
char *tname;
{
	static int inc;
	register char *cp, *lp;

	for (cp = tname, lp = "/tmp/shtm"; (*cp = *lp++) != '\0'; cp++)
		;
	lp = putn(getpid()*1000 + inc++);
	for (; (*cp = *lp++) != '\0'; cp++)
		;
}
