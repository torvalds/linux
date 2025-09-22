/*	$OpenBSD: snake.c,v 1.34 2019/06/28 13:32:52 deraadt Exp $	*/
/*	$NetBSD: snake.c,v 1.8 1995/04/29 00:06:41 mycroft Exp $	*/

/*
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

/*
 * snake - crt hack game.
 *
 * You move around the screen with arrow keys trying to pick up money
 * without getting eaten by the snake.  hjkl work as in vi in place of
 * arrow keys.  You can leave at the exit any time.
 */

#include <curses.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifdef	DEBUG
#define	cashvalue	(loot-penalty)/25
#else
#define	cashvalue	chunk*(loot-penalty)/25
#endif

struct point {
	int col, line;
};

#define	same(s1, s2)	((s1)->line == (s2)->line && (s1)->col == (s2)->col)

#define PENALTY	10	/* % penalty for invoking spacewarp     */

#define ME		'I'
#define SNAKEHEAD	'S'
#define SNAKETAIL	's'
#define TREASURE	'$'
#define GOAL		'#'

#define TOPN	10	/* top scores to print if you lose */
#define SCORES_ENTRIES (TOPN + 1)

#define pchar(point, c)	mvaddch((point)->line + 1, (point)->col + 1, (c))
/* Can't use terminal timing to do delay, in light of X */
#define delay(t)	usleep((t) * 50000)
/* Delay units are 1/20 s */

struct point you;
struct point money;
struct point finish;
struct point snake[6];

int	 lcnt, ccnt;	/* user's idea of screen size */
int	 chunk;		/* amount of money given at a time */
int	 loot, penalty;
int	 moves;
int	 fast = 1;

struct highscore {
	char	name[LOGIN_NAME_MAX];
	short	score;
} scores[SCORES_ENTRIES];
int	 nscores;

char	 scorepath[PATH_MAX];
FILE	*sf;
int	 rawscores;

#ifdef LOGGING
FILE	*logfile;
char	 logpath[PATH_MAX];
#endif

void	chase(struct point *, struct point *);
int	chk(struct point *);
void	drawbox(void);
void	length(int);
void	mainloop(void);
int	post(int, int);
int	pushsnake(void);
int	readscores(int);
void	setup(void);
void	snap(void);
void	snrand(struct point *);
void	snscore(int);
void	spacewarp(int);
void	stop(int);
int	stretch(struct point *);
void	surround(struct point *);
void	suspend(void);
void	win(struct point *);
void	winnings(int);

#ifdef LOGGING
void	logit(char *);
#endif

int	wantstop;

int
main(int argc, char *argv[])
{
	struct	sigaction sa;
	int	ch, i;

#ifdef LOGGING
	const char	*home;

	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		err(1, "getenv");

	snprintf(logpath, sizeof(logpath), "%s/%s", home, ".snake.log");
	logfile = fopen(logpath, "a");
#endif

	while ((ch = getopt(argc, argv, "hl:stw:")) != -1)
		switch ((char)ch) {
		case 'w':	/* width */
			ccnt = strtonum(optarg, 1, INT_MAX, NULL);
			break;
		case 'l':	/* length */
			lcnt = strtonum(optarg, 1, INT_MAX, NULL);
			break;
		case 's': /* score */
			if (readscores(0))
				snscore(0);
			else
				printf("no scores so far\n");
			return 0;
			break;
		case 't': /* slow terminal */
			fast = 0;
			break;
		case 'h':
		default:
			fprintf(stderr, "usage: %s [-st] [-l length] "
			    "[-w width]\n", getprogname());
			return 1;
		}

	readscores(1);
	penalty = loot = 0;
	initscr();

	if (pledge("stdio tty", NULL) == -1)
		err(1, "pledge");

#ifdef KEY_LEFT
	keypad(stdscr, TRUE);
#endif
	nonl();
	cbreak();
	noecho();

	if (!lcnt || lcnt > LINES - 2)
		lcnt = LINES - 2;
	if (!ccnt || ccnt > COLS - 3)
		ccnt = COLS - 3;

	i = lcnt < ccnt ? lcnt : ccnt;
	if (i < 4) {
		endwin();
		errx(1, "screen too small for a fair game.");
	}
	/*
	 * chunk is the amount of money the user gets for each $.
	 * The formula below tries to be fair for various screen sizes.
	 * We only pay attention to the smaller of the 2 edges, since
	 * that seems to be the bottleneck.
	 * This formula is a hyperbola which includes the following points:
	 *	(24, $25)	(original scoring algorithm)
	 *	(12, $40)	(experimentally derived by the "feel")
	 *	(48, $15)	(a guess)
	 * This will give a 4x4 screen $99/shot.  We don't allow anything
	 * smaller than 4x4 because there is a 3x3 game where you can win
	 * an infinite amount of money.
	 */
	if (i < 12)
		i = 12;	/* otherwise it isn't fair */
	/*
	 * Compensate for border.  This really changes the game since
	 * the screen is two squares smaller but we want the default
	 * to be $25, and the high scores on small screens were a bit
	 * much anyway.
	 */
	i += 2;
	chunk = (675.0 / (i + 6)) + 2.5;	/* min screen edge */

	memset(&sa, 0, sizeof sa);
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = stop;
	sigaction(SIGINT, &sa, NULL);

	snrand(&finish);
	snrand(&you);
	snrand(&money);
	snrand(&snake[0]);

	for (i = 1; i < 6; i++)
		chase(&snake[i], &snake[i - 1]);
	setup();
	mainloop();
	return 0;
}

