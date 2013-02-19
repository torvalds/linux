/*
 * regcomp and regexec -- regsub and regerror are elsewhere
 * @(#)regexp.c	1.3 of 18 April 87
 *
 *	Copyright (c) 1986 by University of Toronto.
 *	Written by Henry Spencer.  Not derived from licensed software.
 *
 *	Permission is granted to anyone to use this software for any
 *	purpose on any computer system, and to redistribute it freely,
 *	subject to the following restrictions:
 *
 *	1. The author is not responsible for the consequences of use of
 *		this software, no matter how awful, even if they arise
 *		from defects in it.
 *
 *	2. The origin of this software must not be misrepresented, either
 *		by explicit claim or by omission.
 *
 *	3. Altered versions must be plainly marked as such, and must not
 *		be misrepresented as being the original software.
 *
 * Beware that some of this code is subtly aware of the way operator
 * precedence is structured in regular expressions.  Serious changes in
 * regular-expression syntax might require a total rethink.
 *
 * This code was modified by Ethan Sommer to work within the kernel
 * (it now uses kmalloc etc..)
 *
 * Modified slightly by Matthew Strait to use more modern C.
 */

#include "regexp.h"
#include "regmagic.h"

/* added by ethan and matt.  Lets it work in both kernel and user space.
(So iptables can use it, for instance.)  Yea, it goes both ways... */
#if __KERNEL__
  #define malloc(foo) kmalloc(foo,GFP_ATOMIC)
#else
  #define printk(format,args...) printf(format,##args)
#endif

void regerror(char * s)
{
        printk("<3>Regexp: %s\n", s);
        /* NOTREACHED */
}

/*
 * The "internal use only" fields in regexp.h are present to pass info from
 * compile to execute that permits the execute phase to run lots faster on
 * simple cases.  They are:
 *
 * regstart	char that must begin a match; '\0' if none obvious
 * reganch	is the match anchored (at beginning-of-line only)?
 * regmust	string (pointer into program) that match must include, or NULL
 * regmlen	length of regmust string
 *
 * Regstart and reganch permit very fast decisions on suitable starting points
 * for a match, cutting down the work a lot.  Regmust permits fast rejection
 * of lines that cannot possibly match.  The regmust tests are costly enough
 * that regcomp() supplies a regmust only if the r.e. contains something
 * potentially expensive (at present, the only such thing detected is * or +
 * at the start of the r.e., which can involve a lot of backup).  Regmlen is
 * supplied because the test in regexec() needs it and regcomp() is computing
 * it anyway.
 */

/*
 * Structure for regexp "program".  This is essentially a linear encoding
 * of a nondeterministic finite-state machine (aka syntax charts or
 * "railroad normal form" in parsing technology).  Each node is an opcode
 * plus a "next" pointer, possibly plus an operand.  "Next" pointers of
 * all nodes except BRANCH implement concatenation; a "next" pointer with
 * a BRANCH on both ends of it is connecting two alternatives.  (Here we
 * have one of the subtle syntax dependencies:  an individual BRANCH (as
 * opposed to a collection of them) is never concatenated with anything
 * because of operator precedence.)  The operand of some types of node is
 * a literal string; for others, it is a node leading into a sub-FSM.  In
 * particular, the operand of a BRANCH node is the first node of the branch.
 * (NB this is *not* a tree structure:  the tail of the branch connects
 * to the thing following the set of BRANCHes.)  The opcodes are:
 */

/* definition	number	opnd?	meaning */
#define	END	0	/* no	End of program. */
#define	BOL	1	/* no	Match "" at beginning of line. */
#define	EOL	2	/* no	Match "" at end of line. */
#define	ANY	3	/* no	Match any one character. */
#define	ANYOF	4	/* str	Match any character in this string. */
#define	ANYBUT	5	/* str	Match any character not in this string. */
#define	BRANCH	6	/* node	Match this alternative, or the next... */
#define	BACK	7	/* no	Match "", "next" ptr points backward. */
#define	EXACTLY	8	/* str	Match this string. */
#define	NOTHING	9	/* no	Match empty string. */
#define	STAR	10	/* node	Match this (simple) thing 0 or more times. */
#define	PLUS	11	/* node	Match this (simple) thing 1 or more times. */
#define	OPEN	20	/* no	Mark this point in input as start of #n. */
			/*	OPEN+1 is number 1, etc. */
