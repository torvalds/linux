/*	$OpenBSD: bs.c,v 1.42 2021/10/23 11:22:48 mestre Exp $	*/
/*
 * Copyright (c) 1986, Bruce Holloway
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * - Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * - Neither the name of the <ORGANIZATION> nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * bs.c - original author: Bruce Holloway
 *		salvo option by: Chuck A DeGaul
 * with improved user interface, autoconfiguration and code cleanup
 *		by Eric S. Raymond <esr@snark.thyrsus.com>
 * v1.2 with color support and minor portability fixes, November 1990
 * v2.0 featuring strict ANSI/POSIX conformance, November 1993.
 * v2.1 with ncurses mouse support, September 1995
 * v2.2 with bugfixes and strategical improvements, March 1998.
 */

#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
	char *name;		/* name of the ship type */
	char hits;		/* how many times has this ship been hit? */
	char symbol;		/* symbol for game purposes */
	char length;		/* length of ship */
	signed char x, y;	/* coordinates of ship start point */
	unsigned char dir;	/* direction of `bow' */
	bool placed;		/* has it been placed on the board? */
} ship_t;

static void	 announceopts(void);
static int	 awinna(void);
static bool	 checkplace(int, ship_t *, int);
static int	 collidecheck(int, int, int);
static int	 cpufire(int, int);
static bool	 cpushipcanfit(int, int, int, int);
static int	 cputurn(void);
static void	 do_options(int, char *[]);
static void	 error(char *);
static int	 getcoord(int);
static ship_t	*hitship(int, int);
static void	 initgame(void);
static void	 intro(void);
static void	 placeship(int, ship_t *, int);
static int	 playagain(void);
static int	 plyturn(void);
static void	 prompt(int, const char *, ...)
    __attribute__((__format__ (printf, 2, 3)));
static void	 randomfire(int *, int *);
static void	 randomplace(int, ship_t *);
static int	 rnd(int);
static int	 scount(int);
static int	 sgetc(char *);
__dead static void	 uninitgame(int);
__dead void	 usage(void);

/*
 * Constants for tuning the random-fire algorithm. It prefers moves that
 * diagonal-stripe the board with a stripe separation of srchstep. If
 * no such preferred moves are found, srchstep is decremented.
 */
#define BEGINSTEP	3	/* initial value of srchstep */

/* miscellaneous constants */
#define SHIPTYPES	5
#define OTHER		(1-turn)
#define PLAYER		0
#define COMPUTER	1
#define MARK_HIT	'H'
#define MARK_MISS	'o'
#define CTRLC		'\003'	/* used as terminate command */
#define FF		'\014'	/* used as redraw command */

/* coordinate handling */
#define BWIDTH		10
#define BDEPTH		10

/* display symbols */
#define SHOWHIT		'*'
#define SHOWSPLASH	' '
#define IS_SHIP(c)	isupper(c)

/* how to position us on player board */
#define PYBASE	3
#define PXBASE	3
#define PY(y)	(PYBASE + (y))
#define PX(x)	(PXBASE + (x)*3)
#define pgoto(y, x)	(void)move(PY(y), PX(x))

/* how to position us on cpu board */
#define CYBASE	3
#define CXBASE	48
#define CY(y)	(CYBASE + (y))
#define CX(x)	(CXBASE + (x)*3)
#define CYINV(y)	((y) - CYBASE)
#define CXINV(x)	(((x) - CXBASE) / 3)
#define cgoto(y, x)	(void)move(CY(y), CX(x))

#define ONBOARD(x, y)	(x >= 0 && x < BWIDTH && y >= 0 && y < BDEPTH)

/* other board locations */
#define COLWIDTH	80
#define PROMPTLINE	21			/* prompt line */
#define SYBASE		CYBASE + BDEPTH + 3	/* move key diagram */
#define SXBASE		63
#define MYBASE		SYBASE - 1		/* diagram caption */
#define MXBASE		64
#define HYBASE		SYBASE - 1		/* help area */
#define HXBASE		0

/* this will need to be changed if BWIDTH changes */
static char numbers[] = "   0  1  2  3  4  5  6  7  8  9";

static char carrier[] = "Aircraft Carrier";
static char battle[] = "Battleship";
static char sub[] = "Submarine";
static char destroy[] = "Destroyer";
static char ptboat[] = "PT Boat";

static char name[LOGIN_NAME_MAX];
static char dftname[] = "stranger";

/* direction constants */
#define E	0
#define SE	1
#define S	2
#define SW	3
#define W	4
#define NW	5
#define N	6
#define NE	7
static int xincr[8] = { 1,  1,  0, -1, -1, -1,  0,  1 };
static int yincr[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };

/* current ship position and direction */
static int curx = (BWIDTH / 2);
static int cury = (BDEPTH / 2);

ship_t plyship[SHIPTYPES] =
{
	{ carrier,	0, 'A', 5, 0, 0, 0, FALSE },
	{ battle,	0, 'B', 4, 0, 0, 0, FALSE },
	{ destroy,	0, 'D', 3, 0, 0, 0, FALSE },
	{ sub,		0, 'S', 3, 0, 0, 0, FALSE },
	{ ptboat,	0, 'P', 2, 0, 0, 0, FALSE }
};

