/*	$OpenBSD: fight.c,v 1.14 2016/01/10 13:35:09 mestre Exp $	*/
/*	$NetBSD: fight.c,v 1.2 1995/03/24 03:58:39 cgd Exp $	*/

/*
 * fight.c   Phantasia monster fighting routines
 */

#include <curses.h>
#include <math.h>
#include <setjmp.h>
#include <string.h>

#include "macros.h"
#include "phantdefs.h"
#include "phantglobs.h"

static jmp_buf Fightenv;	/* used to jump into fight routine */

/************************************************************************
/
/ FUNCTION NAME: encounter()
/
/ FUNCTION: monster battle routine
/
/ AUTHOR: E. A. Estes, 2/20/86
/
/ ARGUMENTS:
/	int particular - particular monster to fight if >= 0
/
/ RETURN VALUE: none
/
/ MODULES CALLED: monsthits(), playerhits(), readmessage(), callmonster(), 
/	writerecord(), pickmonster(), displaystats(), pow(), cancelmonster(), 
/	awardtreasure(), more(), death(), wmove(), setjmp(), drandom(), printw(), 
/	longjmp(), wrefresh(), mvprintw(), wclrtobot()
/
/ GLOBAL INPUTS: Curmonster, Whichmonster, LINES, Lines, Circle, Shield, 
/	Player, *stdscr, Fileloc, Fightenv[], *Enemyname
/
/ GLOBAL OUTPUTS: Curmonster, Whichmonster, Lines, Shield, Player, Luckout
/
/ DESCRIPTION:
/	Choose a monster and check against some special types.
/	Arbitrate between monster and player.  Watch for either
/	dying.
/
*************************************************************************/

void
encounter(int particular)
{
	int flockcnt = 1;	/* how many time flocked */
	volatile bool firsthit = Player.p_blessing;	/* set if player gets
							 * the first hit */

	/* let others know what we are doing */
	Player.p_status = S_MONSTER;
	writerecord(&Player, Fileloc);

	Shield = 0.0;		/* no shield up yet */

	if (particular >= 0)
		/* monster is specified */
		Whichmonster = particular;
	else
		/* pick random monster */
		Whichmonster = pickmonster();

	setjmp(Fightenv);	/* this is to enable changing fight state */

	move(6, 0);
	clrtobot();		/* clear bottom area of screen */

	Lines = 9;
	callmonster(Whichmonster);	/* set up monster to fight */

	Luckout = FALSE;	/* haven't tried to luckout yet */

	if (Curmonster.m_type == SM_MORGOTH)
		mvprintw(4, 0, "You've encountered %s, Bane of the Council and Valar.\n",
		    Enemyname);

	if (Curmonster.m_type == SM_UNICORN) {
		if (Player.p_virgin) {
			printw("You just subdued %s, thanks to the virgin.\n", Enemyname);
			Player.p_virgin = FALSE;
		} else {
			printw("You just saw %s running away!\n", Enemyname);
			Curmonster.m_experience = 0.0;
			Curmonster.m_treasuretype = 0;
		}
	} else
		/* not a special monster */
		for (;;)
			/* print header, and arbitrate between player and
			 * monster */
		{
			mvprintw(6, 0, "You are being attacked by %s,   EXP: %.0f   (Size: %.0f)\n",
			    Enemyname, Curmonster.m_experience, Circle);

			displaystats();
			mvprintw(1, 26, "%20.0f", Player.p_energy + Shield);	/* overprint energy */
			readmessage();

			if (Curmonster.m_type == SM_DARKLORD
			    && Player.p_blessing
			    && Player.p_charms > 0)
				/* overpower Dark Lord with blessing and charm */
			{
				mvprintw(7, 0, "You just overpowered %s!", Enemyname);
				Lines = 8;
				Player.p_blessing = FALSE;
				--Player.p_charms;
				break;
			}
			/* allow paralyzed monster to wake up */
			Curmonster.m_speed = MIN(Curmonster.m_speed + 1.0, Curmonster.m_maxspeed);

			if (drandom() * Curmonster.m_speed > drandom() * Player.p_speed
			/* monster is faster */
			    && Curmonster.m_type != SM_DARKLORD
			/* not D. L. */
			    && Curmonster.m_type != SM_SHRIEKER
			/* not mimic */
			    && !firsthit)
				/* monster gets a hit */
				monsthits();
			else
				/* player gets a hit */
			{
				firsthit = FALSE;
				playerhits();
			}

			refresh();

			if (Lines > LINES - 2)
				/* near bottom of screen - pause */
			{
				more(Lines);
				move(Lines = 8, 0);
				clrtobot();
			}
			if (Player.p_energy <= 0.0)
				/* player died */
			{
				more(Lines);
				death(Enemyname);
				cancelmonster();
				break;	/* fight ends if the player is saved
					 * from death */
			}
			if (Curmonster.m_energy <= 0.0)
				/* monster died */
				break;
		}

	/* give player credit for killing monster */
	Player.p_experience += Curmonster.m_experience;

	if (drandom() < Curmonster.m_flock / 100.0)
		/* monster flocks */
	{
		more(Lines);
		++flockcnt;
		longjmp(Fightenv, 1);
	} else
		if (Circle > 1.0
		    && Curmonster.m_treasuretype > 0
		    && drandom() > 0.2 + pow(0.4, (double) (flockcnt / 3 + Circle / 3.0)))
			/* monster has treasure; this takes # of flocks and
			 * size into account */
		{
			more(Lines);
			awardtreasure();
		}
	/* pause before returning */
	getyx(stdscr, Lines, flockcnt);
	more(Lines + 1);

	Player.p_ring.ring_inuse = FALSE;	/* not using ring */

	/* clean up the screen */
	move(4, 0);
	clrtobot();
}
/**/
/************************************************************************
/
/ FUNCTION NAME: pickmonster()
/
/ FUNCTION: choose a monster based upon where we are
/
/ AUTHOR: E. A. Estes, 2/20/86
/
/ ARGUMENTS: none
/
/ RETURN VALUE: monster number to call
/
/ MODULES CALLED: floor(), drandom()
/
/ GLOBAL INPUTS: Marsh, Circle, Player
/
/ GLOBAL OUTPUTS: none
/
/ DESCRIPTION:
/	Certain monsters can be found in certain areas of the grid.
/	We take care of rolling them here.
/	Unfortunately, this routine assumes that the monster data
/	base is arranged in a particular order.  If the data base
/	is altered (to add monsters, or make them tougher), this
/	routine may also need to be changed.
/
*************************************************************************/