#define	CLOSE	30	/* no	Analogous to OPEN. */

/*
 * Opcode notes:
 *
 * BRANCH	The set of branches constituting a single choice are hooked
 *		together with their "next" pointers, since precedence prevents
 *		anything being concatenated to any individual branch.  The
 *		"next" pointer of the last BRANCH in a choice points to the
 *		thing following the whole choice.  This is also where the
 *		final "next" pointer of each individual branch points; each
 *		branch starts with the operand node of a BRANCH node.
 *
 * BACK		Normal "next" pointers all implicitly point forward; BACK
 *		exists to make loop structures possible.
 *
 * STAR,PLUS	'?', and complex '*' and '+', are implemented as circular
 *		BRANCH structures using BACK.  Simple cases (one character
 *		per match) are implemented with STAR and PLUS for speed
 *		and to minimize recursive plunges.
 *
 * OPEN,CLOSE	...are numbered at compile time.
 */

/*
 * A node is one char of opcode followed by two chars of "next" pointer.
 * "Next" pointers are stored as two 8-bit pieces, high order first.  The
 * value is a positive offset from the opcode of the node containing it.
 * An operand, if any, simply follows the node.  (Note that much of the
 * code generation knows about this implicit relationship.)
 *
 * Using two bytes for the "next" pointer is vast overkill for most things,
 * but allows patterns to get big without disasters.
 */
#define	OP(p)	(*(p))
#define	NEXT(p)	(((*((p)+1)&0377)<<8) + (*((p)+2)&0377))
#define	OPERAND(p)	((p) + 3)

/*
 * See regmagic.h for one further detail of program structure.
 */


/*
 * Utility definitions.
 */
#ifndef CHARBITS
#define	UCHARAT(p)	((int)*(unsigned char *)(p))
#else
#define	UCHARAT(p)	((int)*(p)&CHARBITS)
#endif

#define	FAIL(m)	{ regerror(m); return(NULL); }
#define	ISMULT(c)	((c) == '*' || (c) == '+' || (c) == '?')
#define	META	"^$.[()|?+*\\"

/*
 * Flags to be passed up and down.
 */
#define	HASWIDTH	01	/* Known never to match null string. */
#define	SIMPLE		02	/* Simple enough to be STAR/PLUS operand. */
#define	SPSTART		04	/* Starts with * or +. */
#define	WORST		0	/* Worst case. */

/*
 * Global work variables for regcomp().
 */
struct match_globals {
char *reginput;		/* String-input pointer. */
char *regbol;		/* Beginning of input, for ^ check. */
char **regstartp;	/* Pointer to startp array. */
char **regendp;		/* Ditto for endp. */
char *regparse;		/* Input-scan pointer. */
int regnpar;		/* () count. */
char regdummy;
char *regcode;		/* Code-emit pointer; &regdummy = don't. */
long regsize;		/* Code size. */
};

/*
 * Forward declarations for regcomp()'s friends.
 */
#ifndef STATIC
#define	STATIC	static
#endif
STATIC char *reg(struct match_globals *g, int paren,int *flagp);
STATIC char *regbranch(struct match_globals *g, int *flagp);
STATIC char *regpiece(struct match_globals *g, int *flagp);
STATIC char *regatom(struct match_globals *g, int *flagp);
STATIC char *regnode(struct match_globals *g, char op);
STATIC char *regnext(struct match_globals *g, char *p);
STATIC void regc(struct match_globals *g, char b);
STATIC void reginsert(struct match_globals *g, char op, char *opnd);
STATIC void regtail(struct match_globals *g, char *p, char *val);
STATIC void regoptail(struct match_globals *g, char *p, char *val);


__kernel_size_t my_strcspn(const char *s1,const char *s2)
{
        char *scan1;
        char *scan2;
        int count;

        count = 0;
        for (scan1 = (char *)s1; *scan1 != '\0'; scan1++) {
                for (scan2 = (char *)s2; *scan2 != '\0';)       /* ++ moved down. */
                        if (*scan1 == *scan2++)
                                return(count);
                count++;
        }
        return(count);
}

