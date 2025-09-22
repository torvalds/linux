/*	$OpenBSD: dayfile.c,v 1.12 2009/11/22 09:16:02 tobias Exp $	*/
/*	$NetBSD: dayfile.c,v 1.3 1995/03/21 15:07:18 cgd Exp $	*/

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

struct room dayfile[] = {
	{ 0, {0}, 0, {0} },
	/* 1 */
	{ "You are in the main hangar.",
		{ 5, 2, 9, 3, 3, 1, 0, 0 },
"This is a huge bay where many fighters and cargo craft lie.  Alarms are \n\
sounding and fighter pilots are running to their ships.  Above is a gallery\n\
overlooking the bay. The scream of turbo engines is coming from +. The rest\n\
of the hangar is +. There is an exit +.*\n", {0} },
	/* 2 */
	{ "This is the landing bay.",
		{ 1, 0, 10, 0, 0, 0, 0, 0 },
"Ships are landing here, some heavily damaged. Enemy fighters continually\n\
strafe this vulnerable port. The main hangar is +; *\n\
there is an exit +.*\n", {0} },
	/* 3 */
	{ "You are in the gallery.",
		{ 4, 0, 0, 0, 0, 0, 1, 0 },
"From here a view of the entire landing bay reveals that our battlestar\n\
is near destruction. Fires are spreading out of control and laser blasts\n\
lick at the shadows. The control room is +. ***\n", {0} },
	/* 4 */
	{ "You are in the control room.",
		{ 0, 3, 0, 0, 0, 0, 5, 0 },
"Several frantic technicians are flipping switches wildly but otherwise\n\
this room seems fairly deserted.  A weapons locker has been left open.\n\
A staircase leads down. * There is a way -. **        \n", {0} },
	/* 5 */
	{ "This is the launch room.",
		{ 6, 1, 7, 0, 4, 1, 0, 0 },
"From the launch tubes here fighters blast off into space. Only one is left,\n\
and it is guarded by two fierce men. A staircase leads up from here.\n\
There is a cluttered workbench +. From the main hangar come sounds of great\n\
explosions.  The main hangar is +. The viper launch tubes are -.*\n", {0} },
	/* 6 */
	{ "You are at the workbench.",
		{ 0, 5, 7, 0, 0, 0, 0, 0 },
"Strange and unwieldy tools are arranged here including a lunch box \n\
and pneumatic wrenches and turbo sprocket rockets.*\n\
The launch room is +. The remaining viper is +.*\n", {0} },
	/* 7 */
	{ "You are in the viper launch tube.",
		{ 0, 5, 0, 5, 32, 0, 0, 0 },
"The two guards are eyeing you warily! ****\n", {0} },
	/* 8 */
	{ "This is a walk in closet.",
		{ 22, 0, 0, 0, 0, 0, 0, 0 },
"A wardrobe of immense magnitude greets the eye.  Furs and robes of kings\n\
hang on rack after rack.  Silken gowns, capes woven with spun gold, and \n\
delicate synthetic fabrics are stowed here.  The bedroom is +.***\n", {0} },
	/* 9 */
	{ "You are in a wide hallway leading to the main hangar.",
		{ 0, 0, 11, 1, 0, 0, 0, 0 },
"The walls and ceiling here have been blasted through in several places.\n\
It looks as if quite a battle has been fought for possession of the landing bay\n\
Gaping corpses litter the floor.**  The hallway continues +.\n\
The main hangar is +.\n", {0} },
	/* 10 */
	{ "You are in a wide hallway leading to the landing bay.",
		{ 0, 0, 12, 2, 0, 0, 0, 0 },
"Most of the men and supplies needed in the main hangar come through this\n\
corridor, but the wounded are forced to use it too. It very dank and\n\
crowded here, and the floor is slippery with blood.**\n\
The hallway continues -. The landing bay is +.\n", {0} },
	/* 11 */
	{ "The hallway is very congested with rubble here.",
		{ 0, 0, 0, 9, 13, 1, 0, 0 },
"It is too choked with broken steel girders and other debris to continue\n\
on much farther. Above, the ceiling has caved in and it is possible to \n\
climb up. There is not much chance to go -, -, or -.\n\
But the hallway seems clearer +.\n", {0} },
	/* 12 */
	{ "A wide hallway and a more narrow walkway meet here.",
		{ 14, 15, 0, 10, 0, 0, 0, 0 },
"The intersection is crowded with the many wounded who have come up\n\
the wide hallway and continued +. The walkway is less crowded +.\n\
The wide hallway goes *-.\n", {0} },
	/* 13 */
	{ "You are in what was once an elegant stateroom.",
		{ 16, 0, 0, 0, 0, 0, 11, 0 },
"Whoever lived in this stateroom, he and his female companion\n\
were mercilessly slain in their sleep. Clothes, trinkets and personal\n\
belongings are scattered all across the floor. Through a hole in the\n\
collapsed floor I can see a hallway below.  A door is +.***\n", {0} },
	/* 14 */
	{ "You're at the entrance to the sick bay.",
		{ 17, 12, 18, 0, 0, 0, 0, 0 },
"The wounded are entering the sick bay in loudly moaning files.\n\
The walkway continues - and +. A doctor is motioning for you to \n\
come to the -. *\n", {0} },
	/* 15 */
	{ "You're in the walkway.",
		{ 12, 19, 0, 0, 0, 0, 0, 0 },
"Most of the men and supplies were coming from the armory. The walkway\n\
continues -. The armory is +.**\n", {0} },
	/* 16 */
	{ "These are the executive suites of the battlestar.",
		{ 20, 13, 21, 22, 23, 1, 24, 0 },
"Luxurious staterooms carpeted with crushed velvet and adorned with beaten\n\
gold open onto this parlor. A wide staircase with ivory banisters leads\n\
up or down. This parlor leads into a hallway +. The bridal suite is\n\
+. Other rooms lie - and +.\n", {0} },
	/* 17 */
	{ "You're in a long dimly lit hallway.",
		{ 0, 14, 25, 0, 0, 0, 0, 0 },
"This part of the walkway is deserted. There is a dead end +. The\n\
entrance to the sickbay is +. The walkway turns sharply -.*\n", {0} },
	/* 18 */
	{ "This is the sick bay.",
		{ 0, 0, 0, 14, 0, 0, 0, 0 },
"Sinister nurses with long needles and pitiful aim probe the depths of suffering\n\
here. Only the mortally wounded receive medical attention on a battlestar,\n\
but afterwards they are thrown into the incinerators along with the rest.**\n\
Nothing but death and suffering +.  The walkway is +.\n", {0} },
	/* 19 */
	{ "You're in the armory.",
		{ 15, 26, 0, 0, 0, 0, 0, 0 },
"An armed guard is stationed here 365 sectars a yarn to protect the magazine.\n\
The walkway is +. The magazine is +.**\n", {0} },
	/* 20 */
	{ "The hallway ends here at the presidential suite.",
		{ 27, 16, 0, 0, 0, 0, 0, 0 },
"The door to this suite is made from solid magnesium, and the entryway is\n\
inlaid with diamonds and fire opals. The door is ajar +. The hallway\n\
goes -.**\n", {0} },
	/* 21 */
	{ "This is the maid's utility room.",
		{ 0, 0, 0, 16, 0, 0, 0, 0 },
"What a gruesome sight! The maid has been brutally drowned in a bucket of\n\
Pine Sol and repeatedly stabbed in the back with a knife.***\n\
The hallway is +.\n", {0} },
	/* 22 */
	{ "This is a luxurious stateroom.",
		{ 0, 8, 16, 0, 0, 0, 0, 0 },
"The floor is carpeted with a soft animal fur and the great wooden furniture\n\
is inlaid with strips of platinum and gold.  Electronic equipment built\n\
into the walls and ceiling is flashing wildly.  The floor shudders and\n\
the sounds of dull explosions rumble through the room.  From a window in\n\
the wall + comes a view of darkest space.  There is a small adjoining\n\
room +, and a doorway +.*\n", {0} },
	/* 23 */
	{ "You are at the entrance to the dining hall.",
		{ 0, 0, 28, 0, 0, 0, 16, 0 },
"A wide staircase with ebony banisters leads down here.**\n\
The dining hall is -.*\n", {0} },
	/* 24 */
	{ "This was once the first class lounge.",
		{ 0, 0, 29, 0, 16, 1, 0, 0 },
"There is much rubble and destruction here that was not apparent elsewhere.\n\
The walls and ceilings have broken in in some places. A staircase with\n\
red coral banisters leads up. It is impossible to go - or -.\n\
It seems a little clearer +.*\n", {0} },
	/* 25 */
	{ "You are in a narrow stairwell.",
		{ 0, 17, 0, 0, 30, 1, 0, 0 },
"These dusty and decrepit stairs lead up.  There is no way -.  The\n\
hallway turns sharply -.**\n", {0} },
	/* 26 */
	{ "You are in the magazine.",
		{ 19, 0, 0, 0, 0, 0, 0, 0 },
"Rows and rows of neatly stacked ammunition for laser pistols and grenade\n\
launchers are here. The armory is +.***\n", {0} },
	/* 27 */
	{ "You're in the presidential suite.",
		{ 0, 20, 0, 0, 0, 0, 0, 0 },
"Apparently the president has been assassinated. A scorched figure lies\n\
face downward on the carpet clutching his chest.*\n\
The hallway leads -.**\n", {0} },
	/* 28 */
	{ "You are in the dining hall.",
		{ 0, 30, 31, 23, 0, 0, 0, 0 },
"This was the scene of a mass suicide. Hundreds of ambassadors and assorted\n\
dignitaries sit slumped over their breakfast cereal. I suppose the news\n\
of the cylon attack killed them. There is a strange chill in this room.  I\n\
would not linger here. * The kitchen is +. Entrances + and +.\n", {0} },
	/* 29 */
	{ "The debris is very thick here.",
		{ 0, 11, 0, 24, 0, 0, 0, 0 },
"Broken furniture, fallen girders, and other rubble block the way.\n\
There is not much chance to continue -, -, or -.\n\
It would be best to go -.\n", {0} },
	/* 30 */
	{ "You are in the kitchen.",
		{ 28, 0, 0, 0, 0, 0, 0, 0 },
"This room is full of shining stainless steel and burnished bronze cookware. An \n\
assortment of tropical fruits and vegetables as well as fine meats and cheeses \n\
lies on a sterling platter. The chef, unfortunately, has been skewered like a \n\
side of beef. The dining room is +. ** There is a locked door +.\n", {0} },
	/* 31 */
	{ "You are in an arched entry leading to the dining room.",
		{ 0, 0, 0, 28, 0, 0, 0, 0 },
"The door leading out is bolted shut from the outside and is very strong.***\n\
The dining room is +.\n", {0} },
	/* 32 */
	{ "You are in space.",
		{ 33, 34, 35, 36, 37, 1, 33, 1 },
"****\n", {0} },
	/* 33 */
	{ "You are in space.",
		{ 38, 32, 39, 40, 41, 1, 42, 1 },
"****\n", {0} },
	/* 34 */
	{ "You are in space.",
		{ 32, 44, 45, 46, 47, 1, 48, 1 },
"****\n", {0} },
	/* 35 */
	{ "You are in space.",
		{ 40, 45, 49, 32, 50, 1, 51, 1 },
"****\n", {0} },
	/* 36 */
	{ "You are in space.",
		{ 41, 46, 32, 52, 53, 1, 54, 1 },
"****\n", {0} },
	/* 37 */
	{ "You are in space.",
		{ 42, 47, 50, 53, 55, 1, 32, 1 },
"****\n", {0} },
	/* 38 */
	{ "You are in space.",
		{ 43, 48, 51, 54, 32, 1, 56, 1 }, "****\n", {0} },
	/* 39 */
	{ "You are in space.",
		{ 57, 33, 40, 41, 42, 1, 43, 1 },
"****\n", {0} },
	/* 40 */
	{ "You are in space.",
		{ 39, 35, 57, 33, 58, 1, 59, 1 },
"****\n", {0} },
	/* 41 */
	{ "You are in space.",
		{ 39, 36, 33, 59, 60, 1, 61, 1 },
"****\n", {0} },
	/* 42 */
	{ "You are in space.",
		{ 39, 37, 58, 60, 62, 1, 33, 1 },
"****\n", {0} },
	/* 43 */
	{ "You are in space.",
		{ 39, 38, 59, 61, 33, 1, 63, 1 },
"****\n", {0} },
	/* 44 */
	{ "You are in space.",
		{ 34, 64, 45, 46, 47, 1, 48, 1 },
"****\n", {0} },
	/* 45 */
	{ "You are in space.",
		{ 35, 44, 49, 34, 50, 1, 51, 1 },
"****\n", {0} },
	/* 46 */
	{ "You are in space.",
		{ 36, 44, 34, 52, 53, 1, 54, 1 },
"****\n", {0} },
	/* 47 */
	{ "You are in space.",
		{ 37, 44, 50, 53, 55, 1, 34, 1 },
"****\n", {0} },
	/* 48 */
	{ "You are in space.",
		{ 38, 44, 51, 54, 34, 1, 56, 1 },
"****\n", {0} },
	/* 49 */
	{ "You are in space.",
		{ 49, 49, 52, 35, 49, 1, 49, 1 },
"****\n", {0} },
	/* 50 */
	{ "You are in space.",
		{ 58, 47, 49, 37, 55, 1, 35, 1 },
"****\n", {0} },
	/* 51 */
	{ "You are in space.",
		{ 59, 48, 49, 38, 35, 1, 56, 1 },
"****\n", {0} },
	/* 52 */
	{ "You are in space.",
		{ 52, 52, 36, 49, 52, 1, 52, 1 },
"****\n", {0} },
	/* 53 */
	{ "You are in space.",
		{ 60, 46, 37, 52, 55, 1, 36, 1 },
"****\n", {0} },
	/* 54 */
	{ "You are in space.",
		{ 61, 48, 38, 52, 36, 1, 56, 1 },
"****\n", {0} },
	/* 55 */
	{ "You are in space.",
		{ 62, 55, 55, 55, 56, 1, 37, 1 },
"****\n", {0} },
	/* 56 */
	{ "You are in space.",
		{ 56, 56, 56, 56, 38, 1, 55, 1 },
"****\n", {0} },
	/* 57 */
	{ "You are in space.",
		{ 65, 39, 57, 57, 57, 1, 57, 1 },
"****\n", {0} },
	/* 58 */
	{ "You are in space.",
		{ 39, 50, 49, 42, 62, 1, 40, 1 },
"****\n", {0} },
	/* 59 */
	{ "You are in space.",
		{ 39, 51, 49, 43, 40, 1, 63, 1 },
"****\n", {0} },
	/* 60 */
	{ "You are in space.",
		{ 39, 53, 43, 59, 62, 1, 41, 1 },
"****\n", {0} },
	/* 61 */
	{ "You are in space.",
		{ 39, 54, 43, 59, 41, 1, 56, 1 },
"****\n", {0} },
	/* 62 */
	{ "You are in space.",
		{ 39, 55, 62, 62, 56, 1, 42, 1 },
"****\n", {0} },
	/* 63 */
	{ "You are in space.",
		{ 39, 56, 35, 36, 43, 1, 55, 1 },
"****\n", {0} },
	/* 64 */
	{ "You are in space.",
		{ 44, 66, 66, 66, 66, 1, 66, 1 },
"****\n", {0} },
	/* 65 */
	{ "You are in space.",
		{ 67, 57, 67, 67, 67, 1, 67, 1 },
"****\n", {0} },
	/* 66 */
	{ "You are in space.",
		{ 64, 68, 68, 68, 68, 1, 68, 1 },
"****\n", {0} },
	/* 67 */
	{ "You are orbiting a small blue planet.",
		{ 67, 67, 67, 67, 65, 1, 69, 1 },
"****\n", {0} },
	/* 68 */
	{ "You are orbiting a tropical planet.",
		{ 68, 68, 68, 68, 66, 1, 70, 1 },
"****\n", {0} },
	/* 69 */
	{ "You are flying through a dense fog.",
		{ 69, 69, 69, 69, 69, 1, 69, 1 },
"A cold grey sea of mist is swirling around the windshield and water droplets\n\
are spewing from the wingtips. Ominous shadows loom in the darkness and it\n\
feels as if a trap is closing around us. I have lost all sense of direction.\n\
****\n", {0} },
	/* 70 */
	{ "You are approaching an island.",
		{ 71, 72, 73, 74, 68, 1, 0, 1 },
"Coconut palms, sword ferns, orchids, and other lush vegetation drape this\n\
jagged island carved seemingly from pure emerald and set in a turquoise\n\
sea. The land rises sharply +. There is a nice beach* +.*\n", {0} },
	/* 71 */
	{ "You are flying over a mountainous region.",
		{ 75, 73, 76, 77, 68, 1, 0, 1 },
"Below is a magnificent canyon with deep gorges, high pinnacles and\n\
waterfalls plummeting hundreds of feet into mist. Everything in sight\n\
is carpeted with a tropical green.* The ocean is +.**\n", {0} },
	/* 72 */
	{ "You are flying over the ocean.",
		{ 74, 78, 78, 78, 68, 1, 0, 1 },
"You bank over the water and your wingtips dip low to the green waves.  The\n\
sea is very shallow here and the white coral beds beneath us teem with \n\
colorful fish.****\n", {0} },
	/* 73 */
	{ "You are flying over the beach.",
		{ 71, 72, 79, 74, 68, 1, 80, 1 },
"A warm gentle surf caresses the white coral beach here. The land rises\n\
sharply +.* The beach is lost in low cliffs and rocks +.*\n", {0} },
	/* 74 */
	{ "You are flying over a large lagoon.",
		{ 81, 72, 73, 82, 68, 1, 0, 1 },
"Encircled by a coral reef, the palms and ferns in this sheltered spot\n\
have grown down to the water's very brink which winds tortuously inland.\n\
There looks like a small village +.***\n", {0} },
	/* 75 */
	{ "You are flying over a gently sloping plane.",
		{ 83, 71, 84, 85, 68, 1, 0, 1 },
"This is where several alluvial fans and ancient lava flows have run\n\
together forming a fertile plane choked with vegetation. It would be\n\
impossible to land safely here.* The terrain is more rugged +.**\n", {0} },
	/* 76 */
	{ "You are flying through a gorge.",
		{ 0, 0, 86, 71, 68, 1, 102, 1 },
"This narrow, steep sided canyon is lined with waving ferns. The floor is of\n\
light gravel with many freshets pouring from the walls and running along it.\n\
The gorge leads to the sea** +, and to a tumultuous origin +.\n", {0} },
	/* 77 */
	{ "You are flying over a plantation.",
		{ 85, 81, 71, 88, 68, 1, 89, 1 },
"Rows of palms, papayas, mangoes, kiwi, as well as smaller fields of sugar\n\
cane and pineapple are growing here. It might be possible to land here, but\n\
I wouldn't advise it.* There looks like two small settlements +     \n\
and *+.\n", {0} },
	/* 78 */
	{ "You are over the ocean.",
		{ 72, 78, 79, 74, 68, 1, 0, 1 },
"The deep green swells foam and roll into the shore **+*.\n", {0} },
	/* 79 */
	{ "You are flying along the coast.",
		{ 86, 72, 90, 73, 68, 1, 91, 1 },
"The coastline here is very rocky with little or no sand. The surf in some\n\
places is violent and explodes in a shower of sparkling spray.\n\
There is a winding road below which closely follows the shore. ****\n", {0} },
	/* 80 */
	{ "This is a beautiful coral beach.",
		{ 106, 0, 107, 108, 73, 0, 0, 0 },
"Fine silver sand kissed lightly by warm tropical waters stretches at least\n\
30 meters here from the ocean to under gently swaying palms +.***\n", {0} },
	/* 81 */
	{ "You are flying over a small fishing village.",
		{ 77, 74, 71, 82, 68, 1, 92, 1 },
"A few thatched huts a short distance from the water and row of more modern\n\
bungalows on either side of a dirt road are all that is here. The road\n\
continues on ***+.\n", {0} },
	/* 82 */
	{ "You are flying over a clearing.",
		{ 88, 72, 74, 87, 68, 1, 93, 1 },
"There is a dock here (big enough for a seaplane) leading to a grassy\n\
meadow and a road. Some people are having a party down there.  Below is\n\
a good landing site. ****\n", {0} },
	/* 83 */
	{ "You are flying over the shore.",
		{ 94, 75, 95, 96, 68, 1, 0, 1 },
"Rocky lava flows or coarse sandy beaches are all that is here except for\n\
sparse herbs and weeds.****\n", {0} },
	/* 84 */
	{ "You are flying in a wide valley.",
		{ 95, 97, 86, 75, 68, 1, 98, 1 },
"This is a shallow valley yet the floor is obscured by a thick mist.\n\
The valley opens to the sea +. The mist grows thicker +.**\n", {0} },
	/* 85 */
	{ "You are flying near the shore.",
		{ 96, 77, 75, 99, 68, 1, 0, 1 },
"Very tall palm trees growing in neatly planted rows march off from the \n\
water here towards the hills and down to the flat lands *+.*\n\
There is a nice beach +.\n", {0} },
	/* 86 */
	{ "You are flying around the very tip of the island.",
		{ 95, 79, 90, 84, 68, 1, 0, 1 },
"There is no beach here for sheer cliffs rise several hundred feet\n\
to a tree covered summit. Far below, the blue sea gnaws voraciously at\n\
the roots of these cliffs. The island bends around +** and +.\n", {0} },
	/* 87 */
	{ "You are flying along the coastline.",
		{ 99, 82, 88, 100, 68, 1, 101, 1 },
"There is a narrow strip of sand here lined with ferns and shrubs, but very\n\
few trees. The beach is barley wide enough to land on. The beach continues\n\
on -.* There are some buildings +.*\n", {0} },
	/* 88 */
	{ "You are flying over several cottages and buildings",
		{ 99, 82, 77, 87, 68, 1, 103, 1 },
"The grounds here are landscaped with palm trees, ferns, orchids, and beds of\n\
flowering plumeria and antheriums. Directly below is a small ornate white\n\
house with a belltower, a lush green lawn, and a spurting fountain.\n\
Small dirt roads go + and +.**\n", {0} },
	/* 89 */
	{ "You are in a field of sugar cane.",
		{ 109, 110, 111, 112, 77, 0, 0, 0 },
"These strong, thick canes give little shelter but many cuts and scrapes.\n\
There are some large trees ***+.\n", {0} },
	/* 90 */
	{ "You are flying over the ocean.",
		{ 95, 78, 90, 86, 68, 1, 0, 1 },
"The water is a placid turquoise and so clear that fish and sharks\n\
many fathoms below are clearly visible.****\n", {0} },
	/* 91 */
	{ "You are on the coast road.",
		{ 113, 114, 115, 116, 79, 0, 0, 0 },
"The road winds close to the shore here and the sound of crashing surf is\n\
deafening.* The water is +. The road continues - and -.\n", {0} },
	/* 92 */
	{ "You are on the main street of the village.",
		{ 117, 118, 119, 120, 81, 0, 0, 0 },
"Thatched roofs and outrigger canoes, palm trees and vacation bungalows, and\n\
comely natives in a tropical paradise all make this a fantasy come true.\n\
There is an open bungalow +.*  The road continues - and -.\n", {0} },
	/* 93 */
	{ "You are at the sea=plane dock.",
		{ 121, 122, 123, 124, 82, 0, 0, 0 },
"Native girls with skin of gold, clad only in fragrant leis and lavalavas,\n\
line the dockside to greet you. A couple of ukulele=plucking islanders and a\n\
keyboard player are adding appropriate music. A road crosses the clearing \n\
+*.  There are some tables set up +.*\n", {0} },
	/* 94 */
	{ "You are flying over the ocean.",
		{ 94, 83, 95, 96, 68, 1, 0, 1 },
"Sea weeds and kelp surge in the waves off shore here.  The ocean becomes \n\
much deeper +.***\n", {0} },
	/* 95 */
	{ "You are flying along the coast.",
		{ 94, 84, 86, 83, 68, 1, 0, 1 },
"The land is very low here with a river running into the sea +. There\n\
is a wide valley opening up +. The very tip of the island is +.*\n", {0} },
	/* 96 */
	{ "You are flying along the coast.",
		{ 94, 85, 83, 99, 68, 1, 0, 1 },
"There are some secluded sandy stretches of beach here, but too many rocky\n\
outcroppings of lava to land. There is a nicer beach ***+.\n", {0} },
	/* 97 */
	{ "You are lost in a sea of fog.",
		{ 97, 104, 97, 97, 97, 1, 0, 1 },
"What have you gotten us into?\n\
I can't see a thing! ****\n", {0} },
	/* 98 */
	{ "You are on a gravel wash.",
		{ 125, 126, 127, 128, 84, 0, 0, 0 },
"The sound of cascading water is the background for a diluted chorus of \n\
gurgling, splashing, and enchantingly delicate singing. Great billows\n\
of steam are rising *+.**\n", {0} },
	/* 99 */
	{ "You are flying over a wide beach.",
		{ 96, 88, 85, 87, 68, 1, 105, 1 },
"Unlike the leeward beaches, few coconut palms grow here but a well groomed\n\
lawn and garden with traipsing stone walks leads down to the sand.*\n\
There are some buildings +. Some trees are growing +.*\n", {0} },
	/* 100 */
	{ "You are flying over the ocean.",
		{ 100, 100, 87, 100, 68, 1, 0, 1 },
"The sea is a perfectly clear blue with a white sandy bottom.  No coral\n\
grows underwater here, but the force of the waves is broken by the steep\n\
incline.****\n", {0} },
	/* 101 */
	{ "You are on a narrow strip of sand.",
		{ 129, 130, 131, 0, 87, 0, 0, 0 },
"Rather coarse sand makes this beach very steep and only a few meters wide.\n\
A fresh ocean breeze is rustling the ferns **+.*\n", {0} },
	/* 102 */
	{ "This is Fern Canyon.",
		{ 0, 0, 132, 133, 76, 0, 0, 0 },
"Delicate waving ferns flourish here, suckled by warm water dripping from \n\
every fissure and crevice in the solid rock walls.\n\
The canyon winds **-, and -.\n", {0} },
	/* 103 */
	{ "This is the front lawn.",
		{ 134, 135, 136, 137, 88, 0, 0, 0 },
"There is a small fountain here where the driveway meets the lawn.\n\
Across the driveway, +, is an ornate white house with and elegant \n\
woodworking.  The bargeboards are carved with fylfots, the ancient \n\
symbols of luck.  Even a bell tower has been built here.*  There is a \n\
road + which turns into the driveway.*\n", {0} },
	/* 104 */
	{ "You have just crossed the crest of a mountain.",
		{ 97, 79, 86, 71, 68, 1, 0, 1 },
"The fog vanished mysteriously as we flew over the crest.*\n\
Far + I can see the ocean.**\n", {0} },
	/* 105 */
	{ "You are on a sandy beach.",
		{ 138, 139, 140, 0, 99, 0, 0, 0 },
"This is the only good beach on the weather side of the island. Fine coral\n\
sand, a fresh sea breeze, and dramatic surf add to its appeal.**\n\
Stone steps lead to the gardens +.*\n", {0} },
	/* 106 */
	{ "You are among palm trees near the shore.",
		{ 141, 80, 142, 143, 73, 0, 0, 0 },
"Arching coconut palms laden with fruit provide a canopy for the glistening\n\
white sand and sparse grasses growing here. The forest grows denser +.\n\
The ocean is +.**\n", {0} },
	/* 107 */
	{ "You are walking along the beach.",
		{ 144, 0, 145, 80, 73, 0, 0, 0 },
"The warm tropical waters nuzzle your ankles as you walk. Above is a fiercely\n\
blue sky. The slope of the sand is so gentle that two hundred meters\n\
offshore the water is only knee deep.** There are some rocks +.*\n", {0} },
	/* 108 */
	{ "You are walking along the beach.",
		{ 146, 0, 80, 147, 73, 0, 0, 0 },
"Many beautiful shells have been washed up here including bright yellow \n\
cowries, chocolate colored murex, orange conches, striped tritons and the\n\
deadly cone shells.****\n", {0} },
	/* 109 */
	{ "You are in a papaya grove.",
		{ 148, 89, 149, 150, 77, 0, 0, 0 },
"Green slender trees no taller than three meters bulge with their\n\
orange succulent fruit. There are some tall trees +.***\n", {0} },
	/* 110 */
	{ "You are in a field of pineapple.",
		{ 89, 151, 152, 153, 77, 0, 0, 0 },
"The sharp dagger like pineapple leaves can pierce the flesh and hold fast\n\
a skewered victim with tiny barbs.* The field ends +.**\n", {0} },
	/* 111 */
	{ "You are in a field of kiwi plants.",
		{ 149, 154, 155, 89, 77, 0, 0, 0 },
"Round hairy fruit hang from staked vines here. There are some trees +\n\
and +. The field ends in a dirt road +.*\n", {0} },
	/* 112 */
	{ "You are in a large grove of coconuts.",
		{ 150, 153, 89, 156, 77, 0, 0, 0 },
"These trees are much taller than any growing near the shore plus the fat,\n\
juicy coconuts have been selectively cultivated. The grove continues\n\
+, +, *and +.\n", {0} },
	/* 113 */
	{ "You are in the woods.",
		{ 157, 91, 158, 116, 79, 0, 0, 0 },
"Tropical undergrowth makes the going rough here. Sword ferns give no strong\n\
foot hold and the dangling vines would gladly throttle one. Strange cackling\n\
noises are coming from somewhere +.***\n", {0} },
	/* 114 */
	{ "You are at the shore.",
		{ 91, 0, 159, 145, 79, 0, 0, 0 },
"Explosions of surf jetting out of underwater tunnels here make it\n\
impossible to climb down to a small cave entrance below.  Only at rare\n\
minus tides would it be possible to enter.***  The beach is better +.\n", {0} },
	/* 115 */
	{ "You are on the coast road.",
		{ 158, 161, 162, 91, 79, 0, 0, 0 },
"The road is beginning to turn inland.* I can hear the surf +. The road\n\
continues +.*\n", {0} },
	/* 116 */
	{ "The road winds deeper into the trees.",
		{ 163, 142, 91, 164, 79, 0, 0, 0 },
"Only narrow sunbeams filter through the foliage above. The moist rich earth\n\
has nurtured a myriad of trees, shrubs, and flowers to grow here. The\n\
road continues - and *- from here.*\n", {0} },
	/* 117 */
	{ "This is the front porch of the bungalow.",
		{ 165, 92, 0, 0, 81, 0, 0, 0 },
"These wooden steps and porch are very bucolic. A little woven mat on the \n\
doorstep reads \"Don't Tread on Me\". The open front door is +.\n\
A stone walk leads to the main street +.**\n", {0} },
	/* 118 */
	{ "You are on a path leading to the lagoon.",
		{ 92, 166, 167, 168, 81, 0, 0, 0 },
"This path trampled fern, grass, sapling, and anything else that got in its\n\
way.* The water is +.**\n", {0} },
	/* 119 */
	{ "This is a dirt road.",
		{ 169, 118, 170, 92, 81, 0, 0, 0 },
"**The road continues on - here for some distance. A village is +.\n", {0} },
	/* 120 */
	{ "You are on a dirt road.",
		{ 171, 118, 92, 172, 81, 0, 0, 0 },
"**There is a small village +. The road continues +.\n", {0} },
	/* 121 */
	{ "You are on a dirt road.",
		{ 173, 93, 174, 175, 82, 0, 0, 0 },
"The light tan soil of the road contrasts artistically with the lush green\n\
vegetation and searing blue sky.*  There is a clearing and many people +.\n\
The road continues - and -.\n", {0} },
	/* 122 */
	{ "You are at the seaplane dock.",
		{ 93, 0, 176, 177, 82, 0, 0, 0 },
"Several muscular, bronze skinned men greet you warmly as you pass under\n\
a thatched shelter above the dock here. Polynesian hospitality.\n\
There is a clearing +.* A trail runs around the lagoon + and +.\n", {0} },
	/* 123 */
	{ "There are some tables on the lawn here.",
		{ 121, 122, 123, 93, 82, 0, 0, 0 },
"Hors d'oeuvres, canapes, mixed drinks, and various narcotic drugs along with\n\
cartons of Di Gel fill the tables to overflowing. Several other guests are\n\
conversing and talking excitedly****.\n", {0} },
	/* 124 */
	{ "You are nosing around in the bushes.",
		{ 124, 124, 93, 124, 82, 0, 0, 0 },
"There is little here but some old beer cans. You are making fools out of\n\
us in front of the other guests.** It would be best to go -.*\n", {0} },
	/* 125 */
	{ "You are walking in a dry stream bed.",
		{ 178, 98, 179, 0, 84, 0, 0, 0 },
"The large cobblestones are difficult to walk on. No sunlight reaches\n\
below a white canopy of fog seemingly generated from *+.  A dirt path \n\
along the wash is +. A high bank is impossible to climb +.\n", {0} },
	/* 126 */
	{ "You are at the thermal pools.",
		{ 98, 0, 180, 181, 84, 0, 0, 0 },
"Several steaming fumaroles and spluttering geysers drenched by icy mountain\n\
waters from a nearby waterfall heat half a dozen natural pools to a\n\
delicious 42 degrees. Enchantingly beautiful singing seems to flow from the\n\
water itself as it tumbles down the falls.*** There is a mossy entrance\n\
to a cave +.\n", {0} },
	/* 127 */
	{ "You are in the woods.",
		{ 127, 180, 182, 98, 84, 0, 0, 0 },
"Coniferous trees girded by wild huckleberries, elderberries, salmonberries\n\
and thimbleberries enjoy a less tropical climate here in the high mountains.\n\
*The sound of rushing water is coming from +.**\n", {0} },
	/* 128 */
	{ "You are on a dirt trail.",
		{ 179, 181, 98, 0, 84, 0, 0, 0 },
"The trail seems to start here and head -.** High cliffs border the \n\
trail +.\n", {0} },
	/* 129 */
	{ "You are  walking along the beach.",
		{ 183, 101, 184, 0, 87, 0, 0, 0 },
"A rather unnerving surf explodes onto the beach here and dashes itself into\n\
spray on the steep incline. The beach continues + and +.**\n", {0} },
	/* 130 */
	{ "You are walking along the beach.",
		{ 101, 185, 186, 0, 87, 0, 0, 0 },
"This is not a very nice beach. The coarse sand hurts my feet.****\n", {0} },
	/* 131 */
	{ "You are walking through some ferns.",
		{ 184, 186, 187, 101, 87, 0, 0, 0 },
"This is a wide field growing only ferns and small shrubs.** The \n\
ocean is *+.\n", {0} },
	/* 132 */
	{ "You are in a narrow canyon.",
		{ 0, 0, 188, 102, 76, 0, 0, 0 },
"The steep sides here squeeze a little freshet through a gauntlet like\n\
series of riffles and pools.****\n", {0} },
	/* 133 */
	{ "The canyon is much wider here.",
		{ 0, 0, 102, 189, 76, 0, 0, 0 },
"The sheer rock walls rise 10 meters to the forest above. A slender \n\
waterfall careens away from the face of the rock high above and showers\n\
the gravel floor with sparkling raindrops.** The canyon continues -\n\
and -.\n", {0} },
	/* 134 */
	{ "You are on the front porch of the cottage.",
		{ 190, 103, 0, 0, 0, 0, 0, 0 },
"Several giggling native girls came running down the steps as you approached\n\
and headed on down the road.  On the fern rimmed porch is a small table with\n\
matching white wrought iron chairs cushioned with red velvet.  The front\n\
door leads -.  The lawn and fountain are +.**\n", {0} },
	/* 135 */
	{ "You are in a palm grove.",
		{ 103, 191, 192, 105, 88, 0, 0, 0 },
"****\n", {0} },
	/* 136 */
	{ "You are on a dirt road.",
		{ 193, 192, 245, 103, 88, 0, 0, 0 },
"There is a large village +. The road cleaves a coconut plantation +.\n\
A small dirt road goes -, and a driveway peels off +.\n", {0} },
	/* 137 */
	{ "You are in a field of small shrubs.",
		{ 184, 186, 103, 187, 88, 0, 0, 0 },
"**Pine and other coniferous saplings have been planted here.  The rich brown\n\
soil is well tilled and watered.  Across a large lawn, there is a small\n\
cottage +. I can feel a delicious sea breeze blowing from +.\n", {0} },
	/* 138 */
	{ "The beach is pretty rocky here.",
		{ 194, 105, 195, 0, 96, 0, 0, 0 },
"Dangerous surf and lava outcroppings make this a treacherous strand.\n\
The beach is nicer* +.**\n", {0} },
	/* 139 */
	{ "The beach is almost 10 meters wide here.",
		{ 105, 183, 196, 0, 99, 0, 0, 0 },
"The sand has become more coarse and the beach steeper.* It gets \n\
worse +.**\n", {0} },
	/* 140 */
	{ "You are in the gardens.",
		{ 195, 196, 197, 105, 99, 0, 0, 0 },
"Lush green lawns studded with palms and benches stretch as far as the eye\n\
can see.** A path leads -. Stone steps lead down to the beach +.\n", {0} },
	/* 141 */
	{ "You are on the coast road.",
		{ 198, 106, 163, 199, 73, 0, 0, 0 },
"The forest is dense on either side and conceals the road from anyone\n\
approaching it.**  The road continues - and -.\n", {0} },
	/* 142 */
	{ "You are in the forest.",
		{ 116, 107, 91, 106, 73, 0, 0, 0 },
"There are trees and ferns all around.****\n", {0} },
	/* 143 */
	{ "You are in the forest.",
		{ 199, 108, 106, 146, 73, 0, 0, 0 },
"There are trees and ferns all around.****\n", {0} },
	/* 144 */
	{ "You are in a copse.",
		{ 142, 107, 145, 80, 0, 0, 0, 0 },
"This is a secret hidden thicket only noticeable from the beach. Someone\n\
has been digging here recently.****\n", {0} },
	/* 145 */
	{ "You are at the tide pools.",
		{ 91, 0, 114, 107, 79, 0, 0, 0 },
"These rocks and pools are the home for many sea anemones and crustaceans.\n\
**The surf is very rough +. There is a nice beach +.\n", {0} },
	/* 146 */
	{ "You are in the forest.",
		{ 199, 108, 143, 0, 73, 0, 0, 0 },
"This is a shallow depression sheltered from the wind by a thick growth of \n\
thorny shrubs. It looks like someone has camped here. There is a fire pit\n\
with some dry sticks and grass nearby.* The beach is +.* The thorny\n\
shrubs block the way -.\n", {0} },
	/* 147 */
	{ "You are at the mouth of the lagoon.",
		{ 200, 0, 108, 201, 74, 0, 0, 0 },
"The beach ends here where the coral reef rises to form a wide lagoon\n\
bending inland. A path winds around the lagoon to the -.*\n\
The beach continues on -. Only water lies +.\n", {0} },
	/* 148 */
	{ "You are in a breadfruit grove.",
		{ 202, 109, 203, 204, 77, 0, 0, 0 },
"The tall trees bend leisurely in the breeze, holding many round breadfruits\n\
close to their large serrated leaves.  There are coconut palms +,\n\
*+, and +.\n", {0} },
	/* 149 */
	{ "You are in a grove of mango trees.",
		{ 203, 111, 205, 109, 77, 0, 0, 0 },
"The juicy yellow red fruits are nearly ripe on the trees here. There are\n\
some coconut palms +. There are some vines +. There is a road +.*\n", {0} },
	/* 150 */
	{ "You are in a grove of coconut palms.",
		{ 204, 112, 109, 206, 77, 0, 0, 0 },
"All I can see around us are palm trees.****\n", {0} },
	/* 151 */
	{ "You are in a coconut grove.",
		{ 110, 207, 208, 209, 77, 0, 0, 0 },
"There are countless trees here.****\n", {0} },
	/* 152 */
	{ "You are in a field of pineapple.",
		{ 154, 208, 210, 110, 77, 0, 0, 0 },
"The sharp leaves are cutting me to ribbons. There is a road **+.\n\
More pineapple +.\n", {0} },
	/* 153 */
	{ "You are in a coconut grove.",
		{ 112, 209, 110, 211, 77, 0, 0, 0 },
"There is a field of pineapple **+.*\n", {0} },
	/* 154 */
	{ "You are on the edge of a kiwi and pineapple field.",
		{ 111, 152, 155, 110, 77, 0, 0, 0 },
"An irrigation ditch separates the two fields here. There is a road **+.*\n", {0} },
	/* 155 */
	{ "This is a dirt road.",
		{ 205, 210, 212, 111, 77, 0, 0, 0 },
"The road runs - and - here.**\n", {0} },
	/* 156 */
	{ "You are in a palm grove.",
		{ 206, 211, 112, 213, 77, 0, 0, 0 },
"There are palm trees all around us.****\n", {0} },
	/* 157 */
	{ "You are on the edge of a small clearing.",
		{ 157, 113, 157, 157, 79, 0, 0, 0 },
"The ground is rather marshy here and darting in and out of the many tussocks\n\
is a flock of wild chicken like fowl.****\n", {0} },
	/* 158 */
	{ "You are in the woods.",
		{ 158, 115, 215, 113, 79, 0, 0, 0 },
"You have walked a long way and found only trees. ****\n", {0} },
	/* 159 */
	{ "You are walking along the shore.",
		{ 115, 0, 214, 114, 86, 0, 0, 0 },
"You are now about 10 meters above the surf on a gently rising cliffside.**\n\
The land rises +. There is a beach far +.\n", {0} },
	/* 160 */
	{ "You are just inside the entrance to the sea cave.",
		{ 246, 114, 0, 0, 114, 1, 0, 0 },
"The sound of water dripping in darkness and the roar of the ocean just outside\n\
create a very unwelcoming atmosphere inside this cave. Only on rare occasions\n\
such as this is it possible to enter the forbidden catacombs... The cave\n\
continues -.***\n", {0} },
	/* 161 */
	{ "You are in a secret nook beside the road.",
		{ 115, 159, 162, 91, 79, 0, 0, 0 },
"Hidden from all but the most stalwart snoopers are some old clothes, empty\n\
beer cans and a trash baggie full of used Huggies and ordure. Lets get\n\
back to the road +.***\n", {0} },
	/* 162 */
	{ "You are on the coast road.",
		{ 215, 214, 0, 115, 86, 0, 0, 0 },
"The road turns abruptly - here, avoiding the cliffs near the shore\n\
+ and +.*\n", {0} },
	/* 163 */
	{ "You are on a dirt road.",
		{ 216, 116, 113, 141, 79, 0, 0, 0 },
"The roadside is choked with broad leaved plants fighting for every breath of\n\
sunshine. The palm trees are taller than at the shore yet bend over the road \n\
forming a canopy. The road continues *- and *-.\n", {0} },
	/* 164 */
	{ "You have discovered a hidden thicket near the road.",
		{ 163, 142, 116, 106, 73, 0, 0, 0 },
"Stuffed into a little bundle here is a bloody silken robe and many beer cans.\n\
*Some droplets of blood and a major spill sparkle farther +.\n\
The road is +.*\n", {0} },
	/* 165 */
	{ "You are in the living room.",
		{ 0, 117, 217, 218, 0, 0, 0, 0 },
"A decorative entry with fresh flowers and wall to wall carpeting leads into\n\
the living room here where a couch and two chairs converge with an end table.\n\
*The exit is +.* The bedroom is +.\n", {0} },
	/* 166 */
	{ "You are at the lagoon.",
		{ 118, 0, 167, 168, 81, 0, 0, 0 },
"There are several outrigger canoes pulled up on a small beach here and a\n\
catch of colorful fish is drying in the sun. There are paths leading \n\
off -*, -, and -.\n", {0} },
	/* 167 */
	{ "You are at the lagoon.",
		{ 118, 0, 170, 166, 81, 0, 0, 0 },
"This is a grassy little spot near the water. A sightly native girl is frolicking\n\
in the water close to shore here.** The path continues - and -. \n", {0} },
	/* 168 */
	{ "You are at the lagoon.",
		{ 118, 0, 166, 172, 81, 0, 0, 0 },
"The path meanders through tussocks of grass, ferns, and thorny bushes here\n\
and continues on **- and -.\n", {0} },
	/* 169 */
	{ "You are in the woods.",
		{ 219, 119, 220, 92, 81, 0, 0, 0 },
"There are plenty of ferns and thorny bushes here! ****\n", {0} },
	/* 170 */
	{ "You are on a dirt road.",
		{ 220, 167, 199, 119, 74, 0, 0, 0 },
"The road winds rather close to a large lagoon here and many sedges and tall\n\
grasses line the shoulder *+. The road continues - and -.\n", {0} },
	/* 171 */
	{ "You are in the woods beside the road.",
		{ 221, 120, 92, 222, 81, 0, 0, 0 },
"The forest grows darker +. The road is +.**\n", {0} },
	/* 172 */
	{ "The road crosses the lagoon here.",
		{ 222, 0, 120, 174, 81, 0, 0, 0 },
"Coursing through the trees, the road at this point bridges a watery finger\n\
of the lagoon.* The water is +. The road continues - and -.\n", {0} },
	/* 173 */
	{ "You are in a coconut palm grove.",
		{ 223, 121, 224, 225, 82, 0, 0, 0 },
"The tall palms are planted about 30 feet apart with a hardy deep green grass\n\
filling the spaces in between.  There are tire tracks through the grass. The\n\
grove continues -. There is a road +.**\n", {0} },
	/* 174 */
	{ "You are walking along a dirt road.",
		{ 224, 176, 172, 121, 82, 0, 0, 0 },
"You are nearing the lagoon.** The road continues - and -.\n", {0} },
	/* 175 */
	{ "You are on a dirt road.",
		{ 225, 177, 121, 226, 82, 0, 0, 0 },
"The road turns abruptly - here, entering a grove of palm trees.* The road\n\
also continues - toward the lagoon.*\n", {0} },
	/* 176 */
	{ "You are on a trail running around the lagoon.",
		{ 172, 0, 0, 122, 82, 0, 0, 0 },
"The dark waters brush the trail here and the path crosses a bridge +.\n\
There is deep water + and +. The trail continues -.\n", {0} },
	/* 177 */
	{ "This is the mouth of the lagoon.",
		{ 175, 0, 122, 227, 82, 0, 0, 0 },
"The coral reef wraps around a natural bay here to create a wide lagoon which\n\
winds tortuously inland.** A trail goes around the lagoon +. The beach\n\
is +.\n", {0} },
	/* 178 */
	{ "You are in a dry stream bed.",
		{ 0, 125, 0, 0, 84, 0, 0, 0 },
"The dry wash drains over a tall precipice here into a turbid morass below. The\n\
most noisome stench imaginable is wafting up to defile our nostrils. Above,\n\
the lurid sun glows brown through a strange mist.* The only direction \n\
I'm going is -.**\n", {0} },
	/* 179 */
	{ "You are on a dirt path along the wash.",
		{ 0, 128, 125, 228, 84, 0, 0, 0 },
"This path looks more like a deer trail. It scampers away ***+.\n", {0} },
	/* 180 */
	{ "The thermal pools flow into a stream here.",
		{ 127, 0, 229, 126, 84, 0, 0, 0 },
"The gurgling hot waters pour over boulders into a swiftly flowing\n\
stream **+. The pools are +.\n", {0} },
	/* 181 */
	{ "You are at the entrance to a cave.",
		{ 128, 230, 126, 0, 84, 0, 0, 0 },
"A tall narrow fissure in the rock cliffs here has become a well traveled\n\
passage way. A hoof beaten dirt path leads directly into it. A curl of\n\
steam is trailing from a corner of the fissure's gaping mouth. The path\n\
leads - and -. The pools are +.*\n", {0} },
	/* 182 */
	{ "You are in the woods.",
		{ 182, 229, 182, 127, 84, 0, 0, 0 },
"Wild berry bushes plump with fruit and thorns tangle your every effort to\n\
proceed.* The sound of rushing water is +.**\n", {0} },
	/* 183 */
	{ "You are walking along the beach.",
		{ 139, 129, 184, 0, 99, 0, 0, 0 },
"Some dunes here progress inland and make it impossible to get very far in that\n\
direction. The beach continues - and -.* The ocean is +.\n", {0} },
	/* 184 */
	{ "You are in the dunes.",
		{ 183, 101, 184, 129, 87, 0, 0, 0 },
"The endless rolling and pitching sand dunes are enough to make one very queasy!\n\
The only way I'm going is ***+.\n", {0} },
	/* 185 */
	{ "This is a lousy beach.",
		{ 130, 0, 0, 0, 87, 0, 0, 0 },
"Volcanic and viciously sharp bitted grains of sand here bite like cold steel\n\
into my tender feet. I refuse to continue on. Let's get out of here. The\n\
beach is better +.***\n", {0} },
	/* 186 */
	{ "You are in a field of sparse ferns.",
		{ 131, 185, 187, 130, 87, 0, 0, 0 },
"The lava rock outcroppings here will support few plants. There is more \n\
vegetation +. There is a nice beach +.* The ocean is +.\n", {0} },
	/* 187 */
	{ "You are in the woods.",
		{ 131, 131, 137, 131, 87, 0, 0, 0 },
"Young trees and tall shrubs grow densely together at this distance from the \n\
shore.** The trees grow thicker +.*\n", {0} },
	/* 188 */
	{ "The canyon is no wider than a foot here.",
		{ 0, 0, 0, 132, 0, 0, 0, 0 },
"The freshet is gushing through the narrow trough, but the canyon has grown\n\
too narrow to follow it any farther.*** I guess we'll have to go -.\n", {0} },
	/* 189 */
	{ "You are in a narrow part of the canyon.",
		{ 0, 0, 133, 232, 76, 0, 0, 0 },
"The two sheer sides are no more than a few meters apart here. There is a stone\n\
door in the wall +. The gravelly floor runs with tiny rivulets seeping \n\
from the ground itself.* The canyon continues - and -.\n", {0} },
	/* 190 */
	{ "You are in the drawing room.",
		{ 0, 134, 0, 0, 0, 0, 0, 0 },
"Exquisitely decorated with plants and antique furniture of superb\n\
craftsmanship, the parlor reflects its owner's impeccable taste.  The tropical\n\
sun is streaming in through open shutters *+.  There doesn't seem \n\
to be anybody around.  A large immaculate oaken desk is visible in the\n\
study and it even has a old fashioned telephone to complete the decor.**\n", {0} },
	/* 191 */
	{ "You are in a palm grove.",
		{ 135, 191, 233, 191, 88, 0, 0, 0 },
"Grassy rows of palms stretch as far as I can see.** There is a road +.*\n", {0} },
	/* 192 */
	{ "You are on a dirt road.",
		{ 136, 233, 234, 135, 88, 0, 0, 0 },
"The road winds through a coconut palm grove here. It continues on - \n\
and -.**\n", {0} },
	/* 193 */
	{ "The road leads to several large buildings here.",
		{ 235, 136, 236, 237, 88, 0, 0, 0 },
"There is a clubhouse +,* a large barn and stable +, and a garage of \n\
similar construct to the barn +.\n", {0} },
	/* 194 */
	{ "This part of the beach is impassable.",
		{ 0, 138, 0, 0, 96, 0, 0, 0 },
"The huge rocks and thunderous surf here would pound our frail bodies to pulp\n\
in an instant.* The only direction I'm going is -.**\n", {0} },
	/* 195 */
	{ "You are in the gardens.",
		{ 195, 140, 197, 138, 96, 0, 0, 0 },
"So much green grass is a pleasure to the eyes.****\n", {0} },
	/* 196 */
	{ "You are in the gardens.",
		{ 140, 183, 197, 139, 99, 0, 0, 0 },
"Beautiful flowers and shrubs surround a little goldfish pond.****\n", {0} },
	/* 197 */
	{ "You are on a stone walk in the garden.",
		{ 195, 196, 238, 140, 99, 0, 0, 0 },
"The walk leads to a road **+.*\n", {0} },
	/* 198 */
	{ "You are in the forest near the road.",
		{ 198, 141, 216, 198, 73, 0, 0, 0 },
"There are many thorny bushes here!****\n", {0} },
	/* 199 */
	{ "You are at a fork in the road.",
		{ 239, 146, 141, 170, 73, 0, 0, 0 },
"Two roads come together in the forest here. One runs -,* the other \n\
runs - and -.\n", {0} },
	/* 200 */
	{ "You are on a dirt path around the lagoon.",
		{ 170, 147, 146, 0, 74, 0, 0, 0 },
"The still waters reflect bending palms and a cloudless sky. It looks like\n\
the path runs into a clearing +. The path continues -.**\n", {0} },
	/* 201 */
	{ "You are drowning in the lagoon.",
		{ 201, 201, 147, 201, 74, 0, 0, 0 },
"I suggest you get out before you become waterlogged.****\n", {0} },
	/* 202 */
	{ "You are in a coconut palm grove.",
		{ 202, 148, 203, 204, 77, 0, 0, 0 },
"****\n", {0} },
	/* 203 */
	{ "You are in a palm grove.",
		{ 202, 149, 205, 148, 77, 0, 0, 0 },
"****\n", {0} },
	/* 204 */
	{ "You are in a palm grove.",
		{ 202, 150, 148, 206, 77, 0, 0, 0 },
"****\n", {0} },
	/* 205 */
	{ "You are on a dirt road.",
		{ 203, 155, 212, 149, 77, 0, 0, 0 },
"*This road ends here at a palm grove but continues on - for quite\n\
some way.**\n", {0} },
	/* 206 */
	{ "You are in a coconut palm grove.",
		{ 204, 156, 150, 213, 77, 0, 0, 0 },
"****\n", {0} },
	/* 207 */
	{ "You are in a coconut grove.",
		{ 151, 219, 208, 209, 77, 0, 0, 0 },
"*The grove ends +.**\n", {0} },
	/* 208 */
	{ "You are in a coconut grove.",
		{ 152, 207, 239, 151, 77, 0, 0, 0 },
"**There is a dirt road +.*\n", {0} },
	/* 209 */
	{ "You are in a coconut grove.",
		{ 153, 207, 151, 211, 77, 0, 0, 0 },
"****\n", {0} },
	/* 210 */
	{ "This is a dirt road.",
		{ 205, 239, 212, 154, 77, 0, 0, 0 },
"The road continues - and -.**\n", {0} },
	/* 211 */
	{ "You are in a coconut grove.",
		{ 153, 209, 153, 213, 77, 0, 0, 0 },
"****\n", {0} },
	/* 212 */
	{ "You are in the woods near the road.",
		{ 205, 210, 212, 155, 77, 0, 0, 0 },
"There are many thorny bushes here!****\n", {0} },
	/* 213 */
	{ "You are in a coconut grove.",
		{ 213, 213, 156, 234, 88, 0, 0, 0 },
"***The grove ends in a clearing +.\n", {0} },
	/* 214 */
	{ "You are walking along some high cliffs.",
		{ 162, 0, 0, 159, 86, 0, 0, 0 },
"The island bends sharply + here with high cliffs -\n\
and -. The cliffs are lower +.\n", {0} },
	/* 215 */
	{ "You are at the coast road turn around.",
		{ 0, 162, 0, 158, 90, 0, 0, 0 },
"The coast road ends here in a lookout with a view of 100 kilometers of blue\n\
sea and 100 meters of rugged cliff. Far below the waves crash against rocks.\n\
****\n", {0} },
	/* 216 */
	{ "You are in the woods near the road.",
		{ 216, 163, 216, 198, 79, 0, 257, 0 },
"These thorny bushes are killing me.****\n", {0} },
	/* 217 */
	{ "You are in the kitchen.",
		{ 0, 0, 0, 165, 0, 0, 0, 0 },
"A small gas stove and a refrigerator are all the only appliances here. The\n\
gas oven has been left on and the whole room is reeking with natural gas.\n\
One spark from a match and.... The door out is ***+.\n", {0} },
	/* 218 */
	{ "You are in the bedroom.",
		{ 0, 0, 165, 0, 0, 0, 0, 0 },
"A soft feather comforter on top of layers of Answer blankets make this a very\n\
luxurious place to sleep indeed. There are also some end tables and a dresser\n\
here.** The living room is +.*\n", {0} },
	/* 219 */
	{ "You are in the woods.",
		{ 207, 169, 220, 221, 81, 0, 0, 0 },
"There seems to be a clearing +.***\n", {0} },
	/* 220 */
	{ "You are in the woods near the road.",
		{ 219, 170, 239, 169, 81, 0, 0, 0 },
"*As far as I can tell, there are two roads + and +.*\n", {0} },
	/* 221 */
	{ "You are in the woods.",
		{ 207, 171, 219, 222, 81, 0, 0, 0 },
"The forest is clearer +.***\n", {0} },
	/* 222 */
	{ "You are on the lagoon's inland finger.",
		{ 0, 172, 171, 172, 81, 0, 0, 0 },
"It is impossible to follow the lagoon any farther inland because of sharp\n\
and very painful sedges.* The road is +.**\n", {0} },
	/* 223 */
	{ "You are in a grassy coconut grove.",
		{ 240, 173, 224, 241, 82, 0, 0, 0 },
"The tall palms provide a perfect canopy for the lush green grass.***\n\
There is a road +.\n", {0} },
	/* 224 */
	{ "You are near the lagoon's inland finger.",
		{ 0, 174, 0, 173, 82, 0, 0, 0 },
"Very sharp sedges make it impossible to follow the lagoon any farther inland.\n\
*There is a road +.**\n", {0} },
	/* 225 */
	{ "You are on a dirt road.",
		{ 241, 175, 173, 226, 82, 0, 0, 0 },
"The road winds through a coconut grove here and continues - and -.**\n", {0} },
	/* 226 */
	{ "You are in the woods near the road.",
		{ 226, 226, 175, 226, 82, 0, 0, 0 },
"**The road is +.*\n", {0} },
	/* 227 */
	{ "This is a beach?",
		{ 227, 227, 177, 0, 82, 0, 0, 0 },
"Hard jagged rocks that pierce with every footstep hardly comprise a beach.**\n\
Let's go -.*\n", {0} },
	/* 228 */
	{ "The trail is lost in the woods here.",
		{ 241, 241, 179, 241, 84, 0, 0, 0 },
"I suppose the animals that use this trail all depart in different directions\n\
when they get this far into the woods.** The trail goes -.*\n", {0} },
	/* 229 */
	{ "You are on the bank of a stream.",
		{ 182, 0, 242, 180, 84, 0, 0, 0 },
"The stream falls over several small boulders here and continues on **-.*\n", {0} },
	/* 230 */
	{ "You are just inside the cave.",
		{ 181, 267, 0, 0, 0, 0, 0, 0 },
"A steamy hot breath is belching from the depths of the earth within.* The\n\
cave continues -.**\n", {0} },
	/* 231 */
	{ "You are just inside the cave entrance.",
		{ 274, 0, 0, 0, 0, 0, 0, 0 },
"The air is hot and sticky inside. The cave continues -. There is a \n\
stone door in the wall +.  A wooden sign in the dust reads in old elven\n\
runes, \"GSRF KDIRE NLVEMP!\".**\n", {0} },
	/* 232 */
	{ "You are at the edge of a huge chasm.",
		{ 0, 0, 189, 0, 76, 0, 0, 0 },
"Several hundred feet down I can see the glimmer of placid water. The\n\
rivulets drain over the edge and trickle down into the depths. It is \n\
impossible to climb down without a rope.** The canyon continues -.*\n", {0} },
	/* 233 */
	{ "You are on a dirt road.",
		{ 192, 241, 240, 191, 88, 0, 0, 0 },
"The road winds through a coconut grove here. The road continues on -\n\
and -.**\n", {0} },
	/* 234 */
	{ "You are in a coconut palm grove near the road.",
		{ 193, 233, 213, 192, 88, 0, 0, 0 },
"***The road is +.\n", {0} },
	/* 235 */
	{ "You are at the clubhouse.",
		{ 0, 193, 0, 0, 0, 0, 0, 0 },
"The clubhouse is built over the most inland part of the lagoon.  Tropical\n\
bananas and fragrant frangipani grow along the grassy shore.  Walking across\n\
the short wooden bridge, we enter.  Along one wall is a bar with only a few\n\
people seated at it.  The restaurant and dance floor are closed off with\n\
a 2 inch nylon rope. ****\n", {0} },
	/* 236 */
	{ "You are in the stables.",
		{ 0, 0, 0, 193, 0, 0, 0, 0 },
"Neighing horses snacking on hay and oats fill the stalls on both sides of\n\
the barn.  It is rather warm in here but that is not the most offensive\n\
part.  The old boards of the barn part just enough to let in dust laden\n\
shafts of light.  Flies swarm overhead and strafe the ground for dung.\n\
My nose is beginning to itch. ****\n", {0} },
	/* 237 */
	{ "You are in the old garage.",
		{ 0, 0, 193, 0, 0, 0, 0, 0 },
"This is an old wooden building of the same vintage as the stables.  Beneath\n\
a sagging roof stand gardening tools and greasy rags.  Parked in the center\n\
is an underpowered Plymouth Volare' with a red and white striped golf cart\n\
roof. ****\n", {0} },
	/* 238 */
	{ "You are on a dirt road.",
		{ 197, 197, 243, 197, 85, 0, 0, 0 },
"The road leads to a beautiful formal garden laced with stone walks and tropical\n\
flowers and trees.** The road continues -. A walk leads -.\n", {0} },
	/* 239 */
	{ "You are on a dirt road.",
		{ 210, 199, 198, 220, 73, 0, 0, 0 },
"The road runs - and -.**\n", {0} },
	/* 240 */
	{ "You are in a coconut grove near the road.",
		{ 234, 223, 234, 233, 88, 0, 0, 0 },
"***The road is +.\n", {0} },
	/* 241 */
	{ "You are on a dirt road.",
		{ 233, 225, 223, 226, 82, 0, 0, 0 },
"The road continues - and -.**\n", {0} },
	/* 242 */
	{ "The stream plummets over a cliff here.",
		{ 182, 0, 0, 229, 84, 0, 0, 0 },
"Falling 10 agonizing meters into spray, only droplets of the stream are\n\
left to dance off the floor below.  I thought I saw a sparkle of gold\n\
at the bottom of the falls, but now it is gone.  There is no way down,\n\
even with a strong rope. ****\n", {0} },
	/* 243 */
	{ "You are on a dirt road.",
		{ 0, 0, 244, 238, 85, 0, 0, 0 },
"**The road continues - and -.\n", {0} },
	/* 244 */
	{ "You are on a dirt road.",
		{ 0, 245, 0, 243, 88, 0, 0, 0 },
"*The road continues -* and -.\n", {0} },
	/* 245 */
	{ "You are on a dirt road.",
		{ 244, 234, 213, 136, 88, 0, 0, 0 },
"The road goes -* and *-.\n", {0} },
	/* 246 */
	{ "You are in a low passage.",
		{ 247, 160, 0, 0, 0, 0, 0, 0 },
"The passage is partially flooded here and it may be hazardous to proceed.\n\
Water is surging from the tunnel and heading out to sea. Strange moaning\n\
noises rise above the rushing of the water.  They are as thin as a whispering\n\
wind yet penetrate to my very soul.  I think we have come too far...\n\
The passage continues -.***\n", {0} },
	/* 247 */
	{ "The walls are very close together here.",
		{ 248, 0, 0, 0, 0, 0, 0, 0 },
"I can barely squeeze through the jagged opening. Slimy sea weeds provide\n\
no footing at all. This tunnel seems to be an ancient lava tube. There is\n\
a large room -.***\n", {0} },
	/* 248 */
	{ "You are in the cathedral room.",
		{ 249, 251, 249, 251, 0, 0, 0, 0 },
"Your light casts ghostly shadows on the walls but cannot pierce the \n\
engulfing darkness overhead. The sound of water dripping echoes in the void.\n\
*I can see no passages leading out of this room.  We have definitely\n\
come too far.*** \n", {0} },
	/* 249 */
	{ "You are walking through a very round tunnel.",
		{ 252, 0, 0, 0, 252, 1, 0, 0 },
"The round walls of this tunnel are amazingly smooth to the touch. A little\n\
trickle of water flows down the center. The tunnel climbs steadily +.\n\
The cave is beginning to flood again!  Let's get out of here! ***\n", {0} },
	/* 250 */
	{ "You are in the cathedral anteroom.",
		{ 0, 0, 0, 248, 253, 1, 0, 0 },
"This small chamber with a flat stone floor is to one side of the cathedral \n\
room. We appear to be at the bottom of a tall narrow shaft. There are many \n\
puddles of water here. A staircase hewn from solid rock and black lava \n\
leads up.*** The cathedral room is +.\n", {0} },
	/* 251 */
	{ "You are in a wide chamber.",
		{ 0, 0, 248, 254, 0, 0, 0, 0 },
"Water is sprinkling from the ceiling here. A shallow pool populated by a \n\
myriad of blind white creatures sparkles in your light. Tiny shrimp and\n\
crabs scurry away, frightened by the blinding rays.** The cave \n\
continues - and -.\n", {0} },
	/* 252 */
	{ "You are at the top of a sloping passage.",
		{ 0, 0, 255, 256, 257, 1, 0, 0 },
"There is much algae growing here, both green and brown specimens. \n\
Water from an underground sea surges and splashes against the slope of\n\
the rock. The walls glisten with shiny minerals.  High above, light\n\
filters in through a narrow shaft.**  A hallway here runs -\n\
and -.\n", {0} },
	/* 253 */
	{ "You are in an elaborately tiled room.",
		{ 0, 0, 258, 0, 0, 0, 250, 0 },
"Large colorful tiles plate the floor and walls.  The ceiling is a mosaic\n\
of gems set in gold.  Hopefully it is only our footsteps that are echoing in\n\
this hollow chamber.** The room continues -.  A stone staircase\n\
leads down.*\n", {0} },
	/* 254 */
	{ "You are at a dead end.",
		{ 0, 0, 251, 0, 0, 0, 0, 0 },
"The walls here are alive with dark mussels.  They click their shells menacingly\n\
if we disturb them. ** The only exit is +.*\n", {0} },
	/* 255 */
	{ "The tunnel is very low here.",
		{ 0, 0, 259, 252, 0, 0, 0, 0 },
"I practically have to crawl on my knees to pass through this opening. The\n\
air is stiflingly damp, but I can't hear any sounds of water dripping.**\n\
The crawlspace continues -. The tunnel seems wider +.\n", {0} },
	/* 256 */
	{ "This is the supply room.",
		{ 0, 0, 252, 0, 0, 0, 0, 0 },
"Picks and shovels line the walls here, as well as hard hats, boxes of\n\
dynamite, and a cartload of very high grade gold and silver ore.** \n\
A tunnel leads off +.*\n", {0} },
	/* 257 */
	{ "You have found a secret entrance to the catacombs.",
		{ 0, 0, 0, 0, 216, 1, 252, 0 },
"I have a sickening feeling that we should not have entered the catacombs.\n\
Below is a wet, seaweed covered floor. Above is a way out. ****\n", {0} },
	/* 258 */
	{ "You are in the catacombs.",
		{ 0, 0, 260, 253, 0, 0, 0, 0 },
"Ornate tombs and piles of treasure line the walls.  Long spears with many\n\
blades, fine swords and coats of mail, heaps of coins, jewelry, pottery, \n\
and golden statues are tribute of past kings and queens.** The catacombs\n\
continue - and -.\n", {0} },
	/* 259 */
	{ "You are crawling on your stomach.",
		{ 0, 0, 261, 255, 0, 0, 0, 0 },
"The passage is quite narrow and jagged, but the rock is no longer lava.\n\
It appears to be a form of granite.** The crawlspace continues -, \n\
but I would just as soon go -.\n", {0} },
	/* 260 */
	{ "You are in the Sepulcher.",
		{ 0, 0, 0, 258, 0, 0, 0, 0 },
"A single tomb is here.  Encrusted with diamonds and opals, and secured with \n\
straps of a very hard, untarnished silver, this tomb must be of a great king.\n\
Vases overflowing with gold coins stand nearby.  A line of verse on the wall\n\
reads, \"Three he made and gave them to his daughters.\"****\n", {0} },
	/* 261 */
	{ "The passage is wider here.",
		{ 0, 0, 0, 259, 0, 0, 0, 0 },
"You are at the top of a flooded shaft.  About a meter below the edge,\n\
dark water rises and falls to the rhythm of the sea.  A ladder goes\n\
down into water here.***  A small crawlspace goes -.\n", {0} },
	/* 262 */
	{ "You are at the bottom of a ladder.",
		{ 0, 0, 0, 0, 261, 1, 263, 0 },
"This is a narrow platform to rest on before we continue either up or down this\n\
rickety wooden ladder.****\n", {0} },
	/* 263 */
	{ "You are standing in several inches of water.",
		{ 264, 0, 265, 266, 262, 1, 0, 0 },
"This seems to be a working mine. Many different tunnels wander off following\n\
glowing veins of precious metal.  The floor is flooded here since we must\n\
be nearly at sea level.  A ladder leads up. ****\n", {0} },
	/* 264 */
	{ "The tunnel here is blocked by broken rocks.",
		{ 0, 263, 0, 0, 0, 0, 0, 0 },
"The way is blocked, but if you had some dynamite, we might be able to blast our\n\
way through.*  The passage goes -.**\n", {0} },
	/* 265 */
	{ "The tunnel is too flooded to proceed.",
		{ 0, 0, 0, 263, 0, 0, 0, 0 },
"Hidden shafts could swallow us if we tried to continue on down this tunnel.\n\
The flooding is already up to my waist.  Large crystals overhead shimmer\n\
rainbows of reflected light.***  Let's go -.\n", {0} },
	/* 266 */
	{ "The mine is less flooded here.",
		{ 0, 0, 263, 0, 0, 0, 0, 0 },
"A meandering gold laden vein of quartz and blooming crystals of diamonds\n\
and topaz burst from the walls of the cave.  A passage goes -.***\n", {0} },
	/* 267 */
	{ "You are inside the cave.",
		{ 230, 268, 0, 0, 0, 0, 0, 0 },
"A hot steam swirls around our heads, and the walls are warm to the touch.\n\
The trail winds + and +.**\n", {0} },
	/* 268 */
	{ "You are in a rather large chamber.",
		{ 267, 0, 0, 269, 0, 0, 269, 0 },
"Beds of ferns and palm leaves make several cozy nests along the walls. In the\n\
center of the room is a throne of gold and silver which pulls out into a bed\n\
of enormous size.***  A passageway - leads down.\n", {0} },
	/* 269 */
	{ "You are walking along the edge of a huge abyss.",
		{ 0, 0, 268, 0, 268, 1, 270, 0 },
"Steam is rising in great clouds from the immeasurable depths.  A very narrow\n\
trail winds down.**  There is a tunnel +.*\n", {0} },
	/* 270 */
	{ "You are on the edge of a huge abyss.",
		{ 0, 0, 0, 0, 269, 1, 271, 0 },
"The trail winds farther down.****\n", {0} },
	/* 271 */
	{ "You are winding your way along the abyss.",
		{ 0, 0, 0, 0, 270, 1, 272, 0 },
"The trail continues up and down.****\n", {0} },
	/* 272 */
	{ "You are on a wide shelf near the steamy abyss.",
		{ 0, 273, 0, 0, 271, 1, 0, 0 },
"The stifling hot cave seems even hotter to me, staring down into this misty \n\
abyss.  A trail winds up.*  A passageway leads -.**\n", {0} },
	/* 273 */
	{ "You are in a wide tunnel leading to a fuming abyss.",
		{ 272, 274, 0, 0, 0, 0, 0, 0 },
"The passageway winds through many beautiful formations of crystals and\n\
sparkling minerals.  The tunnel continues - and -.**\n", {0} },
	/* 274 */
	{ "You are in a tunnel.",
		{ 273, 231, 0, 0, 0, 0, 0, 0 },
"It is very warm in here.  The smell of steam and hot rocks permeates the place.\n\
The cave continues - and -.**\n", {0} },
	/* 275 */
	{ "You are at the bottom of a pit.",
		{ 0, 0, 0, 0, 232, 0, 0, 0 },
"I can see daylight far up at the mouth of the pit.   A cool draft wafts down.\n\
There doesn't seem to be any way out, and I don't remember how we came in.\n\
If you had a rope it might be possible to climb out. ****\n", {0} }
};
