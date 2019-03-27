#define Extern extern
#include <sys/types.h>
#include <signal.h>
#define _NSIG NSIG
#include <errno.h>
#include <setjmp.h>
#include "sh.h"

/* -------- csyn.c -------- */
/*
 * shell: syntax (C version)
 */

typedef union {
	char	*cp;
	char	**wp;
	int	i;
	struct	op *o;
} YYSTYPE;
#define	WORD	256
#define	LOGAND	257
#define	LOGOR	258
#define	BREAK	259
#define	IF	260
#define	THEN	261
#define	ELSE	262
#define	ELIF	263
#define	FI	264
#define	CASE	265
#define	ESAC	266
#define	FOR	267
#define	WHILE	268
#define	UNTIL	269
#define	DO	270
#define	DONE	271
#define	IN	272
#define	YYERRCODE 300

/* flags to yylex */
#define	CONTIN	01	/* skip new lines to complete command */

/* #include "sh.h" */
#define	SYNTAXERR	zzerr()
static	int	startl;
static	int	peeksym;
static	int	nlseen;
static	int	iounit = IODEFAULT;

static	YYSTYPE	yylval;

_PROTOTYPE(static struct op *pipeline, (int cf ));
_PROTOTYPE(static struct op *andor, (void));
_PROTOTYPE(static struct op *c_list, (void));
_PROTOTYPE(static int synio, (int cf ));
_PROTOTYPE(static void musthave, (int c, int cf ));
_PROTOTYPE(static struct op *simple, (void));
_PROTOTYPE(static struct op *nested, (int type, int mark ));
_PROTOTYPE(static struct op *command, (int cf ));
_PROTOTYPE(static struct op *dogroup, (int onlydone ));
_PROTOTYPE(static struct op *thenpart, (void));
_PROTOTYPE(static struct op *elsepart, (void));
_PROTOTYPE(static struct op *caselist, (void));
_PROTOTYPE(static struct op *casepart, (void));
_PROTOTYPE(static char **pattern, (void));
_PROTOTYPE(static char **wordlist, (void));
_PROTOTYPE(static struct op *list, (struct op *t1, struct op *t2 ));
_PROTOTYPE(static struct op *block, (int type, struct op *t1, struct op *t2, char **wp ));
_PROTOTYPE(static struct op *newtp, (void));
_PROTOTYPE(static struct op *namelist, (struct op *t ));
_PROTOTYPE(static char **copyw, (void));
_PROTOTYPE(static void word, (char *cp ));
_PROTOTYPE(static struct ioword **copyio, (void));
_PROTOTYPE(static struct ioword *io, (int u, int f, char *cp ));
_PROTOTYPE(static void zzerr, (void));
_PROTOTYPE(void yyerror, (char *s ));
_PROTOTYPE(static int yylex, (int cf ));
_PROTOTYPE(int collect, (int c, int c1 ));
_PROTOTYPE(int dual, (int c ));
_PROTOTYPE(static void diag, (int ec ));
_PROTOTYPE(static char *tree, (unsigned size ));
_PROTOTYPE(void printf, (char *s ));

int
yyparse()
{
	startl  = 1;
	peeksym = 0;
	yynerrs = 0;
	outtree = c_list();
	musthave('\n', 0);
	return(yynerrs!=0);
}

static struct op *
pipeline(cf)
int cf;
{
	register struct op *t, *p;
	register int c;

	t = command(cf);
	if (t != NULL) {
		while ((c = yylex(0)) == '|') {
			if ((p = command(CONTIN)) == NULL)
				SYNTAXERR;
			if (t->type != TPAREN && t->type != TCOM) {
				/* shell statement */
				t = block(TPAREN, t, NOBLOCK, NOWORDS);
			}
			t = block(TPIPE, t, p, NOWORDS);
		}
		peeksym = c;
	}
	return(t);
}

static struct op *
andor()
{
	register struct op *t, *p;
	register int c;

	t = pipeline(0);
	if (t != NULL) {
		while ((c = yylex(0)) == LOGAND || c == LOGOR) {
			if ((p = pipeline(CONTIN)) == NULL)
				SYNTAXERR;
			t = block(c == LOGAND? TAND: TOR, t, p, NOWORDS);
		}
		peeksym = c;
	}
	return(t);
}