int
pickmonster(void)
{
	if (Player.p_specialtype == SC_VALAR)
		/* even chance of any monster */
		return ((int) ROLL(0.0, 100.0));

	if (Marsh)
		/* water monsters */
		return ((int) ROLL(0.0, 15.0));

	else if (Circle > 24)
		/* even chance of all non-water monsters */
		return ((int) ROLL(14.0, 86.0));

	else if (Circle > 15)
		/* chance of all non-water monsters, weighted toward middle */
		return ((int) (ROLL(0.0, 50.0) + ROLL(14.0, 37.0)));

	else if (Circle > 8)
		/* not all non-water monsters, weighted toward middle */
		return ((int) (ROLL(0.0, 50.0) + ROLL(14.0, 26.0)));

	else if (Circle > 3)
		/* even chance of some tamer non-water monsters */
		return ((int) ROLL(14.0, 50.0));

	else
		/* even chance of some of the tamest non-water monsters */
		return ((int) ROLL(14.0, 25.0));
}
/**/
/************************************************************************
/
/ FUNCTION NAME: playerhits()
/
/ FUNCTION: prompt player for action in monster battle, and process
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: hitmonster(), throwspell(), inputoption(), cancelmonster(), 
/	floor(), wmove(), drandom(), altercoordinates(), waddstr(), mvprintw(), 
/	wclrtoeol(), wclrtobot()
/
/ GLOBAL INPUTS: Curmonster, Lines, Player, *stdscr, Luckout, *Enemyname
/
/ GLOBAL OUTPUTS: Curmonster, Lines, Player, Luckout
/
/ DESCRIPTION:
/	Process all monster battle options.
/
*************************************************************************/

void
playerhits(void)
{
	double  inflict;	/* damage inflicted */
	int     ch;		/* input */

	mvaddstr(7, 0, "1:Melee  2:Skirmish  3:Evade  4:Spell  5:Nick  ");

	if (!Luckout) {
		/* haven't tried to luckout yet */
		if (Curmonster.m_type == SM_MORGOTH)
			/* cannot luckout against Morgoth */
			addstr("6:Ally  ");
		else
			addstr("6:Luckout  ");
	}

	if (Player.p_ring.ring_type != R_NONE)
		/* player has a ring */
		addstr("7:Use Ring  ");
	else
		clrtoeol();

	ch = inputoption();

	move(8, 0);
	clrtobot();		/* clear any messages from before */
	Lines = 9;
	mvaddstr(4, 0, "\n\n");	/* clear status area */

	switch (ch) {
	case 'T':		/* timeout; lose turn */
		break;

	case ' ':
	case '1':		/* melee */
		/* melee affects monster's energy and strength */
		inflict = ROLL(Player.p_might / 2.0 + 5.0, 1.3 * Player.p_might)
		    + (Player.p_ring.ring_inuse ? Player.p_might : 0.0);

		Curmonster.m_melee += inflict;
		Curmonster.m_strength = Curmonster.m_o_strength
		    - Curmonster.m_melee / Curmonster.m_o_energy
		    * Curmonster.m_o_strength / 4.0;
		hitmonster(inflict);
		break;

	case '2':		/* skirmish */
		/* skirmish affects monter's energy and speed */
		inflict = ROLL(Player.p_might / 3.0 + 3.0, 1.1 * Player.p_might)
		    + (Player.p_ring.ring_inuse ? Player.p_might : 0.0);

		Curmonster.m_skirmish += inflict;
		Curmonster.m_maxspeed = Curmonster.m_o_speed
		    - Curmonster.m_skirmish / Curmonster.m_o_energy
		    * Curmonster.m_o_speed / 4.0;
		hitmonster(inflict);
		break;

	case '3':		/* evade */
		/* use brains and speed to try to evade */
		if ((Curmonster.m_type == SM_DARKLORD
			|| Curmonster.m_type == SM_SHRIEKER
		/* can always run from D. L. and shrieker */
			|| drandom() * Player.p_speed * Player.p_brains
			> drandom() * Curmonster.m_speed * Curmonster.m_brains)
		    && (Curmonster.m_type != SM_MIMIC))
			/* cannot run from mimic */
		{
			mvaddstr(Lines++, 0, "You got away!");
			cancelmonster();
			altercoordinates(0.0, 0.0, A_NEAR);
		} else
			mvprintw(Lines++, 0, "%s is still after you!", Enemyname);

		break;

	case 'M':
	case '4':		/* magic spell */
		throwspell();
		break;

	case '5':		/* nick */
		/* hit 1 plus sword; give some experience */
		inflict = 1.0 + Player.p_sword;
		Player.p_experience += floor(Curmonster.m_experience / 10.0);
		Curmonster.m_experience *= 0.92;
		/* monster gets meaner */
		Curmonster.m_maxspeed += 2.0;
		Curmonster.m_speed = (Curmonster.m_speed < 0.0) ? 0.0 : Curmonster.m_speed + 2.0;
		if (Curmonster.m_type == SM_DARKLORD)
			/* Dark Lord; doesn't like to be nicked */
		{
			mvprintw(Lines++, 0,
			    "You hit %s %.0f times, and made him mad!", Enemyname, inflict);
			Player.p_quickness /= 2.0;
			altercoordinates(0.0, 0.0, A_FAR);
			cancelmonster();
		} else
			hitmonster(inflict);
		break;

	case 'B':
	case '6':		/* luckout */
		if (Luckout)
			mvaddstr(Lines++, 0, "You already tried that.");
		else {
			Luckout = TRUE;
			if (Curmonster.m_type == SM_MORGOTH)
				/* Morgoth; ally */
			{
				if (drandom() < Player.p_sin / 100.0) {
					mvprintw(Lines++, 0, "%s accepted!", Enemyname);
					cancelmonster();
				} else
					mvaddstr(Lines++, 0, "Nope, he's not interested.");
			} else
				/* normal monster; use brains for success */
			{
				if ((drandom() + 0.333) * Player.p_brains
				    < (drandom() + 0.333) * Curmonster.m_brains)
					mvprintw(Lines++, 0, "You blew it, %s.", Player.p_name);
				else {
					mvaddstr(Lines++, 0, "You made it!");
					Curmonster.m_energy = 0.0;
				}
			}
		}
		break;

	case '7':		/* use ring */
		if (Player.p_ring.ring_type != R_NONE) {
			mvaddstr(Lines++, 0, "Now using ring.");
			Player.p_ring.ring_inuse = TRUE;
			if (Player.p_ring.ring_type != R_DLREG)
				/* age ring */
				--Player.p_ring.ring_duration;
		}
		break;
	}

}
/**/
/************************************************************************
/
/ FUNCTION NAME: monsthits()
/
/ FUNCTION: process a monster hitting the player
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: cancelmonster(), scramblestats(), more(), floor(), wmove(), 
/	drandom(), altercoordinates(), longjmp(), waddstr(), mvprintw(), 
/	getanswer()
/
/ GLOBAL INPUTS: Curmonster, Lines, Circle, Shield, Player, *stdscr, 
/	Fightenv[], *Enemyname
/
/ GLOBAL OUTPUTS: Curmonster, Whichmonster, Lines, Shield, Player, 
/	*Enemyname
/
/ DESCRIPTION:
/	Handle all special monsters here.  If the monster is not a special
/	one, simply roll a hit against the player.
/
*************************************************************************/

