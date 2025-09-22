/*	$OpenBSD: globals.c,v 1.7 2009/10/27 23:59:27 deraadt Exp $	*/
/*	$NetBSD: globals.c,v 1.4 1995/04/22 10:36:57 cgd Exp $	*/

/*
 * Copyright (c) 1983, 1993
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

#include "extern.h"

struct scenario scene[] = {
	/*
	 * int winddir;
	 * int windspeed;
	 * int windchange;
	 * int vessels;
	 * char *name;
	 * struct ship ship[NSHIP];
	 */
	{ 5, 3, 5, 2, "Ranger vs. Drake",
		{
			{ "Ranger",		specs+0,  N_A,  7, 20, 4, 0 },
			{ "Drake",		specs+1,  N_B,  7, 31, 5, 0 }
		}
	},
	{ 1, 3, 6, 2, "The Battle of Flamborough Head",
		{
			{ "Bonhomme Rich",	specs+2,  N_A, 13, 40, 2, 0 },
			{ "Serapis",		specs+3,  N_B,  2, 42, 2, 0 }
		}
	},
	{ 5, 5, 5, 10, "Arbuthnot and Des Touches",
		{
			{ "America",		specs+4,  N_B,  7, 37, 4, 0 },
			{ "Befford",		specs+5,  N_B,  5, 35, 4, 0 },
			{ "Adamant",		specs+6,  N_B,  3, 33, 4, 0 },
			{ "London",		specs+7,  N_B,  1, 31, 4, 0 },
			{ "Royal Oak",		specs+8,  N_B, -1, 29, 4, 0 },
			{ "Neptune",		specs+9,  N_F,  6, 44, 4, 0 },
			{ "Duc Bougogne",	specs+10, N_F,  8, 46, 4, 0 },
			{ "Conquerant",		specs+48, N_F, 10, 48, 4, 0 },
			{ "Provence",		specs+11, N_F, 12, 50, 4, 0 },
			{ "Romulus",		specs+12, N_F, 20, 58, 4, 0 }
		}
	},
	{ 1, 3, 5, 10, "Suffren and Hughes",
		{
			{ "Monmouth",		specs+52, N_B,  9, 45, 2, 0 },
			{ "Hero",		specs+5,  N_B, 13, 49, 2, 0 },
			{ "Isis",		specs+6,  N_B, 12, 48, 2, 0 },
			{ "Superb",		specs+50, N_B, 10, 46, 2, 0 },
			{ "Burford",		specs+48, N_B, 11, 47, 2, 0 },
			{ "Flamband",		specs+13, N_F,  7, 59, 4, 0 },
			{ "Annibal",		specs+9,  N_F,  4, 56, 4, 0 },
			{ "Severe",		specs+11, N_F,  2, 54, 4, 0 },
			{ "Brilliant",		specs+49, N_F, -1, 51, 4, 0 },
			{ "Sphinx",		specs+51, N_F, -5, 47, 4, 0 }
		}
	},
	{ 1, 3, 4, 2, "Nymphe vs. Cleopatre",
		{
			{ "Nymphe",		specs+14, N_B, 13, 30, 2, 0 },
			{ "Cleopatre",		specs+15, N_F,  3, 41, 2, 0 }
		}
	},
	{ 1, 3, 5, 2, "Mars vs. Hercule",
		{
			{ "Mars",		specs+16, N_B, 13, 30, 2, 0 },
			{ "Hercule",		specs+17, N_F,  3, 41, 2, 0 }
		}
	},
	{ 5, 3, 5, 2, "Ambuscade vs. Baionnaise",
		{
			{ "Ambuscade",		specs+18, N_B, 13, 30, 2, 0 },
			{ "Baionnaise",		specs+19, N_F,  3, 41, 2, 0 }
		}
	},
	{ 1, 5, 6, 2, "Constellation vs. Insurgent",
		{
			{ "Constellation",	specs+20, N_A,  9, 50, 8, 0 },
			{ "Insurgent",		specs+22, N_F,  4, 24, 2, 0 }
		}
	},
	{ 1, 3, 5, 2, "Constellation vs. Vengeance",
		{
			{ "Constellation",	specs+20, N_A, 12, 40, 2, 0 },
			{ "Vengeance",		specs+21, N_F,  1, 43, 2, 0 }
		}
	},
	{ 1, 3, 6, 10, "The Battle of Lissa",
		{
			{ "Amphion",		specs+23, N_B,  8, 50, 4, 0 },
			{ "Active",		specs+24, N_B,  6, 48, 4, 0 },
			{ "Volage",		specs+25, N_B,  4, 46, 4, 0 },
			{ "Cerberus",		specs+26, N_B,  2, 44, 4, 0 },
			{ "Favorite",		specs+27, N_F,  9, 34, 2, 0 },
			{ "Flore",		specs+21, N_F, 13, 39, 2, 0 },
			{ "Danae",		specs+64, N_F, 15, 37, 2, 0 },
			{ "Bellona",		specs+28, N_F, 17, 35, 2, 0 },
			{ "Corona",		specs+29, N_F, 12, 31, 2, 0 },
			{ "Carolina",		specs+30, N_F, 15, 28, 2, 0 }
		}
	},
	{ 2, 5, 6, 2, "Constitution vs. Guerriere",
		{
			{ "Constitution",	specs+31, N_A,  7, 35, 1, 0 },
			{ "Guerriere",		specs+32, N_B,  7, 47, 4, 0 }
		}
	},
	{ 1, 3, 5, 2, "United States vs. Macedonian",
		{
			{ "United States",	specs+33, N_A,  1, 52, 6, 0 },
			{ "Macedonian",		specs+34, N_B, 14, 40, 1, 0 }
		}
	},
	{ 1, 3, 6, 2, "Constitution vs. Java",
		{
			{ "Constitution",	specs+31, N_A,  1, 40, 2, 0 },
			{ "Java",		specs+35, N_B, 11, 40, 2, 0 }
		}
	},
	{ 1, 3, 5, 2, "Chesapeake vs. Shannon",
		{
			{ "Chesapeake",		specs+36, N_A, 13, 40, 2, 0 },
			{ "Shannon",		specs+37, N_B,  1, 42, 2, 0 }
		}
	},
	{ 1, 1, 6, 5, "The Battle of Lake Erie",
		{
			{ "Lawrence",		specs+38, N_A,  4, 55, 8, 0 },
			{ "Niagara",		specs+42, N_A,  7, 61, 8, 0 },
			{ "Lady Prevost",	specs+39, N_B,  4, 25, 2, 0 },
			{ "Detroit",		specs+40, N_B,  7, 22, 2, 0 },
			{ "Q. Charlotte",	specs+41, N_B, 10, 19, 2, 0 }
		}
	},
	{ 1, 1, 5, 2, "Wasp vs. Reindeer",
		{
			{ "Wasp",		specs+42, N_A,  3, 41, 2, 0 },
			{ "Reindeer",		specs+43, N_B, 10, 48, 2, 0 }
		}
	},
	{ 1, 2, 5, 3, "Constitution vs. Cyane and Levant",
		{
			{ "Constitution",	specs+31, N_A, 10, 45, 2, 0 },
			{ "Cyane",		specs+44, N_B,  3, 37, 2, 0 },
			{ "Levant",		specs+45, N_B,  5, 35, 2, 0 }
		}
	},
	{ 5, 5, 5, 3, "Pellew vs. Droits de L'Homme",
		{
			{ "Indefatigable",	specs+46, N_B, 12, 45, 6, 0 },
			{ "Amazon",		specs+47, N_B,  9, 48, 6, 0 },
			{ "Droits L'Hom",	specs+48, N_F,  3, 28, 5, 0 }
		}
	},
	{ 2, 2, 3, 10, "Algeciras",
		{
			{ "Caesar",		specs+49, N_B,  7, 70, 6, 0 },
			{ "Pompee",		specs+50, N_B,  5, 72, 6, 0 },
			{ "Spencer",		specs+5,  N_B,  3, 74, 6, 0 },
			{ "Hannibal",		specs+7,  N_B,  1, 76, 6, 0 },
			{ "Real-Carlos",	specs+53, N_S,  9, 20, 3, 0 },
			{ "San Fernando",	specs+54, N_S, 11, 16, 3, 0 },
			{ "Argonauta",		specs+55, N_S, 10, 14, 4, 0 },
			{ "San Augustine",	specs+56, N_S,  6, 22, 4, 0 },
			{ "Indomptable",	specs+51, N_F,  7, 23, 5, 0 },
			{ "Desaix",		specs+52, N_F,  7, 27, 7, 0 }
		}
	},
	{ 5, 3, 6, 7, "Lake Champlain",
		{
			{ "Saratoga",		specs+60, N_A,  8, 10, 1, 0 },
			{ "Eagle",		specs+61, N_A,  9, 13, 2, 0 },
			{ "Ticonderoga",	specs+62, N_A, 12, 17, 3, 0 },
			{ "Preble",		specs+63, N_A, 14, 20, 2, 0 },
			{ "Confiance",		specs+57, N_B,  4, 70, 6, 0 },
			{ "Linnet",		specs+58, N_B,  7, 68, 6, 0 },
			{ "Chubb",		specs+59, N_B, 10, 65, 6, 0 }
		}
	},
	{ 5, 3, 6, 4, "Last Voyage of the USS President",
		{
			{ "President",		specs+67, N_A, 12, 42, 5, 0 },
			{ "Endymion",		specs+64, N_B,  5, 42, 5, 0 },
			{ "Pomone",		specs+65, N_B,  7, 82, 6, 0 },
			{ "Tenedos",		specs+66, N_B,  7, -1, 4, 0 }
		}
	},
	{ 7, 5, 5, 2, "Hornblower and the Natividad",
		{
			{ "Lydia",		specs+68, N_B, 12, 40, 2, 0 },
			{ "Natividad",		specs+69, N_S,  2, 40, 4, 0 }
		}
	},
	{ 1, 3, 6, 2, "Curse of the Flying Dutchman",
		{
			{ "Piece of Cake",	specs+19, N_S,  7, 40, 2, 0 },
			{ "Flying Dutchy",	specs+71, N_F,  7, 41, 1, 0 }
		}
	},
	{ 1, 4, 1, 4, "The South Pacific",
		{
			{ "USS Scurvy",		specs+70, N_A,  7, 40, 1, 0 },
			{ "HMS Tahiti",		specs+71, N_B, 12, 60, 1, 0 },
			{ "Australian",		specs+18, N_S,  5, 20, 8, 0 },
			{ "Bikini Atoll",	specs+63, N_F,  2, 60, 4, 0 }
		}
	},
	{ 7, 3, 6, 5, "Hornblower and the battle of Rosas bay",
		{
			{ "Sutherland",		specs+5,  N_B, 13, 30, 2, 0 },
			{ "Turenne",		specs+10, N_F,  9, 35, 6, 0 },
			{ "Nightmare",		specs+9,  N_F,  7, 37, 6, 0 },
			{ "Paris",		specs+53, N_F,  3, 45, 4, 0 },
			{ "Napoleon",		specs+56, N_F,  1, 40, 6, 0 }
		}
	},
	{ 6, 4, 7, 5, "Cape Horn",
		{
			{ "Concord",		specs+51, N_A,  3, 20, 4, 0 },
			{ "Berkeley",		specs+7,  N_A,  5, 50, 5, 0 },
			{ "Thames",		specs+71, N_B, 10, 40, 1, 0 },
			{ "Madrid",		specs+53, N_S, 13, 60, 8, 0 },
			{ "Musket",		specs+10, N_F, 10, 60, 7, 0 }
		}
	},
	{ 8, 3, 7, 3, "New Orleans",
		{
			{ "Alligator",		specs+71, N_A, 13,  5, 1, 0 },
			{ "Firefly",		specs+50, N_B, 10, 20, 8, 0 },
			{ "Cypress",		specs+46, N_B,  5, 10, 6, 0 }
		}
	},
	{ 5, 3, 7, 3, "Botany Bay",
		{
			{ "Shark",		specs+11, N_B,  6, 15, 4, 0 },
			{ "Coral Snake",	specs+31, N_F,  3, 30, 6, 0 },
			{ "Sea Lion",		specs+33, N_F, 13, 50, 8, 0 }
		}
	},
	{ 4, 3, 6, 4, "Voyage to the Bottom of the Sea",
		{
			{ "Seaview",		specs+71, N_A,  6, 3,  3, 0 },
			{ "Flying Sub",		specs+64, N_A,  8, 3,  3, 0 },
			{ "Mermaid",		specs+70, N_B,  2, 5,  5, 0 },
			{ "Giant Squid",	specs+53, N_S, 10, 30, 8, 0 }
		}
	},
	{ 7, 3, 6, 3, "Frigate Action",
		{
			{ "Killdeer",		specs+21, N_A,  7, 20, 8, 0 },
			{ "Sandpiper",		specs+27, N_B,  5, 40, 8, 0 },
			{ "Curlew",		specs+34, N_S, 10, 60, 8, 0 }
		}
	},
	{ 7, 2, 5, 6, "The Battle of Midway",
		{
			{ "Enterprise",		specs+49, N_A, 10, 70, 8, 0 },
			{ "Yorktown",		specs+51, N_A,  3, 70, 7, 0 },
			{ "Hornet",		specs+52, N_A,  6, 70, 7, 0 },
			{ "Akagi",		specs+53, N_J,  6, 10, 4, 0 },
			{ "Kaga",		specs+54, N_J,  4, 12, 4, 0 },
			{ "Soryu",		specs+55, N_J,  2, 14, 4, 0 }
		}
	},
	{ 1, 3, 4, 8, "Star Trek",
		{
			{ "Enterprise",		specs+76, N_D,-10, 60, 7, 0 },
			{ "Yorktown",		specs+77, N_D,  0, 70, 7, 0 },
			{ "Reliant",		specs+78, N_D, 10, 70, 7, 0 },
			{ "Galileo",		specs+79, N_D, 20, 60, 7, 0 },
			{ "Kobayashi Maru",	specs+80, N_K,  0,120, 7, 0 },
			{ "Klingon II",		specs+81, N_K, 10,120, 7, 0 },
			{ "Red Orion",		specs+82, N_O,  0,  0, 3, 0 },
			{ "Blue Orion",		specs+83, N_O, 10,  0, 3, 0 }
		}
	}
};
int nscene = sizeof scene / sizeof (struct scenario);