static struct op *
c_list()
{
	register struct op *t, *p;
	register int c;

	t = andor();
	if (t != NULL) {
		if((peeksym = yylex(0)) == '&')
			t = block(TASYNC, t, NOBLOCK, NOWORDS);
		while ((c = yylex(0)) == ';' || c == '&' || (multiline && c == '\n')) {
			if ((p = andor()) == NULL)
				return(t);
			if((peeksym = yylex(0)) == '&')
				p = block(TASYNC, p, NOBLOCK, NOWORDS);
			t = list(t, p);
		}
		peeksym = c;
	}
	return(t);
}


static int
synio(cf)
int cf;
{
	register struct ioword *iop;
	register int i;
	register int c;

	if ((c = yylex(cf)) != '<' && c != '>') {
		peeksym = c;
		return(0);
	}
	i = yylval.i;
	musthave(WORD, 0);
	iop = io(iounit, i, yylval.cp);
	iounit = IODEFAULT;
	if (i & IOHERE)
		markhere(yylval.cp, iop);
	return(1);
}

static void
musthave(c, cf)
int c, cf;
{
	if ((peeksym = yylex(cf)) != c)
		SYNTAXERR;
	peeksym = 0;
}

static struct op *
simple()
{
	register struct op *t;

	t = NULL;
	for (;;) {
		switch (peeksym = yylex(0)) {
		case '<':
		case '>':
			(void) synio(0);
			break;

		case WORD:
			if (t == NULL) {
				t = newtp();
				t->type = TCOM;
			}
			peeksym = 0;
			word(yylval.cp);
			break;

		default:
			return(t);
		}
	}
}

static struct op *
nested(type, mark)
int type, mark;
{
	register struct op *t;

	multiline++;
	t = c_list();
	musthave(mark, 0);
	multiline--;
	return(block(type, t, NOBLOCK, NOWORDS));
}

static struct op *
command(cf)
int cf;
{
	register struct op *t;
	struct wdblock *iosave;
	register int c;

	iosave = iolist;
	iolist = NULL;
	if (multiline)
		cf |= CONTIN;
	while (synio(cf))
		cf = 0;
	switch (c = yylex(cf)) {
	default:
		peeksym = c;
		if ((t = simple()) == NULL) {
			if (iolist == NULL)
				return((struct op *)NULL);
			t = newtp();
			t->type = TCOM;
		}
		break;

	case '(':
		t = nested(TPAREN, ')');
		break;

	case '{':
		t = nested(TBRACE, '}');
		break;

	case FOR:
		t = newtp();
		t->type = TFOR;
		musthave(WORD, 0);
		startl = 1;
		t->str = yylval.cp;
		multiline++;
		t->words = wordlist();
		if ((c = yylex(0)) != '\n' && c != ';')
			peeksym = c;
		t->left = dogroup(0);
		multiline--;
		break;

	case WHILE:
	case UNTIL:
		multiline++;
		t = newtp();
		t->type = c == WHILE? TWHILE: TUNTIL;
		t->left = c_list();
		t->right = dogroup(1);
		t->words = NULL;
		multiline--;
		break;

	case CASE:
		t = newtp();
		t->type = TCASE;
		musthave(WORD, 0);
		t->str = yylval.cp;
		startl++;
		multiline++;
		musthave(IN, CONTIN);
		startl++;
		t->left = caselist();
		musthave(ESAC, 0);
		multiline--;
		break;

	case IF:
		multiline++;
		t = newtp();
		t->type = TIF;
		t->left = c_list();
		t->right = thenpart();
		musthave(FI, 0);
		multiline--;
		break;
	}
	while (synio(0))
		;
	t = namelist(t);
	iolist = iosave;
	return(t);
}

static struct op *
dogroup(onlydone)
int onlydone;
{
	register int c;
	register struct op *list;

	c = yylex(CONTIN);
	if (c == DONE && onlydone)
		return((struct op *)NULL);
	if (c != DO)
		SYNTAXERR;
	list = c_list();
	musthave(DONE, 0);
	return(list);
}

static struct op *
thenpart()
{
	register int c;
	register struct op *t;

	if ((c = yylex(0)) != THEN) {
		peeksym = c;
		return((struct op *)NULL);
	}
	t = newtp();
	t->type = 0;
	t->left = c_list();
	if (t->left == NULL)
		SYNTAXERR;
	t->right = elsepart();
	return(t);
}