/*
 - regcomp - compile a regular expression into internal code
 *
 * We can't allocate space until we know how big the compiled form will be,
 * but we can't compile it (and thus know how big it is) until we've got a
 * place to put the code.  So we cheat:  we compile it twice, once with code
 * generation turned off and size counting turned on, and once "for real".
 * This also means that we don't allocate space until we are sure that the
 * thing really will compile successfully, and we never have to move the
 * code and thus invalidate pointers into it.  (Note that it has to be in
 * one piece because free() must be able to free it all.)
 *
 * Beware that the optimization-preparation code in here knows about some
 * of the structure of the compiled regexp.
 */
regexp *
regcomp(char *exp,int *patternsize)
{
	register regexp *r;
	register char *scan;
	register char *longest;
	register int len;
	int flags;
	struct match_globals g;
	
	/* commented out by ethan
	   extern char *malloc();
	*/

	if (exp == NULL)
		FAIL("NULL argument");

	/* First pass: determine size, legality. */
	g.regparse = exp;
	g.regnpar = 1;
	g.regsize = 0L;
	g.regcode = &g.regdummy;
	regc(&g, MAGIC);
	if (reg(&g, 0, &flags) == NULL)
		return(NULL);

	/* Small enough for pointer-storage convention? */
	if (g.regsize >= 32767L)		/* Probably could be 65535L. */
		FAIL("regexp too big");

	/* Allocate space. */
	*patternsize=sizeof(regexp) + (unsigned)g.regsize;
	r = (regexp *)malloc(sizeof(regexp) + (unsigned)g.regsize);
	if (r == NULL)
		FAIL("out of space");

	/* Second pass: emit code. */
	g.regparse = exp;
	g.regnpar = 1;
	g.regcode = r->program;
	regc(&g, MAGIC);
	if (reg(&g, 0, &flags) == NULL)
		return(NULL);

	/* Dig out information for optimizations. */
	r->regstart = '\0';	/* Worst-case defaults. */
	r->reganch = 0;
	r->regmust = NULL;
	r->regmlen = 0;
	scan = r->program+1;			/* First BRANCH. */
	if (OP(regnext(&g, scan)) == END) {		/* Only one top-level choice. */
		scan = OPERAND(scan);

		/* Starting-point info. */
		if (OP(scan) == EXACTLY)
			r->regstart = *OPERAND(scan);
		else if (OP(scan) == BOL)
			r->reganch++;

		/*
		 * If there's something expensive in the r.e., find the
		 * longest literal string that must appear and make it the
		 * regmust.  Resolve ties in favor of later strings, since
		 * the regstart check works with the beginning of the r.e.
		 * and avoiding duplication strengthens checking.  Not a
		 * strong reason, but sufficient in the absence of others.
		 */
		if (flags&SPSTART) {
			longest = NULL;
			len = 0;
			for (; scan != NULL; scan = regnext(&g, scan))
				if (OP(scan) == EXACTLY && strlen(OPERAND(scan)) >= len) {
					longest = OPERAND(scan);
					len = strlen(OPERAND(scan));
				}
			r->regmust = longest;
			r->regmlen = len;
		}
	}

	return(r);
}

/*
 - reg - regular expression, i.e. main body or parenthesized thing
 *
 * Caller must absorb opening parenthesis.
 *
 * Combining parenthesis handling with the base level of regular expression
 * is a trifle forced, but the need to tie the tails of the branches to what
 * follows makes it hard to avoid.
 */
