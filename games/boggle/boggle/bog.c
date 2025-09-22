/*	$OpenBSD: bog.c,v 1.34 2021/10/23 11:22:48 mestre Exp $	*/
/*	$NetBSD: bog.c,v 1.5 1995/04/24 12:22:32 cgd Exp $	*/

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Barry Brachman.
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

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "bog.h"
#include "extern.h"

static void	init(void);
static void	init_adjacencies(void);
static int	compar(const void *, const void *);

struct dictindex dictindex[26];

static int **adjacency, **letter_map;

char *board;
int wordpath[MAXWORDLEN + 1];
int wordlen;		/* Length of last word returned by nextword() */
int usedbits;
int ncubes;
int grid = 4;

char **pword, *pwords, *pwordsp;
int npwords, maxpwords = MAXPWORDS, maxpspace = MAXPSPACE;

char **mword, *mwords, *mwordsp;
int nmwords, maxmwords = MAXMWORDS, maxmspace = MAXMSPACE;

int ngames = 0;
int tnmwords = 0, tnpwords = 0;

jmp_buf env;

time_t start_t;

static FILE *dictfp;

int batch;
int challenge;
int debug;
int minlength;
int reuse;
int selfuse;
int tlimit;

int
main(int argc, char *argv[])
{
	int ch, done;
	char *bspec, *p;

	batch = debug = reuse = selfuse;
	bspec = NULL;
	minlength = -1;
	tlimit = 180;		/* 3 minutes is standard */

	while ((ch = getopt(argc, argv, "Bbcdht:w:")) != -1)
		switch(ch) {
		case 'B':
			grid = 5;
			break;
		case 'b':
			batch = 1;
			break;
		case 'c':
			challenge = 1;
			break;
		case 'd':
			debug = 1;
			break;
		case 't':
			if ((tlimit = atoi(optarg)) < 1)
				errx(1, "bad time limit");
			break;
		case 'w':
			if ((minlength = atoi(optarg)) < 3)
				errx(1, "min word length must be > 2");
			break;
		case 'h':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	ncubes = grid * grid;

	/* process final arguments */
	if (argc > 0) {
		if (strcmp(argv[0], "+") == 0)
			reuse = 1;
		else if (strcmp(argv[0], "++") == 0)
			selfuse = 1;
	}

	if (reuse || selfuse) {
		argc -= 1;
		argv += 1;
	}

	if (argc == 1) {
		if (strlen(argv[0]) != ncubes)
			usage();
		for (p = argv[0]; *p != '\0'; p++)
			if (!islower((unsigned char)*p))
				errx(1, "only lower case letters are allowed "
				    "in boardspec");
		bspec = argv[0];
	} else if (argc != 0)
		usage();

	if (batch && bspec == NULL)
		errx(1, "must give both -b and a board setup");

	init();
	if (batch) {
		newgame(bspec);
		while ((p = batchword(stdin)) != NULL)
			(void) printf("%s\n", p);
		return 0;
	}
	setup();

	if (pledge("stdio rpath tty", NULL) == -1)
		err(1, "pledge");

	prompt("Loading the dictionary...");
	if ((dictfp = opendict(DICT)) == NULL) {
		warn("%s", DICT);
		cleanup();
		return 1;
	}
#ifdef LOADDICT
	if (loaddict(dictfp) < 0) {
		warnx("can't load %s", DICT);
		cleanup();
		return 1;
	}
	(void)fclose(dictfp);
	dictfp = NULL;
#endif
	if (loadindex(DICTINDEX) < 0) {
		warnx("can't load %s", DICTINDEX);
		cleanup();
		return 1;
	}

	prompt("Type <space> to begin...");
	while (inputch() != ' ');

	for (done = 0; !done;) {
		newgame(bspec);
		bspec = NULL;	/* reset for subsequent games */
		playgame();
		prompt("Type <space> to continue, any cap to quit...");
		delay(10);	/* wait for user to quit typing */
		flushin(stdin);
		for (;;) {
			ch = inputch();
			if (ch == '\033')
				findword();
			else if (ch == '\014' || ch == '\022')	/* ^l or ^r */
				redraw();
			else {
				if (isupper(ch)) {
					done = 1;
					break;
				}
				if (ch == ' ')
					break;
			}
		}
	}
	cleanup();
	return 0;
}

/*
 * Read a line from the given stream and check if it is legal
 * Return a pointer to a legal word or a null pointer when EOF is reached
 */
char *
batchword(FILE *fp)
{
	int *p, *q;
	char *w;

	q = &wordpath[MAXWORDLEN + 1];
	p = wordpath;
	while (p < q)
		*p++ = -1;
	while ((w = nextword(fp)) != NULL) {
		if (wordlen < minlength)
			continue;
		p = wordpath;
		while (p < q && *p != -1)
			*p++ = -1;
		usedbits = 0;
		if (checkword(w, -1, wordpath) != -1)
			return (w);
	}
	return (NULL);
}

/*
 * Play a single game
 * Reset the word lists from last game
 * Keep track of the running stats
 */
void
playgame(void)
{
	int i, *p, *q;
	time_t t;
	char buf[MAXWORDLEN + 1];

	ngames++;
	npwords = 0;
	pwordsp = pwords;
	nmwords = 0;
	mwordsp = mwords;

	time(&start_t);

	q = &wordpath[MAXWORDLEN + 1];
	p = wordpath;
	while (p < q)
		*p++ = -1;
	showboard(board);
	startwords();
	if (setjmp(env)) {
		badword();
		goto timesup;
	}

	while (1) {
		if (get_line(buf) == NULL) {
			if (feof(stdin))
				clearerr(stdin);
			break;
		}
		time(&t);
		if (t - start_t >= tlimit) {
			badword();
			break;
		}
		if (buf[0] == '\0') {
			int remaining;

			remaining = tlimit - (int) (t - start_t);
			(void)snprintf(buf, sizeof(buf),
			    "%d:%02d", remaining / 60, remaining % 60);
			showstr(buf, 1);
			continue;
		}
		if (strlen(buf) < (size_t)minlength) {
			badword();
			continue;
		}

		p = wordpath;
		while (p < q && *p != -1)
			*p++ = -1;
		usedbits = 0;

		if (checkword(buf, -1, wordpath) < 0)
			badword();
		else {
			if (debug) {
				(void) printf("[");
				for (i = 0; wordpath[i] != -1; i++)
					(void) printf(" %d", wordpath[i]);
				(void) printf(" ]\n");
			}
			for (i = 0; i < npwords; i++) {
				if (strcmp(pword[i], buf) == 0)
					break;
			}
			if (i != npwords) {	/* already used the word */
				badword();
				showword(i);
			}
			else if (!validword(buf))
				badword();
			else {
				int len;

				if (npwords == maxpwords - 1) {
					maxpwords += MAXPWORDS;
					pword = reallocarray(pword, maxpwords,
					    sizeof(char *));
					if (pword == NULL) {
						cleanup();
						errx(1, "%s", strerror(ENOMEM));
					}
				}
				len = strlen(buf) + 1;
				if (pwordsp + len >= &pwords[maxpspace]) {
					maxpspace += MAXPSPACE;
					pwords = realloc(pwords, maxpspace);
					if (pwords == NULL) {
						cleanup();
						errx(1, "%s", strerror(ENOMEM));
					}
				}
				pword[npwords++] = pwordsp;
				memcpy(pwordsp, buf, len);
				pwordsp += len;
				addword(buf);
			}
		}
	}

timesup: ;

	/*
	 * Sort the player's words and terminate the list with a null
	 * entry to help out checkdict()
	 */
	qsort(pword, npwords, sizeof(pword[0]), compar);
	pword[npwords] = NULL;

	/*
	 * These words don't need to be sorted since the dictionary is sorted
	 */
	checkdict();

	tnmwords += nmwords;
	tnpwords += npwords;

	results();
}

/*
 * Check if the given word is present on the board, with the constraint
 * that the first letter of the word is adjacent to square 'prev'
 * Keep track of the current path of squares for the word
 * A 'q' must be followed by a 'u'
 * Words must end with a null
 * Return 1 on success, -1 on failure
 */
int
checkword(char *word, int prev, int *path)
{
	char *p, *q;
	int i, *lm;

	if (debug) {
		(void) printf("checkword(%s, %d, [", word, prev);
			for (i = 0; wordpath[i] != -1; i++)
				(void) printf(" %d", wordpath[i]);
			(void) printf(" ]\n");
	}

	if (*word == '\0')
		return (1);

	lm = letter_map[*word - 'a'];

	if (prev == -1) {
		char subword[MAXWORDLEN + 1];

		/*
		 * Check for letters not appearing in the cube to eliminate some
		 * recursive calls
		 * Fold 'qu' into 'q'
		 */
		p = word;
		q = subword;
		while (*p != '\0') {
			if (*letter_map[*p - 'a'] == -1)
				return (-1);
			*q++ = *p;
			if (*p++ == 'q') {
				if (*p++ != 'u')
					return (-1);
			}
		}
		*q = '\0';
		while (*lm != -1) {
			*path = *lm;
			usedbits |= (1 << *lm);
			if (checkword(subword + 1, *lm, path + 1) > 0)
				return (1);
			usedbits &= ~(1 << *lm);
			lm++;
		}
		return (-1);
	}

	/*
	 * A cube is only adjacent to itself in the adjacency matrix if selfuse
	 * was set, so a cube can't be used twice in succession if only the
	 * reuse flag is set
	 */
	for (i = 0; lm[i] != -1; i++) {
		if (adjacency[prev][lm[i]]) {
			int used;

			used = 1 << lm[i];
			/*
			 * If necessary, check if the square has already
			 * been used.
			 */
			if (!reuse && !selfuse && (usedbits & used))
					continue;
			*path = lm[i];
			usedbits |= used;
			if (checkword(word + 1, lm[i], path + 1) > 0)
				return (1);
			usedbits &= ~used;
		}
	}
	*path = -1;		/* in case of a backtrack */
	return (-1);
}

/*
 * A word is invalid if it is not in the dictionary
 * At this point it is already known that the word can be formed from
 * the current board
 */
int
validword(char *word)
{
	int j;
	char *q, *w;

	j = word[0] - 'a';
	if (dictseek(dictfp, dictindex[j].start, SEEK_SET) < 0) {
		cleanup();
		errx(1, "seek error in validword()");
	}

	while ((w = nextword(dictfp)) != NULL) {
		int ch;

		if (*w != word[0])	/* end of words starting with word[0] */
			break;
		q = word;
		while ((ch = *w++) == *q++ && ch != '\0')
			;
		if (*(w - 1) == '\0' && *(q - 1) == '\0')
			return (1);
	}
	if (dictfp != NULL && feof(dictfp))	/* Special case for z's */
		clearerr(dictfp);
	return (0);
}

/*
 * Check each word in the dictionary against the board
 * Delete words from the machine list that the player has found
 * Assume both the dictionary and the player's words are already sorted
 */
void
checkdict(void)
{
	char **pw, *w;
	int i;
	int prevch, previndex, *pi, *qi, st;

	mwordsp = mwords;
	nmwords = 0;
	pw = pword;
	prevch ='a';
	qi = &wordpath[MAXWORDLEN + 1];

	(void) dictseek(dictfp, 0L, SEEK_SET);
	while ((w = nextword(dictfp)) != NULL) {
		if (wordlen < minlength)
			continue;
		if (*w != prevch) {
			/*
			 * If we've moved on to a word with a different first
			 * letter then we can speed things up by skipping all
			 * words starting with a letter that doesn't appear in
			 * the cube.
			 */
			i = (int) (*w - 'a');
			while (i < 26 && letter_map[i][0] == -1)
				i++;
			if (i == 26)
				break;
			previndex = prevch - 'a';
			prevch = i + 'a';
			/*
			 * Fall through if the word's first letter appears in
			 * the cube (i.e., if we can't skip ahead), otherwise
			 * seek to the beginning of words in the dictionary
			 * starting with the next letter (alphabetically)
			 * appearing in the cube and then read the first word.
			 */
			if (i != previndex + 1) {
				if (dictseek(dictfp,
				    dictindex[i].start, SEEK_SET) < 0) {
					cleanup();
					errx(1, "seek error in checkdict()");
				}
				continue;
			}
		}

		pi = wordpath;
		while (pi < qi && *pi != -1)
			*pi++ = -1;
		usedbits = 0;
		if (checkword(w, -1, wordpath) == -1)
			continue;

		st = 1;
		while (*pw != NULL && (st = strcmp(*pw, w)) < 0)
			pw++;
		if (st == 0)			/* found it */
			continue;
		if (nmwords == maxmwords - 1) {
			maxmwords += MAXMWORDS;
			mword = reallocarray(mword, maxmwords, sizeof(char *));
			if (mword == NULL) {
				cleanup();
				errx(1, "%s", strerror(ENOMEM));
			}
		}
		if (mwordsp + wordlen + 1 >= &mwords[maxmspace]) {
			maxmspace += MAXMSPACE;
			mwords = realloc(mwords, maxmspace);
			if (mwords == NULL) {
				cleanup();
				errx(1, "%s", strerror(ENOMEM));
			}
		}
		mword[nmwords++] = mwordsp;
		memcpy(mwordsp, w, wordlen + 1);
		mwordsp += wordlen + 1;
	}
}

/*
 * Crank up a new game
 * If the argument is non-null then it is assumed to be a legal board spec
 * in ascending cube order, oth. make a random board
 */
void
newgame(char *b)
{
	int i, p, q;
	char *tmp, **cubes;
	int *lm[26];
	char chal_cube[] = "iklmqu";	/* challenge cube */
	static char *cubes4[] = {
		"ednosw", "aaciot", "acelrs", "ehinps",
		"eefhiy", "elpstu", "acdemp", "gilruw",
		"egkluy", "ahmors", "abilty", "adenvz",
		"bfiorx", "dknotu", "abjmoq", "egintv"
	};
	static char *cubes5[] = {
		"aaafrs", "aaeeee", "aafirs", "adennn", "aeeeem",
		"aeegmu", "aegmnn", "afirsy", "bjkqxz", "ccnstw",
		"ceiilt", "ceilpt", "ceipst", "ddlnor", "dhhlor",
		"dhhnot", "dhlnor", "eiiitt", "emottt", "ensssu",
		"fiprsy", "gorrvw", "hiprry", "nootuw", "ooottu"
	};

	cubes = grid == 4 ? cubes4 : cubes5;
	if (b == NULL) {
		/* Shuffle the cubes using Fisher-Yates (aka Knuth P). */
		p = ncubes;
		while (--p) {
			q = (int)arc4random_uniform(p + 1);
			tmp = cubes[p];
			cubes[p] = cubes[q];
			cubes[q] = tmp;
		}

		/* Build the board by rolling each cube. */
		for (i = 0; i < ncubes; i++)
			board[i] = cubes[i][arc4random_uniform(6)];

		/*
		 * For challenge mode, roll chal_cube and replace a random
		 * cube with its value.  Set the high bit to distinguish it.
		 */
		if (challenge) {
			i = arc4random_uniform(ncubes);
			board[i] = SETHI(chal_cube[arc4random_uniform(6)]);
		}
	} else {
		for (i = 0; i < ncubes; i++)
			board[i] = b[i];
	}
	board[ncubes] = '\0';

	/*
	 * Set up the map from letter to location(s)
	 * Each list is terminated by a -1 entry
	 */
	for (i = 0; i < 26; i++) {
		lm[i] = letter_map[i];
		*lm[i] = -1;
	}

	for (i = 0; i < ncubes; i++) {
		int j;

		j = (int) (SEVENBIT(board[i]) - 'a');
		*lm[j] = i;
		*(++lm[j]) = -1;
	}

	if (debug) {
		for (i = 0; i < 26; i++) {
			int ch, j;

			(void) printf("%c:", 'a' + i);
			for (j = 0; (ch = letter_map[i][j]) != -1; j++)
				(void) printf(" %d", ch);
			(void) printf("\n");
		}
	}

}

static int
compar(const void *p, const void *q)
{
	return (strcmp(*(char **)p, *(char **)q));
}

/*
 * Allocate and initialize data structures.
 */
static void
init(void)
{
	int i;

	if (minlength == -1)
		minlength = grid - 1;
	init_adjacencies();
	board = malloc(ncubes + 1);
	if (board == NULL)
		err(1, NULL);
	letter_map = calloc(26, sizeof(int *));
	if (letter_map == NULL)
		err(1, NULL);
	for (i = 0; i < 26; i++) {
		letter_map[i] = calloc(ncubes, sizeof(int));
		if (letter_map[i] == NULL)
			err(1, NULL);
	}
	pword = calloc(maxpwords, sizeof(char *));
	if (pword == NULL)
		err(1, NULL);
	pwords = malloc(maxpspace);
	if (pwords == NULL)
		err(1, NULL);
	mword = calloc(maxmwords, sizeof(char *));
	if (mword == NULL)
		err(1, NULL);
	mwords = malloc(maxmspace);
	if (mwords == NULL)
		err(1, NULL);
}

#define SET_ADJ(r) do {							\
	if (col > 0)							\
		adj[r - 1] = 1;						\
	adj[r] = 1;							\
	if (col + 1 < grid)						\
		adj[r + 1] = 1;						\
} while(0)

/*
 * Compute adjacency matrix for the grid
 */
static void
init_adjacencies(void)
{
	int cube, row, col, *adj;

	adjacency = calloc(ncubes, sizeof(int *));
	if (adjacency == NULL)
		err(1, NULL);

	/*
	 * Fill in adjacencies.  This is an ncubes x ncubes matrix where
	 * the position X,Y is set to 1 if cubes X and Y are adjacent.
	 */
	for (cube = 0; cube < ncubes; cube++) {
		adj = adjacency[cube] = calloc(ncubes, sizeof(int));
		if (adj == NULL)
			err(1, NULL);

		row = cube / grid;
		col = cube % grid;
	     
		/* this row */
		SET_ADJ(cube);
		if (!selfuse)
			adj[cube] = 0;

		/* prev row */
		if (row > 0)
			SET_ADJ(cube - grid);

		/* next row */
		if (row + 1 < grid)
			SET_ADJ(cube + grid);
	}
}

void
usage(void)
{
	extern char *__progname;

	(void) fprintf(stderr, "usage: "
	    "%s [-Bbcd] [-t time] [-w length] [+[+]] [boardspec]\n",
	    __progname);
	exit(1);
}
