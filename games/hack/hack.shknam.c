/*	$OpenBSD: hack.shknam.c,v 1.8 2009/10/27 23:59:25 deraadt Exp $	*/

/*
 * Copyright (c) 1985, Stichting Centrum voor Wiskunde en Informatica,
 * Amsterdam
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of the Stichting Centrum voor Wiskunde en
 * Informatica, nor the names of its contributors may be used to endorse or
 * promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982 Jay Fenlason <hack@gnu.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "hack.h"

char *shkliquors[] = {
	/* Ukraine */
	"Njezjin", "Tsjernigof", "Gomel", "Ossipewsk", "Gorlowka",
	/* N. Russia */
	"Konosja", "Weliki Oestjoeg", "Syktywkar", "Sablja",
	"Narodnaja", "Kyzyl",
	/* Silezie */
	"Walbrzych", "Swidnica", "Klodzko", "Raciborz", "Gliwice",
	"Brzeg", "Krnov", "Hradec Kralove",
	/* Schweiz */
	"Leuk", "Brig", "Brienz", "Thun", "Sarnen", "Burglen", "Elm",
	"Flims", "Vals", "Schuls", "Zum Loch",
	0
};

char *shkbooks[] = {
	/* Eire */
	"Skibbereen", "Kanturk", "Rath Luirc", "Ennistymon", "Lahinch",
	"Loughrea", "Croagh", "Maumakeogh", "Ballyjamesduff",
	"Kinnegad", "Lugnaquillia", "Enniscorthy", "Gweebarra",
	"Kittamagh", "Nenagh", "Sneem", "Ballingeary", "Kilgarvan",
	"Cahersiveen", "Glenbeigh", "Kilmihil", "Kiltamagh",
	"Droichead Atha", "Inniscrone", "Clonegal", "Lisnaskea",
	"Culdaff", "Dunfanaghy", "Inishbofin", "Kesh",
	0
};

char *shkarmors[] = {
	/* Turquie */
	"Demirci", "Kalecik", "Boyabai", "Yildizeli", "Gaziantep",
	"Siirt", "Akhalataki", "Tirebolu", "Aksaray", "Ermenak",
	"Iskenderun", "Kadirli", "Siverek", "Pervari", "Malasgirt",
	"Bayburt", "Ayancik", "Zonguldak", "Balya", "Tefenni",
	"Artvin", "Kars", "Makharadze", "Malazgirt", "Midyat",
	"Birecik", "Kirikkale", "Alaca", "Polatli", "Nallihan",
	0
};

char *shkwands[] = {
	/* Wales */
	"Yr Wyddgrug", "Trallwng", "Mallwyd", "Pontarfynach",
	"Rhaeader", "Llandrindod", "Llanfair-ym-muallt",
	"Y-Fenni", "Measteg", "Rhydaman", "Beddgelert",
	"Curig", "Llanrwst", "Llanerchymedd", "Caergybi",
	/* Scotland */
	"Nairn", "Turriff", "Inverurie", "Braemar", "Lochnagar",
	"Kerloch", "Beinn a Ghlo", "Drumnadrochit", "Morven",
	"Uist", "Storr", "Sgurr na Ciche", "Cannich", "Gairloch",
	"Kyleakin", "Dunvegan",
	0
};

char *shkrings[] = {
	/* Hollandse familienamen */
	"Feyfer", "Flugi", "Gheel", "Havic", "Haynin", "Hoboken",
	"Imbyze", "Juyn", "Kinsky", "Massis", "Matray", "Moy",
	"Olycan", "Sadelin", "Svaving", "Tapper", "Terwen", "Wirix",
	"Ypey",
	/* Skandinaviske navne */
	"Rastegaisa", "Varjag Njarga", "Kautekeino", "Abisko",
	"Enontekis", "Rovaniemi", "Avasaksa", "Haparanda",
	"Lulea", "Gellivare", "Oeloe", "Kajaani", "Fauske",
	0
};

char *shkfoods[] = {
	/* Indonesia */
	"Djasinga", "Tjibarusa", "Tjiwidej", "Pengalengan",
	"Bandjar", "Parbalingga", "Bojolali", "Sarangan",
	"Ngebel", "Djombang", "Ardjawinangun", "Berbek",
	"Papar", "Baliga", "Tjisolok", "Siboga", "Banjoewangi",
	"Trenggalek", "Karangkobar", "Njalindoeng", "Pasawahan",
	"Pameunpeuk", "Patjitan", "Kediri", "Pemboeang", "Tringanoe",
	"Makin", "Tipor", "Semai", "Berhala", "Tegal", "Samoe",
	0
};

char *shkweapons[] = {
	/* Perigord */
	"Voulgezac", "Rouffiac", "Lerignac", "Touverac", "Guizengeard",
	"Melac", "Neuvicq", "Vanzac", "Picq", "Urignac", "Corignac",
	"Fleac", "Lonzac", "Vergt", "Queyssac", "Liorac", "Echourgnac",
	"Cazelon", "Eypau", "Carignan", "Monbazillac", "Jonzac",
	"Pons", "Jumilhac", "Fenouilledes", "Laguiolet", "Saujon",
	"Eymoutiers", "Eygurande", "Eauze", "Labouheyre",
	0
};

char *shkgeneral[] = {
	/* Suriname */
	"Hebiwerie", "Possogroenoe", "Asidonhopo", "Manlobbi",
	"Adjama", "Pakka Pakka", "Kabalebo", "Wonotobo",
	"Akalapi", "Sipaliwini",
	/* Greenland */
	"Annootok", "Upernavik", "Angmagssalik",
	/* N. Canada */
	"Aklavik", "Inuvik", "Tuktoyaktuk",
	"Chicoutimi", "Ouiatchouane", "Chibougamau",
	"Matagami", "Kipawa", "Kinojevis",
	"Abitibi", "Maganasipi",
	/* Iceland */
	"Akureyri", "Kopasker", "Budereyri", "Akranes", "Bordeyri",
	"Holmavik",
	0
};

struct shk_nx {
	char x;
	char **xn;
} shk_nx[] = {
	{ POTION_SYM,	shkliquors },
	{ SCROLL_SYM,	shkbooks },
	{ ARMOR_SYM,	shkarmors },
	{ WAND_SYM,	shkwands },
	{ RING_SYM,	shkrings },
	{ FOOD_SYM,	shkfoods },
	{ WEAPON_SYM,	shkweapons },
	{ 0,		shkgeneral }
};

void
findname(char *nampt, size_t len, char let)
{
	struct shk_nx *p = shk_nx;
	char **q;
	int i;

	while(p->x && p->x != let) p++;
	q = p->xn;
	for(i=0; i<dlevel; i++) if(!q[i]){
		/* Not enough names, try general name */
		if(let) findname(nampt, len, 0);
		else {
			(void) strlcpy(nampt, "Dirk", len);
		}
		return;
	}
	(void) strncpy(nampt, q[i], len-1);
	nampt[len-1] = '\0';
}