static char *
reg(struct match_globals *g, int paren, int *flagp /* Parenthesized? */ )
{
	register char *ret;
	register char *br;
	register char *ender;
	register int parno = 0; /* 0 makes gcc happy */
	int flags;

	*flagp = HASWIDTH;	/* Tentatively. */

	/* Make an OPEN node, if parenthesized. */
	if (paren) {
		if (g->regnpar >= NSUBEXP)
			FAIL("too many ()");
		parno = g->regnpar;
		g->regnpar++;
		ret = regnode(g, OPEN+parno);
	} else
		ret = NULL;

	/* Pick up the branches, linking them together. */
	br = regbranch(g, &flags);
	if (br == NULL)
		return(NULL);
	if (ret != NULL)
		regtail(g, ret, br);	/* OPEN -> first. */
	else
		ret = br;
	if (!(flags&HASWIDTH))
		*flagp &= ~HASWIDTH;
	*flagp |= flags&SPSTART;
	while (*g->regparse == '|') {
		g->regparse++;
		br = regbranch(g, &flags);
		if (br == NULL)
			return(NULL);
		regtail(g, ret, br);	/* BRANCH -> BRANCH. */
		if (!(flags&HASWIDTH))
			*flagp &= ~HASWIDTH;
		*flagp |= flags&SPSTART;
	}

	/* Make a closing node, and hook it on the end. */
	ender = regnode(g, (paren) ? CLOSE+parno : END);	
	regtail(g, ret, ender);

	/* Hook the tails of the branches to the closing node. */
	for (br = ret; br != NULL; br = regnext(g, br))
		regoptail(g, br, ender);

	/* Check for proper termination. */
	if (paren && *g->regparse++ != ')') {
		FAIL("unmatched ()");
	} else if (!paren && *g->regparse != '\0') {
		if (*g->regparse == ')') {
			FAIL("unmatched ()");
		} else
			FAIL("junk on end");	/* "Can't happen". */
		/* NOTREACHED */
	}

	return(ret);
}

/*
 - regbranch - one alternative of an | operator
 *
 * Implements the concatenation operator.
 */
static char *
regbranch(struct match_globals *g, int *flagp)
{
	register char *ret;
	register char *chain;
	register char *latest;
	int flags;

	*flagp = WORST;		/* Tentatively. */

	ret = regnode(g, BRANCH);
	chain = NULL;
	while (*g->regparse != '\0' && *g->regparse != '|' && *g->regparse != ')') {
		latest = regpiece(g, &flags);
		if (latest == NULL)
			return(NULL);
		*flagp |= flags&HASWIDTH;
		if (chain == NULL)	/* First piece. */
			*flagp |= flags&SPSTART;
		else
			regtail(g, chain, latest);
		chain = latest;
	}
	if (chain == NULL)	/* Loop ran zero times. */
		(void) regnode(g, NOTHING);

	return(ret);
}

/*
 - regpiece - something followed by possible [*+?]
 *
 * Note that the branching code sequences used for ? and the general cases
 * of * and + are somewhat optimized:  they use the same NOTHING node as
 * both the endmarker for their branch list and the body of the last branch.
 * It might seem that this node could be dispensed with entirely, but the
 * endmarker role is not redundant.
 */
static char *
regpiece(struct match_globals *g, int *flagp)
{
	register char *ret;
	register char op;
	register char *next;
	int flags;

	ret = regatom(g, &flags);
	if (ret == NULL)
		return(NULL);

	op = *g->regparse;
	if (!ISMULT(op)) {
		*flagp = flags;
		return(ret);
	}

	if (!(flags&HASWIDTH) && op != '?')
		FAIL("*+ operand could be empty");
	*flagp = (op != '+') ? (WORST|SPSTART) : (WORST|HASWIDTH);

	if (op == '*' && (flags&SIMPLE))
		reginsert(g, STAR, ret);
	else if (op == '*') {
		/* Emit x* as (x&|), where & means "self". */
		reginsert(g, BRANCH, ret);			/* Either x */
		regoptail(g, ret, regnode(g, BACK));		/* and loop */
		regoptail(g, ret, ret);			/* back */
		regtail(g, ret, regnode(g, BRANCH));		/* or */
		regtail(g, ret, regnode(g, NOTHING));		/* null. */
	} else if (op == '+' && (flags&SIMPLE))
		reginsert(g, PLUS, ret);
	else if (op == '+') {
		/* Emit x+ as x(&|), where & means "self". */
		next = regnode(g, BRANCH);			/* Either */
		regtail(g, ret, next);
		regtail(g, regnode(g, BACK), ret);		/* loop back */
		regtail(g, next, regnode(g, BRANCH));		/* or */
		regtail(g, ret, regnode(g, NOTHING));		/* null. */
	} else if (op == '?') {
		/* Emit x? as (x|) */
		reginsert(g, BRANCH, ret);			/* Either x */
		regtail(g, ret, regnode(g, BRANCH));		/* or */
		next = regnode(g, NOTHING);		/* null. */
		regtail(g, ret, next);
		regoptail(g, ret, next);
	}
	g->regparse++;
	if (ISMULT(*g->regparse))
		FAIL("nested *?+");

	return(ret);
}