struct shipspecs specs[] = {
/*      bs fs ta guns   hull  crew1   crew3    gunR  carR   rig2  rig4 pts */
/*                 class   qual   crew2    gunL   carL   rig1  rig3        */
/*00*/{	4, 7, 3,  19, 5,  5, 4,  2,  2,  2,  2,  2, 0, 0,  4, 4, 4,  4,  7 },
/*01*/{	4, 7, 3,  17, 5,  5, 4,  2,  2,  2,  0,  0, 4, 4,  3, 3, 3,  3,  6 },
/*02*/{	3, 5, 2,  42, 4,  7, 4,  2,  2,  2,  2,  2, 0, 0,  5, 5, 5, -1, 11 },
/*03*/{	4, 6, 3,  44, 3,  7, 4,  2,  2,  2,  3,  3, 0, 0,  5, 5, 5,  5, 12 },
/*04*/{	3, 5, 2,  64, 2, 17, 4,  8,  6,  6, 12, 12, 2, 2,  7, 7, 7, -1, 20 },
/*05*/{	3, 5, 2,  74, 2, 20, 4,  8,  8,  8, 16, 16, 2, 2,  7, 7, 7, -1, 26 },
/*06*/{	3, 5, 2,  50, 2, 12, 4,  6,  4,  4,  8,  8, 2, 2,  6, 6, 6, -1, 17 },
/*07*/{	3, 5, 1,  98, 1, 23, 4, 10, 10, 10, 18, 18, 2, 2,  8, 8, 8, -1, 28 },
/*08*/{	3, 5, 2,  74, 2, 20, 4,  8,  8,  8, 16, 16, 2, 2,  7, 7, 7, -1, 26 },
/*09*/{	3, 5, 2,  74, 2, 21, 3, 10, 10,  8, 20, 20, 0, 0,  7, 7, 7, -1, 24 },
/*10*/{	3, 5, 1,  80, 1, 23, 3, 12, 12, 10, 22, 22, 0, 0,  7, 7, 7, -1, 27 },
/*11*/{	3, 5, 2,  64, 2, 18, 3,  8,  8,  6, 12, 12, 0, 0,  7, 7, 7, -1, 18 },
/*12*/{	3, 5, 2,  44, 2, 11, 3,  4,  4,  4,  6,  6, 2, 2,  5, 5, 5, -1, 10 },
/*13*/{	3, 5, 2,  50, 2, 14, 3,  6,  6,  4,  8,  8, 0, 0,  6, 6, 6, -1, 14 },
/*14*/{	4, 6, 3,  36, 3, 11, 4,  4,  4,  2,  4,  4, 2, 2,  5, 5, 5,  5, 11 },
/*15*/{	4, 6, 3,  36, 3, 11, 3,  4,  4,  4,  4,  4, 2, 2,  5, 5, 5,  5, 10 },
/*16*/{	3, 5, 2,  74, 2, 21, 4, 10,  8,  8, 18, 18, 2, 2,  7, 7, 7, -1, 26 },
/*17*/{	3, 5, 2,  74, 2, 21, 3, 10, 10,  8, 20, 20, 2, 2,  7, 7, 7, -1, 23 },
/*18*/{	4, 6, 3,  32, 3,  8, 3,  4,  2,  2,  4,  4, 2, 2,  5, 5, 5,  5,  9 },
/*19*/{	4, 6, 3,  24, 4,  6, 3,  4,  4,  4,  2,  2, 0, 0,  4, 4, 4,  4,  9 },
/*20*/{	4, 7, 3,  38, 4, 14, 5,  6,  4,  4,  4,  4, 6, 6,  5, 5, 5,  5, 17 },
/*21*/{	4, 6, 3,  40, 3, 15, 3,  8,  6,  6,  6,  6, 4, 4,  5, 5, 5,  5, 15 },
/*22*/{	4, 7, 3,  36, 4, 11, 3,  6,  6,  4,  4,  4, 2, 2,  5, 5, 5,  5, 11 },
/*23*/{	4, 6, 3,  32, 3, 11, 5,  4,  4,  2,  4,  4, 2, 2,  5, 5, 5,  5, 13 },
/*24*/{	4, 6, 3,  38, 3, 14, 5,  4,  4,  4,  6,  6, 4, 4,  5, 5, 5,  5, 18 },
/*25*/{	4, 6, 3,  22, 3,  6, 5,  2,  2,  2,  0,  0, 8, 8,  4, 4, 4,  4, 11 },
/*26*/{	4, 6, 3,  32, 3, 11, 5,  4,  4,  2,  4,  4, 2, 2,  5, 5, 5,  5, 13 },
/*27*/{	4, 6, 3,  40, 3, 14, 3,  6,  6,  4,  6,  6, 4, 4,  5, 5, 5,  5, 15 },
/*28*/{	4, 6, 3,  32, 3, 11, 2,  4,  4,  4,  4,  4, 0, 0,  5, 5, 5,  5,  9 },
/*29*/{	4, 6, 3,  40, 3, 14, 2,  6,  6,  4,  6,  6, 4, 4,  5, 5, 5,  5, 12 },
/*30*/{	4, 6, 3,  32, 3,  8, 2,  4,  4,  1,  2,  2, 0, 0,  4, 4, 4,  4,  7 },
/*31*/{	4, 7, 3,  44, 4, 18, 5,  6,  6,  6,  8,  8, 6, 6,  6, 6, 6,  6, 24 },
/*32*/{	4, 6, 3,  38, 3, 14, 4,  4,  4,  2,  6,  6, 4, 4,  5, 5, 5,  5, 15 },
/*33*/{	4, 5, 3,  44, 3, 18, 5,  8,  6,  6,  8,  8, 8, 8,  6, 6, 6,  6, 24 },
/*34*/{	4, 6, 3,  38, 3, 14, 4,  4,  4,  4,  6,  6, 4, 4,  5, 5, 5,  5, 16 },
/*35*/{	4, 7, 3,  38, 4, 14, 4,  6,  6,  6,  6,  6, 6, 6,  5, 5, 5,  5, 19 },
/*36*/{	4, 6, 3,  38, 3, 14, 3,  6,  6,  4,  6,  6, 6, 6,  5, 5, 5,  5, 14 },
/*37*/{	4, 6, 3,  38, 3, 14, 5,  6,  4,  4,  6,  6, 6, 6,  5, 5, 5,  5, 17 },
/*38*/{	4, 7, 3,  20, 5,  6, 4,  4,  2,  2,  0,  0, 6, 6,  4, 4, 4,  4,  9 },
/*39*/{	4, 7, 3,  13, 6,  3, 4,  0,  2,  2,  0,  0, 2, 2,  2, 2, 2,  2,  5 },
/*40*/{	4, 7, 3,  19, 5,  5, 4,  2,  2,  2,  2,  2, 0, 0,  4, 4, 4,  4,  7 },
/*41*/{	4, 7, 3,  17, 5,  5, 4,  2,  2,  2,  2,  2, 0, 0,  3, 3, 3,  3,  6 },
/*42*/{	4, 7, 3,  20, 5,  6, 5,  4,  2,  2,  0,  0, 6, 6,  4, 4, 4,  4, 12 },
/*43*/{	4, 7, 3,  18, 5,  5, 5,  2,  2,  2,  0,  0, 6, 6,  4, 4, 4,  4,  9 },
/*44*/{	4, 7, 3,  24, 5,  6, 4,  4,  2,  2,  0,  0,10,10,  4, 4, 4,  4, 11 },
/*45*/{	4, 7, 3,  20, 5,  6, 4,  2,  2,  2,  0,  0, 8, 8,  4, 4, 4,  4, 10 },
/*46*/{	4, 6, 3,  44, 3, 11, 5,  4,  4,  4,  4,  4, 2, 2,  5, 5, 5,  5, 14 },
/*47*/{	4, 6, 3,  36, 3, 12, 4,  4,  4,  4,  6,  6, 2, 2,  5, 5, 5,  5, 14 },
/*48*/{	3, 5, 2,  74, 2, 21, 3, 10,  8,  8, 20, 20, 2, 2,  4, 4, 7, -1, 24 },
/*49*/{	3, 5, 2,  80, 2, 24, 4, 10,  8,  8, 20, 20, 2, 2,  8, 8, 8, -1, 31 },
/*50*/{	3, 5, 2,  74, 2, 21, 4,  8,  8,  6, 16, 16, 4, 4,  7, 7, 7, -1, 27 },
/*51*/{	3, 5, 2,  80, 2, 24, 3, 12, 12, 10, 22, 22, 2, 2,  7, 7, 7, -1, 27 },
/*52*/{	3, 5, 2,  74, 2, 21, 3, 10, 10,  8, 20, 20, 2, 2,  7, 7, 7, -1, 24 },
/*53*/{	3, 5, 1, 112, 1, 27, 2, 12, 12, 12, 24, 24, 0, 0,  9, 9, 9, -1, 27 },
/*54*/{	3, 5, 1,  96, 1, 24, 2, 12, 12, 10, 20, 20, 0, 0,  8, 8, 8, -1, 24 },
/*55*/{	3, 5, 2,  80, 2, 23, 2, 10, 10,  8, 20, 20, 0, 0,  7, 7, 7, -1, 23 },
/*56*/{	3, 5, 2,  74, 2, 21, 2, 10,  8,  8, 16, 16, 4, 4,  7, 7, 7, -1, 20 },
/*57*/{	4, 6, 3,  37, 3, 12, 4,  4,  4,  2,  6,  6, 4, 4,  5, 5, 5,  5, 14 },
/*58*/{	4, 7, 3,  16, 5,  5, 5,  2,  2,  2,  0,  0, 4, 4,  4, 4, 4,  4, 10 },
/*59*/{	4, 7, 3,  11, 6,  3, 4,  2,  2,  2,  0,  0, 2, 2,  2, 2, 2,  2,  5 },
/*60*/{	4, 7, 3,  26, 5,  6, 4,  4,  2,  2,  2,  2, 6, 6,  4, 4, 4,  4, 12 },
/*61*/{	4, 7, 3,  20, 5,  6, 4,  4,  2,  2,  0,  0, 6, 6,  4, 4, 4,  4, 11 },
/*62*/{	4, 7, 3,  17, 5,  5, 4,  2,  2,  2,  0,  0, 6, 6,  4, 4, 4,  4,  9 },
/*63*/{	4, 7, 3,   7, 6,  3, 4,  0,  2,  2,  0,  0, 2, 2,  2, 2, 2,  2,  4 },
/*64*/{	4, 6, 3,  40, 3, 15, 4,  4,  4,  4,  8,  8, 6, 6,  5, 5, 5,  5, 17 },
/*65*/{	4, 6, 3,  44, 3, 15, 4,  8,  8,  6, 10, 10, 2, 2,  6, 6, 6,  6, 20 },
/*66*/{	4, 6, 3,  38, 3, 14, 4,  4,  4,  4,  6,  6, 6, 6,  5, 5, 5,  5, 15 },
/*67*/{	4, 5, 3,  44, 3, 18, 5,  8,  6,  6,  8,  8, 8, 8,  6, 6, 6,  6, 24 },
/*68*/{	4, 6, 3,  36, 3,  9, 5,  4,  4,  2,  4,  4, 2, 2,  5, 5, 5,  5, 13 },
/*69*/{	3, 5, 2,  50, 2, 14, 2,  6,  6,  6,  8,  8, 0, 0,  6, 6, 6, -1, 14 },
/*70*/{	3, 5, 1, 136, 1, 30, 1,  8, 14, 14, 28, 28, 0, 0,  9, 9, 9, -1, 27 },
/*71*/{	3, 5, 1, 120, 1, 27, 5, 16, 14, 14, 28, 28, 2, 2,  9, 9, 9, -1, 43 },
/*72*/{	3, 5, 1, 120, 2, 21, 5, 15, 17, 15, 25, 25, 7, 7,  9, 9, 9, -1, 36 },
/*73*/{	3, 5, 1,  90, 3, 18, 4, 13, 15, 13, 20, 20, 6, 6,  5, 5, 5,  5, 28 },
/*74*/{	4, 7, 3,   6, 6,  3, 4,  2,  2,  2, 20, 20, 6, 6,  2, 2, 3,  3,  5 },
/*75*/{	3, 5, 1, 110, 2, 20, 4, 14, 15, 11, 26, 26, 8, 8,  7, 8, 9, -1, 34 },
/*76*/{	4, 7, 3, 450, 1, 99, 5, 50, 40, 40, 50, 50,25,25,  9, 9, 9, -1, 75 },
/*77*/{	4, 7, 3, 450, 1, 99, 5, 50, 40, 40, 50, 50,25,25,  9, 9, 9, -1, 75 },
/*78*/{	4, 7, 3, 450, 1, 99, 5, 50, 40, 40, 50, 50,25,25,  9, 9, 9, -1, 75 },
/*79*/{	4, 7, 3, 450, 1, 99, 5, 50, 40, 40, 50, 50,25,25,  9, 9, 9, -1, 75 },
/*80*/{	4, 7, 3, 450, 1, 99, 5, 50, 40, 40, 50, 50,25,25,  9, 9, 9, -1, 75 },
/*81*/{	4, 7, 3, 450, 1, 99, 5, 50, 40, 40, 50, 50,25,25,  9, 9, 9, -1, 75 },
/*82*/{	4, 7, 3, 450, 1, 99, 5, 50, 40, 40, 50, 50,25,25,  9, 9, 9, -1, 75 },
/*83*/{	4, 7, 3, 450, 1, 99, 5, 50, 40, 40, 50, 50,25,25,  9, 9, 9, -1, 75 }
/*      bs fs ta guns   hull  crew1   crew3    gunR  carR   rig2  rig4 pts */
/*                 class   qual   crew2    gunL   carL   rig1  rig3        */
};