/* Main command loop */
void
mainloop(void)
{
	int	k;
	int	c, lastc = 0;
	int repeat = 1;

	for (;;) {
		if (wantstop) {
			endwin();
			length(moves);
			exit(0);
		}

		/* Highlight you, not left & above */
		move(you.line + 1, you.col + 1);
		refresh();
		if (((c = getch()) <= '9') && (c >= '0')) {
			repeat = c - '0';
			while (((c = getch()) <= '9') && (c >= '0'))
				repeat = 10 * repeat + (c - '0');
		} else {
			if (c != '.')
				repeat = 1;
		}
		if (c == '.')
			c = lastc;
		if (!fast)
			flushinp();
		lastc = c;

		switch (c) {
		case CTRL('z'):
			suspend();
			continue;
		case '\044':
		case 'x':
		case 0177:	/* del or end of file */
		case ERR:
			endwin();
			length(moves);
#ifdef LOGGING
			logit("quit");
#endif
			exit(0);
		case CTRL('l'):
			setup();
			winnings(cashvalue);
			continue;
		case 'p':
		case 'd':
			snap();
			continue;
		case 'w':
			spacewarp(0);
			continue;
		case 'A':
			repeat = you.col;
			c = 'h';
			break;
		case 'H':
		case 'S':
			repeat = you.col - money.col;
			c = 'h';
			break;
		case 'T':
			repeat = you.line;
			c = 'k';
			break;
		case 'K':
		case 'E':
			repeat = you.line - money.line;
			c = 'k';
			break;
		case 'P':
			repeat = ccnt - 1 - you.col;
			c = 'l';
			break;
		case 'L':
		case 'F':
			repeat = money.col - you.col;
			c = 'l';
			break;
		case 'B':
			repeat = lcnt - 1 - you.line;
			c = 'j';
			break;
		case 'J':
		case 'C':
			repeat = money.line - you.line;
			c = 'j';
			break;
		}
		for (k = 1; k <= repeat; k++) {
			moves++;
			switch (c) {
			case 's':
			case 'h':
#ifdef KEY_LEFT
			case KEY_LEFT:
#endif
			case '\b':
				if (you.col > 0) {
					if ((fast) || (k == 1))
						pchar(&you, ' ');
					you.col--;
					if ((fast) || (k == repeat) ||
					    (you.col == 0))
						pchar(&you, ME);
				}
				break;
			case 'f':
			case 'l':
#ifdef KEY_RIGHT
			case KEY_RIGHT:
#endif
			case ' ':
				if (you.col < ccnt - 1) {
					if ((fast) || (k == 1))
						pchar(&you, ' ');
					you.col++;
					if ((fast) || (k == repeat) ||
					    (you.col == ccnt - 1))
						pchar(&you, ME);
				}
				break;
			case CTRL('p'):
			case 'e':
			case 'k':
#ifdef KEY_UP
			case KEY_UP:
#endif
			case 'i':
				if (you.line > 0) {
					if ((fast) || (k == 1))
						pchar(&you, ' ');
					you.line--;
					if ((fast) || (k == repeat) ||
					    (you.line == 0))
						pchar(&you, ME);
				}
				break;
			case CTRL('n'):
			case 'c':
			case 'j':
#ifdef KEY_DOWN
			case KEY_DOWN:
#endif
			case '\n':
			case '\r':
			case 'm':
				if (you.line + 1 < lcnt) {
					if ((fast) || (k == 1))
						pchar(&you, ' ');
					you.line++;
					if ((fast) || (k == repeat) ||
					    (you.line == lcnt - 1))
						pchar(&you, ME);
				}
				break;
			}

			if (same(&you, &money)) {
				loot += 25;
				if (k < repeat)
					pchar(&you, ' ');
				do {
					snrand(&money);
				} while ((money.col == finish.col &&
				    money.line == finish.line) ||
				    (money.col < 5 && money.line == 0) ||
				    (money.col == you.col &&
				    money.line == you.line));
				pchar(&money, TREASURE);
				winnings(cashvalue);
/*				continue;		 Previously, snake missed a turn! */
			}
			if (same(&you, &finish)) {
				win(&finish);
				flushinp();
				endwin();
				printf("You have won with $%d.\n", cashvalue);
				fflush(stdout);
#ifdef LOGGING
				logit("won");
#endif
				length(moves);
				post(cashvalue, 1);
				close(rawscores);
				exit(0);
			}
			if (pushsnake())
				break;
		}
	}
}