ship_t cpuship[SHIPTYPES] =
{
	{ carrier,	0, 'A', 5, 0, 0, 0, FALSE },
	{ battle,	0, 'B', 4, 0, 0, 0, FALSE },
	{ destroy,	0, 'D', 3, 0, 0, 0, FALSE },
	{ sub,		0, 'S', 3, 0, 0, 0, FALSE },
	{ ptboat,	0, 'P', 2, 0, 0, 0, FALSE }
};

/* The following variables (and associated defines), used for computer 
 * targetting, must be global so that they can be reset for each new game 
 * played without restarting the program.
 */
#define POSSIBLE(x, y)	(ONBOARD(x, y) && !hits[COMPUTER][x][y])
#define RANDOM_FIRE	0
#define RANDOM_HIT	1
#define HUNT_DIRECT	2
#define FIRST_PASS	3
#define REVERSE_JUMP	4
#define SECOND_PASS	5
    static int next = RANDOM_FIRE;
    static int turncount = 0;
    static int srchstep = BEGINSTEP;
/* Computer needs to keep track of longest and shortest player ships still
 * not sunk, for better targetting. 
 */
static int cpushortest;
static int cpulongest;

/* "Hits" board, and main board. */
static char hits[2][BWIDTH][BDEPTH], board[2][BWIDTH][BDEPTH];

static int turn;			/* 0=player, 1=computer */
static int plywon=0, cpuwon=0;		/* How many games has each won? */

static int salvo, blitz, closepack;

/* end the game, either normally or due to signal */
static void
uninitgame(int sig)
{
    clear();
    (void)refresh();
    (void)resetterm();
    (void)echo();
    (void)endwin();
    exit(sig);
}

/* announce which game options are enabled */
static void
announceopts(void)
{
    if (salvo || blitz || closepack)
    {
	(void) printw("Playing optional game (");
	if (salvo)
	    (void) printw("salvo, ");
	else
	    (void) printw("nosalvo, ");
	if (blitz)
	    (void) printw("blitz ");
	else
	    (void) printw("noblitz, ");
	if (closepack)
	    (void) printw("closepack)");
	else
	    (void) printw("noclosepack)");
    }
    else
	(void) printw(
	"Playing standard game (noblitz, nosalvo, noclosepack)");
}

static void
intro(void)
{
    char *tmpname;

    (void) signal(SIGINT,uninitgame);
    if(signal(SIGQUIT,SIG_IGN) != SIG_IGN)
	(void)signal(SIGQUIT,uninitgame);

    if ((tmpname = getlogin()) != NULL)
    {
	(void)strlcpy(name, tmpname, sizeof(name));
	name[0] = toupper((unsigned char)name[0]);
    }
    else
	(void)strlcpy(name, dftname, sizeof(name));

    (void)initscr();
    keypad(stdscr, TRUE);
    (void)saveterm();
    (void)nonl();
    (void)cbreak();
    (void)noecho();

    if ((LINES < PROMPTLINE + 3) || (COLS < COLWIDTH)) {
	endwin();
	errx(1, "screen must be at least %dx%d.", PROMPTLINE + 3, COLWIDTH);
    }

#define	PR	(void)addstr
    (void)clear();
    (void)mvaddstr(4,29,"Welcome to Battleship!");
    (void)move(8,0);
    PR("                                                  \\\n");
    PR("                           \\                     \\ \\\n");
    PR("                          \\ \\                   \\ \\ \\_____________\n");
    PR("                         \\ \\ \\_____________      \\ \\/            |\n");
    PR("                          \\ \\/     \\__/    \\      \\/             |\n");
    PR("                           \\/     \\/  \\/    \\_____/              |__\n");
    PR("           ________________/    /\\/  ..\\/                         |\n");
    PR("           \\  S.S. Puffy        \\/\\___o/                          |\n");
    PR("            \\                     / /\\ \\                          /\n");
    PR("             \\___________________________________________________/\n");

    (void) mvaddstr(22,27,"Hit any key to continue..."); (void)refresh();
    (void) getch();

    start_color();

    init_pair(COLOR_BLACK, COLOR_BLACK, COLOR_BLACK);
    init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
    init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);

    (void) mousemask(BUTTON1_CLICKED, (mmask_t *)NULL);
}

/* print a message at the prompt line */
static void
prompt(int n, const char *f, ...)
{
    va_list va;

    (void) move(PROMPTLINE + n, 0);
    (void) clrtoeol();
    va_start(va, f);
    (void) vw_printw(stdscr, f, va);
    va_end(va);
    (void) refresh();
}

static void
error(char *s)
{
    (void) move(PROMPTLINE + 2, 0);
    (void) clrtoeol();
    if (s)
    {
	(void) addstr(s);
	(void) beep();
    }
}

static void
placeship(int b, ship_t *ss, int vis)
{
    int l;

    for(l = 0; l < ss->length; ++l)
    {
	int newx = ss->x + l * xincr[ss->dir];
	int newy = ss->y + l * yincr[ss->dir];

	board[b][newx][newy] = ss->symbol;
	if (vis)
	{
	    pgoto(newy, newx);
	    (void) addch((chtype)ss->symbol);
	}
    }
    ss->hits = 0;
}