/*
 - regatom - the lowest level
 *
 * Optimization:  gobbles an entire sequence of ordinary characters so that
 * it can turn them into a single node, which is smaller to store and
 * faster to run.  Backslashed characters are exceptions, each becoming a
 * separate node; the code is simpler that way and it's not worth fixing.
 */
static char *
regatom(struct match_globals *g, int *flagp)
{
	register char *ret;
	int flags;

	*flagp = WORST;		/* Tentatively. */

	switch (*g->regparse++) {
	case '^':
		ret = regnode(g, BOL);
		break;
	case '$':
		ret = regnode(g, EOL);
		break;
	case '.':
		ret = regnode(g, ANY);
		*flagp |= HASWIDTH|SIMPLE;
		break;
	case '[': {
			register int class;
			register int classend;

			if (*g->regparse == '^') {	/* Complement of range. */
				ret = regnode(g, ANYBUT);
				g->regparse++;
			} else
				ret = regnode(g, ANYOF);
			if (*g->regparse == ']' || *g->regparse == '-')
				regc(g, *g->regparse++);
			while (*g->regparse != '\0' && *g->regparse != ']') {
				if (*g->regparse == '-') {
					g->regparse++;
					if (*g->regparse == ']' || *g->regparse == '\0')
						regc(g, '-');
					else {
						class = UCHARAT(g->regparse-2)+1;
						classend = UCHARAT(g->regparse);
						if (class > classend+1)
							FAIL("invalid [] range");
						for (; class <= classend; class++)
							regc(g, class);
						g->regparse++;
					}
				} else
					regc(g, *g->regparse++);
			}
			regc(g, '\0');
			if (*g->regparse != ']')
				FAIL("unmatched []");
			g->regparse++;
			*flagp |= HASWIDTH|SIMPLE;
		}
		break;
	case '(':
		ret = reg(g, 1, &flags);
		if (ret == NULL)
			return(NULL);
		*flagp |= flags&(HASWIDTH|SPSTART);
		break;
	case '\0':
	case '|':
	case ')':
		FAIL("internal urp");	/* Supposed to be caught earlier. */
		break;
	case '?':
	case '+':
	case '*':
		FAIL("?+* follows nothing");
		break;
	case '\\':
		if (*g->regparse == '\0')
			FAIL("trailing \\");
		ret = regnode(g, EXACTLY);
		regc(g, *g->regparse++);
		regc(g, '\0');
		*flagp |= HASWIDTH|SIMPLE;
		break;
	default: {
			register int len;
			register char ender;

			g->regparse--;
			len = my_strcspn((const char *)g->regparse, (const char *)META);
			if (len <= 0)
				FAIL("internal disaster");
			ender = *(g->regparse+len);
			if (len > 1 && ISMULT(ender))
				len--;		/* Back off clear of ?+* operand. */
			*flagp |= HASWIDTH;
			if (len == 1)
				*flagp |= SIMPLE;
			ret = regnode(g, EXACTLY);
			while (len > 0) {
				regc(g, *g->regparse++);
				len--;
			}
			regc(g, '\0');
		}
		break;
	}

	return(ret);
}

/*
 - regnode - emit a node
 */
static char *			/* Location. */
regnode(struct match_globals *g, char op)
{
	register char *ret;
	register char *ptr;

	ret = g->regcode;
	if (ret == &g->regdummy) {
		g->regsize += 3;
		return(ret);
	}

	ptr = ret;
	*ptr++ = op;
	*ptr++ = '\0';		/* Null "next" pointer. */
	*ptr++ = '\0';
	g->regcode = ptr;

	return(ret);
}

/*
 - regc - emit (if appropriate) a byte of code
 */
static void
regc(struct match_globals *g, char b)
{
	if (g->regcode != &g->regdummy)
		*g->regcode++ = b;
	else
		g->regsize++;
}

/*
 - reginsert - insert an operator in front of already-emitted operand
 *
 * Means relocating the operand.
 */
static void
reginsert(struct match_globals *g, char op, char* opnd)
{
	register char *src;
	register char *dst;
	register char *place;

	if (g->regcode == &g->regdummy) {
		g->regsize += 3;
		return;
	}

	src = g->regcode;
	g->regcode += 3;
	dst = g->regcode;
	while (src > opnd)
		*--dst = *--src;

	place = opnd;		/* Op node, where operand used to be. */
	*place++ = op;
	*place++ = '\0';
	*place++ = '\0';
}