static struct op *
elsepart()
{
	register int c;
	register struct op *t;

	switch (c = yylex(0)) {
	case ELSE:
		if ((t = c_list()) == NULL)
			SYNTAXERR;
		return(t);

	case ELIF:
		t = newtp();
		t->type = TELIF;
		t->left = c_list();
		t->right = thenpart();
		return(t);

	default:
		peeksym = c;
		return((struct op *)NULL);
	}
}

static struct op *
caselist()
{
	register struct op *t;

	t = NULL;
	while ((peeksym = yylex(CONTIN)) != ESAC)
		t = list(t, casepart());
	return(t);
}

static struct op *
casepart()
{
	register struct op *t;

	t = newtp();
	t->type = TPAT;
	t->words = pattern();
	musthave(')', 0);
	t->left = c_list();
	if ((peeksym = yylex(CONTIN)) != ESAC)
		musthave(BREAK, CONTIN);
	return(t);
}

static char **
pattern()
{
	register int c, cf;

	cf = CONTIN;
	do {
		musthave(WORD, cf);
		word(yylval.cp);
		cf = 0;
	} while ((c = yylex(0)) == '|');
	peeksym = c;
	word(NOWORD);
	return(copyw());
}

static char **
wordlist()
{
	register int c;

	if ((c = yylex(0)) != IN) {
		peeksym = c;
		return((char **)NULL);
	}
	startl = 0;
	while ((c = yylex(0)) == WORD)
		word(yylval.cp);
	word(NOWORD);
	peeksym = c;
	return(copyw());
}

/*
 * supporting functions
 */
static struct op *
list(t1, t2)
register struct op *t1, *t2;
{
	if (t1 == NULL)
		return(t2);
	if (t2 == NULL)
		return(t1);
	return(block(TLIST, t1, t2, NOWORDS));
}

static struct op *
block(type, t1, t2, wp)
int type;
struct op *t1, *t2;
char **wp;
{
	register struct op *t;

	t = newtp();
	t->type = type;
	t->left = t1;
	t->right = t2;
	t->words = wp;
	return(t);
}

struct res {
	char	*r_name;
	int	r_val;
} restab[] = {
	"for",		FOR,
	"case",		CASE,
	"esac",		ESAC,
	"while",	WHILE,
	"do",		DO,
	"done",		DONE,
	"if",		IF,
	"in",		IN,
	"then",		THEN,
	"else",		ELSE,
	"elif",		ELIF,
	"until",	UNTIL,
	"fi",		FI,

	";;",		BREAK,
	"||",		LOGOR,
	"&&",		LOGAND,
	"{",		'{',
	"}",		'}',

	0,
};

int
rlookup(n)
register char *n;
{
	register struct res *rp;

	for (rp = restab; rp->r_name; rp++)
		if (strcmp(rp->r_name, n) == 0)
			return(rp->r_val);
	return(0);
}

static struct op *
newtp()
{
	register struct op *t;

	t = (struct op *)tree(sizeof(*t));
	t->type = 0;
	t->words = NULL;
	t->ioact = NULL;
	t->left = NULL;
	t->right = NULL;
	t->str = NULL;
	return(t);
}

static struct op *
namelist(t)
register struct op *t;
{
	if (iolist) {
		iolist = addword((char *)NULL, iolist);
		t->ioact = copyio();
	} else
		t->ioact = NULL;
	if (t->type != TCOM) {
		if (t->type != TPAREN && t->ioact != NULL) {
			t = block(TPAREN, t, NOBLOCK, NOWORDS);
			t->ioact = t->left->ioact;
			t->left->ioact = NULL;
		}
		return(t);
	}
	word(NOWORD);
	t->words = copyw();
	return(t);
}

static char **
copyw()
{
	register char **wd;

	wd = getwords(wdlist);
	wdlist = 0;
	return(wd);
}

static void
word(cp)
char *cp;
{
	wdlist = addword(cp, wdlist);
}

static struct ioword **
copyio()
{
	register struct ioword **iop;

	iop = (struct ioword **) getwords(iolist);
	iolist = 0;
	return(iop);
}

static struct ioword *
io(u, f, cp)
int u;
int f;
char *cp;
{
	register struct ioword *iop;

	iop = (struct ioword *) tree(sizeof(*iop));
	iop->io_unit = u;
	iop->io_flag = f;
	iop->io_name = cp;
	iolist = addword((char *)iop, iolist);
	return(iop);
}