static int
rnd(int n)
{
    return(arc4random_uniform(n));
}

/* generate a valid random ship placement into px,py */
static void
randomplace(int b, ship_t *ss)
{
    do {
	ss->dir = rnd(2) ? E : S;
	ss->x = rnd(BWIDTH - (ss->dir == E ? ss->length : 0));
	ss->y = rnd(BDEPTH - (ss->dir == S ? ss->length : 0));
    } while
	(!checkplace(b, ss, FALSE));
}

static void
initgame(void)
{
    int i, j, unplaced;
    ship_t *ss;

    (void) clear();
    (void) mvaddstr(0,35,"BATTLESHIPS");
    (void) move(PROMPTLINE + 2, 0);
    announceopts();

    /* Set up global CPU algorithm variables. */
    next = RANDOM_FIRE;
    turncount = 0;
    srchstep = BEGINSTEP;
    /* set up cpulongest and cpushortest (computer targetting variables) */
    cpushortest = cpulongest = cpuship->length;

    memset(board, 0, sizeof(char) * BWIDTH * BDEPTH * 2);
    memset(hits,  0, sizeof(char) * BWIDTH * BDEPTH * 2);
    for (i = 0; i < SHIPTYPES; i++)
    {
	ss = cpuship + i;
	ss->x = ss->y = ss->dir = ss->hits = 0;
	ss->placed = FALSE;
	ss = plyship + i;
	ss->x = ss->y = ss->dir = ss->hits = 0;
	ss->placed = FALSE;

     if (ss->length > cpulongest)
		cpulongest  = ss->length;
     if (ss->length < cpushortest)
		cpushortest = ss->length;
    }

    /* draw empty boards */
    (void) mvaddstr(PYBASE - 2, PXBASE + 5, "Main Board");
    (void) mvaddstr(PYBASE - 1, PXBASE - 3,numbers);
    for(i=0; i < BDEPTH; ++i)
    {
	(void) mvaddch(PYBASE + i, PXBASE - 3, (chtype)(i + 'A'));
	if (has_colors())
	    attron(COLOR_PAIR(COLOR_BLUE));
	(void) addch(' ');
	for (j = 0; j < BWIDTH; j++)
	    (void) addstr(" . ");
	attrset(0);
	(void) addch(' ');
	(void) addch((chtype)(i + 'A'));
    }
    (void) mvaddstr(PYBASE + BDEPTH, PXBASE - 3,numbers);
    (void) mvaddstr(CYBASE - 2, CXBASE + 7,"Hit/Miss Board");
    (void) mvaddstr(CYBASE - 1, CXBASE - 3, numbers);
    for(i=0; i < BDEPTH; ++i)
    {
	(void) mvaddch(CYBASE + i, CXBASE - 3, (chtype)(i + 'A'));
	if (has_colors())
	    attron(COLOR_PAIR(COLOR_BLUE));
	(void) addch(' ');
	for (j = 0; j < BWIDTH; j++)
	    (void) addstr(" . ");
	attrset(0);
	(void) addch(' ');
	(void) addch((chtype)(i + 'A'));
    }

    (void) mvaddstr(CYBASE + BDEPTH,CXBASE - 3,numbers);

    (void) mvprintw(HYBASE,  HXBASE,
		    "To position your ships: move the cursor to a spot, then");
    (void) mvprintw(HYBASE+1,HXBASE,
		    "type the first letter of a ship type to select it, then");
    (void) mvprintw(HYBASE+2,HXBASE,
		    "type a direction ([hjkl] or [4862]), indicating how the");
    (void) mvprintw(HYBASE+3,HXBASE,
		    "ship should be pointed. You may also type a ship letter");
    (void) mvprintw(HYBASE+4,HXBASE,
		    "followed by `r' to position it randomly, or type `R' to");
    (void) mvprintw(HYBASE+5,HXBASE,
		    "place all remaining ships randomly.");

    (void) mvaddstr(MYBASE,   MXBASE, "Aiming keys:");
    (void) mvaddstr(SYBASE,   SXBASE, "y k u    7 8 9");
    (void) mvaddstr(SYBASE+1, SXBASE, " \\|/      \\|/ ");
    (void) mvaddstr(SYBASE+2, SXBASE, "h-+-l    4-+-6");
    (void) mvaddstr(SYBASE+3, SXBASE, " /|\\      /|\\ ");
    (void) mvaddstr(SYBASE+4, SXBASE, "b j n    1 2 3");

    /* have the computer place ships */
    for(ss = cpuship; ss < cpuship + SHIPTYPES; ss++)
    {
	randomplace(COMPUTER, ss);
	placeship(COMPUTER, ss, FALSE);
    }

    ss = (ship_t *)NULL;
    do {
	char docked[SHIPTYPES + 2], *cp = docked;
	int c;

	/* figure which ships still wait to be placed */
	*cp++ = 'R';
	for (i = 0; i < SHIPTYPES; i++)
	    if (!plyship[i].placed)
		*cp++ = plyship[i].symbol;
	*cp = '\0';

	/* get a command letter */
	prompt(1, "Type one of [%s] to pick a ship.", docked+1);
	do {
	    c = getcoord(PLAYER);
	} while
	    (!strchr(docked, c));

	if (c == 'R')
	    (void) ungetch('R');
	else
	{
	    /* map that into the corresponding symbol */
	    for (ss = plyship; ss < plyship + SHIPTYPES; ss++)
		if (ss->symbol == c)
		    break;

	    prompt(1, "Type one of [hjklrR] to place your %s.", ss->name);
	    pgoto(cury, curx);
	}
regetchar:
	c = getch();
	switch (c) {
	case FF:
	    (void)clearok(stdscr, TRUE);
	    (void)refresh();
	    break;
	case 'r':
	    prompt(1, "Random-placing your %s", ss->name);
	    randomplace(PLAYER, ss);
	    placeship(PLAYER, ss, TRUE);
		error(NULL);
	    ss->placed = TRUE;
	    break;
	case 'R':
	    prompt(1, "Placing the rest of your fleet at random...");
	    for (ss = plyship; ss < plyship + SHIPTYPES; ss++)
		if (!ss->placed)
		{
		    randomplace(PLAYER, ss);
		    placeship(PLAYER, ss, TRUE);
		    ss->placed = TRUE;
		}
	    error(NULL);
	    break;

	case 'k': case 'j': case 'h': case 'l':
	case '8': case '2': case '4': case '6':
	case KEY_LEFT: case KEY_RIGHT: case KEY_UP: case KEY_DOWN:
	    ss->x = curx;
	    ss->y = cury;

	    switch(c)
	    {
	    case 'k': case '8': case KEY_UP: ss->dir = N; break;
	    case 'j': case '2': case KEY_DOWN: ss->dir = S; break;
	    case 'h': case '4': case KEY_LEFT: ss->dir = W; break;
	    case 'l': case '6': case KEY_RIGHT: ss->dir = E; break;
	    }

	    if (checkplace(PLAYER, ss, TRUE))
	    {
		placeship(PLAYER, ss, TRUE);
		error(NULL);
		ss->placed = TRUE;
	    }
	    break;
	default:
	    goto regetchar;
	}

	for (unplaced = i = 0; i < SHIPTYPES; i++)
	    unplaced += !plyship[i].placed;
    } while
	(unplaced);

    turn = rnd(2);

    (void) mvprintw(HYBASE,  HXBASE,
		    "To fire, move the cursor to your chosen aiming point   ");
    (void) mvprintw(HYBASE+1,  HXBASE,
		    "and strike any key other than a motion key.            ");
    (void) mvprintw(HYBASE+2,  HXBASE,
		    "                                                       ");
    (void) mvprintw(HYBASE+3,  HXBASE,
		    "                                                       ");
    (void) mvprintw(HYBASE+4,  HXBASE,
		    "                                                       ");
    (void) mvprintw(HYBASE+5,  HXBASE,
		    "                                                       ");

    (void) prompt(0, "Press any key to start...");
    (void) getch();
}