const struct windeffects WET[7][6] = {
	{ {9,9,9,9}, {9,9,9,9}, {9,9,9,9}, {9,9,9,9}, {9,9,9,9}, {9,9,9,9} },
	{ {3,2,2,0}, {3,2,1,0}, {3,2,1,0}, {3,2,1,0}, {2,1,0,0}, {2,1,0,0} },
	{ {1,1,1,0}, {1,1,0,0}, {1,0,0,0}, {1,0,0,0}, {1,0,0,0}, {1,0,0,0} },
	{ {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} },
	{ {0,0,0,0}, {1,0,0,0}, {1,1,0,0}, {1,1,0,0}, {2,2,1,0}, {2,2,1,0} },
	{ {1,0,0,0}, {1,1,0,0}, {1,1,1,0}, {1,1,1,0}, {3,2,2,0}, {3,2,2,0} },
	{ {2,1,1,0}, {3,2,1,0}, {3,2,1,0}, {3,2,1,0}, {3,3,2,0}, {3,3,2,0} }
};

const struct Tables RigTable[11][6] = {
	{ {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,1}, {0,0,1,0} },
	{ {0,0,0,0}, {0,0,0,0}, {0,0,0,1}, {0,0,1,0}, {1,0,0,1}, {0,1,1,1} },
	{ {0,0,0,0}, {0,0,0,1}, {0,0,1,1}, {0,1,0,1}, {0,1,0,1}, {1,0,1,2} },
	{ {0,0,0,0}, {0,0,1,1}, {0,1,0,1}, {0,0,0,2}, {0,1,0,2}, {1,0,1,2} },
	{ {0,1,0,1}, {1,0,0,1}, {0,1,1,2}, {0,1,0,2}, {0,0,1,3}, {1,0,1,4} },
	{ {0,0,1,1}, {0,1,0,2}, {1,0,0,3}, {0,1,1,3}, {1,0,0,4}, {1,1,1,4} },
	{ {0,0,1,2}, {0,1,1,2}, {1,1,0,3}, {0,1,0,4}, {1,0,0,4}, {1,0,1,5} },
	{ {0,0,1,2}, {0,1,0,3}, {1,1,0,3}, {1,0,2,4}, {0,2,1,5}, {2,1,0,5} },
	{ {0,2,1,3}, {1,0,0,3}, {2,1,0,4}, {0,1,1,4}, {0,1,0,5}, {1,0,2,6} },
	{ {1,1,0,4}, {1,0,1,4}, {2,0,0,5}, {0,2,1,5}, {0,1,2,6}, {0,2,0,7} },
	{ {1,0,1,5}, {0,2,0,6}, {1,2,0,6}, {1,1,1,6}, {2,0,2,6}, {1,1,2,7} }
};