/*
 - regtail - set the next-pointer at the end of a node chain
 */
static void
regtail(struct match_globals *g, char *p, char *val)
{
	register char *scan;
	register char *temp;
	register int offset;

	if (p == &g->regdummy)
		return;

	/* Find last node. */
	scan = p;
	for (;;) {
		temp = regnext(g, scan);
		if (temp == NULL)
			break;
		scan = temp;
	}

	if (OP(scan) == BACK)
		offset = scan - val;
	else
		offset = val - scan;
	*(scan+1) = (offset>>8)&0377;
	*(scan+2) = offset&0377;
}

/*
 - regoptail - regtail on operand of first argument; nop if operandless
 */
static void
regoptail(struct match_globals *g, char *p, char *val)
{
	/* "Operandless" and "op != BRANCH" are synonymous in practice. */
	if (p == NULL || p == &g->regdummy || OP(p) != BRANCH)
		return;
	regtail(g, OPERAND(p), val);
}

/*
 * regexec and friends
 */


/*
 * Forwards.
 */
STATIC int regtry(struct match_globals *g, regexp *prog, char *string);
STATIC int regmatch(struct match_globals *g, char *prog);
STATIC int regrepeat(struct match_globals *g, char *p);

#ifdef DEBUG
int regnarrate = 0;
void regdump();
STATIC char *regprop(char *op);
#endif

/*
 - regexec - match a regexp against a string
 */
int
regexec(regexp *prog, char *string)
{
	register char *s;
	struct match_globals g;

	/* Be paranoid... */
	if (prog == NULL || string == NULL) {
		printk("<3>Regexp: NULL parameter\n");
		return(0);
	}

	/* Check validity of program. */
	if (UCHARAT(prog->program) != MAGIC) {
		printk("<3>Regexp: corrupted program\n");
		return(0);
	}

	/* If there is a "must appear" string, look for it. */
	if (prog->regmust != NULL) {
		s = string;
		while ((s = strchr(s, prog->regmust[0])) != NULL) {
			if (strncmp(s, prog->regmust, prog->regmlen) == 0)
				break;	/* Found it. */
			s++;
		}
		if (s == NULL)	/* Not present. */
			return(0);
	}

	/* Mark beginning of line for ^ . */
	g.regbol = string;

	/* Simplest case:  anchored match need be tried only once. */
	if (prog->reganch)
		return(regtry(&g, prog, string));

	/* Messy cases:  unanchored match. */
	s = string;
	if (prog->regstart != '\0')
		/* We know what char it must start with. */
		while ((s = strchr(s, prog->regstart)) != NULL) {
			if (regtry(&g, prog, s))
				return(1);
			s++;
		}
	else
		/* We don't -- general case. */
		do {
			if (regtry(&g, prog, s))
				return(1);
		} while (*s++ != '\0');

	/* Failure. */
	return(0);
}

/*
 - regtry - try match at specific point
 */
static int			/* 0 failure, 1 success */
regtry(struct match_globals *g, regexp *prog, char *string)
{
	register int i;
	register char **sp;
	register char **ep;

	g->reginput = string;
	g->regstartp = prog->startp;
	g->regendp = prog->endp;

	sp = prog->startp;
	ep = prog->endp;
	for (i = NSUBEXP; i > 0; i--) {
		*sp++ = NULL;
		*ep++ = NULL;
	}
	if (regmatch(g, prog->program + 1)) {
		prog->startp[0] = string;
		prog->endp[0] = g->reginput;
		return(1);
	} else
		return(0);
}

/*
 - regmatch - main matching routine
 *
 * Conceptually the strategy is simple:  check to see whether the current
 * node matches, call self recursively to see whether the rest matches,
 * and then act accordingly.  In practice we make some effort to avoid
 * recursion, in particular by going through "ordinary" nodes (that don't
 * need to know whether the rest of the match failed) by a loop instead of
 * by recursion.
 */