static int
getcoord(int atcpu)
{
    int ny, nx, c;

    if (atcpu)
	cgoto(cury,curx);
    else
	pgoto(cury, curx);
    (void)refresh();
    for (;;)
    {
	if (atcpu)
	{
	    (void) mvprintw(CYBASE + BDEPTH+1, CXBASE+11, "(%d, %c)", curx, 'A'+cury);
	    cgoto(cury, curx);
	}
	else
	{
	    (void) mvprintw(PYBASE + BDEPTH+1, PXBASE+11, "(%d, %c)", curx, 'A'+cury);
	    pgoto(cury, curx);
	}

	switch(c = getch())
	{
	case 'k': case '8':
	case KEY_UP:
	    ny = cury+BDEPTH-1; nx = curx;
	    break;
	case 'j': case '2':
	case KEY_DOWN:
	    ny = cury+1;        nx = curx;
	    break;
	case 'h': case '4':
	case KEY_LEFT:
	    ny = cury;          nx = curx+BWIDTH-1;
	    break;
	case 'l': case '6':
	case KEY_RIGHT:
	    ny = cury;          nx = curx+1;
	    break;
	case 'y': case '7':
	case KEY_A1:
	    ny = cury+BDEPTH-1; nx = curx+BWIDTH-1;
	    break;
	case 'b': case '1':
	case KEY_C1:
	    ny = cury+1;        nx = curx+BWIDTH-1;
	    break;
	case 'u': case '9':
	case KEY_A3:
	    ny = cury+BDEPTH-1; nx = curx+1;
	    break;
	case 'n': case '3':
	case KEY_C3:
	    ny = cury+1;        nx = curx+1;
	    break;
	case FF:
	    nx = curx; ny = cury;
	    (void)clearok(stdscr, TRUE);
	    (void)refresh();
	    break;
	case KEY_MOUSE:
	    {
		MEVENT	myevent;

		getmouse(&myevent);
		if (atcpu
			&& myevent.y >= CY(0) && myevent.y < CY(BDEPTH)
			&& myevent.x >= CX(0) && myevent.x < CX(BWIDTH))
		{
		    curx = CXINV(myevent.x);
		    cury = CYINV(myevent.y);
		    return(' ');
		}
		else
		    beep();
	    }
	    break;
	case ERR:
	    uninitgame(1);
	    break;
	default:
	    if (atcpu)
		(void) mvaddstr(CYBASE + BDEPTH + 1, CXBASE + 11, "      ");
	    else
		(void) mvaddstr(PYBASE + BDEPTH + 1, PXBASE + 11, "      ");
	    return(c);
	}

	curx = nx % BWIDTH;
	cury = ny % BDEPTH;
    }
}