/* set up the board */
void
setup(void)
{
	int	i;

	erase();
	pchar(&you, ME);
	pchar(&finish, GOAL);
	pchar(&money, TREASURE);
	for (i = 1; i < 6; i++) {
		pchar(&snake[i], SNAKETAIL);
	}
	pchar(&snake[0], SNAKEHEAD);
	drawbox();
	refresh();
}

void
drawbox(void)
{
	int i;

	for (i = 1; i <= ccnt; i++) {
		mvaddch(0, i, '-');
		mvaddch(lcnt + 1, i, '-');
	}
	for (i = 0; i <= lcnt + 1; i++) {
		mvaddch(i, 0, '|');
		mvaddch(i, ccnt + 1, '|');
	}
}

void
snrand(struct point *sp)
{
	struct point p;
	int i;

	for (;;) {
		p.col = arc4random_uniform(ccnt);
		p.line = arc4random_uniform(lcnt);

		/* make sure it's not on top of something else */
		if (p.line == 0 && p.col < 5)
			continue;
		if (same(&p, &you))
			continue;
		if (same(&p, &money))
			continue;
		if (same(&p, &finish))
			continue;
		for (i = 0; i < 6; i++)
			if (same(&p, &snake[i]))
				break;
		if (i < 6)
			continue;
		break;
	}
	*sp = p;
}

int
post(int iscore, int flag)
{
	struct  highscore tmp;
	int	rank = nscores;
	short	oldbest = 0;

	/* I want to printf() the scores for terms that clear on endwin(),
	 * but this routine also gets called with flag == 0 to see if
	 * the snake should wink.  If (flag) then we're at game end and
	 * can printf.
	 */
	if (flag == 0) {
		if (nscores > 0)
			return (iscore > scores[nscores - 1].score);
		else
			return (iscore > 0);
	}

	if (nscores > 0) {
		oldbest = scores[0].score;
		scores[nscores].score = iscore;
		if (nscores < TOPN)
			nscores++;
	} else {
		nscores = 1;
		scores[0].score = iscore;
		oldbest = 0;
	}

	/* Insert this joker's current score */
	while (rank-- > 0 && iscore > scores[rank].score) {
		memcpy(&tmp, &scores[rank], sizeof(struct highscore));
		memcpy(&scores[rank], &scores[rank + 1],
		    sizeof(struct highscore));
		memcpy(&scores[rank + 1], &tmp, sizeof(struct highscore));
	}

	if (rank++ < 0)
		printf("\nYou bettered your previous best of $%d\n", oldbest);
	else if (rank < nscores)
		printf("\nYour score of $%d is ranked %d of all times!\n",
		    iscore, rank + 1);

	if (fseek(sf, 0L, SEEK_SET) == -1)
		err(1, "fseek");
	if (fwrite(scores, sizeof(scores[0]), nscores, sf) < (u_int)nscores)
		err(1, "fwrite");
	if (fclose(sf))
		err(1, "fclose");

	/* See if we have a new champ */
	snscore(TOPN);
	return(1);
}