static void
zzerr()
{
	yyerror("syntax error");
}

void
yyerror(s)
char *s;
{
	yynerrs++;
	if (talking && e.iop <= iostack) {
		multiline = 0;
		while (eofc() == 0 && yylex(0) != '\n')
			;
	}
	err(s);
	fail();
}

static int
yylex(cf)
int cf;
{
	register int c, c1;
	int atstart;

	if ((c = peeksym) > 0) {
		peeksym = 0;
		if (c == '\n')
			startl = 1;
		return(c);
	}
	nlseen = 0;
	e.linep = line;
	atstart = startl;
	startl = 0;
	yylval.i = 0;

loop:
	while ((c = getc(0)) == ' ' || c == '\t')
		;
	switch (c) {
	default:
		if (any(c, "0123456789")) {
			unget(c1 = getc(0));
			if (c1 == '<' || c1 == '>') {
				iounit = c - '0';
				goto loop;
			}
			*e.linep++ = c;
			c = c1;
		}
		break;

	case '#':
		while ((c = getc(0)) != 0 && c != '\n')
			;
		unget(c);
		goto loop;

	case 0:
		return(c);

	case '$':
		*e.linep++ = c;
		if ((c = getc(0)) == '{') {
			if ((c = collect(c, '}')) != '\0')
				return(c);
			goto pack;
		}
		break;

	case '`':
	case '\'':
	case '"':
		if ((c = collect(c, c)) != '\0')
			return(c);
		goto pack;

	case '|':
	case '&':
	case ';':
		if ((c1 = dual(c)) != '\0') {
			startl = 1;
			return(c1);
		}
		startl = 1;
		return(c);
	case '^':
		startl = 1;
		return('|');
	case '>':
	case '<':
		diag(c);
		return(c);

	case '\n':
		nlseen++;
		gethere();
		startl = 1;
		if (multiline || cf & CONTIN) {
			if (talking && e.iop <= iostack)
				prs(cprompt->value);
			if (cf & CONTIN)
				goto loop;
		}
		return(c);

	case '(':
	case ')':
		startl = 1;
		return(c);
	}

	unget(c);

pack:
	while ((c = getc(0)) != 0 && !any(c, "`$ '\"\t;&<>()|^\n"))
		if (e.linep >= elinep)
			err("word too long");
		else
			*e.linep++ = c;
	unget(c);
	if(any(c, "\"'`$"))
		goto loop;
	*e.linep++ = '\0';
	if (atstart && (c = rlookup(line))!=0) {
		startl = 1;
		return(c);
	}
	yylval.cp = strsave(line, areanum);
	return(WORD);
}

int
collect(c, c1)
register c, c1;
{
	char s[2];

	*e.linep++ = c;
	while ((c = getc(c1)) != c1) {
		if (c == 0) {
			unget(c);
			s[0] = c1;
			s[1] = 0;
			prs("no closing "); yyerror(s);
			return(YYERRCODE);
		}
		if (talking && c == '\n' && e.iop <= iostack)
			prs(cprompt->value);
		*e.linep++ = c;
	}
	*e.linep++ = c;
	return(0);
}

int
dual(c)
register c;
{
	char s[3];
	register char *cp = s;

	*cp++ = c;
	*cp++ = getc(0);
	*cp = 0;
	if ((c = rlookup(s)) == 0)
		unget(*--cp);
	return(c);
}

static void
diag(ec)
register int ec;
{
	register int c;

	c = getc(0);
	if (c == '>' || c == '<') {
		if (c != ec)
			zzerr();
		yylval.i = ec == '>'? IOWRITE|IOCAT: IOHERE;
		c = getc(0);
	} else
		yylval.i = ec == '>'? IOWRITE: IOREAD;
	if (c != '&' || yylval.i == IOHERE)
		unget(c);
	else
		yylval.i |= IODUP;
}

static char *
tree(size)
unsigned size;
{
	register char *t;

	if ((t = getcell(size)) == NULL) {
		prs("command line too complicated\n");
		fail();
		/* NOTREACHED */
	}
	return(t);
}

/* VARARGS1 */
/* ARGSUSED */
void 
printf(s)	/* yyparse calls it */
char *s;
{
}

