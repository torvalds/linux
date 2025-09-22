/*	$OpenBSD: main.c,v 1.23 2024/05/21 05:00:47 jsg Exp $	*/
/*	$NetBSD: main.c,v 1.3 1995/04/24 12:24:37 cgd Exp $	*/

/*
 * Phantasia 3.3.2 -- Interterminal fantasy game
 *
 * Edward A. Estes
 * AT&T, March 12, 1986
 */

/* DISCLAIMER:
 *
 * This game is distributed for free as is.  It is not guaranteed to work
 * in every conceivable environment.  It is not even guaranteed to work
 * in ANY environment.
 *
 * This game is distributed without notice of copyright, therefore it
 * may be used in any manner the recipient sees fit.  However, the
 * author assumes no responsibility for maintaining or revising this
 * game, in its original form, or any derivitives thereof.
 *
 * The author shall not be responsible for any loss, cost, or damage,
 * including consequential damage, caused by reliance on this material.
 *
 * The author makes no warranties, express or implied, including warranties
 * of merchantability or fitness for a particular purpose or use.
 *
 * AT&T is in no way connected with this game.
 */

#include <curses.h>
#include <err.h>
#include <math.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#ifdef TERMIOS
#include <termios.h>
#endif
#include <time.h>
#include <unistd.h>

#include "macros.h"
#include "pathnames.h"
#include "phantdefs.h"
#include "phantglobs.h"

/*
 * The program allocates as much file space as it needs to store characters,
 * so the possibility exists for the character file to grow without bound.
 * The file is purged upon normal entry to try to avoid that problem.
 * A similar problem exists for energy voids.  To alleviate the problem here,
 * the void file is cleared with every new king, and a limit is placed
 * on the size of the energy void file.
 */

/*
 * Put one line of text into the file 'motd' for announcements, etc.
 */

/*
 * The scoreboard file is updated when someone dies, and keeps track
 * of the highest character to date for that login.
 * Being purged from the character file does not cause the scoreboard
 * to be updated.
 */

/*
 * main.c	Main routines for Phantasia
 */

/***************************************************************************
/ FUNCTION NAME: main()
/
/ FUNCTION: initialize state, and call main process
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	int	argc - argument count
/	char	**argv - argument vector
/
/ RETURN VALUE: none
/
/ MODULES CALLED: monstlist(), checkenemy(), activelist(), 
/	throneroom(), checkbattle(), readmessage(), changestats(), writerecord(), 
/	tradingpost(), adjuststats(), recallplayer(), displaystats(), checktampered(), 
/	fabs(), rollnewplayer(), time(), exit(), sqrt(), floor(), wmove(), 
/	signal(), strlcat(), purgeoldplayers(), getuid(), isatty(), wclear(), 
/	strlcpy(), system(), altercoordinates(), cleanup(), waddstr(), procmain(), 
/	playinit(), leavegame(), localtime(), getanswer(), neatstuff(), initialstate(), 
/	scorelist(), titlelist()
/
/ GLOBAL INPUTS: *Login, Throne, Wizard, Player, *stdscr, Changed, Databuf[], 
/	Fileloc, Stattable[]
/
/ GLOBAL OUTPUTS: Wizard, Player, Changed, Fileloc, Timeout, *Statptr
/
/ DESCRIPTION:
/	Process arguments, initialize program, and loop forever processing
/	player input.
/
****************************************************************************/