/* is this location on the selected zboard adjacent to a ship? */
static int
collidecheck(int b, int y, int x)
{
    int	collide;

    /* anything on the square */
    if ((collide = IS_SHIP(board[b][x][y])) != 0)
	return(collide);

    /* anything on the neighbors */
    if (!closepack)
    {
	int i;

	for (i = 0; i < 8; i++)
	{
	    int xend, yend;

	    yend = y + yincr[i];
	    xend = x + xincr[i];
	    if (ONBOARD(xend, yend))
		collide += IS_SHIP(board[b][xend][yend]);
	}
    }
    return(collide);
}

static bool
checkplace(int b, ship_t *ss, int vis)
{
    int l, xend, yend;

    /* first, check for board edges */
    xend = ss->x + (ss->length - 1) * xincr[ss->dir];
    yend = ss->y + (ss->length - 1) * yincr[ss->dir];
    if (!ONBOARD(xend, yend))
    {
	if (vis)
	    switch(rnd(3))
	    {
	    case 0:
		error("Ship is hanging from the edge of the world");
		break;
	    case 1:
		error("Try fitting it on the board");
		break;
	    case 2:
		error("Figure I won't find it if you put it there?");
		break;
	    }
	return(FALSE);
    }

    for(l = 0; l < ss->length; ++l)
    {
	if(collidecheck(b, ss->y+l*yincr[ss->dir], ss->x+l*xincr[ss->dir]))
	{
	    if (vis)
		switch(rnd(3))
		{
		    case 0:
			error("There's already a ship there");
			break;
		    case 1:
			error("Collision alert!  Aaaaaagh!");
			break;
		    case 2:
			error("Er, Admiral, what about the other ship?");
			break;
		    }
	    return(FALSE);
	    }
	}
    return(TRUE);
}

static int
awinna(void)
{
    int i, j;
    ship_t *ss;

    for(i=0; i<2; ++i)
    {
	ss = (i) ? cpuship : plyship;
	for(j=0; j < SHIPTYPES; ++j, ++ss)
	    if(ss->length > ss->hits)
		break;
	if (j == SHIPTYPES)
	    return(OTHER);
    }
    return(-1);
}

/* register a hit on the targeted ship */
static ship_t *
hitship(int x, int y)
{
    ship_t *sb, *ss;
    char sym;
    int oldx, oldy;

    getyx(stdscr, oldy, oldx);
    sb = (turn) ? plyship : cpuship;
    if(!(sym = board[OTHER][x][y]))
	return((ship_t *)NULL);
    for(ss = sb; ss < sb + SHIPTYPES; ++ss)
	if(ss->symbol == sym)
	{
	    if (++ss->hits < ss->length)	/* still afloat? */
		return((ship_t *)NULL);
	    else				/* sunk! */
	    {
		int i, j;

		if (!closepack)
		    for (j = -1; j <= 1; j++)
		    {
			int bx = ss->x + j * xincr[(ss->dir + 2) % 8];
			int by = ss->y + j * yincr[(ss->dir + 2) % 8];

			for (i = -1; i <= ss->length; ++i)
			{
			    int x1, y1;

			    x1 = bx + i * xincr[ss->dir];
			    y1 = by + i * yincr[ss->dir];
			    if (ONBOARD(x1, y1))
			    {
				hits[turn][x1][y1] = MARK_MISS;
				if (turn == PLAYER)
				{
				    cgoto(y1, x1);
				    if (has_colors())
					attron(COLOR_PAIR(COLOR_GREEN));
				    (void)addch(MARK_MISS);
				    attrset(0);
				}
			    }
			}
		    }

		for (i = 0; i < ss->length; ++i)
		{
		    int x1 = ss->x + i * xincr[ss->dir];
		    int y1 = ss->y + i * yincr[ss->dir];

		    hits[turn][x1][y1] = ss->symbol;
		    if (turn  == PLAYER)
		    {
			cgoto(y1, x1);
			(void) addch((chtype)(ss->symbol));
		    }
		}

		(void) move(oldy, oldx);
		return(ss);
	    }
	}
    (void) move(oldy, oldx);
    return((ship_t *)NULL);
}