const struct Tables HullTable[11][6] = {
	{ {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {1,0,0,0}, {0,1,0,0} },
	{ {0,0,0,0}, {0,0,0,0}, {0,1,0,0}, {1,1,0,0}, {1,0,1,0}, {1,0,1,1} },
	{ {0,1,0,0}, {1,0,0,0}, {1,1,0,0}, {1,0,1,0}, {1,0,1,1}, {2,1,0,0} },
	{ {0,1,1,0}, {1,0,0,0}, {1,1,1,0}, {2,0,0,1}, {2,0,1,0}, {2,2,0,0} },
	{ {0,1,1,0}, {1,0,0,1}, {2,1,0,1}, {2,2,1,0}, {3,0,1,0}, {3,1,0,0} },
	{ {1,1,1,0}, {2,0,2,1}, {2,1,1,0}, {2,2,0,0}, {3,1,0,1}, {3,1,1,0} },
	{ {1,2,2,0}, {2,0,2,1}, {2,1,0,1}, {2,2,0,0}, {3,1,1,0}, {4,2,1,0} },
	{ {2,1,1,0}, {2,0,1,1}, {3,2,2,0}, {3,2,0,0}, {4,2,1,0}, {4,2,1,1} },
	{ {2,1,2,0}, {3,1,1,1}, {3,2,2,0}, {4,2,1,0}, {4,1,0,2}, {4,2,2,0} },
	{ {2,3,1,0}, {3,2,2,0}, {3,2,2,1}, {4,2,2,0}, {4,1,0,3}, {5,1,2,0} },
	{ {2,2,4,0}, {3,3,1,1}, {4,2,1,1}, {5,1,0,2}, {5,1,2,1}, {6,2,2,0} },
};

const char AMMO[9][4] = {
	{ -1, 1, 0, 1 },
	{ -1, 1, 0, 1 },
	{ -1, 1, 0, 1 },
	{ -2, 1, 0, 2 },
	{ -2, 2, 0, 2 },
	{ -2, 2, 0, 2 },
	{ -3, 2, 0, 2 },
	{ -3, 2, 0, 3 },
	{ -3, 2, 0, 3 }
};
	
const char HDT[9][10] = {
	{ 1, 0,-1,-2,-3,-3,-4,-4,-4,-4 },
	{ 1, 1, 0,-1,-2,-2,-3,-3,-3,-3 },
	{ 2, 1, 0,-1,-2,-2,-3,-3,-3,-3 },
	{ 2, 2, 1, 0,-1,-1,-2,-2,-2,-2 },
	{ 3, 2, 1, 0,-1,-1,-2,-2,-2,-2 },
	{ 3, 3, 2, 1, 0, 0,-1,-1,-1,-1 },
	{ 4, 3, 2, 1, 0, 0,-1,-1,-1,-1 },
	{ 4, 4, 3, 2, 1, 1, 0, 0, 0, 0 },
	{ 5, 4, 3, 2, 1, 1, 0, 0, 0, 0 }
};

const char HDTrake[9][10] = {
	{ 2, 1, 0,-1,-2,-2,-3,-3,-3,-3 },
	{ 2, 2, 1, 0,-1,-1,-2,-2,-2,-2 },
	{ 3, 2, 1, 0,-1,-1,-2,-2,-2,-2 },
	{ 4, 3, 2, 1, 0, 0,-1,-1,-1,-1 },
	{ 5, 4, 3, 2, 1, 1, 0, 0, 0, 0 },
	{ 6, 5, 4, 3, 2, 2, 1, 1, 1, 1 },
	{ 7, 6, 5, 4, 3, 3, 2, 2, 2, 2 },
	{ 8, 7, 6, 5, 4, 4, 3, 3, 3, 3 },
	{ 9, 8, 7, 6, 5, 5, 4, 4, 4, 4 }
};

const char QUAL[9][5] = {
	{ -1, 0, 0, 1, 1 },
	{ -1, 0, 0, 1, 1 },
	{ -1, 0, 0, 1, 2 },
	{ -1, 0, 0, 1, 2 },
	{ -1, 0, 0, 2, 2 },
	{ -1,-1, 0, 2, 2 },
	{ -2,-1, 0, 2, 2 },
	{ -2,-1, 0, 2, 2 },
	{ -2,-1, 0, 2, 3 }
};

const char MT[9][3] = {
	{ 1, 0, 0 },
	{ 1, 1, 0 },
	{ 2, 1, 0 },
	{ 2, 1, 1 },
	{ 2, 2, 1 },
	{ 3, 2, 1 },
	{ 3, 2, 2 },
	{ 4, 3, 2 },
	{ 4, 4, 2 }
};

const char rangeofshot[] = {
	0,
	1,		/* grape */
	3,		/* chain */
	10,		/* round */
	1		/* double */
};

const char *const countryname[] = {
	"American", "British", "Spanish", "French", "Japanese",
	"Federation", "Klingon", "Orion"
};

const char *const classname[] = {
	"Drift wood",
	"Ship of the Line",
	"Ship of the Line",
	"Frigate",
	"Corvette",
	"Sloop",
	"Brig"
};

const char *const directionname[] = {
	"dead ahead",
	"off the starboard bow",
	"off the starboard beam",
	"off the starboard quarter",
	"dead astern",
	"off the port quarter",
	"off the port beam",
	"off the port bow",
	"dead ahead"
};

const char *const qualname[] = {
	"dead", "mutinous", "green", "mundane", "crack", "elite"
};

const char loadname[] = { '-', 'G', 'C', 'R', 'D', 'E' };

const char dr[] = { 0, 1, 1, 0, -1, -1, -1, 0, 1 };
const char dc[] = { 0, 0, -1, -1, -1, 0, 1, 1, 1 };

int mode;
jmp_buf restart;

char debug;				/* -D */
char randomize;				/* -x, give first available ship */
char longfmt;				/* -l, print score in long format */
char nobells;				/* -b, don't ring bell before Signal */

gid_t gid, egid;

struct scenario *cc;		/* the current scenario */
struct ship *ls;		/* &cc->ship[cc->vessels] */

int winddir;
int windspeed;
int turn;
int game;
int alive;
int people;
char hasdriver;