const int	mx[8] = { 0, 1, 1, 1, 0,-1,-1,-1};
const int	my[8] = {-1,-1, 0, 1, 1, 1, 0,-1};
const float	absv[8] = {1, 1.4, 1, 1.4, 1, 1.4, 1, 1.4};
int	oldw = 0;

void
chase(struct point *np, struct point *sp)
{
	/* this algorithm has bugs; otherwise the snake would get too good */
	struct point d;
	int	w, i, wt[8];
	double	v1, v2, vp, max;

	d.col = you.col-sp->col;
	d.line = you.line-sp->line;
	v1 = sqrt((double)(d.col * d.col + d.line * d.line) );
	w  = 0;
	max = 0;
	for (i = 0; i < 8; i++) {
		vp = d.col * mx[i] + d.line * my[i];
		v2 = absv[i];
		if (v1 > 0)
			vp = ((double)vp) / (v1 * v2);
		else
			vp = 1.0;
		if (vp > max) {
			max = vp;
			w = i;
		}
	}
	for (i = 0; i < 8; i++) {
		d.col = sp->col + mx[i];
		d.line = sp->line + my[i];
		wt[i] = 0;
		if (d.col < 0 || d.col >= ccnt || d.line < 0 || d.line >= lcnt)
			continue;
		/*
		 * Change to allow snake to eat you if you're on the money;
		 * otherwise, you can just crouch there until the snake goes
		 * away.  Not positive it's right.
		 *
		 * if (d.line == 0 && d.col < 5) continue;
		 */
		if (same(&d, &money) || same(&d,&finish))
			continue;
		wt[i] = (i == w ? loot/10 : 1);
		if (i == oldw)
			wt[i] += loot/20;
	}
	for (w = i = 0; i < 8; i++)
		w += wt[i];
	vp = arc4random_uniform(w);
	for (i = 0; i < 8; i++)
		if (vp < wt[i])
			break;
		else
			vp -= wt[i];
	if (i == 8) {
		printw("failure\n");
		i = 0;
		while (wt[i] == 0)
			i++;
	}
	oldw = w = i;
	np->col = sp->col + mx[w];
	np->line = sp->line + my[w];
}

void
spacewarp(int w)
{
	struct point p;
	int	j;
	const char  *str;

	snrand(&you);
	p.col = COLS / 2 - 8;
	p.line = LINES / 2 - 1;
	if (p.col < 0)
		p.col = 0;
	if (p.line < 0)
		p.line = 0;
	if (w) {
		str = "BONUS!!!";
		loot = loot - penalty;
		penalty = 0;
	} else {
		str = "SPACE WARP!!!";
		penalty += loot / PENALTY;
	}
	for (j = 0; j < 3; j++) {
		erase();
		refresh();
		delay(5);
		mvaddstr(p.line + 1, p.col + 1, str);
		mvaddstr(0, 0, "");
		refresh();
		delay(10);
	}
	setup();
	winnings(cashvalue);
}

void
snap(void)
{

	if (!stretch(&money))
		if (!stretch(&finish)) {
			pchar(&you, '?');
			refresh();
			delay(10);
			pchar(&you, ME);
		}
	refresh();
}