static int			/* 0 failure, 1 success */
regmatch(struct match_globals *g, char *prog)
{
	register char *scan = prog; /* Current node. */
	char *next;		    /* Next node. */

#ifdef DEBUG
	if (scan != NULL && regnarrate)
		fprintf(stderr, "%s(\n", regprop(scan));
#endif
	while (scan != NULL) {
#ifdef DEBUG
		if (regnarrate)
			fprintf(stderr, "%s...\n", regprop(scan));
#endif
		next = regnext(g, scan);

		switch (OP(scan)) {
		case BOL:
			if (g->reginput != g->regbol)
				return(0);
			break;
		case EOL:
			if (*g->reginput != '\0')
				return(0);
			break;
		case ANY:
			if (*g->reginput == '\0')
				return(0);
			g->reginput++;
			break;
		case EXACTLY: {
				register int len;
				register char *opnd;

				opnd = OPERAND(scan);
				/* Inline the first character, for speed. */
				if (*opnd != *g->reginput)
					return(0);
				len = strlen(opnd);
				if (len > 1 && strncmp(opnd, g->reginput, len) != 0)
					return(0);
				g->reginput += len;
			}
			break;
		case ANYOF:
			if (*g->reginput == '\0' || strchr(OPERAND(scan), *g->reginput) == NULL)
				return(0);
			g->reginput++;
			break;
		case ANYBUT:
			if (*g->reginput == '\0' || strchr(OPERAND(scan), *g->reginput) != NULL)
				return(0);
			g->reginput++;
			break;
		case NOTHING:
		case BACK:
			break;
		case OPEN+1:
		case OPEN+2:
		case OPEN+3:
		case OPEN+4:
		case OPEN+5:
		case OPEN+6:
		case OPEN+7:
		case OPEN+8:
		case OPEN+9: {
				register int no;
				register char *save;

				no = OP(scan) - OPEN;
				save = g->reginput;

				if (regmatch(g, next)) {
					/*
					 * Don't set startp if some later
					 * invocation of the same parentheses
					 * already has.
					 */
					if (g->regstartp[no] == NULL)
						g->regstartp[no] = save;
					return(1);
				} else
					return(0);
			}
			break;
		case CLOSE+1:
		case CLOSE+2:
		case CLOSE+3:
		case CLOSE+4:
		case CLOSE+5:
		case CLOSE+6:
		case CLOSE+7:
		case CLOSE+8:
		case CLOSE+9:
			{
				register int no;
				register char *save;

				no = OP(scan) - CLOSE;
				save = g->reginput;

				if (regmatch(g, next)) {
					/*
					 * Don't set endp if some later
					 * invocation of the same parentheses
					 * already has.
					 */
					if (g->regendp[no] == NULL)
						g->regendp[no] = save;
					return(1);
				} else
					return(0);
			}
			break;
		case BRANCH: {
				register char *save;

				if (OP(next) != BRANCH)		/* No choice. */
					next = OPERAND(scan);	/* Avoid recursion. */
				else {
					do {
						save = g->reginput;
						if (regmatch(g, OPERAND(scan)))
							return(1);
						g->reginput = save;
						scan = regnext(g, scan);
					} while (scan != NULL && OP(scan) == BRANCH);
					return(0);
					/* NOTREACHED */
				}
			}
			break;
		case STAR:
		case PLUS: {
				register char nextch;
				register int no;
				register char *save;
				register int min;

				/*
				 * Lookahead to avoid useless match attempts
				 * when we know what character comes next.
				 */
				nextch = '\0';
				if (OP(next) == EXACTLY)
					nextch = *OPERAND(next);
				min = (OP(scan) == STAR) ? 0 : 1;
				save = g->reginput;
				no = regrepeat(g, OPERAND(scan));
				while (no >= min) {
					/* If it could work, try it. */
					if (nextch == '\0' || *g->reginput == nextch)
						if (regmatch(g, next))
							return(1);
					/* Couldn't or didn't -- back up. */
					no--;
					g->reginput = save + no;
				}
				return(0);
			}
			break;
		case END:
			return(1);	/* Success! */
			break;
		default:
			printk("<3>Regexp: memory corruption\n");
			return(0);
			break;
		}

		scan = next;
	}

	/*
	 * We get here only if there's trouble -- normally "case END" is
	 * the terminating point.
	 */
	printk("<3>Regexp: corrupted pointers\n");
	return(0);
}

/*
 - regrepeat - repeatedly match something simple, report how many
 */