static int
plyturn(void)
{
    ship_t *ss;
    int hit;
    char *m = NULL;

    prompt(1, "Where do you want to shoot? ");
    for (;;)
    {
	(void) getcoord(COMPUTER);
	if (hits[PLAYER][curx][cury])
	{
	    prompt(1, "You shelled this spot already! Try again.");
	    beep();
	}
	else
	    break;
    }
    hit = IS_SHIP(board[COMPUTER][curx][cury]);
    hits[PLAYER][curx][cury] = hit ? MARK_HIT : MARK_MISS;
    cgoto(cury, curx);
    if (has_colors()) {
	if (hit)
	    attron(COLOR_PAIR(COLOR_RED));
	else
	    attron(COLOR_PAIR(COLOR_GREEN));
    }
    (void) addch((chtype)hits[PLAYER][curx][cury]);
    attrset(0);

    prompt(1, "You %s.", hit ? "scored a hit" : "missed");
    if(hit && (ss = hitship(curx, cury)))
    {
	switch(rnd(5))
	{
	case 0:
	    m = " You sank my %s!";
	    break;
	case 1:
	    m = " I have this sinking feeling about my %s....";
	    break;
	case 2:
	    m = " My %s has gone to Davy Jones's locker!";
	    break;
	case 3:
	    m = " Glub, glub -- my %s is headed for the bottom!";
	    break;
	case 4:
	    m = " You'll pick up survivors from my %s, I hope...!";
	    break;
	}
	(void)printw(m, ss->name);
	(void)beep();
    }
    return(hit);
}

static int
sgetc(char *s)
{
    char *s1;
    int ch;

    (void)refresh();
    for(;;)
    {
	ch = getch();
	if (islower(ch))
	    ch = toupper(ch);
	if (ch == CTRLC)
	    uninitgame(0);
	for (s1=s; *s1 && ch != *s1; ++s1)
	    continue;
	if (*s1)
	{
	    (void) addch((chtype)ch);
	    (void) refresh();
	    return(ch);
	    }
	}
}

/* Checks to see if there's room for a ship of a given length in a given
 * direction.  If direction is negative, check in all directions.  Note
 * that North and South are equivalent, as are East and West.
 */
static bool
cpushipcanfit(int x, int y, int length, int direction)
{
	int len = 1;
	int x1, y1;

	if (direction >= 0)
	{
		direction %= 4;
		while (direction < 8)
		{
			x1 = x + xincr[direction];
			y1 = y + yincr[direction];
			while (POSSIBLE(x1,y1))
			{
			    len++;
			    x1 += xincr[direction];
			    y1 += yincr[direction];
			}
			direction += 4;
		}
		return (len >= length);
	}
	else
	{
		return ((cpushipcanfit(x,y,length,E)) ||
			    (cpushipcanfit(x,y,length,S)));
	}
}

/* random-fire routine -- implements simple diagonal-striping strategy */
static void
randomfire(int *px, int *py)
{
    static int huntoffs;		/* Offset on search strategy */
    int ypossible[BWIDTH * BDEPTH], xpossible[BWIDTH * BDEPTH], nposs;
    int x, y, i;

    if (turncount++ == 0)
	huntoffs = rnd(srchstep);

    /* first, list all possible moves on the diagonal stripe */
    nposs = 0;
    for (x = 0; x < BWIDTH; x++)
	for (y = 0; y < BDEPTH; y++)
	    if ((!hits[COMPUTER][x][y]) &&
		 	(((x+huntoffs) % srchstep) == (y % srchstep)) &&
			(cpushipcanfit(x,y,cpulongest,-1)))
	    {
		    xpossible[nposs] = x;
		    ypossible[nposs] = y;
		    nposs++;
		}
    if (nposs)
    {
	i = rnd(nposs);

	*px = xpossible[i];
	*py = ypossible[i];
    }
	else if (srchstep > cpulongest)
    {
	     --srchstep; 
	     randomfire(px, py);
    }
	else
    {
		error("No moves possible?? Help!");
		exit(1);
    }
}

#define S_MISS	0
#define S_HIT	1
#define S_SUNK	-1

/* fire away at given location */
static int
cpufire(int x, int y)
{
    int hit;
    bool sunk;
    ship_t *ss = NULL;

    hits[COMPUTER][x][y] = (hit = (board[PLAYER][x][y])) ? MARK_HIT : MARK_MISS;
    (void) mvprintw(PROMPTLINE, 0,
	"I shoot at %c%d. I %s!", y + 'A', x, hit ? "hit" : "miss");
    if ((sunk = (hit && (ss = hitship(x, y)))))
	(void) printw(" I've sunk your %s", ss->name);
    (void)clrtoeol();

    pgoto(y, x);
    if (has_colors()) {
	if (hit)
	    attron(COLOR_PAIR(COLOR_RED));
	else
	    attron(COLOR_PAIR(COLOR_GREEN));
    }
    (void) addch((chtype)(hit ? SHOWHIT : SHOWSPLASH));
    attrset(0);

    return(hit ? (sunk ? S_SUNK : S_HIT) : S_MISS);
}

/*
 * This code implements a fairly irregular FSM, so please forgive the rampant
 * unstructuredness below. The five labels are states which need to be held
 * between computer turns.
 */
