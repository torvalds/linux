/*	$OpenBSD: gamesupport.c,v 1.10 2016/08/27 02:00:10 guenther Exp $	*/
/*	$NetBSD: gamesupport.c,v 1.3 1995/04/24 12:24:28 cgd Exp $	*/

/*
 * gamesupport.c - auxiliary routines for support of Phantasia
 */

#include <curses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pathnames.h"
#include "phantdefs.h"
#include "phantglobs.h"

/************************************************************************
/
/ FUNCTION NAME: changestats()
/
/ FUNCTION: examine/change statistics for a player
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	bool ingameflag - set if called while playing game (Wizard only)
/
/ RETURN VALUE: none
/
/ MODULES CALLED: freerecord(), writerecord(), descrstatus(), truncstring(), 
/	time(), more(), wmove(), wclear(), strcmp(), printw(), strlcpy(), 
/	infloat(), waddstr(), cleanup(), findname(), userlist(), mvprintw(), 
/	localtime(), getanswer(), descrtype(), getstring()
/
/ GLOBAL INPUTS: LINES, *Login, Other, Wizard, Player, *stdscr, Databuf[], 
/	Fileloc
/
/ GLOBAL OUTPUTS: Echo
/
/ DESCRIPTION:
/	Prompt for player name to examine/change.
/	If the name is NULL, print a list of all players.
/	If we are called from within the game, check for the
/	desired name being the same as the current player's name.
/	Only the 'Wizard' may alter players.
/	Items are changed only if a non-zero value is specified.
/	To change an item to 0, use 0.1; it will be truncated later.
/
/	Players may alter their names and passwords, if the following
/	are true:
/	    - current login matches the character's logins
/	    - the password is known
/	    - the player is not in the middle of the game (ingameflag == FALSE)
/
/	The last condition is imposed for two reasons:
/	    - the game could possibly get a bit hectic if a player were
/	      continually changing his/her name
/	    - another player structure would be necessary to check for names
/	      already in use
/
*************************************************************************/