int
main(int argc, char **argv)
{
	bool    noheader = FALSE;	/* set if don't want header */
	bool    headeronly = FALSE;	/* set if only want header */
	bool    examine = FALSE;	/* set if examine a character */
	time_t  seconds;		/* for time of day */
	double  dtemp;			/* for temporary calculations */

	initialstate();			/* init globals */

	/* process arguments */
	while (--argc && (*++argv)[0] == '-')
		switch ((*argv)[1]) {
		case 's':	/* short */
			noheader = TRUE;
			break;

		case 'H':	/* Header */
			headeronly = TRUE;
			break;

		case 'a':	/* all users */
			activelist();
			cleanup(TRUE);

		case 'p':	/* purge old players */
			purgeoldplayers();
			cleanup(TRUE);

		case 'S':	/* set 'Wizard' */
			Wizard = !getuid();
			break;

		case 'x':	/* examine */
			examine = TRUE;
			break;

		case 'm':	/* monsters */
			monstlist();
			cleanup(TRUE);

		case 'b':	/* scoreboard */
			scorelist();
			cleanup(TRUE);
		}

	if (!isatty(0))		/* don't let non-tty's play */
		cleanup(TRUE);

	playinit();		/* set up to catch signals, init curses */

	if (examine) {
		changestats(FALSE);
		cleanup(TRUE);
	}
	if (!noheader) {
		titlelist();
		purgeoldplayers();	/* clean up old characters */
	}
	if (headeronly)
		cleanup(TRUE);

	do
		/* get the player structure filled */
	{
		Fileloc = -1L;

		mvaddstr(22, 17, "Do you have a character to run [Q = Quit] ? ");

		switch (getanswer("NYQ", FALSE)) {
		case 'Y':
			Fileloc = recallplayer();
			break;

		case 'Q':
			cleanup(TRUE);

		default:
			Fileloc = rollnewplayer();
			break;
		}
		clear();
	}
	while (Fileloc < 0L);

	if (Player.p_level > 5.0)
		/* low level players have long timeout */
		Timeout = TRUE;

	/* update some important player statistics */
	strlcpy(Player.p_login, Login, LOGIN_NAME_MAX);
	time(&seconds);
	Player.p_lastused = localtime(&seconds)->tm_yday;
	Player.p_status = S_PLAYING;
	writerecord(&Player, Fileloc);

	Statptr = &Stattable[Player.p_type];	/* initialize pointer */

	altercoordinates(Player.p_x, Player.p_y, A_FORCED);	/* set some flags */

	clear();

	for (;;)
		/* loop forever, processing input */
	{

		adjuststats();	/* cleanup stats */

		if (Throne && Player.p_crowns == 0 && Player.p_specialtype != SC_KING)
			/* not allowed on throne -- move */
		{
			mvaddstr(5, 0, "You're not allowed in the Lord's Chamber without a crown.\n");
			altercoordinates(0.0, 0.0, A_NEAR);
		}
		checktampered();/* check for energy voids, etc. */

		if (Player.p_status != S_CLOAKED
		/* not cloaked */
		    && (dtemp = fabs(Player.p_x)) == fabs(Player.p_y)
		/* |x| = |y| */
		    && !Throne)
			/* not on throne */
		{
			dtemp = sqrt(dtemp / 100.0);
			if (floor(dtemp) == dtemp)
				/* |x| / 100 == n*n; at a trading post */
			{
				tradingpost();
				clear();
			}
		}
		checkbattle();	/* check for player to player battle */
		neatstuff();	/* gurus, medics, etc. */

		if (Player.p_status == S_CLOAKED) {
			/* costs 3 mana per turn to be cloaked */
			if (Player.p_mana > 3.0)
				Player.p_mana -= 3.0;
			else
				/* ran out of mana, uncloak */
			{
				Player.p_status = S_PLAYING;
				Changed = TRUE;
			}
		}

		if (Player.p_status != S_PLAYING && Player.p_status != S_CLOAKED)
			/* change status back to S_PLAYING */
		{
			Player.p_status = S_PLAYING;
			Changed = TRUE;
		}
		if (Changed)
			/* update file only if important stuff has changed */
		{
			writerecord(&Player, Fileloc);
			Changed = FALSE;
			continue;
		}
		readmessage();	/* read message, if any */

		displaystats();	/* print statistics */

		move(6, 0);

		if (Throne)
			/* maybe make king, print prompt, etc. */
			throneroom();

		/* print status line */
		addstr("1:Move  2:Players  3:Talk  4:Stats  5:Quit  ");
		if (Player.p_level >= MEL_CLOAK && Player.p_magiclvl >= ML_CLOAK)
			addstr("6:Cloak  ");
		if (Player.p_level >= MEL_TELEPORT && Player.p_magiclvl >= ML_TELEPORT)
			addstr("7:Teleport  ");
		if (Player.p_specialtype >= SC_COUNCIL || Wizard)
			addstr("8:Intervene  ");

		procmain();	/* process input */
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: initialstate()
/
/ FUNCTION: initialize some important global variable
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: fopen(), error(), getuid(), getlogin(), getpwuid()
/
/ GLOBAL INPUTS: 
/
/ GLOBAL OUTPUTS: *Energyvoidfp, Echo, Marsh, *Login, Users, Beyond, 
/	Throne, Wizard, Changed, Okcount, Timeout, Windows, *Monstfp, *Messagefp, 
/	*Playersfp
/
/ DESCRIPTION:
/	Set global flags, and open files which remain open.
/
*************************************************************************/

void
initialstate(void)
{
#ifdef TERMIOS
    struct termios tty;
#endif

	Beyond = FALSE;
	Marsh = FALSE;
	Throne = FALSE;
	Changed = FALSE;
	Wizard = FALSE;
	Timeout = FALSE;
	Users = 0;
	Windows = FALSE;
	Echo = TRUE;

	/* setup login name */
	if ((Login = getlogin()) == NULL) {
		struct passwd *gpwd;

		gpwd = getpwuid(getuid());
		if (gpwd != NULL)
			Login = gpwd->pw_name;
		else
			errx(1, "Who are you?");
	}

#ifdef TERMIOS
	/* setup terminal keys */
	if (tcgetattr(0, &tty) == 0) {
		Ch_Erase = tty.c_cc[VERASE];
		Ch_Kill = tty.c_cc[VKILL];
	} else {
		Ch_Erase = CH_ERASE;
		Ch_Kill = CH_KILL;
	}
#else
	Ch_Erase = CH_ERASE;
	Ch_Kill = CH_KILL;
#endif

	/* open some files */
	if ((Playersfp = fopen(_PATH_PEOPLE, "r+")) == NULL)
		error(_PATH_PEOPLE);

	if ((Monstfp = fopen(_PATH_MONST, "r+")) == NULL)
		error(_PATH_MONST);

	if ((Messagefp = fopen(_PATH_MESS, "r")) == NULL)
		error(_PATH_MESS);

	if ((Energyvoidfp = fopen(_PATH_VOID, "r+")) == NULL)
		error(_PATH_VOID);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: rollnewplayer()
/
/ FUNCTION: roll up a new character
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: initplayer(), allocrecord(), truncstring(), fabs(), wmove(), 
/	wclear(), sscanf(), strcmp(), genchar(), waddstr(), findname(), mvprintw(), 
/	getanswer(), getstring()
/
/ GLOBAL INPUTS: Other, Wizard, Player, *stdscr, Databuf[]
/
/ GLOBAL OUTPUTS: Echo
/
/ DESCRIPTION:
/	Prompt player, and roll up new character.
/
*************************************************************************/

long
rollnewplayer(void)
{
	int     chartype;	/* character type */
	int     ch;		/* input */

	initplayer(&Player);	/* initialize player structure */

	clear();
	mvaddstr(4, 21, "Which type of character do you want:");
	mvaddstr(8, 4,
"1:Magic User  2:Fighter  3:Elf  4:Dwarf  5:Halfling  6:Experimento  ");
	if (Wizard) {
		addstr("7:Super  ? ");
		chartype = getanswer("1234567", FALSE);
	} else {
		addstr("?  ");
		chartype = getanswer("123456", FALSE);
	}

	do {
		genchar(chartype);	/* roll up a character */

		/* print out results */
		mvprintw(12, 14,
		    "Strength    :  %2.0f  Quickness:  %2.0f  Mana       :  %2.0f\n",
		    Player.p_strength, Player.p_quickness, Player.p_mana);
		mvprintw(13, 14,
		    "Energy Level:  %2.0f  Brains   :  %2.0f  Magic Level:  %2.0f\n",
		    Player.p_energy, Player.p_brains, Player.p_magiclvl);

		if (Player.p_type == C_EXPER || Player.p_type == C_SUPER)
			break;

		mvaddstr(14, 14, "Type '1' to keep >");
		ch = getanswer(" ", TRUE);
	}
	while (ch != '1');

	if (Player.p_type == C_EXPER || Player.p_type == C_SUPER)
		/* get coordinates for experimento */
		for (;;) {
			mvaddstr(16, 0, "Enter the X Y coordinates of your experimento ? ");
			getstring(Databuf, SZ_DATABUF);
			sscanf(Databuf, "%lf %lf", &Player.p_x, &Player.p_y);

			if (fabs(Player.p_x) > D_EXPER || fabs(Player.p_y) > D_EXPER)
				mvaddstr(17, 0, "Invalid coordinates.  Try again.\n");
			else
				break;
		}

	for (;;)
		/* name the new character */
	{
		mvprintw(18, 0,
		    "Give your character a name [up to %d characters] ?  ", SZ_NAME - 1);
		getstring(Player.p_name, SZ_NAME);
		truncstring(Player.p_name);	/* remove trailing blanks */

		if (Player.p_name[0] == '\0')
			/* no null names */
			mvaddstr(19, 0, "Invalid name.");
		else
			if (findname(Player.p_name, &Other) >= 0L)
				/* cannot have duplicate names */
				mvaddstr(19, 0, "Name already in use.");
			else
				/* name is acceptable */
				break;

		addstr("  Pick another.\n");
	}

	/* get a password for character */
	Echo = FALSE;

	do {
		mvaddstr(20, 0, "Give your character a password [up to 8 characters] ? ");
		getstring(Player.p_password, SZ_PASSWORD);
		mvaddstr(21, 0, "One more time to verify ? ");
		getstring(Databuf, SZ_PASSWORD);
	}
	while (strcmp(Player.p_password, Databuf) != 0);

	Echo = TRUE;

	return (allocrecord());
}
/**/
/************************************************************************
/
/ FUNCTION NAME: procmain()
/
/ FUNCTION: process input from player
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: dotampered(), changestats(), inputoption(), allstatslist(), 
/	fopen(), wmove(), drandom(), sscanf(), fclose(), altercoordinates(), 
/	waddstr(), fprintf(), distance(), userlist(), leavegame(), encounter(), 
/	getstring(), wclrtobot()
/
/ GLOBAL INPUTS: Circle, Illcmd[], Throne, Wizard, Player, *stdscr, 
/	Databuf[], Illmove[]
/
/ GLOBAL OUTPUTS: Player, Changed
/
/ DESCRIPTION:
/	Process main menu options.
/
*************************************************************************/

void
procmain(void)
{
	int     ch;		/* input */
	double  x;		/* desired new x coordinate */
	double  y;		/* desired new y coordinate */
	double  temp;		/* for temporary calculations */
	FILE   *fp;		/* for opening files */
	int     loop;		/* a loop counter */
	bool    hasmoved = FALSE;	/* set if player has moved */

	ch = inputoption();
	mvaddstr(4, 0, "\n\n");	/* clear status area */

	move(7, 0);
	clrtobot();		/* clear data on bottom area of screen */

	if (Player.p_specialtype == SC_VALAR && (ch == '1' || ch == '7'))
		/* valar cannot move */
		ch = ' ';

	switch (ch) {
	case 'K':		/* move up/north */
	case 'N':
		x = Player.p_x;
		y = Player.p_y + MAXMOVE();
		hasmoved = TRUE;
		break;

	case 'J':		/* move down/south */
	case 'S':
		x = Player.p_x;
		y = Player.p_y - MAXMOVE();
		hasmoved = TRUE;
		break;

	case 'L':		/* move right/east */
	case 'E':
		x = Player.p_x + MAXMOVE();
		y = Player.p_y;
		hasmoved = TRUE;
		break;

	case 'H':		/* move left/west */
	case 'W':
		x = Player.p_x - MAXMOVE();
		y = Player.p_y;
		hasmoved = TRUE;
		break;

	default:		/* rest */
		Player.p_energy += (Player.p_maxenergy + Player.p_shield) / 15.0
		    + Player.p_level / 3.0 + 2.0;
		Player.p_energy =
		    MIN(Player.p_energy, Player.p_maxenergy + Player.p_shield);

		if (Player.p_status != S_CLOAKED)
			/* cannot find mana if cloaked */
		{
			Player.p_mana += (Circle + Player.p_level) / 4.0;

			if (drandom() < 0.2 && Player.p_status == S_PLAYING && !Throne)
				/* wandering monster */
				encounter(-1);
		}
		break;

	case 'X':		/* change/examine a character */
		changestats(TRUE);
		break;

	case '1':		/* move */
		for (loop = 3; loop; --loop) {
			mvaddstr(4, 0, "X Y Coordinates ? ");
			getstring(Databuf, SZ_DATABUF);

			if (sscanf(Databuf, "%lf %lf", &x, &y) != 2)
				mvaddstr(5, 0, "Try again\n");
			else
				if (distance(Player.p_x, x, Player.p_y, y) > MAXMOVE())
					ILLMOVE();
				else {
					hasmoved = TRUE;
					break;
				}
		}
		break;

	case '2':		/* players */
		userlist(TRUE);
		break;

	case '3':		/* message */
		mvaddstr(4, 0, "Message ? ");
		getstring(Databuf, SZ_DATABUF);
		/* we open the file for writing to erase any data which is
		 * already there */
		fp = fopen(_PATH_MESS, "w");
		if (Databuf[0] != '\0')
			fprintf(fp, "%s: %s", Player.p_name, Databuf);
		fclose(fp);
		break;

	case '4':		/* stats */
		allstatslist();
		break;

	case '5':		/* good-bye */
		leavegame();

	case '6':		/* cloak */
		if (Player.p_level < MEL_CLOAK || Player.p_magiclvl < ML_CLOAK)
			ILLCMD();
		else
			if (Player.p_status == S_CLOAKED)
				Player.p_status = S_PLAYING;
			else
				if (Player.p_mana < MM_CLOAK)
					mvaddstr(5, 0, "No mana left.\n");
				else {
					Changed = TRUE;
					Player.p_mana -= MM_CLOAK;
					Player.p_status = S_CLOAKED;
				}
		break;

	case '7':		/* teleport */
		/*
	         * conditions for teleport
	         *	- 20 per (level plus magic level)
	         *	- OR council of the wise or valar or ex-valar
	         *	- OR transport from throne
	         * transports from throne cost no mana
	         */
		if (Player.p_level < MEL_TELEPORT || Player.p_magiclvl < ML_TELEPORT)
			ILLCMD();
		else
			for (loop = 3; loop; --loop) {
				mvaddstr(4, 0, "X Y Coordinates ? ");
				getstring(Databuf, SZ_DATABUF);

				if (sscanf(Databuf, "%lf %lf", &x, &y) == 2) {
					temp = distance(Player.p_x, x, Player.p_y, y);
					if (!Throne
					/* can transport anywhere from throne */
					    && Player.p_specialtype <= SC_COUNCIL
					/* council, valar can transport
					 * anywhere */
					    && temp > (Player.p_level + Player.p_magiclvl) * 20.0)
						/* can only move 20 per exp.
						 * level + mag. level */
						ILLMOVE();
					else {
						temp = (temp / 75.0 + 1.0) * 20.0;	/* mana used */

						if (!Throne && temp > Player.p_mana)
							mvaddstr(5, 0, "Not enough power for that distance.\n");
						else {
							if (!Throne)
								Player.p_mana -= temp;
							hasmoved = TRUE;
							break;
						}
					}
				}
			}
		break;

	case 'C':
	case '9':		/* monster */
		if (Throne)
			/* no monsters while on throne */
			mvaddstr(5, 0, "No monsters in the chamber!\n");
		else
			if (Player.p_specialtype != SC_VALAR)
				/* the valar cannot call monsters */
			{
				Player.p_sin += 1e-6;
				encounter(-1);
			}
		break;

	case '0':		/* decree */
		if (Wizard || (Player.p_specialtype == SC_KING && Throne))
			/* kings must be on throne to decree */
			dotampered();
		else
			ILLCMD();
		break;

	case '8':		/* intervention */
		if (Wizard || Player.p_specialtype >= SC_COUNCIL)
			dotampered();
		else
			ILLCMD();
		break;
	}

	if (hasmoved)
		/* player has moved -- alter coordinates, and do random
		 * monster */
	{
		altercoordinates(x, y, A_SPECIFIC);

		if (drandom() < 0.2 && Player.p_status == S_PLAYING && !Throne)
			encounter(-1);
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: titlelist()
/
/ FUNCTION: print title page
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: fread(), fseek(), fopen(), fgets(), wmove(), strlcpy(), 
/	fclose(), strlen(), waddstr(), snprintf(), wrefresh()
/
/ GLOBAL INPUTS: Lines, Other, *stdscr, Databuf[], *Playersfp
/
/ GLOBAL OUTPUTS: Lines
/
/ DESCRIPTION:
/	Print important information about game, players, etc.
/
*************************************************************************/

void
titlelist(void)
{
	FILE   *fp;		/* used for opening various files */
	bool    councilfound = FALSE;	/* set if we find a member of the
					 * council */
	bool    kingfound = FALSE;	/* set if we find a king */
	double  hiexp, nxtexp;	/* used for finding the two highest players */
	double  hilvl, nxtlvl;	/* used for finding the two highest players */
	char    hiname[21], nxtname[21];	/* used for finding the two
						 * highest players */

	nxtexp = 0;
	mvaddstr(0, 14,
	    "W e l c o m e   t o   P h a n t a s i a (vers. 3.3.2)!");

	/* print message of the day */
	if ((fp = fopen(_PATH_MOTD, "r")) != NULL
	    && fgets(Databuf, SZ_DATABUF, fp) != NULL) {
		mvaddstr(2, 40 - strlen(Databuf) / 2, Databuf);
		fclose(fp);
	}
	/* search for king */
	fseek(Playersfp, 0L, SEEK_SET);
	while (fread(&Other, SZ_PLAYERSTRUCT, 1, Playersfp) == 1)
		if (Other.p_specialtype == SC_KING &&
		    Other.p_status != S_NOTUSED)
			/* found the king */
		{
			snprintf(Databuf, sizeof Databuf,
			    "The present ruler is %s  Level:%.0f",
			    Other.p_name, Other.p_level);
			mvaddstr(4, 40 - strlen(Databuf) / 2, Databuf);
			kingfound = TRUE;
			break;
		}
	if (!kingfound)
		mvaddstr(4, 24, "There is no ruler at this time.");

	/* search for valar */
	fseek(Playersfp, 0L, SEEK_SET);
	while (fread(&Other, SZ_PLAYERSTRUCT, 1, Playersfp) == 1)
		if (Other.p_specialtype == SC_VALAR && Other.p_status != S_NOTUSED)
			/* found the valar */
		{
			snprintf(Databuf, sizeof Databuf,
			    "The Valar is %s   Login:  %s", Other.p_name,
			    Other.p_login);
			mvaddstr(6, 40 - strlen(Databuf) / 2, Databuf);
			break;
		}
	/* search for council of the wise */
	fseek(Playersfp, 0L, SEEK_SET);
	Lines = 10;
	while (fread(&Other, SZ_PLAYERSTRUCT, 1, Playersfp) == 1)
		if (Other.p_specialtype == SC_COUNCIL && Other.p_status != S_NOTUSED)
			/* found a member of the council */
		{
			if (!councilfound) {
				mvaddstr(8, 30, "Council of the Wise:");
				councilfound = TRUE;
			}
			/* This assumes a finite (<=5) number of C.O.W.: */
			snprintf(Databuf, sizeof Databuf,
			    "%s   Login:  %s", Other.p_name, Other.p_login);
			mvaddstr(Lines++, 40 - strlen(Databuf) / 2, Databuf);
		}
	/* search for the two highest players */
	nxtname[0] = hiname[0] = '\0';
	hiexp = 0.0;
	nxtlvl = hilvl = 0;

	fseek(Playersfp, 0L, SEEK_SET);
	while (fread(&Other, SZ_PLAYERSTRUCT, 1, Playersfp) == 1)
		if (Other.p_experience > hiexp && Other.p_specialtype <= SC_KING && Other.p_status != S_NOTUSED)
			/* highest found so far */
		{
			nxtexp = hiexp;
			hiexp = Other.p_experience;
			nxtlvl = hilvl;
			hilvl = Other.p_level;
			strlcpy(nxtname, hiname, sizeof nxtname);
			strlcpy(hiname, Other.p_name, sizeof hiname);
		} else
			if (Other.p_experience > nxtexp
			    && Other.p_specialtype <= SC_KING
			    && Other.p_status != S_NOTUSED)
				/* next highest found so far */
			{
				nxtexp = Other.p_experience;
				nxtlvl = Other.p_level;
				strlcpy(nxtname, Other.p_name, sizeof nxtname);
			}
	mvaddstr(15, 28, "Highest characters are:");
	snprintf(Databuf, sizeof Databuf,
	    "%s  Level:%.0f   and   %s  Level:%.0f",
	    hiname, hilvl, nxtname, nxtlvl);
	mvaddstr(17, 40 - strlen(Databuf) / 2, Databuf);

	/* print last to die */
	if ((fp = fopen(_PATH_LASTDEAD, "r")) != NULL
	    && fgets(Databuf, SZ_DATABUF, fp) != NULL) {
		mvaddstr(19, 25, "The last character to die was:");
		mvaddstr(20, 40 - strlen(Databuf) / 2, Databuf);
		fclose(fp);
	}
	refresh();
}
/**/
/************************************************************************
/
/ FUNCTION NAME: recallplayer()
/
/ FUNCTION: find a character on file
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: writerecord(), truncstring(), more(), death(), wmove(), 
/	wclear(), strcmp(), printw(), cleanup(), waddstr(), findname(), mvprintw(), 
/	getanswer(), getstring()
/
/ GLOBAL INPUTS: Player, *stdscr, Databuf[]
/
/ GLOBAL OUTPUTS: Echo, Player
/
/ DESCRIPTION:
/	Search for a character of a certain name, and check password.
/
*************************************************************************/

long
recallplayer(void)
{
	long    loc = 0L;	/* location in player file */
	int     loop;		/* loop counter */
	int     ch;		/* input */

	clear();
	mvprintw(10, 0, "What was your character's name ? ");
	getstring(Databuf, SZ_NAME);
	truncstring(Databuf);

	if ((loc = findname(Databuf, &Player)) >= 0L)
		/* found character */
	{
		Echo = FALSE;

		for (loop = 0; loop < 2; ++loop) {
			/* prompt for password */
			mvaddstr(11, 0, "Password ? ");
			getstring(Databuf, SZ_PASSWORD);
			if (strcmp(Databuf, Player.p_password) == 0)
				/* password good */
			{
				Echo = TRUE;

				if (Player.p_status != S_OFF)
					/* player did not exit normally last
					 * time */
				{
					clear();
					addstr("Your character did not exit normally last time.\n");
					addstr("If you think you have good cause to have your character saved,\n");
					printw("you may quit and mail your reason to 'root'.\n");
					addstr("Otherwise, continuing spells certain death.\n");
					addstr("Do you want to quit ? ");
					ch = getanswer("YN", FALSE);
					if (ch == 'Y') {
						Player.p_status = S_HUNGUP;
						writerecord(&Player, loc);
						cleanup(TRUE);
					}
					death("Stupidity");
				}
				return (loc);
			} else
				mvaddstr(12, 0, "No good.\n");
		}

		Echo = TRUE;
	} else
		mvaddstr(11, 0, "Not found.\n");

	more(13);
	return (-1L);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: neatstuff()
/
/ FUNCTION: do random stuff
/
/ AUTHOR: E. A. Estes, 3/3/86
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: collecttaxes(), floor(), wmove(), drandom(), infloat(), 
/	waddstr(), mvprintw(), getanswer()
/
/ GLOBAL INPUTS: Player, *stdscr, *Statptr
/
/ GLOBAL OUTPUTS: Player
/
/ DESCRIPTION:
/	Handle gurus, medics, etc.
/
*************************************************************************/

void
neatstuff(void)
{
	double  temp;		/* for temporary calculations */
	int     ch;		/* input */

	switch ((int) ROLL(0.0, 100.0)) {
	case 1:
	case 2:
		if (Player.p_poison > 0.0) {
			mvaddstr(4, 0, "You've found a medic!  How much will you offer to be cured ? ");
			temp = floor(infloat());
			if (temp < 0.0 || temp > Player.p_gold)
				/* negative gold, or more than available */
			{
				mvaddstr(6, 0, "He was not amused, and made you worse.\n");
				Player.p_poison += 1.0;
			} else if (drandom() / 2.0 > (temp + 1.0) / MAX(Player.p_gold, 1))
				/* medic wants 1/2 of available gold */
				mvaddstr(5, 0, "Sorry, he wasn't interested.\n");
			else {
				mvaddstr(5, 0, "He accepted.");
				Player.p_poison = MAX(0.0, Player.p_poison - 1.0);
				Player.p_gold -= temp;
			}
		}
		break;

	case 3:
		mvaddstr(4, 0, "You've been caught raping and pillaging!\n");
		Player.p_experience += 4000.0;
		Player.p_sin += 0.5;
		break;

	case 4:
		temp = ROLL(10.0, 75.0);
		mvprintw(4, 0, "You've found %.0f gold pieces, want them ? ", temp);
		ch = getanswer("NY", FALSE);

		if (ch == 'Y')
			collecttaxes(temp, 0.0);
		break;

	case 5:
		if (Player.p_sin > 1.0) {
			mvaddstr(4, 0, "You've found a Holy Orb!\n");
			Player.p_sin -= 0.25;
		}
		break;

	case 6:
		if (Player.p_poison < 1.0) {
			mvaddstr(4, 0, "You've been hit with a plague!\n");
			Player.p_poison += 1.0;
		}
		break;

	case 7:
		mvaddstr(4, 0, "You've found some holy water.\n");
		++Player.p_holywater;
		break;

	case 8:
		mvaddstr(4, 0, "You've met a Guru. . .");
		if (drandom() * Player.p_sin > 1.0)
			addstr("You disgusted him with your sins!\n");
		else if (Player.p_poison > 0.0) {
			addstr("He looked kindly upon you, and cured you.\n");
			Player.p_poison = 0.0;
		} else {
			addstr("He rewarded you for your virtue.\n");
			Player.p_mana += 50.0;
			Player.p_shield += 2.0;
		}
		break;

	case 9:
		mvaddstr(4, 0, "You've found an amulet.\n");
		++Player.p_amulets;
		break;

	case 10:
		if (Player.p_blindness) {
			mvaddstr(4, 0, "You've regained your sight!\n");
			Player.p_blindness = FALSE;
		}
		break;

	default:		/* deal with poison */
		if (Player.p_poison > 0.0) {
			temp = Player.p_poison * Statptr->c_weakness
			    * Player.p_maxenergy / 600.0;
			if (Player.p_energy > Player.p_maxenergy / 10.0
			    && temp + 5.0 < Player.p_energy)
				Player.p_energy -= temp;
		}
		break;
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: genchar()
/
/ FUNCTION: generate a random character
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	int type - ASCII value of character type to generate
/
/ RETURN VALUE: none
/
/ MODULES CALLED: floor(), drandom()
/
/ GLOBAL INPUTS: Wizard, Player, Stattable[]
/
/ GLOBAL OUTPUTS: Player
/
/ DESCRIPTION:
/	Use the lookup table for rolling stats.
/
*************************************************************************/

void
genchar(int type)
{
	int     subscript;	/* used for subscripting into Stattable */
	struct charstats *statptr;	/* for pointing into Stattable */

	subscript = type - '1';

	if (subscript < C_MAGIC || subscript > C_EXPER)
		if (subscript != C_SUPER || !Wizard)
			/* fighter is default */
			subscript = C_FIGHTER;

	statptr = &Stattable[subscript];

	Player.p_quickness =
	    ROLL(statptr->c_quickness.base, statptr->c_quickness.interval);
	Player.p_strength =
	    ROLL(statptr->c_strength.base, statptr->c_strength.interval);
	Player.p_mana =
	    ROLL(statptr->c_mana.base, statptr->c_mana.interval);
	Player.p_maxenergy =
	    Player.p_energy =
	    ROLL(statptr->c_energy.base, statptr->c_energy.interval);
	Player.p_brains =
	    ROLL(statptr->c_brains.base, statptr->c_brains.interval);
	Player.p_magiclvl =
	    ROLL(statptr->c_magiclvl.base, statptr->c_magiclvl.interval);

	Player.p_type = subscript;

	if (Player.p_type == C_HALFLING)
		/* give halfling some experience */
		Player.p_experience = ROLL(600.0, 200.0);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: playinit()
/
/ FUNCTION: initialize for playing game
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: signal(), wclear(), noecho(), cbreak(), initscr(), 
/	wrefresh()
/
/ GLOBAL INPUTS: *stdscr
/
/ GLOBAL OUTPUTS: Windows
/
/ DESCRIPTION:
/	Catch a bunch of signals, and turn on curses stuff.
/
*************************************************************************/

void
playinit(void)
{
	initscr();		/* turn on curses */
	noecho();		/* do not echo input */
	cbreak();		/* do not process erase, kill */
	clear();
	refresh();
	Windows = TRUE;		/* mark the state */
}
/**/
/************************************************************************
/
/ FUNCTION NAME: cleanup()
/
/ FUNCTION: close some files, and maybe exit
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	bool doexit - exit flag
/
/ RETURN VALUE: none
/
/ MODULES CALLED: exit(), wmove(), fclose(), endwin(), nocbreak(), wrefresh()
/
/ GLOBAL INPUTS: *Energyvoidfp, LINES, *stdscr, Windows, *Monstfp, 
/	*Messagefp, *Playersfp
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Close all open files.  If we are "in curses" terminate curses.
/	If 'doexit' is set, exit, otherwise return.
/
*************************************************************************/

void
cleanup(int doexit)
{
	if (Windows) {
		move(LINES - 2, 0);
		refresh();
		nocbreak();
		endwin();
	}

	if (Playersfp) {
		fclose(Playersfp);
		Playersfp = NULL;
	}
	if (Monstfp) {
		fclose(Monstfp);
		Monstfp = NULL;
	}
	if (Messagefp) {
		fclose(Messagefp);
		Messagefp = NULL;
	}
	if (Energyvoidfp) {
		fclose(Energyvoidfp);
		Energyvoidfp = NULL;
	}

	if (doexit)
		exit(0);
}
