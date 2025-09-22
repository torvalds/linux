/*	$OpenBSD: misc.c,v 1.22 2023/10/10 09:43:52 tb Exp $	*/
/*	$NetBSD: misc.c,v 1.2 1995/03/24 03:59:03 cgd Exp $	*/

/*
 * misc.c  Phantasia miscellaneous support routines
 */

#include <curses.h>
#include <err.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "macros.h"
#include "pathnames.h"
#include "phantdefs.h"
#include "phantglobs.h"

/************************************************************************
/
/ FUNCTION NAME: movelevel()
/
/ FUNCTION: move player to new level
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: death(), floor(), wmove(), drandom(), waddstr(), explevel()
/
/ GLOBAL INPUTS: Player, *stdscr, *Statptr, Stattable[]
/
/ GLOBAL OUTPUTS: Player, Changed
/
/ DESCRIPTION:
/	Use lookup table to increment important statistics when
/	progressing to new experience level.
/	Players are rested to maximum as a bonus for making a new
/	level.
/	Check for council of wise, and being too big to be king.
/
*************************************************************************/

void
movelevel(void)
{
	struct charstats *statptr;	/* for pointing into Stattable */
	double  new;		/* new level */
	double  inc;		/* increment between new and old levels */

	Changed = TRUE;

	if (Player.p_type == C_EXPER)
		/* roll a type to use for increment */
		statptr = &Stattable[(int) ROLL(C_MAGIC, C_HALFLING - C_MAGIC + 1)];
	else
		statptr = Statptr;

	new = explevel(Player.p_experience);
	inc = new - Player.p_level;
	Player.p_level = new;

	/* add increments to statistics */
	Player.p_strength += statptr->c_strength.increase * inc;
	Player.p_mana += statptr->c_mana.increase * inc;
	Player.p_brains += statptr->c_brains.increase * inc;
	Player.p_magiclvl += statptr->c_magiclvl.increase * inc;
	Player.p_maxenergy += statptr->c_energy.increase * inc;

	/* rest to maximum upon reaching new level */
	Player.p_energy = Player.p_maxenergy + Player.p_shield;

	if (Player.p_crowns > 0 && Player.p_level >= 1000.0)
		/* no longer able to be king -- turn crowns into cash */
	{
		Player.p_gold += ((double) Player.p_crowns) * 5000.0;
		Player.p_crowns = 0;
	}
	if (Player.p_level >= 3000.0 && Player.p_specialtype < SC_COUNCIL)
		/* make a member of the council */
	{
		mvaddstr(6, 0, "You have made it to the Council of the Wise.\n");
		addstr("Good Luck on your search for the Holy Grail.\n");

		Player.p_specialtype = SC_COUNCIL;

		/* no rings for council and above */
		Player.p_ring.ring_type = R_NONE;
		Player.p_ring.ring_duration = 0;

		Player.p_lives = 3;	/* three extra lives */
	}
	if (Player.p_level > 9999.0 && Player.p_specialtype != SC_VALAR)
		death("Old age");
}
/**/
/************************************************************************
/
/ FUNCTION NAME: descrlocation()
/
/ FUNCTION: return a formatted description of location
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	struct player playerp - pointer to player structure
/	bool shortflag - set if short form is desired
/
/ RETURN VALUE: pointer to string containing result
/
/ MODULES CALLED: fabs(), floor(), snprintf(), distance()
/
/ GLOBAL INPUTS: Databuf[]
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Look at coordinates and return an appropriately formatted
/	string.
/
*************************************************************************/

char *
descrlocation(struct player *playerp, bool shortflag)
{
	double  circle;		/* corresponding circle for coordinates */
	int     quadrant;	/* quandrant of grid */
	char   *label;		/* pointer to place name */
	static char *nametable[4][4] =	/* names of places */
	{
		{"Anorien", "Ithilien", "Rohan", "Lorien"},
		{"Gondor", "Mordor", "Dunland", "Rovanion"},
		{"South Gondor", "Khand", "Eriador", "The Iron Hills"},
		{"Far Harad", "Near Harad", "The Northern Waste", "Rhun"}
	};

	if (playerp->p_specialtype == SC_VALAR)
		return (" is in Valhala");
	else if ((circle = CIRCLE(playerp->p_x, playerp->p_y)) >= 1000.0) {
		if (MAX(fabs(playerp->p_x), fabs(playerp->p_y)) > D_BEYOND)
			label = "The Point of No Return";
		else
			label = "The Ashen Mountains";
	} else if (circle >= 55)
		label = "Morannon";
	else if (circle >= 35)
		label = "Kennaquahair";
	else if (circle >= 20)
		label = "The Dead Marshes";
	else if (circle >= 9)
		label = "The Outer Waste";
	else if (circle >= 5)
		label = "The Moors Adventurous";
	else {
		if (playerp->p_x == 0.0 && playerp->p_y == 0.0)
			label = "The Lord's Chamber";
		else {
			/* this expression is split to prevent compiler
			 * loop with some compilers */
			quadrant = ((playerp->p_x > 0.0) ? 1 : 0);
			quadrant += ((playerp->p_y >= 0.0) ? 2 : 0);
			label = nametable[((int) circle) - 1][quadrant];
		}
	}

	if (shortflag)
		snprintf(Databuf, sizeof Databuf, "%.29s", label);
	else
		snprintf(Databuf, sizeof Databuf,
			" is in %s  (%.0f,%.0f)", label, playerp->p_x, playerp->p_y);

	return (Databuf);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: tradingpost()
/
/ FUNCTION: do trading post stuff
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: writerecord(), adjuststats(), fabs(), more(), sqrt(), 
/	sleep(), floor(), wmove(), drandom(), wclear(), printw(), 
/	altercoordinates(), infloat(), waddstr(), wrefresh(), mvprintw(), getanswer(), 
/	wclrtoeol(), wclrtobot()
/
/ GLOBAL INPUTS: Menu[], Circle, Player, *stdscr, Fileloc, Nobetter[]
/
/ GLOBAL OUTPUTS: Player
/
/ DESCRIPTION:
/	Different trading posts have different items.
/	Merchants cannot be cheated, but they can be dishonest
/	themselves.
/
/	Shields, swords, and quicksilver are not cumulative.  This is
/	one major area of complaint, but there are two reasons for this:
/		1) It becomes MUCH too easy to make very large versions
/		   of these items.
/		2) In the real world, one cannot simply weld two swords
/		   together to make a bigger one.
/
/	At one time, it was possible to sell old weapons at half the purchase
/	price.  This resulted in huge amounts of gold floating around,
/	and the game lost much of its challenge.
/
/	Also, purchasing gems defeats the whole purpose of gold.  Gold
/	is small change for lower level players.  They really shouldn't
/	be able to accumulate more than enough gold for a small sword or
/	a few books.  Higher level players shouldn't even bother to pick
/	up gold, except maybe to buy mana once in a while.
/
*************************************************************************/

