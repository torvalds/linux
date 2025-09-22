/*	$OpenBSD: phantglobs.c,v 1.4 2016/01/06 14:28:09 mestre Exp $	*/
/*	$NetBSD: phantglobs.c,v 1.2 1995/03/24 03:59:33 cgd Exp $	*/

/*
 * phantglobs.c - globals for Phantasia
 */

#include <stdio.h>

#include "phantdefs.h"
#include "phantstruct.h"

double	Circle;		/* which circle player is in			*/
double	Shield;		/* force field thrown up in monster battle	*/

bool	Beyond;		/* set if player is beyond point of no return	*/
bool	Marsh;		/* set if player is in dead marshes		*/
bool	Throne;		/* set if player is on throne			*/
bool	Changed;	/* set if important player stats have changed	*/
bool	Wizard;		/* set if player is the 'wizard' of the game	*/
bool	Timeout;	/* set if short timeout waiting for input	*/
bool	Windows;	/* set if we are set up for curses stuff	*/
bool	Luckout;	/* set if we have tried to luck out in fight	*/
bool	Foestrikes;	/* set if foe gets a chance to hit in battleplayer()	*/
bool	Echo;		/* set if echo input to terminal		*/

int	Users;		/* number of users currently playing		*/
int	Whichmonster;	/* which monster we are fighting		*/
int	Lines;		/* line on screen counter for fight routines	*/

char	Ch_Erase;	/* backspace key */
char	Ch_Kill;	/* linekill key */

long	Fileloc;	/* location in file of player statistics	*/

const char *Login;	/* pointer to login of player			*/
char	*Enemyname;	/* pointer name of monster/player we are battling*/

struct	player	Player;	/* stats for player				*/
struct	player	Other;	/* stats for another player			*/

struct	monster	Curmonster;/* stats for current monster			*/

struct	energyvoid Enrgyvoid;/* energy void buffer			*/

struct	charstats *Statptr;/* pointer into Stattable[]			*/

/* lookup table for character type dependent statistics */
struct	charstats Stattable[7] = {
	/* MAGIC USER */
	{
		15.0, 200.0, 18.0, 175.0, 10,
			{30, 6, 0.0},	{10, 6, 2.0},	{50, 51, 75.0},
			{30, 16, 20.0},	{60, 26, 6.0},	{5, 5, 2.75}
	},

	/* FIGHTER */
	{
		10.0, 110.0, 15.0, 220.0, 20,
			{30, 6, 0.0},	{40, 16, 3.0},	{30, 21, 40.0},
			{45, 26, 30.0},	{25, 21, 3.0},	{3, 4, 1.5}
	},

	/* ELF */
	{
		12.0, 150.0, 17.0, 190.0, 13,
			{32, 7, 0.0},	{35, 11, 2.5},	{45, 46, 65.0},
			{30, 21, 25.0},	{40, 26, 4.0},	{4, 4, 2.0}
	},

	/* DWARF */
	{	 7.0, 80.0, 13.0, 255.0,  25,
			{25, 6, 0.0},	{50, 21, 5.0},	{25, 21, 30.0},
			{60, 41, 35.0},	{20, 21, 2.5},	{2, 4, 1.0}
	},

	/* HALFLING */
	{
		11.0, 80.0, 10.0, 125.0, 40,
			{34, 0, 0.0},	{20, 6, 2.0},	{25, 21, 30.0},
			{55, 36, 30.0},	{40, 36, 4.5},	{1, 4, 1.0}
	},

	/* EXPERIMENTO */
	{	 9.0, 90.0, 16.0, 160.0, 20,
			{27, 0, 0.0},	{25, 0, 0.0},	{100, 0, 0.0},
			{35, 0, 0.0},	{25, 0, 0.0},	{2, 0, 0.0}
	},

	/* SUPER */
	{
		15.0, 200.0, 10.0, 225.0, 40,
			{38, 0, 0.0},	{65, 0, 5.0},	{100, 0, 75.0},
			{80, 0, 35.0},	{85, 0, 6.0},	{9, 0, 2.75}
	}
};

/* menu of items for purchase */
struct menuitem	Menu[] = {
	{"Mana", 1},
	{"Shield", 5},
	{"Book", 200},
	{"Sword", 500},
	{"Charm", 1000},
	{"Quicksilver", 2500},
	{"Blessing", 1000},
};

FILE	*Playersfp;	/* pointer to open player file			*/
FILE	*Monstfp;	/* pointer to open monster file			*/
FILE	*Messagefp;	/* pointer to open message file			*/
FILE	*Energyvoidfp;	/* pointer to open energy void file		*/

char	Databuf[SZ_DATABUF];	/* a place to read data into		*/

/* some canned strings for messages */
char	Illcmd[] = "Illegal command.\n";
char	Illmove[] = "Too far.\n";
char	Illspell[] = "Illegal spell.\n";
char	Nomana[] = "Not enought mana for that spell.\n";
char	Somebetter[] = "But you already have something better.\n";
char	Nobetter[] = "That's no better than what you already have.\n";