static int
regrepeat(struct match_globals *g, char *p)
{
	register int count = 0;
	register char *scan;
	register char *opnd;

	scan = g->reginput;
	opnd = OPERAND(p);
	switch (OP(p)) {
	case ANY:
		count = strlen(scan);
		scan += count;
		break;
	case EXACTLY:
		while (*opnd == *scan) {
			count++;
			scan++;
		}
		break;
	case ANYOF:
		while (*scan != '\0' && strchr(opnd, *scan) != NULL) {
			count++;
			scan++;
		}
		break;
	case ANYBUT:
		while (*scan != '\0' && strchr(opnd, *scan) == NULL) {
			count++;
			scan++;
		}
		break;
	default:		/* Oh dear.  Called inappropriately. */
		printk("<3>Regexp: internal foulup\n");
		count = 0;	/* Best compromise. */
		break;
	}
	g->reginput = scan;

	return(count);
}

/*
 - regnext - dig the "next" pointer out of a node
 */
static char*
regnext(struct match_globals *g, char *p)
{
	register int offset;

	if (p == &g->regdummy)
		return(NULL);

	offset = NEXT(p);
	if (offset == 0)
		return(NULL);

	if (OP(p) == BACK)
		return(p-offset);
	else
		return(p+offset);
}

#ifdef DEBUG

STATIC char *regprop();

/*
 - regdump - dump a regexp onto stdout in vaguely comprehensible form
 */
void
regdump(regexp *r)
{
	register char *s;
	register char op = EXACTLY;	/* Arbitrary non-END op. */
	register char *next;
	/* extern char *strchr(); */


	s = r->program + 1;
	while (op != END) {	/* While that wasn't END last time... */
		op = OP(s);
		printf("%2d%s", s-r->program, regprop(s));	/* Where, what. */
		next = regnext(s);
		if (next == NULL)		/* Next ptr. */
			printf("(0)");
		else
			printf("(%d)", (s-r->program)+(next-s));
		s += 3;
		if (op == ANYOF || op == ANYBUT || op == EXACTLY) {
			/* Literal string, where present. */
			while (*s != '\0') {
				putchar(*s);
				s++;
			}
			s++;
		}
		putchar('\n');
	}

	/* Header fields of interest. */
	if (r->regstart != '\0')
		printf("start `%c' ", r->regstart);
	if (r->reganch)
		printf("anchored ");
	if (r->regmust != NULL)
		printf("must have \"%s\"", r->regmust);
	printf("\n");
}

/*
 - regprop - printable representation of opcode
 */
static char *
regprop(char *op)
{
#define BUFLEN 50
	register char *p;
	static char buf[BUFLEN];

	strcpy(buf, ":");

	switch (OP(op)) {
	case BOL:
		p = "BOL";
		break;
	case EOL:
		p = "EOL";
		break;
	case ANY:
		p = "ANY";
		break;
	case ANYOF:
		p = "ANYOF";
		break;
	case ANYBUT:
		p = "ANYBUT";
		break;
	case BRANCH:
		p = "BRANCH";
		break;
	case EXACTLY:
		p = "EXACTLY";
		break;
	case NOTHING:
		p = "NOTHING";
		break;
	case BACK:
		p = "BACK";
		break;
	case END:
		p = "END";
		break;
	case OPEN+1:
	case OPEN+2:
	case OPEN+3:
	case OPEN+4:
	case OPEN+5:
	case OPEN+6:
	case OPEN+7:
	case OPEN+8:
	case OPEN+9:
		snprintf(buf+strlen(buf),BUFLEN-strlen(buf), "OPEN%d", OP(op)-OPEN);
		p = NULL;
		break;
	case CLOSE+1:
	case CLOSE+2:
	case CLOSE+3:
	case CLOSE+4:
	case CLOSE+5:
	case CLOSE+6:
	case CLOSE+7:
	case CLOSE+8:
	case CLOSE+9:
		snprintf(buf+strlen(buf),BUFLEN-strlen(buf), "CLOSE%d", OP(op)-CLOSE);
		p = NULL;
		break;
	case STAR:
		p = "STAR";
		break;
	case PLUS:
		p = "PLUS";
		break;
	default:
		printk("<3>Regexp: corrupted opcode\n");
		break;
	}
	if (p != NULL)
		strncat(buf, p, BUFLEN-strlen(buf));
	return(buf);
}
#endif