static int
cputurn(void)
{
    static bool used[4];
    static ship_t ts;
    int navail, x, y, d, n, hit = S_MISS;
    bool closenoshot = FALSE;

    switch(next)
    {
    case RANDOM_FIRE:	/* last shot was random and missed */
    refire:
	randomfire(&x, &y);
	if (!(hit = cpufire(x, y)))
	    next = RANDOM_FIRE;
	else
	{
	    ts.x = x; ts.y = y;
	    ts.hits = 1;
	    next = (hit == S_SUNK) ? RANDOM_FIRE : RANDOM_HIT;
	}
	break;

    case RANDOM_HIT:	/* last shot was random and hit */
	used[E/2] = used[W/2] = (!(cpushipcanfit(ts.x,ts.y,cpushortest,E)));
	used[S/2] = used[N/2] = (!(cpushipcanfit(ts.x,ts.y,cpushortest,S)));
	/* FALLTHROUGH */

    case HUNT_DIRECT:	/* last shot hit, we're looking for ship's long axis */
	for (d = navail = 0; d < 4; d++)
	{
	    x = ts.x + xincr[d*2]; y = ts.y + yincr[d*2];
	    if (!used[d] && POSSIBLE(x, y))
		navail++;
	    else
		used[d] = TRUE;
	}
	if (navail == 0)	/* no valid places for shots adjacent... */
	    goto refire;	/* ...so we must random-fire */
	else
	{
	    for (d = 0, n = rnd(navail) + 1; n; n--,d++)
		while (used[d])
		    d++;
	    d--;

	    x = ts.x + xincr[d*2];
	    y = ts.y + yincr[d*2];

	    if (!(hit = cpufire(x, y)))
		next = HUNT_DIRECT;
	    else
	    {
		ts.x = x; ts.y = y; ts.dir = d*2; ts.hits++;
		next = (hit == S_SUNK) ? RANDOM_FIRE : FIRST_PASS;
	    }
	}
	break;

    case FIRST_PASS:	/* we have a start and a direction now */
	x = ts.x + xincr[ts.dir];
	y = ts.y + yincr[ts.dir];
	if (POSSIBLE(x, y))
	{
	    if ((hit = cpufire(x, y)))
	    {
		    ts.x = x; ts.y = y; ts.hits++;
		    next = (hit == S_SUNK) ? RANDOM_FIRE : FIRST_PASS;
	    }
	    else
	         next = REVERSE_JUMP;
	    break;
	}
	else
	    next = REVERSE_JUMP;
	/* FALL THROUGH */

    case REVERSE_JUMP:	/* nail down the ship's other end */
	ts.dir = (ts.dir + 4) % 8;
	ts.x += (ts.hits-1) * xincr[ts.dir];
	ts.y += (ts.hits-1) * yincr[ts.dir];
	/* FALL THROUGH */

    case SECOND_PASS:	/* kill squares not caught on first pass */
	x = ts.x + xincr[ts.dir];
	y = ts.y + yincr[ts.dir];
	if (POSSIBLE(x, y))
	{
	    if ((hit = cpufire(x, y)))
	    {
		    ts.x = x; ts.y = y; ts.hits++;
		    next = (hit == S_SUNK) ? RANDOM_FIRE : SECOND_PASS;
	    }
	    else
	    {
	    /* The only way to get here is if closepack is on; otherwise,
	     * we _have_ sunk the ship.  I set hit to S_SUNK just to get
	     * the additional closepack logic at the end of the switch.
	     */
/*assert closepack*/
if (!closepack)  error("Assertion failed: not closepack 1");
		    hit = S_SUNK;
		    next = RANDOM_FIRE;
	    }
	}
	else
	{
/*assert closepack*/
if (!closepack)  error("Assertion failed: not closepack 2");
	    hit = S_SUNK;
	    closenoshot = TRUE;  /* Didn't shoot yet! */
	    next = RANDOM_FIRE;
	}
	break;
    }   /* switch(next) */

    if (hit == S_SUNK)
    {
	   /* Update cpulongest and cpushortest.  We could increase srchstep
	    * if it's smaller than cpushortest but that makes strategic sense
	    * only if we've been doing continuous diagonal stripes, and that's
	    * less interesting to watch.
	    */
	    ship_t *sp = plyship;

	    cpushortest = cpulongest;
	    cpulongest  = 0;
	    for (d=0 ; d < SHIPTYPES; d++, sp++)
	    {
		   if (sp->hits < sp->length)
		   {
		cpushortest = (cpushortest < sp->length) ? cpushortest : sp->length;
		cpulongest  = (cpulongest  > sp->length) ? cpulongest  : sp->length;
		   }
	    }
	    /* Now, if we're in closepack mode, we may have knocked off part of
	     * another ship, in which case we shouldn't do RANDOM_FIRE.  A
		* more robust implementation would probably do this check regardless
		* of whether closepack was set or not.
		* Note that MARK_HIT is set only for ships that aren't sunk;
		* hitship() changes the marker to the ship's character when the
		* ship is sunk.
	     */
	    if (closepack)
	    {
		  ts.hits = 0;
		  for (x = 0; x < BWIDTH; x++)
			for (y = 0; y < BDEPTH; y++)
			{
				if (hits[COMPUTER][x][y] == MARK_HIT)
				{
				/* So we found part of another ship.  It may have more
				 * than one hit on it.  Check to see if it does.  If no
				 * hit does, take the last MARK_HIT and be RANDOM_HIT.
				 */
	ts.x = x; ts.y = y; ts.hits = 1;
	for (d = 0; d < 8; d += 2)
	{
	    while ((ONBOARD(ts.x, ts.y)) && 
			(hits[COMPUTER][(int)ts.x][(int)ts.y] == MARK_HIT))
	    {
		    ts.x += xincr[d]; ts.y += yincr[d]; ts.hits++;
	    }
	    if ((--ts.hits > 1) && (ONBOARD(ts.x, ts.y)) &&
		    (hits[COMPUTER][(int)ts.x][(int)ts.y] == 0))
	    {
		    ts.dir = d;
		    ts.x -= xincr[d]; ts.y -= yincr[d];
		    d = 100;                  /* use as a flag */
		    x = BWIDTH; y = BDEPTH;   /* end the loop */
	    } else {
	         ts.x = x; ts.y = y; ts.hits = 1;
	    }
	
	}
				}
				if (ts.hits)
				{
					next = (d >= 100) ? FIRST_PASS : RANDOM_HIT;
				} else
					next = RANDOM_FIRE;
				}
	    }
	    if (closenoshot)
	    {
		   return(cputurn());
	    }
    }

    /* check for continuation and/or winner */
    if (salvo)
    {
	(void)refresh();
	(void)sleep(1);
    }

    return(hit);
}