int
stretch(struct point *ps)
{
	struct point p;

	p.col = you.col;
	p.line = you.line;
	if ((abs(ps->col - you.col) < (ccnt / 12)) && (you.line != ps->line)) {
		if (you.line < ps->line) {
			for (p.line = you.line + 1; p.line <= ps->line; p.line++)
				pchar(&p, 'v');
			refresh();
			delay(10);
			for (; p.line > you.line; p.line--)
				chk(&p);
		} else {
			for (p.line = you.line - 1; p.line >= ps->line; p.line--)
				pchar(&p, '^');
			refresh();
			delay(10);
			for (; p.line < you.line; p.line++)
				chk(&p);
		}
		return(1);
	} else if ((abs(ps->line - you.line) < (lcnt / 7)) && (you.col != ps->col)) {
		p.line = you.line;
		if (you.col < ps->col) {
			for (p.col = you.col + 1; p.col <= ps->col; p.col++)
				pchar(&p, '>');
			refresh();
			delay(10);
			for (; p.col > you.col; p.col--)
				chk(&p);
		} else {
			for (p.col = you.col - 1; p.col >= ps->col; p.col--)
				pchar(&p, '<');
			refresh();
			delay(10);
			for (; p.col < you.col; p.col++)
				chk(&p);
		}
		return(1);
	}
	return(0);
}

void
surround(struct point *ps)
{
	int	j;

	if (ps->col == 0)
		ps->col++;
	if (ps->line == 0)
		ps->line++;
	if (ps->line == LINES - 1)
		ps->line--;
	if (ps->col == COLS - 1)
		ps->col--;
	mvaddstr(ps->line, ps->col, "/*\\");
	mvaddstr(ps->line + 1, ps->col, "* *");
	mvaddstr(ps->line + 2, ps->col, "\\*/");
	for (j = 0; j < 20; j++) {
		pchar(ps, '@');
		refresh();
		delay(1);
		pchar(ps, ' ');
		refresh();
		delay(1);
	}
	if (post(cashvalue, 0)) {
		mvaddstr(ps->line, ps->col, "   ");
		mvaddstr(ps->line + 1, ps->col, "o.o");
		mvaddstr(ps->line + 2, ps->col, "\\_/");
		refresh();
		delay(6);
		mvaddstr(ps->line, ps->col, "   ");
		mvaddstr(ps->line + 1, ps->col, "o.-");
		mvaddstr(ps->line + 2, ps->col, "\\_/");
		refresh();
		delay(6);
	}
	mvaddstr(ps->line, ps->col, "   ");
	mvaddstr(ps->line + 1, ps->col, "o.o");
	mvaddstr(ps->line + 2, ps->col, "\\_/");
	refresh();
	delay(6);
}

void
win(struct point *ps)
{
	struct point x;
	int	j, k;
	int	boxsize;	/* actually diameter of box, not radius */

	boxsize = (fast ? 10 : 4);
	x.col = ps->col;
	x.line = ps->line;
	for (j = 1; j < boxsize; j++) {
		for (k = 0; k < j; k++) {
			pchar(&x, '#');
			x.line--;
		}
		for (k = 0; k < j; k++) {
			pchar(&x, '#');
			x.col++;
		}
		j++;
		for (k = 0; k < j; k++) {
			pchar(&x, '#');
			x.line++;
		}
		for (k = 0; k < j; k++) {
			pchar(&x, '#');
			x.col--;
		}
		refresh();
		delay(1);
	}
}

int
pushsnake(void)
{
	int	i, bonus;
	int	issame = 0;
	struct point tmp;

	for (i = 4; i >= 0; i--)
		if (same(&snake[i], &snake[5]))
			issame++;
	if (!issame) {
		char sp = ' ';
		if (same(&money, &snake[5]))
			sp = TREASURE;
		pchar(&snake[5], sp);
	}
	/* Need the following to catch you if you step on the snake's tail */
	tmp.col = snake[5].col;
	tmp.line = snake[5].line;
	for (i = 4; i >= 0; i--)
		snake[i + 1] = snake[i];
	chase(&snake[0], &snake[1]);
	pchar(&snake[1], SNAKETAIL);
	pchar(&snake[0], SNAKEHEAD);
	for (i = 0; i < 6; i++) {
		if ((same(&snake[i], &you)) || (same(&tmp, &you))) {
			surround(&you);
			i = (cashvalue) % 10;
			bonus = arc4random_uniform(10);
			refresh();
			delay(30);
			if (bonus == i) {
				spacewarp(1);
#ifdef LOGGING
				logit("bonus");
#endif
				flushinp();
				return(1);
			}
			flushinp();
			endwin();
			if (loot >= penalty) {
				printf("\nYou and your $%d have been eaten\n", cashvalue);
			} else {
				printf("\nThe snake ate you.  You owe $%d.\n", -cashvalue);
			}
#ifdef LOGGING
			logit("eaten");
#endif
			length(moves);
			snscore(TOPN);
			close(rawscores);
			exit(0);
		}
	}
	return(0);
}