void
tradingpost(void)
{
	double  numitems;	/* number of items to purchase */
	double  cost;		/* cost of purchase */
	double  blessingcost;	/* cost of blessing */
	int     ch;		/* input */
	int     size;		/* size of the trading post */
	int     loop;		/* loop counter */
	int     cheat = 0;	/* number of times player has tried to cheat */
	bool    dishonest = FALSE;	/* set when merchant is dishonest */

	Player.p_status = S_TRADING;
	writerecord(&Player, Fileloc);

	clear();
	addstr("You are at a trading post. All purchases must be made with gold.");

	size = sqrt(fabs(Player.p_x / 100)) + 1;
	size = MIN(7, size);

	/* set up cost of blessing */
	blessingcost = 1000.0 * (Player.p_level + 5.0);

	/* print Menu */
	move(7, 0);
	for (loop = 0; loop < size; ++loop)
		/* print Menu */
	{
		if (loop == 6)
			cost = blessingcost;
		else
			cost = Menu[loop].cost;
		printw("(%d) %-12s: %6.0f\n", loop + 1, Menu[loop].item, cost);
	}

	mvprintw(5, 0, "L:Leave  P:Purchase  S:Sell Gems ? ");

	for (;;) {
		adjuststats();	/* truncate any bad values */

		/* print some important statistics */
		mvprintw(1, 0, "Gold:   %9.0f  Gems:  %9.0f  Level:   %6.0f  Charms: %6d\n",
		    Player.p_gold, Player.p_gems, Player.p_level, Player.p_charms);
		printw("Shield: %9.0f  Sword: %9.0f  Quicksilver:%3.0f  Blessed: %s\n",
		    Player.p_shield, Player.p_sword, Player.p_quksilver,
		    (Player.p_blessing ? " True" : "False"));
		printw("Brains: %9.0f  Mana:  %9.0f", Player.p_brains, Player.p_mana);

		move(5, 36);
		ch = getanswer("LPS", FALSE);
		move(15, 0);
		clrtobot();
		switch (ch) {
		case 'L':	/* leave */
		case '\n':
			altercoordinates(0.0, 0.0, A_NEAR);
			return;

		case 'P':	/* make purchase */
			mvaddstr(15, 0, "What what would you like to buy ? ");
			ch = getanswer(" 1234567", FALSE);
			move(15, 0);
			clrtoeol();

			if (ch - '0' > size)
				addstr("Sorry, this merchant doesn't have that.");
			else
				switch (ch) {
				case '1':
					printw("Mana is one per %.0f gold piece.  How many do you want (%.0f max) ? ",
					    Menu[0].cost, floor(Player.p_gold / Menu[0].cost));
					cost = (numitems = floor(infloat())) * Menu[0].cost;

					if (cost > Player.p_gold || numitems < 0)
						++cheat;
					else {
						cheat = 0;
						Player.p_gold -= cost;
						if (drandom() < 0.02)
							dishonest = TRUE;
						else
							Player.p_mana += numitems;
					}
					break;

				case '2':
					printw("Shields are %.0f per +1.  How many do you want (%.0f max) ? ",
					    Menu[1].cost, floor(Player.p_gold / Menu[1].cost));
					cost = (numitems = floor(infloat())) * Menu[1].cost;

					if (numitems == 0.0)
						break;
					else if (cost > Player.p_gold || numitems < 0)
						++cheat;
					else if (numitems < Player.p_shield)
						NOBETTER();
					else {
						cheat = 0;
						Player.p_gold -= cost;
						if (drandom() < 0.02)
							dishonest = TRUE;
						else
							Player.p_shield = numitems;
					}
					break;

				case '3':
					printw("A book costs %.0f gp.  How many do you want (%.0f max) ? ",
					    Menu[2].cost, floor(Player.p_gold / Menu[2].cost));
					cost = (numitems = floor(infloat())) * Menu[2].cost;

					if (cost > Player.p_gold || numitems < 0)
						++cheat;
					else {
						cheat = 0;
						Player.p_gold -= cost;
						if (drandom() < 0.02)
							dishonest = TRUE;
						else if (drandom() * numitems > Player.p_level / 10.0
						    && numitems != 1) {
							printw("\nYou blew your mind!\n");
							Player.p_brains /= 5;
						} else {
							Player.p_brains += floor(numitems) * ROLL(20, 8);
						}
					}
					break;

				case '4':
					printw("Swords are %.0f gp per +1.  How many + do you want (%.0f max) ? ",
					    Menu[3].cost, floor(Player.p_gold / Menu[3].cost));
					cost = (numitems = floor(infloat())) * Menu[3].cost;

					if (numitems == 0.0)
						break;
					else if (cost > Player.p_gold || numitems < 0)
						++cheat;
					else if (numitems < Player.p_sword)
						NOBETTER();
					else {
						cheat = 0;
						Player.p_gold -= cost;
						if (drandom() < 0.02)
							dishonest = TRUE;
						else
							Player.p_sword = numitems;
					}
					break;

				case '5':
					printw("A charm costs %.0f gp.  How many do you want (%.0f max) ? ",
					    Menu[4].cost, floor(Player.p_gold / Menu[4].cost));
					cost = (numitems = floor(infloat())) * Menu[4].cost;

					if (cost > Player.p_gold || numitems < 0)
						++cheat;
					else {
						cheat = 0;
						Player.p_gold -= cost;
						if (drandom() < 0.02)
							dishonest = TRUE;
						else
							Player.p_charms += numitems;
					}
					break;

				case '6':
					printw("Quicksilver is %.0f gp per +1.  How many + do you want (%.0f max) ? ",
					    Menu[5].cost, floor(Player.p_gold / Menu[5].cost));
					cost = (numitems = floor(infloat())) * Menu[5].cost;

					if (numitems == 0.0)
						break;
					else if (cost > Player.p_gold || numitems < 0)
						++cheat;
					else if (numitems < Player.p_quksilver)
						NOBETTER();
					else {
						cheat = 0;
						Player.p_gold -= cost;
						if (drandom() < 0.02)
							dishonest = TRUE;
						else
							Player.p_quksilver = numitems;
					}
					break;

				case '7':
					if (Player.p_blessing) {
						addstr("You already have a blessing.");
						break;
					}
					printw("A blessing requires a %.0f gp donation.  Still want one ? ", blessingcost);
					ch = getanswer("NY", FALSE);

					if (ch == 'Y') {
						if (Player.p_gold < blessingcost)
							++cheat;
						else {
							cheat = 0;
							Player.p_gold -= blessingcost;
							if (drandom() < 0.02)
								dishonest = TRUE;
							else
								Player.p_blessing = TRUE;
						}
					}
					break;
				}
			break;

		case 'S':	/* sell gems */
			mvprintw(15, 0, "A gem is worth %.0f gp.  How many do you want to sell (%.0f max) ? ",
			    (double) N_GEMVALUE, Player.p_gems);
			numitems = floor(infloat());

			if (numitems > Player.p_gems || numitems < 0)
				++cheat;
			else {
				cheat = 0;
				Player.p_gems -= numitems;
				Player.p_gold += numitems * N_GEMVALUE;
			}
		}

		if (cheat == 1)
			mvaddstr(17, 0, "Come on, merchants aren't stupid.  Stop cheating.\n");
		else if (cheat == 2) {
			mvaddstr(17, 0, "You had your chance.  This merchant happens to be\n");
			printw("a %.0f level magic user, and you made %s mad!\n",
			    ROLL(Circle * 20.0, 40.0), (drandom() < 0.5) ? "him" : "her");
			altercoordinates(0.0, 0.0, A_FAR);
			Player.p_energy /= 2.0;
			++Player.p_sin;
			more(23);
			return;
		} else if (dishonest) {
			mvaddstr(17, 0, "The merchant stole your money!");
			refresh();
			altercoordinates(Player.p_x - Player.p_x / 10.0,
			    Player.p_y - Player.p_y / 10.0, A_SPECIFIC);
			sleep(2);
			return;
		}
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: displaystats()
/
/ FUNCTION: print out important player statistics
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: descrstatus(), descrlocation(), mvprintw()
/
/ GLOBAL INPUTS: Users, Player
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Important player statistics are printed on the screen.
/
*************************************************************************/

void
displaystats(void)
{
	mvprintw(0, 0, "%s%s\n", Player.p_name, descrlocation(&Player, FALSE));
	mvprintw(1, 0, "Level :%7.0f   Energy  :%9.0f(%9.0f)  Mana :%9.0f  Users:%3d\n",
	    Player.p_level, Player.p_energy, Player.p_maxenergy + Player.p_shield,
	    Player.p_mana, Users);
	mvprintw(2, 0, "Quick :%3.0f(%3.0f)  Strength:%9.0f(%9.0f)  Gold :%9.0f  %s\n",
	    Player.p_speed, Player.p_quickness + Player.p_quksilver, Player.p_might,
	    Player.p_strength + Player.p_sword, Player.p_gold, descrstatus(&Player));
}
/**/
/************************************************************************
/
/ FUNCTION NAME: allstatslist()
/
/ FUNCTION: show player items
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: mvprintw(), descrtype()
/
/ GLOBAL INPUTS: Player
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Print out some player statistics of lesser importance.
/
*************************************************************************/

void
allstatslist(void)
{
	static char *flags[] =	/* to print value of some bools */
	{
		"False",
		" True"
	};

	mvprintw(8, 0, "Type: %s\n", descrtype(&Player, FALSE));

	mvprintw(10, 0, "Experience: %9.0f", Player.p_experience);
	mvprintw(11, 0, "Brains    : %9.0f", Player.p_brains);
	mvprintw(12, 0, "Magic Lvl : %9.0f", Player.p_magiclvl);
	mvprintw(13, 0, "Sin       : %9.5f", Player.p_sin);
	mvprintw(14, 0, "Poison    : %9.5f", Player.p_poison);
	mvprintw(15, 0, "Gems      : %9.0f", Player.p_gems);
	mvprintw(16, 0, "Age       : %9ld", Player.p_age);
	mvprintw(10, 40, "Holy Water: %9d", Player.p_holywater);
	mvprintw(11, 40, "Amulets   : %9d", Player.p_amulets);
	mvprintw(12, 40, "Charms    : %9d", Player.p_charms);
	mvprintw(13, 40, "Crowns    : %9d", Player.p_crowns);
	mvprintw(14, 40, "Shield    : %9.0f", Player.p_shield);
	mvprintw(15, 40, "Sword     : %9.0f", Player.p_sword);
	mvprintw(16, 40, "Quickslver: %9.0f", Player.p_quksilver);

	mvprintw(18, 0, "Blessing: %s   Ring: %s   Virgin: %s   Palantir: %s",
	    flags[(int)Player.p_blessing],
	    flags[Player.p_ring.ring_type != R_NONE],
	    flags[(int)Player.p_virgin],
	    flags[(int)Player.p_palantir]);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: descrtype()
/
/ FUNCTION: return a string specifying player type
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	struct player playerp - pointer to structure for player
/	bool shortflag - set if short form is desired
/
/ RETURN VALUE: pointer to string describing player type
/
/ MODULES CALLED: strlcpy()
/
/ GLOBAL INPUTS: Databuf[]
/
/ GLOBAL OUTPUTS: Databuf[]
/
/ DESCRIPTION:
/	Return a string describing the player type.
/	King, council, valar, supersedes other types.
/	The first character of the string is '*' if the player
/	has a crown.
/	If 'shortflag' is TRUE, return a 3 character string.
/
*************************************************************************/

char *
descrtype(struct player *playerp, bool shortflag)
{
	int     type;		/* for caluculating result subscript */
	static char *results[] =/* description table */
	{
		" Magic User", " MU",
		" Fighter", " F ",
		" Elf", " E ",
		" Dwarf", " D ",
		" Halfling", " H ",
		" Experimento", " EX",
		" Super", " S ",
		" King", " K ",
		" Council of Wise", " CW",
		" Ex-Valar", " EV",
		" Valar", " V ",
		" ? ", " ? "
	};

	type = playerp->p_type;

	switch (playerp->p_specialtype) {
	case SC_NONE:
		type = playerp->p_type;
		break;

	case SC_KING:
		type = 7;
		break;

	case SC_COUNCIL:
		type = 8;
		break;

	case SC_EXVALAR:
		type = 9;
		break;

	case SC_VALAR:
		type = 10;
		break;
	}

	type *= 2;		/* calculate offset */

	if (type > 20)
		/* error */
		type = 22;

	if (shortflag)
		/* use short descriptions */
		++type;

	if (playerp->p_crowns > 0) {
		strlcpy(Databuf, results[type], sizeof Databuf);
		Databuf[0] = '*';
		return (Databuf);
	} else
		return (results[type]);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: findname()
/
/ FUNCTION: find location in player file of given name
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	char *name - name of character to look for
/	struct player *playerp - pointer of structure to fill
/
/ RETURN VALUE: location of player if found, -1 otherwise
/
/ MODULES CALLED: fread(), fseek(), strcmp()
/
/ GLOBAL INPUTS: Wizard, *Playersfp
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Search the player file for the player of the given name.
/	If player is found, fill structure with player data.
/
*************************************************************************/

long
findname(char *name, struct player *playerp)
{
	long    loc = 0;	/* location in the file */

	fseek(Playersfp, 0L, SEEK_SET);
	while (fread(playerp, SZ_PLAYERSTRUCT, 1, Playersfp) == 1) {
		if (strcmp(playerp->p_name, name) == 0) {
			if (playerp->p_status != S_NOTUSED || Wizard)
				/* found it */
				return (loc);
		}
		loc += SZ_PLAYERSTRUCT;
	}

	return (-1);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: allocrecord()
/
/ FUNCTION: find space in the player file for a new character
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: location of free space in file
/
/ MODULES CALLED: initplayer(), writerecord(), fread(), fseek()
/
/ GLOBAL INPUTS: Other, *Playersfp
/
/ GLOBAL OUTPUTS: Player
/
/ DESCRIPTION:
/	Search the player file for an unused entry.  If none are found,
/	make one at the end of the file.
/
*************************************************************************/

long
allocrecord(void)
{
	long    loc = 0L;	/* location in file */

	fseek(Playersfp, 0L, SEEK_SET);
	while (fread(&Other, SZ_PLAYERSTRUCT, 1, Playersfp) == 1) {
		if (Other.p_status == S_NOTUSED)
			/* found an empty record */
			return (loc);
		else
			loc += SZ_PLAYERSTRUCT;
	}

	/* make a new record */
	initplayer(&Other);
	Player.p_status = S_OFF;
	writerecord(&Other, loc);

	return (loc);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: freerecord()
/
/ FUNCTION: free up a record on the player file
/
/ AUTHOR: E. A. Estes, 2/7/86
/
/ ARGUMENTS:
/	struct player playerp - pointer to structure to free
/	long loc - location in file to free
/
/ RETURN VALUE: none
/
/ MODULES CALLED: writerecord()
/
/ GLOBAL INPUTS: none
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Mark structure as not used, and update player file.
/
*************************************************************************/

void
freerecord(struct player *playerp, long loc)
{
	playerp->p_name[0] = CH_MARKDELETE;
	playerp->p_status = S_NOTUSED;
	writerecord(playerp, loc);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: leavegame()
/
/ FUNCTION: leave game
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: freerecord(), writerecord(), cleanup()
/
/ GLOBAL INPUTS: Player, Fileloc
/
/ GLOBAL OUTPUTS: Player
/
/ DESCRIPTION:
/	Mark player as inactive, and cleanup.
/	Do not save players below level 1.
/
*************************************************************************/

void
leavegame(void)
{

	if (Player.p_level < 1.0)
		/* delete character */
		freerecord(&Player, Fileloc);
	else {
		Player.p_status = S_OFF;
		writerecord(&Player, Fileloc);
	}

	cleanup(TRUE);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: death()
/
/ FUNCTION: death routine
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	char *how - pointer to string describing cause of death
/
/ RETURN VALUE: none
/
/ MODULES CALLED: freerecord(), enterscore(), more(), exit(), fread(), 
/	fseek(), execl(), fopen(), floor(), wmove(), drandom(), wclear(), strcmp(), 
/	fwrite(), fflush(), printw(), strlcpy(), fclose(), waddstr(), cleanup(), 
/	fprintf(), wrefresh(), getanswer(), descrtype()
/
/ GLOBAL INPUTS: Curmonster, Wizard, Player, *stdscr, Fileloc, *Monstfp
/
/ GLOBAL OUTPUTS: Player
/
/ DESCRIPTION:
/	Kill off current player.
/	Handle rings, and multiple lives.
/	Print an appropriate message.
/	Update scoreboard, lastdead, and let other players know about
/	the demise of their comrade.
/
*************************************************************************/

void
death(char *how)
{
	FILE   *fp;		/* for updating various files */
	int     ch;		/* input */
	static char *deathmesg[] =
	/* add more messages here, if desired */
	{
		"You have been wounded beyond repair.  ",
		"You have been disemboweled.  ",
		"You've been mashed, mauled, and spit upon.  (You're dead.)\n",
		"You died!  ",
		"You're a complete failure -- you've died!!\n",
		"You have been dealt a fatal blow!  "
	};

	clear();

	if (strcmp(how, "Stupidity") != 0) {
		if (Player.p_level > 9999.0)
			/* old age */
			addstr("Characters must be retired upon reaching level 10000.  Sorry.");
		else if (Player.p_lives > 0) {
			/* extra lives */
			addstr("You should be more cautious.  You've been killed.\n");
			printw("You only have %d more chance(s).\n", --Player.p_lives);
			more(3);
			Player.p_energy = Player.p_maxenergy;
			return;
		} else if (Player.p_specialtype == SC_VALAR) {
			addstr("You had your chances, but Valar aren't totally\n");
			addstr("immortal.  You are now left to wither and die . . .\n");
			more(3);
			Player.p_brains = Player.p_level / 25.0;
			Player.p_energy = Player.p_maxenergy /= 5.0;
			Player.p_quksilver = Player.p_sword = 0.0;
			Player.p_specialtype = SC_COUNCIL;
			return;
		} else if (Player.p_ring.ring_inuse &&
		    (Player.p_ring.ring_type == R_DLREG || Player.p_ring.ring_type == R_NAZREG))
			/* good ring in use - saved from death */
		{
			mvaddstr(4, 0, "Your ring saved you from death!\n");
			refresh();
			Player.p_ring.ring_type = R_NONE;
			Player.p_energy = Player.p_maxenergy / 12.0 + 1.0;
			if (Player.p_crowns > 0)
				--Player.p_crowns;
			return;
		} else if (Player.p_ring.ring_type == R_BAD
		    || Player.p_ring.ring_type == R_SPOILED)
			/* bad ring in possession; name idiot after player */
		{
			mvaddstr(4, 0,
			    "Your ring has taken control of you and turned you into a monster!\n");
			fseek(Monstfp, 13L * SZ_MONSTERSTRUCT, SEEK_SET);
			fread(&Curmonster, SZ_MONSTERSTRUCT, 1, Monstfp);
			strlcpy(Curmonster.m_name, Player.p_name,
			    sizeof Curmonster.m_name);
			fseek(Monstfp, 13L * SZ_MONSTERSTRUCT, SEEK_SET);
			fwrite(&Curmonster, SZ_MONSTERSTRUCT, 1, Monstfp);
			fflush(Monstfp);
		}
	}
	enterscore();		/* update score board */

	/* put info in last dead file */
	fp = fopen(_PATH_LASTDEAD, "w");
	fprintf(fp, "%s (%s, run by %s, level %.0f, killed by %s)",
	    Player.p_name, descrtype(&Player, TRUE),
	    Player.p_login, Player.p_level, how);
	fclose(fp);

	/* let other players know */
	fp = fopen(_PATH_MESS, "w");
	fprintf(fp, "%s was killed by %s.", Player.p_name, how);
	fclose(fp);

	freerecord(&Player, Fileloc);

	clear();
	move(10, 0);
	addstr(deathmesg[(int) ROLL(0.0, (double) sizeof(deathmesg) / sizeof(char *))]);
	addstr("Care to give it another try ? ");
	ch = getanswer("NY", FALSE);

	if (ch == 'Y') {
		cleanup(FALSE);
		execl(_PATH_GAMEPROG, "phantasia", "-s",
		    (Wizard ? "-S" : (char *)NULL), (char *)NULL);
		exit(0);
	}
	cleanup(TRUE);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: writerecord()
/
/ FUNCTION: update structure in player file
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	struct player *playerp - pointer to structure to write out
/	long place - location in file to updata
/
/ RETURN VALUE: none
/
/ MODULES CALLED: fseek(), fwrite(), fflush()
/
/ GLOBAL INPUTS: *Playersfp
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Update location in player file with given structure.
/
*************************************************************************/

void
writerecord(struct player *playerp, long place)
{
	fseek(Playersfp, place, SEEK_SET);
	fwrite(playerp, SZ_PLAYERSTRUCT, 1, Playersfp);
	fflush(Playersfp);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: explevel()
/
/ FUNCTION: calculate level based upon experience
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	double experience - experience to calculate experience level from
/
/ RETURN VALUE: experience level
/
/ MODULES CALLED: pow(), floor()
/
/ GLOBAL INPUTS: none
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION: 
/	Experience level is a geometric progression.  This has been finely
/	tuned over the years, and probably should not be changed.
/
*************************************************************************/

double
explevel(double experience)
{
	if (experience < 1.1e7)
		return (floor(pow((experience / 1000.0), 0.4875)));
	else
		return (floor(pow((experience / 1250.0), 0.4865)));
}
/**/
/************************************************************************
/
/ FUNCTION NAME: truncstring()
/
/ FUNCTION: truncate trailing blanks off a string
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: 
/	char *string - pointer to null terminated string
/
/ RETURN VALUE: none
/
/ MODULES CALLED: strlen()
/
/ GLOBAL INPUTS: none
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION: 
/	Put nul characters in place of spaces at the end of the string.
/
*************************************************************************/

void
truncstring(char *string)
{
	int     length;		/* length of string */

	length = strlen(string);
	while (string[--length] == ' ')
		string[length] = '\0';
}
/**/
/************************************************************************
/
/ FUNCTION NAME: altercoordinates()
/
/ FUNCTION: Alter x, y coordinates and set/check location flags
/
/ AUTHOR: E. A. Estes, 12/16/85
/
/ ARGUMENTS: 
/	double xnew, ynew - new x, y coordinates
/	int operation - operation to perform with coordinates
/
/ RETURN VALUE: none
/
/ MODULES CALLED: fabs(), floor(), drandom(), distance()
/
/ GLOBAL INPUTS: Circle, Beyond, Player
/
/ GLOBAL OUTPUTS: Marsh, Circle, Beyond, Throne, Player, Changed
/
/ DESCRIPTION: 
/	This module is called whenever the player's coordinates are altered.
/	If the player is beyond the point of no return, he/she is forced
/	to stay there.
/
*************************************************************************/

void
altercoordinates(double xnew, double ynew, int operation)
{
	switch (operation) {
	case A_FORCED:		/* move with no checks */
		break;

	case A_NEAR:		/* pick random coordinates near */
		xnew = Player.p_x + ROLL(1.0, 5.0);
		ynew = Player.p_y - ROLL(1.0, 5.0);
		/* fall through for check */

	case A_SPECIFIC:	/* just move player */
		if (Beyond && fabs(xnew) < D_BEYOND && fabs(ynew) < D_BEYOND)
			/*
			 * cannot move back from point of no return
			 * pick the largest coordinate to remain unchanged
			 */
		{
			if (fabs(xnew) > fabs(ynew))
				xnew = SGN(Player.p_x) * MAX(fabs(Player.p_x), D_BEYOND);
			else
				ynew = SGN(Player.p_y) * MAX(fabs(Player.p_y), D_BEYOND);
		}
		break;

	case A_FAR:		/* pick random coordinates far */
		xnew = Player.p_x + SGN(Player.p_x) * ROLL(50 * Circle, 250 * Circle);
		ynew = Player.p_y + SGN(Player.p_y) * ROLL(50 * Circle, 250 * Circle);
		break;
	}

	/* now set location flags and adjust coordinates */
	Circle = CIRCLE(Player.p_x = floor(xnew), Player.p_y = floor(ynew));

	/* set up flags based upon location */
	Throne = Marsh = Beyond = FALSE;

	if (Player.p_x == 0.0 && Player.p_y == 0.0)
		Throne = TRUE;
	else if (Circle < 35 && Circle >= 20)
		Marsh = TRUE;
	else if (MAX(fabs(Player.p_x), fabs(Player.p_y)) >= D_BEYOND)
		Beyond = TRUE;

	Changed = TRUE;
}
/**/
/************************************************************************
/
/ FUNCTION NAME: readrecord()
/
/ FUNCTION: read a player structure from file
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	struct player *playerp - pointer to structure to fill
/	int loc - location of record to read
/
/ RETURN VALUE: none
/
/ MODULES CALLED: fread(), fseek()
/
/ GLOBAL INPUTS: *Playersfp
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Read structure information from player file.
/
*************************************************************************/

void
readrecord(struct player *playerp, long loc)
{
	fseek(Playersfp, loc, SEEK_SET);
	fread(playerp, SZ_PLAYERSTRUCT, 1, Playersfp);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: adjuststats()
/
/ FUNCTION: adjust player statistics
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: death(), floor(), drandom(), explevel(), movelevel()
/
/ GLOBAL INPUTS: Player, *Statptr
/
/ GLOBAL OUTPUTS: Circle, Player, Timeout
/
/ DESCRIPTION:
/	Handle adjustment and maximums on various player characteristics.
/
*************************************************************************/

void
adjuststats(void)
{
	double  dtemp;		/* for temporary calculations */

	if (explevel(Player.p_experience) > Player.p_level)
		/* move one or more levels */
	{
		movelevel();
		if (Player.p_level > 5.0)
			Timeout = TRUE;
	}
	if (Player.p_specialtype == SC_VALAR)
		/* valar */
		Circle = Player.p_level / 5.0;

	/* calculate effective quickness */
	dtemp = ((Player.p_gold + Player.p_gems / 2.0) - 1000.0) / Statptr->c_goldtote
	    - Player.p_level;
	dtemp = MAX(0.0, dtemp);/* gold slows player down */
	Player.p_speed = Player.p_quickness + Player.p_quksilver - dtemp;

	/* calculate effective strength */
	if (Player.p_poison > 0.0)
		/* poison makes player weaker */
	{
		dtemp = 1.0 - Player.p_poison * Statptr->c_weakness / 800.0;
		dtemp = MAX(0.1, dtemp);
	} else
		dtemp = 1.0;
	Player.p_might = dtemp * Player.p_strength + Player.p_sword;

	/* insure that important things are within limits */
	Player.p_quksilver = MIN(99.0, Player.p_quksilver);
	Player.p_mana = MIN(Player.p_mana,
	    Player.p_level * Statptr->c_maxmana + 1000.0);
	Player.p_brains = MIN(Player.p_brains,
	    Player.p_level * Statptr->c_maxbrains + 200.0);
	Player.p_charms = MIN(Player.p_charms, Player.p_level + 10.0);

	/*
         * some implementations have problems with floating point compare
         * we work around it with this stuff
         */
	Player.p_gold = floor(Player.p_gold) + 0.1;
	Player.p_gems = floor(Player.p_gems) + 0.1;
	Player.p_mana = floor(Player.p_mana) + 0.1;

	if (Player.p_ring.ring_type != R_NONE)
		/* do ring things */
	{
		/* rest to max */
		Player.p_energy = Player.p_maxenergy + Player.p_shield;

		if (Player.p_ring.ring_duration <= 0)
			/* clean up expired rings */
			switch (Player.p_ring.ring_type) {
			case R_BAD:	/* ring drives player crazy */
				Player.p_ring.ring_type = R_SPOILED;
				Player.p_ring.ring_duration = (short) ROLL(10.0, 25.0);
				break;

			case R_NAZREG:	/* ring disappears */
				Player.p_ring.ring_type = R_NONE;
				break;

			case R_SPOILED:	/* ring kills player */
				death("A cursed ring");
				break;

			case R_DLREG:	/* this ring doesn't expire */
				Player.p_ring.ring_duration = 0;
				break;
			}
	}
	if (Player.p_age / N_AGE > Player.p_degenerated)
		/* age player slightly */
	{
		++Player.p_degenerated;
		if (Player.p_quickness > 23.0)
			Player.p_quickness *= 0.99;
		Player.p_strength *= 0.97;
		Player.p_brains *= 0.95;
		Player.p_magiclvl *= 0.97;
		Player.p_maxenergy *= 0.95;
		Player.p_quksilver *= 0.95;
		Player.p_sword *= 0.93;
		Player.p_shield *= 0.93;
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: initplayer()
/
/ FUNCTION: initialize a character
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	struct player *playerp - pointer to structure to init
/
/ RETURN VALUE: none
/
/ MODULES CALLED: floor(), drandom()
/
/ GLOBAL INPUTS: none
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Put a bunch of default values in the given structure.
/
*************************************************************************/

void
initplayer(struct player *playerp)
{
	playerp->p_experience =
	    playerp->p_level =
	    playerp->p_strength =
	    playerp->p_sword =
	    playerp->p_might =
	    playerp->p_energy =
	    playerp->p_maxenergy =
	    playerp->p_shield =
	    playerp->p_quickness =
	    playerp->p_quksilver =
	    playerp->p_speed =
	    playerp->p_magiclvl =
	    playerp->p_mana =
	    playerp->p_brains =
	    playerp->p_poison =
	    playerp->p_gems =
	    playerp->p_sin =
	    playerp->p_1scratch =
	    playerp->p_2scratch = 0.0;

	playerp->p_gold = ROLL(50.0, 75.0) + 0.1;	/* give some gold */

	playerp->p_x = ROLL(-125.0, 251.0);
	playerp->p_y = ROLL(-125.0, 251.0);	/* give random x, y */

	/* clear ring */
	playerp->p_ring.ring_type = R_NONE;
	playerp->p_ring.ring_duration = 0;
	playerp->p_ring.ring_inuse = FALSE;

	playerp->p_age = 0L;

	playerp->p_degenerated = 1;	/* don't degenerate initially */

	playerp->p_type = C_FIGHTER;	/* default */
	playerp->p_specialtype = SC_NONE;
	playerp->p_lives =
	    playerp->p_crowns =
	    playerp->p_charms =
	    playerp->p_amulets =
	    playerp->p_holywater =
	    playerp->p_lastused = 0;
	playerp->p_status = S_NOTUSED;
	playerp->p_tampered = T_OFF;
	playerp->p_istat = I_OFF;

	playerp->p_palantir =
	    playerp->p_blessing =
	    playerp->p_virgin =
	    playerp->p_blindness = FALSE;

	playerp->p_name[0] =
	    playerp->p_password[0] =
	    playerp->p_login[0] = '\0';
}
/**/
/************************************************************************
/
/ FUNCTION NAME: readmessage()
/
/ FUNCTION: read message from other players
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: fseek(), fgets(), wmove(), waddstr(), wclrtoeol()
/
/ GLOBAL INPUTS: *stdscr, Databuf[], *Messagefp
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	If there is a message from other players, print it.
/
*************************************************************************/

void
readmessage(void)
{
	move(3, 0);
	clrtoeol();
	fseek(Messagefp, 0L, SEEK_SET);
	if (fgets(Databuf, SZ_DATABUF, Messagefp) != NULL)
		addstr(Databuf);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: error()
/
/ FUNCTION: process environment error
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	char *whichfile - pointer to name of file which caused error
/
/ RETURN VALUE: none
/
/ MODULES CALLED: wclear(), cleanup()
/
/ GLOBAL INPUTS: errno, *stdscr, printf(), Windows
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Print message about offending file, and exit.
/
*************************************************************************/

__dead void
error(char *whichfile)
{

	if (Windows)
		clear();
	cleanup(FALSE);

	warn("%s", whichfile);
	fprintf(stderr, "Please run 'setup' to determine the problem.\n");
	exit(1);
}
/**/
/************************************************************************
/
/ FUNCTION NAME: distance()
/
/ FUNCTION: calculate distance between two points
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: 
/	double x1, y1 - x, y coordinates of first point
/	double x2, y2 - x, y coordinates of second point
/
/ RETURN VALUE: distance between the two points
/
/ MODULES CALLED: sqrt()
/
/ GLOBAL INPUTS: none
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	This function is provided because someone's hypot() library function
/	fails if x1 == x2 && y1 == y2.
/
*************************************************************************/

double
distance(double x1, double x2, double y1, double y2)
{
	double  deltax, deltay;

	deltax = x1 - x2;
	deltay = y1 - y2;
	return (sqrt(deltax * deltax + deltay * deltay));
}
/************************************************************************
/
/ FUNCTION NAME: descrstatus()
/
/ FUNCTION: return a string describing the player status
/
/ AUTHOR: E. A. Estes, 3/3/86
/
/ ARGUMENTS:
/	struct player playerp - pointer to player structure to describe
/
/ RETURN VALUE: string describing player's status
/
/ MODULES CALLED: none
/
/ GLOBAL INPUTS: none
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Return verbal description of player status.
/	If player status is S_PLAYING, check for low energy and blindness.
/
*************************************************************************/

char *
descrstatus(struct player *playerp)
{
	switch (playerp->p_status) {
	case S_PLAYING:
		if (playerp->p_energy < 0.2 * (playerp->p_maxenergy + playerp->p_shield))
			return ("Low Energy");
		else if (playerp->p_blindness)
			return ("Blind");
		else
			return ("In game");

	case S_CLOAKED:
		return ("Cloaked");

	case S_INBATTLE:
		return ("In Battle");

	case S_MONSTER:
		return ("Encounter");

	case S_TRADING:
		return ("Trading");

	case S_OFF:
		return ("Off");

	case S_HUNGUP:
		return ("Hung up");

	default:
		return ("");
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: drandom()
/
/ FUNCTION: return a random floating point number from 0.0 < 1.0
/
/ AUTHOR: E. A. Estes, 2/7/86
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: arc4random()
/
/ GLOBAL INPUTS: none
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Convert random integer from library routine into a floating
/	point number, and divide by the largest possible random number.
/
*************************************************************************/

double
drandom(void)
{
	return ((double) arc4random() / (UINT32_MAX + 1.0));
}
/**/
/************************************************************************
/
/ FUNCTION NAME: collecttaxes()
/
/ FUNCTION: collect taxes from current player
/
/ AUTHOR: E. A. Estes, 2/7/86
/
/ ARGUMENTS:
/	double gold - amount of gold to tax
/	double gems - amount of gems to tax
/
/ RETURN VALUE: none
/
/ MODULES CALLED: fread(), fseek(), fopen(), floor(), fwrite(), fclose()
/
/ GLOBAL INPUTS: Player
/
/ GLOBAL OUTPUTS: Player
/
/ DESCRIPTION:
/	Pay taxes on gold and gems.  If the player does not have enough
/	gold to pay taxes on the added gems, convert some gems to gold.
/	Add taxes to tax data base; add remaining gold and gems to
/	player's cache.
/
*************************************************************************/

void
collecttaxes(double gold, double gems)
{
	FILE   *fp;		/* to update Goldfile */
	double  dtemp;		/* for temporary calculations */
	double  taxes;		/* tax liability */

	/* add to cache */
	Player.p_gold += gold;
	Player.p_gems += gems;

	/* calculate tax liability */
	taxes = N_TAXAMOUNT / 100.0 * (N_GEMVALUE * gems + gold);

	if (Player.p_gold < taxes)
		/* not enough gold to pay taxes, must convert some gems to
		 * gold */
	{
		dtemp = floor(taxes / N_GEMVALUE + 1.0);	/* number of gems to
								 * convert */

		if (Player.p_gems >= dtemp)
			/* player has enough to convert */
		{
			Player.p_gems -= dtemp;
			Player.p_gold += dtemp * N_GEMVALUE;
		} else
			/* take everything; this should never happen */
		{
			Player.p_gold += Player.p_gems * N_GEMVALUE;
			Player.p_gems = 0.0;
			taxes = Player.p_gold;
		}
	}
	Player.p_gold -= taxes;

	if ((fp = fopen(_PATH_GOLD, "r+")) != NULL)
		/* update taxes */
	{
		dtemp = 0.0;
		fread(&dtemp, sizeof(double), 1, fp);
		dtemp += floor(taxes);
		fseek(fp, 0L, SEEK_SET);
		fwrite(&dtemp, sizeof(double), 1, fp);
		fclose(fp);
	}
}