static int
playagain(void)
{
    int j;
    ship_t *ss;

    for (ss = cpuship; ss < cpuship + SHIPTYPES; ss++)
	for(j = 0; j < ss->length; j++)
	{
	    cgoto(ss->y + j * yincr[ss->dir], ss->x + j * xincr[ss->dir]);
	    (void) addch((chtype)ss->symbol);
	}

    if(awinna())
	++cpuwon;
    else
	++plywon;
    j = 18 + strlen(name);
	/* If you play a hundred games or more at a go, you deserve a badly
	 * centred score output.
	 */
    if(plywon >= 10)
	++j;
    if(cpuwon >= 10)
	++j;
    (void) mvprintw(1,(COLWIDTH-j)/2,
		    "%s: %d     Computer: %d",name,plywon,cpuwon);

    prompt(2, (awinna()) ? "Want to be humiliated again, %s [yn]? "
	   : "Going to give me a chance for revenge, %s [yn]? ",name);
    return(sgetc("YN") == 'Y');
}

__dead void
usage(void)
{
	(void) fprintf(stderr, "usage: %s [-b | -s] [-c]\n", getprogname());
	(void) fprintf(stderr, "\tWhere the options are:\n");
	(void) fprintf(stderr, "\t-b : play a blitz game\n");
	(void) fprintf(stderr, "\t-s : play a salvo game\n");
	(void) fprintf(stderr, "\t-c : ships may be adjacent\n");
	exit(1);
}

static void
do_options(int c, char *op[])
{
    int ch;

    while ((ch = getopt(c, op, "bchs")) != -1) {
	switch (ch) {
	case 'b':
	    blitz = 1;
	    if (salvo == 1)
	    {
		(void) fprintf(stderr,
			"Bad Arg: -b and -s are mutually exclusive\n");
		exit(1);
	    }
	    break;
	case 's':
	    salvo = 1;
	    if (blitz == 1)
	    {
		(void) fprintf(stderr,
			"Bad Arg: -s and -b are mutually exclusive\n");
		exit(1);
	    }
	    break;
	case 'c':
	    closepack = 1;
	    break;
	case 'h':
	default:
	    (void) usage();
	    exit(1);
	}
    }
    if (op[optind] != NULL)
	(void) usage();
}

static int
scount(int who)
{
    int i, shots;
    ship_t *sp;

    if (who)
	sp = cpuship;	/* count cpu shots */
    else
	sp = plyship;	/* count player shots */

    for (i = 0, shots = 0; i < SHIPTYPES; i++, sp++)
    {
	if (sp->hits >= sp->length)
	    continue;		/* dead ship */
	else
	    shots++;
    }
    return(shots);
}

int
main(int argc, char *argv[])
{
    do_options(argc, argv);

    intro();

    if (pledge("stdio tty", NULL) == -1)
        err(1, "pledge");

    do {
	initgame();
	while(awinna() == -1)
	{
	    if (!blitz)
	    {
		if (!salvo)
		{
	    	    if(turn)
			(void) cputurn();
		    else
			(void) plyturn();
		}
		else  /* salvo */
		{
		    int i;

		    i = scount(turn);
		    while (i--)
		    {
			if (turn)
			{
			    if (cputurn() && awinna() != -1)
				i = 0;
			}
			else
			{
			    if (plyturn() && awinna() != -1)
				i = 0;
			}
		    }
		}
	    }
	    else  /* blitz */
	    	while(turn ? cputurn() : plyturn())
		{
		    if (turn)   /* Pause between successive computer shots */
		    {
			(void)refresh();
			(void)sleep(1);
		    }
		    if (awinna() != -1)
		     break;
		}
	    turn = OTHER;
	}
    } while
	(playagain());
    uninitgame(0);
    return 0;
}