void
monsthits(void)
{
	double  inflict;	/* damage inflicted */
	int     ch;		/* input */

	switch (Curmonster.m_type)
		/* may be a special monster */
	{
	case SM_DARKLORD:
		/* hits just enough to kill player */
		inflict = (Player.p_energy + Shield) * 1.02;
		goto SPECIALHIT;

	case SM_SHRIEKER:
		/* call a big monster */
		mvaddstr(Lines++, 0,
		    "Shrieeeek!!  You scared it, and it called one of its friends.");
		more(Lines);
		Whichmonster = (int) ROLL(70.0, 30.0);
		longjmp(Fightenv, 1);

	case SM_BALROG:
		/* take experience away */
		inflict = ROLL(10.0, Curmonster.m_strength);
		inflict = MIN(Player.p_experience, inflict);
		mvprintw(Lines++, 0,
		    "%s took away %.0f experience points.", Enemyname, inflict);
		Player.p_experience -= inflict;
		return;

	case SM_FAERIES:
		if (Player.p_holywater > 0)
			/* holy water kills when monster tries to hit */
		{
			mvprintw(Lines++, 0, "Your holy water killed it!");
			--Player.p_holywater;
			Curmonster.m_energy = 0.0;
			return;
		}
		break;

	case SM_NONE:
		/* normal hit */
		break;

	default:
		if (drandom() > 0.2)
			/* normal hit */
			break;

		/* else special things */
		switch (Curmonster.m_type) {
		case SM_LEANAN:
			/* takes some of the player's strength */
			inflict = ROLL(1.0, (Circle - 1.0) / 2.0);
			inflict = MIN(Player.p_strength, inflict);
			mvprintw(Lines++, 0, "%s sapped %0.f of your strength!",
			    Enemyname, inflict);
			Player.p_strength -= inflict;
			Player.p_might -= inflict;
			break;

		case SM_SARUMAN:
			if (Player.p_palantir)
				/* take away palantir */
			{
				mvprintw(Lines++, 0, "Wormtongue stole your palantir!");
				Player.p_palantir = FALSE;
			} else
				if (drandom() > 0.5)
					/* gems turn to gold */
				{
					mvprintw(Lines++, 0,
					    "%s transformed your gems into gold!", Enemyname);
					Player.p_gold += Player.p_gems;
					Player.p_gems = 0.0;
				} else
					/* scramble some stats */
				{
					mvprintw(Lines++, 0, "%s scrambled your stats!", Enemyname);
					scramblestats();
				}
			break;

		case SM_THAUMATURG:
			/* transport player */
			mvprintw(Lines++, 0, "%s transported you!", Enemyname);
			altercoordinates(0.0, 0.0, A_FAR);
			cancelmonster();
			break;

		case SM_VORTEX:
			/* suck up some mana */
			inflict = ROLL(0, 7.5 * Circle);
			inflict = MIN(Player.p_mana, floor(inflict));
			mvprintw(Lines++, 0,
			    "%s sucked up %.0f of your mana!", Enemyname, inflict);
			Player.p_mana -= inflict;
			break;

		case SM_NAZGUL:
			/* try to take ring if player has one */
			if (Player.p_ring.ring_type != R_NONE)
				/* player has a ring */
			{
				mvaddstr(Lines++, 0, "Will you relinguish your ring ? ");
				ch = getanswer("YN", FALSE);
				if (ch == 'Y')
					/* take ring away */
				{
					Player.p_ring.ring_type = R_NONE;
					Player.p_ring.ring_inuse = FALSE;
					cancelmonster();
					break;
				}
			}
			/* otherwise, take some brains */
			mvprintw(Lines++, 0,
			    "%s neutralized 1/5 of your brain!", Enemyname);
			Player.p_brains *= 0.8;
			break;

		case SM_TIAMAT:
			/* take some gold and gems */
			mvprintw(Lines++, 0,
			    "%s took half your gold and gems and flew off.", Enemyname);
			Player.p_gold /= 2.0;
			Player.p_gems /= 2.0;
			cancelmonster();
			break;

		case SM_KOBOLD:
			/* steal a gold piece and run */
			mvprintw(Lines++, 0,
			    "%s stole one gold piece and ran away.", Enemyname);
			Player.p_gold = MAX(0.0, Player.p_gold - 1.0);
			cancelmonster();
			break;

		case SM_SHELOB:
			/* bite and (medium) poison */
			mvprintw(Lines++, 0,
			    "%s has bitten and poisoned you!", Enemyname);
			Player.p_poison -= 1.0;
			break;

		case SM_LAMPREY:
			/* bite and (small) poison */
			mvprintw(Lines++, 0, "%s bit and poisoned you!", Enemyname);
			Player.p_poison += 0.25;
			break;

		case SM_BONNACON:
			/* fart and run */
			mvprintw(Lines++, 0, "%s farted and scampered off.", Enemyname);
			Player.p_energy /= 2.0;	/* damage from fumes */
			cancelmonster();
			break;

		case SM_SMEAGOL:
			if (Player.p_ring.ring_type != R_NONE)
				/* try to steal ring */
			{
				mvprintw(Lines++, 0,
				    "%s tried to steal your ring, ", Enemyname);
				if (drandom() > 0.1)
					addstr("but was unsuccessful.");
				else {
					addstr("and ran away with it!");
					Player.p_ring.ring_type = R_NONE;
					cancelmonster();
				}
			}
			break;

		case SM_SUCCUBUS:
			/* inflict damage through shield */
			inflict = ROLL(15.0, Circle * 10.0);
			inflict = MIN(inflict, Player.p_energy);
			mvprintw(Lines++, 0, "%s sapped %.0f of your energy.",
			    Enemyname, inflict);
			Player.p_energy -= inflict;
			break;

		case SM_CERBERUS:
			/* take all metal treasures */
			mvprintw(Lines++, 0,
			    "%s took all your metal treasures!", Enemyname);
			Player.p_crowns = 0;
			Player.p_sword =
			    Player.p_shield =
			    Player.p_gold = 0.0;
			cancelmonster();
			break;

		case SM_UNGOLIANT:
			/* (large) poison and take a quickness */
			mvprintw(Lines++, 0,
			    "%s poisoned you, and took one quik.", Enemyname);
			Player.p_poison += 5.0;
			Player.p_quickness -= 1.0;
			break;

		case SM_JABBERWOCK:
			/* fly away, and leave either a Jubjub bird or
			 * Bonnacon */
			mvprintw(Lines++, 0,
			    "%s flew away, and left you to contend with one of its friends.",
			    Enemyname);
			Whichmonster = 55 + ((drandom() > 0.5) ? 22 : 0);
			longjmp(Fightenv, 1);

		case SM_TROLL:
			/* partially regenerate monster */
			mvprintw(Lines++, 0,
			    "%s partially regenerated his energy.!", Enemyname);
			Curmonster.m_energy +=
			    floor((Curmonster.m_o_energy - Curmonster.m_energy) / 2.0);
			Curmonster.m_strength = Curmonster.m_o_strength;
			Curmonster.m_melee = Curmonster.m_skirmish = 0.0;
			Curmonster.m_maxspeed = Curmonster.m_o_speed;
			break;

		case SM_WRAITH:
			if (!Player.p_blindness)
				/* make blind */
			{
				mvprintw(Lines++, 0, "%s blinded you!", Enemyname);
				Player.p_blindness = TRUE;
				Enemyname = "A monster";
			}
			break;
		}
		return;
	}

	/* fall through to here if monster inflicts a normal hit */
	inflict = drandom() * Curmonster.m_strength + 0.5;
SPECIALHIT:
	mvprintw(Lines++, 0, "%s hit you %.0f times!", Enemyname, inflict);

	if ((Shield -= inflict) < 0) {
		Player.p_energy += Shield;
		Shield = 0.0;
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: cancelmonster()
/
/ FUNCTION: mark current monster as no longer active
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: none
/
/ GLOBAL INPUTS: none
/
/ GLOBAL OUTPUTS: Curmonster
/
/ DESCRIPTION:
/	Clear current monster's energy, experience, treasure type, and
/	flock.  This is the same as having the monster run away.
/
*************************************************************************/

void
cancelmonster(void)
{
    Curmonster.m_energy = 0.0;
    Curmonster.m_experience = 0.0;
    Curmonster.m_treasuretype = 0;
    Curmonster.m_flock = 0.0;
}
/**/
/************************************************************************
/
/ FUNCTION NAME: hitmonster()
/
/ FUNCTION: inflict damage upon current monster
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS:
/	double inflict - damage to inflict upon monster
/
/ RETURN VALUE: none
/
/ MODULES CALLED: monsthits(), wmove(), strcmp(), waddstr(), mvprintw()
/
/ GLOBAL INPUTS: Curmonster, Lines, Player, *stdscr, *Enemyname
/
/ GLOBAL OUTPUTS: Curmonster, Lines
/
/ DESCRIPTION:
/	Hit monster specified number of times.  Handle when monster dies,
/	and a few special monsters.
/
*************************************************************************/

void
hitmonster(double inflict)
{
	mvprintw(Lines++, 0, "You hit %s %.0f times!", Enemyname, inflict);
	Curmonster.m_energy -= inflict;
	if (Curmonster.m_energy > 0.0) {
		if (Curmonster.m_type == SM_DARKLORD || Curmonster.m_type == SM_SHRIEKER)
			/* special monster didn't die */
			monsthits();
	} else
		/* monster died.  print message. */
	{
		if (Curmonster.m_type == SM_MORGOTH)
			mvaddstr(Lines++, 0, "You have defeated Morgoth, but he may return. . .");
		else
			/* all other types of monsters */
		{
			mvprintw(Lines++, 0, "You killed it.  Good work, %s.", Player.p_name);

			if (Curmonster.m_type == SM_MIMIC
			    && strcmp(Curmonster.m_name, "A Mimic") != 0
			    && !Player.p_blindness)
				mvaddstr(Lines++, 0, "The body slowly changes into the form of a mimic.");
		}
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: throwspell()
/
/ FUNCTION: throw a magic spell
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: hitmonster(), cancelmonster(), sqrt(), floor(), wmove(), 
/	drandom(), altercoordinates(), longjmp(), infloat(), waddstr(), mvprintw(), 
/	getanswer()
/
/ GLOBAL INPUTS: Curmonster, Whichmonster, Nomana[], Player, *stdscr, 
/	Fightenv[], Illspell[], *Enemyname
/
/ GLOBAL OUTPUTS: Curmonster, Whichmonster, Shield, Player
/
/ DESCRIPTION:
/	Prompt player and process magic spells.
/
*************************************************************************/

void
throwspell(void)
{
	double  inflict;	/* damage inflicted */
	double  dtemp;		/* for dtemporary calculations */
	int     ch;		/* input */

	inflict = 0;
	mvaddstr(7, 0, "\n\n");	/* clear menu area */

	if (Player.p_magiclvl >= ML_ALLORNOTHING)
		mvaddstr(7, 0, "1:All or Nothing  ");
	if (Player.p_magiclvl >= ML_MAGICBOLT)
		addstr("2:Magic Bolt  ");
	if (Player.p_magiclvl >= ML_FORCEFIELD)
		addstr("3:Force Field  ");
	if (Player.p_magiclvl >= ML_XFORM)
		addstr("4:Transform  ");
	if (Player.p_magiclvl >= ML_INCRMIGHT)
		addstr("5:Increase Might\n");
	if (Player.p_magiclvl >= ML_INVISIBLE)
		mvaddstr(8, 0, "6:Invisibility  ");
	if (Player.p_magiclvl >= ML_XPORT)
		addstr("7:Transport  ");
	if (Player.p_magiclvl >= ML_PARALYZE)
		addstr("8:Paralyze  ");
	if (Player.p_specialtype >= SC_COUNCIL)
		addstr("9:Specify");
	mvaddstr(4, 0, "Spell ? ");

	ch = getanswer(" ", TRUE);

	mvaddstr(7, 0, "\n\n");	/* clear menu area */

	if (Curmonster.m_type == SM_MORGOTH && ch != '3')
		/* can only throw force field against Morgoth */
		ILLSPELL();
	else
		switch (ch) {
		case '1':	/* all or nothing */
			if (drandom() < 0.25)
				/* success */
			{
				inflict = Curmonster.m_energy * 1.01 + 1.0;

				if (Curmonster.m_type == SM_DARKLORD)
					/* all or nothing doesn't quite work
					 * against D. L. */
					inflict *= 0.9;
			} else
				/* failure -- monster gets stronger and
				 * quicker */
			{
				Curmonster.m_o_strength = Curmonster.m_strength *= 2.0;
				Curmonster.m_maxspeed *= 2.0;
				Curmonster.m_o_speed *= 2.0;

				/* paralyzed monsters wake up a bit */
				Curmonster.m_speed = MAX(1.0, Curmonster.m_speed * 2.0);
			}

			if (Player.p_mana >= MM_ALLORNOTHING)
				/* take a mana if player has one */
				Player.p_mana -= MM_ALLORNOTHING;

			hitmonster(inflict);
			break;

		case '2':	/* magic bolt */
			if (Player.p_magiclvl < ML_MAGICBOLT)
				ILLSPELL();
			else {
				do
					/* prompt for amount to expend */
				{
					mvaddstr(4, 0, "How much mana for bolt? ");
					dtemp = floor(infloat());
				}
				while (dtemp < 0.0 || dtemp > Player.p_mana);

				Player.p_mana -= dtemp;

				if (Curmonster.m_type == SM_DARKLORD)
					/* magic bolts don't work against D.
					 * L. */
					inflict = 0.0;
				else
					inflict = dtemp * ROLL(15.0, sqrt(Player.p_magiclvl / 3.0 + 1.0));
				mvaddstr(5, 0, "Magic Bolt fired!\n");
				hitmonster(inflict);
			}
			break;

		case '3':	/* force field */
			if (Player.p_magiclvl < ML_FORCEFIELD)
				ILLSPELL();
			else if (Player.p_mana < MM_FORCEFIELD)
				NOMANA();
			else {
				Player.p_mana -= MM_FORCEFIELD;
				Shield = (Player.p_maxenergy + Player.p_shield) * 4.2 + 45.0;
				mvaddstr(5, 0, "Force Field up.\n");
			}
			break;

		case '4':	/* transform */
			if (Player.p_magiclvl < ML_XFORM)
				ILLSPELL();
			else if (Player.p_mana < MM_XFORM)
				NOMANA();
			else {
				Player.p_mana -= MM_XFORM;
				Whichmonster = (int) ROLL(0.0, 100.0);
				longjmp(Fightenv, 1);
				}
			break;

		case '5':	/* increase might */
			if (Player.p_magiclvl < ML_INCRMIGHT)
				ILLSPELL();
			else if (Player.p_mana < MM_INCRMIGHT)
				NOMANA();
			else {
				Player.p_mana -= MM_INCRMIGHT;
				Player.p_might +=
				    (1.2 * (Player.p_strength + Player.p_sword)
				    + 5.0 - Player.p_might) / 2.0;
				mvprintw(5, 0, "New strength:  %.0f\n", Player.p_might);
			}
			break;

		case '6':	/* invisible */
			if (Player.p_magiclvl < ML_INVISIBLE)
				ILLSPELL();
			else if (Player.p_mana < MM_INVISIBLE)
				NOMANA();
			else {
				Player.p_mana -= MM_INVISIBLE;
				Player.p_speed +=
				    (1.2 * (Player.p_quickness + Player.p_quksilver)
				    + 5.0 - Player.p_speed) / 2.0;
				mvprintw(5, 0, "New quickness:  %.0f\n", Player.p_speed);
			}
			break;

		case '7':	/* transport */
			if (Player.p_magiclvl < ML_XPORT)
				ILLSPELL();
			else if (Player.p_mana < MM_XPORT)
				NOMANA();
			else {
				Player.p_mana -= MM_XPORT;
				if (Player.p_brains + Player.p_magiclvl
				    < Curmonster.m_experience / 200.0 * drandom()) {
					mvaddstr(5, 0, "Transport backfired!\n");
					altercoordinates(0.0, 0.0, A_FAR);
					cancelmonster();
				} else {
					mvprintw(5, 0, "%s is transported.\n", Enemyname);
					if (drandom() < 0.3)
						/* monster didn't drop
						 * its treasure */
						Curmonster.m_treasuretype = 0;

					Curmonster.m_energy = 0.0;
				}
			}
			break;

		case '8':	/* paralyze */
			if (Player.p_magiclvl < ML_PARALYZE)
				ILLSPELL();
			else if (Player.p_mana < MM_PARALYZE)
				NOMANA();
			else {
				Player.p_mana -= MM_PARALYZE;
				if (Player.p_magiclvl >
				    Curmonster.m_experience / 1000.0 * drandom()) {
					mvprintw(5, 0, "%s is held.\n", Enemyname);
					Curmonster.m_speed = -2.0;
				} else
					mvaddstr(5, 0, "Monster unaffected.\n");
			}
			break;

		case '9':	/* specify */
			if (Player.p_specialtype < SC_COUNCIL)
				ILLSPELL();
			else if (Player.p_mana < MM_SPECIFY)
				NOMANA();
			else {
				Player.p_mana -= MM_SPECIFY;
				mvaddstr(5, 0, "Which monster do you want [0-99] ? ");
				Whichmonster = (int) infloat();
				Whichmonster = MAX(0, MIN(99, Whichmonster));
				longjmp(Fightenv, 1);
			}
			break;
		}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: callmonster()
/
/ FUNCTION: read monster from file, and fill structure
/
/ AUTHOR: E. A. Estes, 2/25/86
/
/ ARGUMENTS:
/	int which - which monster to call
/
/ RETURN VALUE: none
/
/ MODULES CALLED: truncstring(), fread(), fseek(), floor(), drandom(), 
/	strlcpy()
/
/ GLOBAL INPUTS: Curmonster, Circle, Player, *Monstfp
/
/ GLOBAL OUTPUTS: Curmonster, Player, *Enemyname
/
/ DESCRIPTION:
/	Read specified monster from monster database and fill up
/	current monster structure.
/	Adjust statistics based upon current size.
/	Handle some special monsters.
/
*************************************************************************/

void
callmonster(int which)
{
	struct monster Othermonster;	/* to find a name for mimics */

	which = MIN(which, 99);	/* make sure within range */

	/* fill structure */
	fseek(Monstfp, (long) which * (long) SZ_MONSTERSTRUCT, SEEK_SET);
	fread(&Curmonster, SZ_MONSTERSTRUCT, 1, Monstfp);

	/* handle some special monsters */
	if (Curmonster.m_type == SM_MODNAR) {
		if (Player.p_specialtype < SC_COUNCIL)
			/* randomize some stats */
		{
			Curmonster.m_strength *= drandom() + 0.5;
			Curmonster.m_brains *= drandom() + 0.5;
			Curmonster.m_speed *= drandom() + 0.5;
			Curmonster.m_energy *= drandom() + 0.5;
			Curmonster.m_experience *= drandom() + 0.5;
			Curmonster.m_treasuretype =
			    (int) ROLL(0.0, (double) Curmonster.m_treasuretype);
		} else
			/* make Modnar into Morgoth */
		{
			strlcpy(Curmonster.m_name, "Morgoth",
			    sizeof Curmonster.m_name);
			Curmonster.m_strength = drandom() * (Player.p_maxenergy + Player.p_shield) / 1.4
			    + drandom() * (Player.p_maxenergy + Player.p_shield) / 1.5;
			Curmonster.m_brains = Player.p_brains;
			Curmonster.m_energy = Player.p_might * 30.0;
			Curmonster.m_type = SM_MORGOTH;
			Curmonster.m_speed = Player.p_speed * 1.1
			    + ((Player.p_specialtype == SC_EXVALAR) ? Player.p_speed : 0.0);
			Curmonster.m_flock = 0.0;
			Curmonster.m_treasuretype = 0;
			Curmonster.m_experience = 0.0;
		}
	} else
		if (Curmonster.m_type == SM_MIMIC)
			/* pick another name */
		{
			which = (int) ROLL(0.0, 100.0);
			fseek(Monstfp, (long) which * (long) SZ_MONSTERSTRUCT, SEEK_SET);
			fread(&Othermonster, SZ_MONSTERSTRUCT, 1, Monstfp);
			strlcpy(Curmonster.m_name, Othermonster.m_name,
			    sizeof Curmonster.m_name);
		}
	truncstring(Curmonster.m_name);

	if (Curmonster.m_type != SM_MORGOTH)
		/* adjust stats based on which circle player is in */
	{
		Curmonster.m_strength *= (1.0 + Circle / 2.0);
		Curmonster.m_brains *= Circle;
		Curmonster.m_speed += Circle * 1.e-9;
		Curmonster.m_energy *= Circle;
		Curmonster.m_experience *= Circle;
	}
	if (Player.p_blindness)
		/* cannot see monster if blind */
		Enemyname = "A monster";
	else
		Enemyname = Curmonster.m_name;

	if (Player.p_speed <= 0.0)
		/* make Player.p_speed positive */
	{
		Curmonster.m_speed += -Player.p_speed;
		Player.p_speed = 1.0;
	}
	/* fill up the rest of the structure */
	Curmonster.m_o_strength = Curmonster.m_strength;
	Curmonster.m_o_speed = Curmonster.m_maxspeed = Curmonster.m_speed;
	Curmonster.m_o_energy = Curmonster.m_energy;
	Curmonster.m_melee = Curmonster.m_skirmish = 0.0;
}
/**/
/************************************************************************
/
/ FUNCTION NAME: awardtreasure()
/
/ FUNCTION: select a treasure
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: pickmonster(), collecttaxes(), more(), cursedtreasure(), 
/	floor(), wmove(), drandom(), sscanf(), printw(), altercoordinates(), 
/	longjmp(), infloat(), waddstr(), getanswer(), getstring(), wclrtobot()
/
/ GLOBAL INPUTS: Somebetter[], Curmonster, Whichmonster, Circle, Player, 
/	*stdscr, Databuf[], *Statptr, Fightenv[]
/
/ GLOBAL OUTPUTS: Whichmonster, Shield, Player
/
/ DESCRIPTION:
/	Roll up a treasure based upon monster type and size, and
/	certain player statistics.
/	Handle cursed treasure.
/
*************************************************************************/

void
awardtreasure(void)
{
	int	whichtreasure;	/* calculated treasure to grant */
	int	temp;		/* temporary */
	int	ch;		/* input */
	double	treasuretype;	/* monster's treasure type */
	double	gold = 0.0;	/* gold awarded */
	double	gems = 0.0;	/* gems awarded */
	double	dtemp;		/* for temporary calculations */

    whichtreasure = (int) ROLL(1.0, 3.0);	/* pick a treasure */
    treasuretype = (double) Curmonster.m_treasuretype;

    move(4, 0);
    clrtobot();
    move(6, 0);

    if (drandom() > 0.65)
	/* gold and gems */
	{
	if (Curmonster.m_treasuretype > 7)
	    /* gems */
	    {
	    gems = ROLL(1.0, (treasuretype - 7.0)
		* (treasuretype - 7.0) * (Circle - 1.0) / 4.0);
	    printw("You have discovered %.0f gems!", gems);
	} else
	    /* gold */
	    {
	    gold = ROLL(treasuretype * 10.0, treasuretype
		* treasuretype * 10.0 * (Circle - 1.0));
	    printw("You have found %.0f gold pieces.", gold);
	    }

	addstr("  Do you want to pick them up ? ");
	ch = getanswer("NY", FALSE);
	addstr("\n\n");

	if (ch == 'Y') {
	    if (drandom() < treasuretype / 35.0 + 0.04)
		/* cursed */
		{
		addstr("They were cursed!\n");
		cursedtreasure();
		}
	    else
		collecttaxes(gold, gems);
	}

	return;
	}
    else   
	/* other treasures */
	{
	addstr("You have found some treasure.  Do you want to inspect it ? ");
	ch = getanswer("NY", FALSE);
	addstr("\n\n");

	if (ch != 'Y')
	    return;
	else
	    if (drandom() < 0.08 && Curmonster.m_treasuretype != 4)
		{
		addstr("It was cursed!\n");
		cursedtreasure();
		return;
		}
	    else
		switch (Curmonster.m_treasuretype) {
		    case 1:	/* treasure type 1 */
			switch (whichtreasure) {
			    case 1:
				addstr("You've discovered a power booster!\n");
				Player.p_mana += ROLL(Circle * 4.0, Circle * 30.0);
				break;

			    case 2:
				addstr("You have encountered a druid.\n");
				Player.p_experience +=
				    ROLL(0.0, 2000.0 + Circle * 400.0);
				break;

			    case 3:
				addstr("You have found a holy orb.\n");
				Player.p_sin = MAX(0.0, Player.p_sin - 0.25);
				break;
			    }
			break;
		    /* end treasure type 1 */

		    case 2:	/* treasure type 2 */
			switch (whichtreasure) {
			    case 1:
				addstr("You have found an amulet.\n");
				++Player.p_amulets;
				break;

			    case 2:
				addstr("You've found some holy water!\n");
				++Player.p_holywater;
				break;

			    case 3:
				addstr("You've met a hermit!\n");
				Player.p_sin *= 0.75;
				Player.p_mana += 12.0 * Circle;
				break;
			    }
			break;
		    /* end treasure type 2 */

		    case 3:	/* treasure type 3 */
			switch (whichtreasure) {
			    case 1:
				dtemp = ROLL(7.0, 30.0 + Circle / 10.0);
				printw("You've found a +%.0f shield!\n", dtemp);
				if (dtemp >= Player.p_shield)
				    Player.p_shield = dtemp;
				else
				    SOMEBETTER();
				break;

			    case 2:
				addstr("You have rescued a virgin.  Will you be honorable ? ");
				ch = getanswer("NY", FALSE);
				addstr("\n\n");
				if (ch == 'Y')
				    Player.p_virgin = TRUE;
				else
				    {
				    Player.p_experience += 2000.0 * Circle;
				    ++Player.p_sin;
				    }
				break;

			    case 3:
				addstr("You've discovered some athelas!\n");
				--Player.p_poison;
				break;
			    }
			break;
		    /* end treasure type 3 */

		    case 4:	/* treasure type 4 */
			addstr("You've found a scroll.  Will you read it ? ");
			ch = getanswer("NY", FALSE);
			addstr("\n\n");

			if (ch == 'Y')
			    switch ((int) ROLL(1, 6)) {
				case 1:
				    addstr("It throws up a shield for you next monster.\n");
				    getyx(stdscr, whichtreasure, ch);
				    more(whichtreasure);
				    Shield =
					(Player.p_maxenergy + Player.p_energy) * 5.5 + Circle * 50.0;
				    Whichmonster = pickmonster();
				    longjmp(Fightenv, 1);

				case 2:
				    addstr("It makes you invisible for you next monster.\n");
				    getyx(stdscr, whichtreasure, ch);
				    more(whichtreasure);
				    Player.p_speed = 1e6;
				    Whichmonster = pickmonster();
				    longjmp(Fightenv, 1);

				case 3:
				    addstr("It increases your strength ten fold to fight your next monster.\n");
				    getyx(stdscr, whichtreasure, ch);
				    more(whichtreasure);
				    Player.p_might *= 10.0;
				    Whichmonster = pickmonster();
				    longjmp(Fightenv, 1);

				case 4:
				    addstr("It is a general knowledge scroll.\n");
				    Player.p_brains += ROLL(2.0, Circle);
				    Player.p_magiclvl += ROLL(1.0, Circle / 2.0);
				    break;

				case 5:
				    addstr("It tells you how to pick your next monster.\n");
				    addstr("Which monster do you want [0-99] ? ");
				    Whichmonster = (int) infloat();
				    Whichmonster = MIN(99, MAX(0, Whichmonster));
				    longjmp(Fightenv, 1);

				case 6:
				    addstr("It was cursed!\n");
				    cursedtreasure();
				    break;
				}
			    break;
		    /* end treasure type 4 */

		    case 5:	/* treasure type 5 */
			switch (whichtreasure) {
			    case 1:
				dtemp = ROLL(Circle / 4.0 + 5.0, Circle / 2.0 + 9.0);
				printw("You've discovered a +%.0f dagger.\n", dtemp);
				if (dtemp >= Player.p_sword)
				    Player.p_sword = dtemp;
				else
				    SOMEBETTER();
				break;

			    case 2:
				dtemp = ROLL(7.5 + Circle * 3.0, Circle * 2.0 + 160.0);
				printw("You have found some +%.0f armour!\n", dtemp);
				if (dtemp >= Player.p_shield)
				    Player.p_shield = dtemp;
				else
				    SOMEBETTER();
				break;

			    case 3:
				addstr("You've found a tablet.\n");
				Player.p_brains += 4.5 * Circle;
				break;
			    }
			break;
		    /* end treasure type 5 */

		    case 6:	/* treasure type 6 */
			switch (whichtreasure) {
			    case 1:
				addstr("You've found a priest.\n");
				Player.p_energy = Player.p_maxenergy + Player.p_shield;
				Player.p_sin /= 2.0;
				Player.p_mana += 24.0 * Circle;
				Player.p_brains += Circle;
				break;

			    case 2:
				addstr("You have come upon Robin Hood!\n");
				Player.p_shield += Circle * 2.0;
				Player.p_strength += Circle / 2.5 + 1.0;
				break;

			    case 3:
				dtemp = ROLL(2.0 + Circle / 4.0, Circle / 1.2 + 10.0);
				printw("You have found a +%.0f axe!\n", dtemp);
				if (dtemp >= Player.p_sword)
				    Player.p_sword = dtemp;
				else
				    SOMEBETTER();
				break;
			    }
			break;
		    /* end treasure type 6 */

		    case 7:	/* treasure type 7 */
			switch (whichtreasure) {
			    case 1:
				addstr("You've discovered a charm!\n");
				++Player.p_charms;
				break;

			    case 2:
				addstr("You have encountered Merlyn!\n");
				Player.p_brains += Circle + 5.0;
				Player.p_magiclvl += Circle / 3.0 + 5.0;
				Player.p_mana += Circle * 10.0;
				break;

			    case 3:
				dtemp = ROLL(5.0 + Circle / 3.0, Circle / 1.5 + 20.0);
				printw("You have found a +%.0f war hammer!\n", dtemp);
				if (dtemp >= Player.p_sword)
				    Player.p_sword = dtemp;
				else
				    SOMEBETTER();
				break;
			    }
			break;
		    /* end treasure type 7 */

		    case 8:	/* treasure type 8 */
			switch (whichtreasure) {
			    case 1:
				addstr("You have found a healing potion.\n");
				Player.p_poison = MIN(-2.0, Player.p_poison - 2.0);
				break;

			    case 2:
				addstr("You have discovered a transporter.  Do you wish to go anywhere ? ");
				ch = getanswer("NY", FALSE);
				addstr("\n\n");
				if (ch == 'Y') {
				    double x, y;

				    addstr("X Y Coordinates ? ");
				    getstring(Databuf, SZ_DATABUF);
				    sscanf(Databuf, "%lf %lf", &x, &y);
				    altercoordinates(x, y, A_FORCED);
				    }
				break;

			    case 3:
				dtemp = ROLL(10.0 + Circle / 1.2, Circle * 3.0 + 30.0);
				printw("You've found a +%.0f sword!\n", dtemp);
				if (dtemp >= Player.p_sword)
				    Player.p_sword = dtemp;
				else
				    SOMEBETTER();
				break;
			    }
			break;
		    /* end treasure type 8 */

		    case 10:
		    case 11:
		    case 12:
		    case 13:	/* treasure types 10 - 13 */
			if (drandom() < 0.33) {
			    if (Curmonster.m_treasuretype == 10) {
				addstr("You've found a pair of elven boots!\n");
				Player.p_quickness += 2.0;
				break;
				}
			    else if (Curmonster.m_treasuretype == 11
				&& !Player.p_palantir)
				{
				addstr("You've acquired Saruman's palantir.\n");
				Player.p_palantir = TRUE;
				break;
				}
			    else if (Player.p_ring.ring_type == R_NONE
				&& Player.p_specialtype < SC_COUNCIL
				&& (Curmonster.m_treasuretype == 12
				|| Curmonster.m_treasuretype == 13))
				/* roll up a ring */
				{
				if (drandom() < 0.8)
				    /* regular rings */
				    {
				    if (Curmonster.m_treasuretype == 12)
					{
					whichtreasure = R_NAZREG;
					temp = 35;
					}
				    else
					{
					whichtreasure = R_DLREG;
					temp = 0;
					}
				    }
				else
				    /* bad rings */
				    {
				    whichtreasure = R_BAD;
				    temp = 15 + Statptr->c_ringduration + (int) ROLL(0,5);
				    }

				addstr("You've discovered a ring.  Will you pick it up ? ");
				ch = getanswer("NY", FALSE);
				addstr("\n\n");

				if (ch == 'Y')
				    {
				    Player.p_ring.ring_type = whichtreasure;
				    Player.p_ring.ring_duration = temp;
				    }

				break;
				}
			    }
			/* end treasure types 10 - 13 */
			/* fall through to treasure type 9 if no treasure from above */

			case 9:	/* treasure type 9 */
			    switch (whichtreasure)
				{
				case 1:
				    if (Player.p_level <= 1000.0
					&& Player.p_crowns <= 3
					&& Player.p_level >= 10.0)
					{
					addstr("You have found a golden crown!\n");
					++Player.p_crowns;
					break;
					}
				    /* fall through otherwise */

				case 2:
				    addstr("You've been blessed!\n");
				    Player.p_blessing = TRUE;
				    Player.p_sin /= 3.0;
				    Player.p_energy = Player.p_maxenergy + Player.p_shield;
				    Player.p_mana += 100.0 * Circle;
				    break;

				case 3:
				    dtemp = ROLL(1.0, Circle / 5.0 + 5.0);
				    dtemp = MIN(dtemp, 99.0);
				    printw("You have discovered some +%.0f quicksilver!\n",dtemp);
				    if (dtemp >= Player.p_quksilver)
					Player.p_quksilver = dtemp;
				    else
					SOMEBETTER();
				    break;
				}
			    break;
		    /* end treasure type 9 */
		    }
	}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: cursedtreasure()
/
/ FUNCTION: take care of cursed treasure
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: waddstr()
/
/ GLOBAL INPUTS: Player, *stdscr
/
/ GLOBAL OUTPUTS: Player
/
/ DESCRIPTION:
/	Handle cursed treasure.  Look for amulets and charms to save
/	the player from the curse.
/
*************************************************************************/

void
cursedtreasure(void)
{
	if (Player.p_charms > 0) {
		addstr("But your charm saved you!\n");
		--Player.p_charms;
	} else
		if (Player.p_amulets > 0) {
			addstr("But your amulet saved you!\n");
			--Player.p_amulets;
		} else {
			Player.p_energy =
			    (Player.p_maxenergy + Player.p_shield) / 10.0;
			Player.p_poison += 0.25;
		}
}
/**/
/************************************************************************
/
/ FUNCTION NAME: scramblestats()
/
/ FUNCTION: scramble some selected statistics
/
/ AUTHOR: E. A. Estes, 12/4/85
/
/ ARGUMENTS: none
/
/ RETURN VALUE: none
/
/ MODULES CALLED: floor(), drandom()
/
/ GLOBAL INPUTS: Player
/
/ GLOBAL OUTPUTS: Player
/
/ DESCRIPTION:
/	Swap a few player statistics randomly.
/
*************************************************************************/

void
scramblestats(void)
{
	double  dbuf[6];	/* to put statistic in */
	double  dtemp1, dtemp2;	/* for swapping values */
	int first, second;	/* indices for swapping */
	double *dptr;		/* pointer for filling and emptying buf[] */

	/* fill buffer */
	dptr = &dbuf[0];
	*dptr++ = Player.p_strength;
	*dptr++ = Player.p_mana;
	*dptr++ = Player.p_brains;
	*dptr++ = Player.p_magiclvl;
	*dptr++ = Player.p_energy;
	*dptr = Player.p_sin;

	/* pick values to swap */
	first = (int) ROLL(0, 5);
	second = (int) ROLL(0, 5);

	/* swap values */
	dptr = &dbuf[0];
	dtemp1 = dptr[first];
	/* this expression is split to prevent a compiler loop on some
	 * compilers */
	dtemp2 = dptr[second];
	dptr[first] = dtemp2;
	dptr[second] = dtemp1;

	/* empty buffer */
	Player.p_strength = *dptr++;
	Player.p_mana = *dptr++;
	Player.p_brains = *dptr++;
	Player.p_magiclvl = *dptr++;
	Player.p_energy = *dptr++;
	Player.p_sin = *dptr;
}