void
changestats(bool ingameflag)
{
	static char flag[2] =	/* for printing values of bools */
	{'F', 'T'};
	struct player *playerp;	/* pointer to structure to alter */
	char   *prompt;		/* pointer to prompt string */
	int     c;		/* input */
	int     today;		/* day of year of today */
	int     temp;		/* temporary variable */
	long    loc;		/* location in player file */
	time_t  now;		/* time now */
	double  dtemp;		/* temporary variable */
	bool   *bptr;		/* pointer to bool item to change */
	double *dptr;		/* pointer to double item to change */
	short  *sptr;		/* pointer to short item to change */

	clear();

	for (;;)
		/* get name of player to examine/alter */
	{
		mvaddstr(5, 0, "Which character do you want to look at ? ");
		getstring(Databuf, SZ_DATABUF);
		truncstring(Databuf);

		if (Databuf[0] == '\0')
			userlist(ingameflag);
		else
			break;
	}

	loc = -1L;

	if (!ingameflag)
		/* use 'Player' structure */
		playerp = &Player;
	else
		if (strcmp(Databuf, Player.p_name) == 0)
			/* alter/examine current player */
		{
			playerp = &Player;
			loc = Fileloc;
		} else
			/* use 'Other' structure */
			playerp = &Other;

	/* find player on file */
	if (loc < 0L && (loc = findname(Databuf, playerp)) < 0L)
		/* didn't find player */
	{
		clear();
		mvaddstr(11, 0, "Not found.");
		return;
	}
	time(&now);
	today = localtime(&now)->tm_yday;

	clear();

	for (;;)
		/* print player structure, and prompt for action */
	{
		mvprintw(0, 0, "A:Name         %s\n", playerp->p_name);

		if (Wizard)
			printw("B:Password     %s\n", playerp->p_password);
		else
			addstr("B:Password     XXXXXXXX\n");

		printw(" :Login        %s\n", playerp->p_login);

		printw("C:Experience   %.0f\n", playerp->p_experience);
		printw("D:Level        %.0f\n", playerp->p_level);
		printw("E:Strength     %.0f\n", playerp->p_strength);
		printw("F:Sword        %.0f\n", playerp->p_sword);
		printw(" :Might        %.0f\n", playerp->p_might);
		printw("G:Energy       %.0f\n", playerp->p_energy);
		printw("H:Max-Energy   %.0f\n", playerp->p_maxenergy);
		printw("I:Shield       %.0f\n", playerp->p_shield);
		printw("J:Quickness    %.0f\n", playerp->p_quickness);
		printw("K:Quicksilver  %.0f\n", playerp->p_quksilver);
		printw(" :Speed        %.0f\n", playerp->p_speed);
		printw("L:Magic Level  %.0f\n", playerp->p_magiclvl);
		printw("M:Mana         %.0f\n", playerp->p_mana);
		printw("N:Brains       %.0f\n", playerp->p_brains);

		if (Wizard || playerp->p_specialtype != SC_VALAR)
			mvaddstr(0, 40, descrstatus(playerp));

		mvprintw(1, 40, "O:Poison       %0.3f\n", playerp->p_poison);
		mvprintw(2, 40, "P:Gold         %.0f\n", playerp->p_gold);
		mvprintw(3, 40, "Q:Gem          %.0f\n", playerp->p_gems);
		mvprintw(4, 40, "R:Sin          %0.3f\n", playerp->p_sin);
		if (Wizard) {
			mvprintw(5, 40, "S:X-coord      %.0f\n", playerp->p_x);
			mvprintw(6, 40, "T:Y-coord      %.0f\n", playerp->p_y);
		} else {
			mvaddstr(5, 40, "S:X-coord      ?\n");
			mvaddstr(6, 40, "T:Y-coord      ?\n");
		}

		mvprintw(7, 40, "U:Age          %ld\n", playerp->p_age);
		mvprintw(8, 40, "V:Degenerated  %d\n", playerp->p_degenerated);

		mvprintw(9, 40, "W:Type         %d (%s)\n",
		    playerp->p_type, descrtype(playerp, FALSE) + 1);
		mvprintw(10, 40, "X:Special Type %d\n", playerp->p_specialtype);
		mvprintw(11, 40, "Y:Lives        %d\n", playerp->p_lives);
		mvprintw(12, 40, "Z:Crowns       %d\n", playerp->p_crowns);
		mvprintw(13, 40, "0:Charms       %d\n", playerp->p_charms);
		mvprintw(14, 40, "1:Amulets      %d\n", playerp->p_amulets);
		mvprintw(15, 40, "2:Holy Water   %d\n", playerp->p_holywater);

		temp = today - playerp->p_lastused;
		if (temp < 0)
			/* last year */
			temp += 365;
		mvprintw(16, 40, "3:Lastused     %d  (%d)\n", playerp->p_lastused, temp);

		mvprintw(18, 8, "4:Palantir %c  5:Blessing %c  6:Virgin %c  7:Blind %c",
		    flag[(int)playerp->p_palantir],
		    flag[(int)playerp->p_blessing],
		    flag[(int)playerp->p_virgin],
		    flag[(int)playerp->p_blindness]);

		if (!Wizard)
			mvprintw(19, 8, "8:Ring    %c",
			    flag[playerp->p_ring.ring_type != R_NONE]);
		else
			mvprintw(19, 8, "8:Ring    %d  9:Duration %d",
			    playerp->p_ring.ring_type, playerp->p_ring.ring_duration);

		if (!Wizard
		/* not wizard */
		    && (ingameflag || strcmp(Login, playerp->p_login) != 0))
			/* in game or not examining own character */
		{
			if (ingameflag) {
				more(LINES - 1);
				clear();
				return;
			} else
				cleanup(TRUE);
		}
		mvaddstr(20, 0, "!:Quit       ?:Delete");
		mvaddstr(21, 0, "What would you like to change ? ");

		if (Wizard)
			c = getanswer(" ", TRUE);
		else
			/* examining own player; allow to change name and
			 * password */
			c = getanswer("!BA", FALSE);

		switch (c) {
		case 'A':	/* change name */
		case 'B':	/* change password */
			if (!Wizard)
				/* prompt for password */
			{
				mvaddstr(23, 0, "Password ? ");
				Echo = FALSE;
				getstring(Databuf, 9);
				Echo = TRUE;
				if (strcmp(Databuf, playerp->p_password) != 0)
					continue;
			}
			if (c == 'A')
				/* get new name */
			{
				mvaddstr(23, 0, "New name: ");
				getstring(Databuf, SZ_NAME);
				truncstring(Databuf);
				if (Databuf[0] != '\0')
					if (Wizard || findname(Databuf, &Other) < 0L)
						strlcpy(playerp->p_name, Databuf,
						    sizeof playerp->p_name);
			} else
				/* get new password */
			{
				if (!Wizard)
					Echo = FALSE;

				do
					/* get two copies of new password
					 * until they match */
				{
					/* get first copy */
					mvaddstr(23, 0, "New password ? ");
					getstring(Databuf, SZ_PASSWORD);
					if (Databuf[0] == '\0')
						break;

					/* get second copy */
					mvaddstr(23, 0, "One more time ? ");
					getstring(playerp->p_password, SZ_PASSWORD);
				}
				while (strcmp(playerp->p_password, Databuf) != 0);

				Echo = TRUE;
			}

			continue;

		case 'C':	/* change experience */
			prompt = "experience";
			dptr = &playerp->p_experience;
			goto DALTER;

		case 'D':	/* change level */
			prompt = "level";
			dptr = &playerp->p_level;
			goto DALTER;

		case 'E':	/* change strength */
			prompt = "strength";
			dptr = &playerp->p_strength;
			goto DALTER;

		case 'F':	/* change swords */
			prompt = "sword";
			dptr = &playerp->p_sword;
			goto DALTER;

		case 'G':	/* change energy */
			prompt = "energy";
			dptr = &playerp->p_energy;
			goto DALTER;

		case 'H':	/* change maximum energy */
			prompt = "max energy";
			dptr = &playerp->p_maxenergy;
			goto DALTER;

		case 'I':	/* change shields */
			prompt = "shield";
			dptr = &playerp->p_shield;
			goto DALTER;

		case 'J':	/* change quickness */
			prompt = "quickness";
			dptr = &playerp->p_quickness;
			goto DALTER;

		case 'K':	/* change quicksilver */
			prompt = "quicksilver";
			dptr = &playerp->p_quksilver;
			goto DALTER;

		case 'L':	/* change magic */
			prompt = "magic level";
			dptr = &playerp->p_magiclvl;
			goto DALTER;

		case 'M':	/* change mana */
			prompt = "mana";
			dptr = &playerp->p_mana;
			goto DALTER;

		case 'N':	/* change brains */
			prompt = "brains";
			dptr = &playerp->p_brains;
			goto DALTER;

		case 'O':	/* change poison */
			prompt = "poison";
			dptr = &playerp->p_poison;
			goto DALTER;

		case 'P':	/* change gold */
			prompt = "gold";
			dptr = &playerp->p_gold;
			goto DALTER;

		case 'Q':	/* change gems */
			prompt = "gems";
			dptr = &playerp->p_gems;
			goto DALTER;

		case 'R':	/* change sin */
			prompt = "sin";
			dptr = &playerp->p_sin;
			goto DALTER;

		case 'S':	/* change x coord */
			prompt = "x";
			dptr = &playerp->p_x;
			goto DALTER;

		case 'T':	/* change y coord */
			prompt = "y";
			dptr = &playerp->p_y;
			goto DALTER;

		case 'U':	/* change age */
			mvprintw(23, 0, "age = %ld; age = ", playerp->p_age);
			dtemp = infloat();
			if (dtemp != 0.0)
				playerp->p_age = (long) dtemp;
			continue;

		case 'V':	/* change degen */
			mvprintw(23, 0, "degen = %d; degen = ", playerp->p_degenerated);
			dtemp = infloat();
			if (dtemp != 0.0)
				playerp->p_degenerated = (int) dtemp;
			continue;

		case 'W':	/* change type */
			prompt = "type";
			sptr = &playerp->p_type;
			goto SALTER;

		case 'X':	/* change special type */
			prompt = "special type";
			sptr = &playerp->p_specialtype;
			goto SALTER;

		case 'Y':	/* change lives */
			prompt = "lives";
			sptr = &playerp->p_lives;
			goto SALTER;

		case 'Z':	/* change crowns */
			prompt = "crowns";
			sptr = &playerp->p_crowns;
			goto SALTER;

		case '0':	/* change charms */
			prompt = "charm";
			sptr = &playerp->p_charms;
			goto SALTER;

		case '1':	/* change amulet */
			prompt = "amulet";
			sptr = &playerp->p_amulets;
			goto SALTER;

		case '2':	/* change holy water */
			prompt = "holy water";
			sptr = &playerp->p_holywater;
			goto SALTER;

		case '3':	/* change last-used */
			prompt = "last-used";
			sptr = &playerp->p_lastused;
			goto SALTER;

		case '4':	/* change palantir */
			prompt = "palantir";
			bptr = &playerp->p_palantir;
			goto BALTER;

		case '5':	/* change blessing */
			prompt = "blessing";
			bptr = &playerp->p_blessing;
			goto BALTER;

		case '6':	/* change virgin */
			prompt = "virgin";
			bptr = &playerp->p_virgin;
			goto BALTER;

		case '7':	/* change blindness */
			prompt = "blindness";
			bptr = &playerp->p_blindness;
			goto BALTER;

		case '8':	/* change ring type */
			prompt = "ring-type";
			sptr = &playerp->p_ring.ring_type;
			goto SALTER;

		case '9':	/* change ring duration */
			prompt = "ring-duration";
			sptr = &playerp->p_ring.ring_duration;
			goto SALTER;

		case '!':	/* quit, update */
			if (Wizard &&
			    (!ingameflag || playerp != &Player))
				/* turn off status if not modifying self */
			{
				playerp->p_status = S_OFF;
				playerp->p_tampered = T_OFF;
			}
			writerecord(playerp, loc);
			clear();
			return;

		case '?':	/* delete player */
			if (ingameflag && playerp == &Player)
				/* cannot delete self */
				continue;

			freerecord(playerp, loc);
			clear();
			return;

		default:
			continue;
		}
DALTER:
		mvprintw(23, 0, "%s = %f; %s = ", prompt, *dptr, prompt);
		dtemp = infloat();
		if (dtemp != 0.0)
			*dptr = dtemp;
		continue;

SALTER:
		mvprintw(23, 0, "%s = %d; %s = ", prompt, *sptr, prompt);
		dtemp = infloat();
		if (dtemp != 0.0)
			*sptr = (short) dtemp;
		continue;

BALTER:
		mvprintw(23, 0, "%s = %c; %s = ", prompt, flag[(int)*bptr],
		    prompt);
		c = getanswer("\nTF", TRUE);
		if (c == 'T')
			*bptr = TRUE;
		else
			if (c == 'F')
				*bptr = FALSE;
		continue;
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: monstlist()
/
/ FUNCTION: print a monster listing
/
/ AUTHOR: E. A. Estes, 2/27/86
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: puts(), fread(), fseek(), printf()
/
/ GLOBAL INPUTS: Curmonster, *Monstfp
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Read monster file, and print a monster listing on standard output.
/
*************************************************************************/

void
monstlist(void)
{
	int     count = 0;	/* count in file */

	puts(" #)  Name                 Str  Brain  Quick  Energy  Exper  Treas  Type  Flock%\n");
	fseek(Monstfp, 0L, SEEK_SET);
	while (fread(&Curmonster, SZ_MONSTERSTRUCT, 1, Monstfp) == 1)
		printf("%2d)  %-20.20s%4.0f   %4.0f     %2.0f   %5.0f  %5.0f     %2d    %2d     %3.0f\n", count++,
		    Curmonster.m_name, Curmonster.m_strength, Curmonster.m_brains,
		    Curmonster.m_speed, Curmonster.m_energy, Curmonster.m_experience,
		    Curmonster.m_treasuretype, Curmonster.m_type, Curmonster.m_flock);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: scorelist()
/
/ FUNCTION: print player score board
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: fread(), fopen(), printf(), fclose()
/
/ GLOBAL INPUTS: 
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Read the scoreboard file and print the contents.
/
*************************************************************************/

void
scorelist(void)
{
	struct scoreboard sbuf;	/* for reading entries */
	FILE   *fp;		/* to open the file */

	if ((fp = fopen(_PATH_SCORE, "r")) != NULL) {
		while (fread(&sbuf, SZ_SCORESTRUCT, 1, fp) == 1)
			printf("%-20s   (%-9s)  Level: %6.0f  Type: %s\n",
			    sbuf.sb_name, sbuf.sb_login, sbuf.sb_level, sbuf.sb_type);
		fclose(fp);
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: activelist()
/
/ FUNCTION: print list of active players to standard output
/
/ AUTHOR: E. A. Estes, 3/7/86
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: descrstatus(), fread(), fseek(), printf(), descrtype()
/
/ GLOBAL INPUTS: Other, *Playersfp
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Read player file, and print list of active records to standard output.
/
*************************************************************************/

void
activelist(void)
{
	fseek(Playersfp, 0L, SEEK_SET);
	printf("Current characters on file are:\n\n");

	while (fread(&Other, SZ_PLAYERSTRUCT, 1, Playersfp) == 1)
		if (Other.p_status != S_NOTUSED)
			printf("%-20s   (%-9s)  Level: %6.0f  %s  (%s)\n",
			    Other.p_name, Other.p_login, Other.p_level,
			    descrtype(&Other, FALSE), descrstatus(&Other));

}
/**/
/************************************************************************
/
/ FUNCTION NAME: purgeoldplayers()
/
/ FUNCTION: purge inactive players from player file
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: freerecord(), time(), fread(), fseek(), localtime()
/
/ GLOBAL INPUTS: Other, *Playersfp
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Delete characters which have not been used with the last
/	three weeks.
/
*************************************************************************/

void
purgeoldplayers(void)
{
	int     today;		/* day of year for today */
	int     daysold;	/* how many days since the character has been
				 * used */
	time_t  ltime;		/* time in seconds */
	long    loc = 0L;	/* location in file */

	time(&ltime);
	today = localtime(&ltime)->tm_yday;

	for (;;) {
		fseek(Playersfp, loc, SEEK_SET);
		if (fread(&Other, SZ_PLAYERSTRUCT, 1, Playersfp) != 1)
			break;

		daysold = today - Other.p_lastused;
		if (daysold < 0)
			daysold += 365;

		if (daysold > N_DAYSOLD)
			/* player hasn't been used in a while; delete */
			freerecord(&Other, loc);

		loc += SZ_PLAYERSTRUCT;
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: enterscore()
/
/ FUNCTION: enter player into scoreboard
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: fread(), fseek(), fopen(), error(), strcmp(), fclose(), 
/	strlcpy(), fwrite(), descrtype()
/
/ GLOBAL INPUTS: Player
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	The scoreboard keeps track of the highest character on a
/	per-login basis.
/	Search the scoreboard for an entry for the current login,
/	if an entry is found, and it is lower than the current player,
/	replace it, otherwise create an entry.
/
*************************************************************************/

void
enterscore(void)
{
	struct scoreboard sbuf;	/* buffer to read in scoreboard entries */
	FILE   *fp;		/* to open scoreboard file */
	long    loc = 0L;	/* location in scoreboard file */
	bool    found = FALSE;	/* set if we found an entry for this login */

	if ((fp = fopen(_PATH_SCORE, "r+")) != NULL) {
		while (fread(&sbuf, SZ_SCORESTRUCT, 1, fp) == 1)
			if (strcmp(Player.p_login, sbuf.sb_login) == 0) {
				found = TRUE;
				break;
			} else
				loc += SZ_SCORESTRUCT;
	} else {
		error(_PATH_SCORE);
	}

	/*
         * At this point, 'loc' will either indicate a point beyond
         * the end of file, or the place where the previous entry
         * was found.
         */

	if ((!found) || Player.p_level > sbuf.sb_level)
		/* put new entry in for this login */
	{
		strlcpy(sbuf.sb_login, Player.p_login,
		    sizeof sbuf.sb_login);
		strlcpy(sbuf.sb_name, Player.p_name,
		    sizeof sbuf.sb_name);
		sbuf.sb_level = Player.p_level;
		strlcpy(sbuf.sb_type, descrtype(&Player, TRUE),
		    sizeof sbuf.sb_type);
	}
	/* update entry */
	fseek(fp, loc, SEEK_SET);
	fwrite(&sbuf, SZ_SCORESTRUCT, 1, fp);
	fclose(fp);
}