int
chk(struct point *sp)
{
	int	j;

	if (same(sp, &money)) {
		pchar(sp, TREASURE);
		return(2);
	}
	if (same(sp, &finish)) {
		pchar(sp, GOAL);
		return(3);
	}
	if (same(sp, &snake[0])) {
		pchar(sp, SNAKEHEAD);
		return(4);
	}
	for (j = 1; j < 6; j++) {
		if (same(sp, &snake[j])) {
			pchar(sp, SNAKETAIL);
			return(4);
		}
	}
	if ((sp->col < 4) && (sp->line == 0)) {
		winnings(cashvalue);
		if ((you.line == 0) && (you.col < 4))
			pchar(&you, ME);
		return(5);
	}
	if (same(sp, &you)) {
		pchar(sp, ME);
		return(1);
	}
	pchar(sp, ' ');
	return(0);
}

void
winnings(int won)
{
	if (won > 0)
		mvprintw(1, 1, "$%d  ", won);
}

void
stop(int dummy)
{
	wantstop = 1;
}

void
suspend(void)
{
	endwin();
	kill(getpid(), SIGTSTP);
	refresh();
	winnings(cashvalue);
}

void
length(int num)
{
	printf("You made %d moves.\n", num);
}

void
snscore(int topn)
{
	int i;

	if (nscores == 0)
		return;

	printf("%sSnake scores to date:\n", topn > 0 ? "Top " : "");
	for (i = 0; i < nscores; i++)
		printf("%2d.\t$%d\t%s\n", i+1, scores[i].score, scores[i].name);
}

#ifdef LOGGING
void
logit(char *msg)
{
	time_t t;

	if (logfile != NULL) {
		time(&t);
		fprintf(logfile, "%s $%d %dx%d %s %s",
		    getlogin(), cashvalue, lcnt, ccnt, msg, ctime(&t));
		fflush(logfile);
	}
}
#endif

int
readscores(int create)
{
	const char	*home;
	const char	*name;
	const char	*modstr;
	int		 modint;
	int		 ret;

	if (create == 0) {
		modint = O_RDONLY;
		modstr = "r";
	} else {
		modint = O_RDWR | O_CREAT;
		modstr = "r+";
	}

	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		err(1, "getenv");

	ret = snprintf(scorepath, sizeof(scorepath), "%s/%s", home,
	    ".snake.scores");
	if (ret < 0 || ret >= PATH_MAX)
		errc(1, ENAMETOOLONG, "%s/%s", home, ".snake.scores");

	rawscores = open(scorepath, modint, 0666);
	if (rawscores == -1) {
		if (create == 0)
			return 0;
		err(1, "cannot open %s", scorepath);
	}
	if ((sf = fdopen(rawscores, modstr)) == NULL)
		err(1, "cannot fdopen %s", scorepath);
	nscores = fread(scores, sizeof(scores[0]), TOPN, sf);
	if (ferror(sf))
		err(1, "error reading %s", scorepath);

	name = getenv("LOGNAME");
	if (name == NULL || *name == '\0')
		name = getenv("USER");
	if (name == NULL || *name == '\0')
		name = getlogin();
	if (name == NULL || *name == '\0')
		name = "  ???";

	if (nscores > TOPN)
		nscores = TOPN;
	strlcpy(scores[nscores].name, name, sizeof(scores[nscores].name));
	scores[nscores].score = 0;

	return 1;
}
