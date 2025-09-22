/*	$OpenBSD: nightfile.c,v 1.11 2009/10/27 23:59:24 deraadt Exp $	*/
/*	$NetBSD: nightfile.c,v 1.3 1995/03/21 15:07:41 cgd Exp $	*/

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

struct room nightfile[] = {
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
strafe this vulnerable port. The main hangar is +, *\n\
There is an exit +.*\n", {0} },
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
the sounds of dull explosions rumble though the room.  From a window in\n\
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
		{ 43, 48, 51, 54, 32, 1, 56, 1 },
"****\n", {0} },
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
"Feather palms outlined by mellow moonlight and a silvery black ocean line\n\
the perimeter of the island.  Mighty mountains of emerald and amethyst rise\n\
like jagged teeth from black gums.  The land rises sharply +.  The shore\n\
line stretches on *+.*\n", {0} },
	/* 71 */
	{ "You are flying over a mountainous region.",
		{ 75, 73, 76, 77, 68, 1, 0, 1 },
"Below is a shadow=filled canyon with looming spires and pinnacles and\n\
luminous waterfalls plummeting down beyond sight.  **The ocean is +.*\n", {0} },
	/* 72 */
	{ "You are flying over the ocean.",
		{ 74, 78, 78, 78, 68, 1, 0, 1 },
"You bank over the water and your wingtips dip low to the green waves.  The\n\
sea is very shallow here and the white coral beds beneath us teem with \n\
shadowy fish.****\n", {0} },
	/* 73 */
	{ "You are flying over the beach.",
		{ 71, 72, 79, 74, 68, 1, 80, 1 },
"A warm gentle surf caresses the beach here. The land rises\n\
sharply +.* The beach is lost in low cliffs +.*\n", {0} },
	/* 74 */
	{ "You are flying over a large lagoon.",
		{ 81, 72, 73, 82, 68, 1, 0, 1 },
"The water's brink winds tortuously inland.  There are some lights +.***\n", {0} },
	/* 75 */
	{ "You are flying over a gently sloping plane.",
		{ 83, 71, 84, 85, 68, 1, 0, 1 },
"The ground appears to be choked with vegetation.*  The terrain is more\n\
rugged +.**\n", {0} },
	/* 76 */
	{ "You are flying through a gorge.",
		{ 0, 0, 86, 71, 68, 1, 102, 1 },
"This is a narrow, steep sided canyon lined with plants.  The stars above\n\
glisten through the over hanging trees.  The gorge leads to the\n\
sea** +, and to a tumultuous origin +.\n", {0} },
	/* 77 */
	{ "You are flying over a plantation.",
		{ 85, 81, 71, 88, 68, 1, 89, 1 },
"It might be possible to land here, but not in the dark.*  There are some lights\n\
+ and *+.\n", {0} },
	/* 78 */
	{ "You are over the ocean.",
		{ 72, 78, 79, 74, 68, 1, 0, 1 },
"The deep green swells foam and roll into the shore **+*.\n", {0} },
	/* 79 */
	{ "You are flying along the coast.",
		{ 86, 72, 90, 73, 68, 1, 91, 1 },
"The coastline here is very rocky. The surf in some places is violent\n\
and explodes in a shower of sparkling, moonlit spray. ****\n", {0} },
	/* 80 */
	{ "This is a beautiful coral beach.",
		{ 106, 0, 107, 108, 73, 0, 0, 0 },
"Fine silver sand kissed lightly by warm tropical waters and illuminated\n\
by a huge tropical moon stretches at least 30 meters inland.\n\
Gently swaying palm trees are +.***\n", {0} },
	/* 81 */
	{ "You are flying over a small fishing village.",
		{ 77, 74, 71, 82, 68, 1, 92, 1 },
"A few thatched huts lit with torches and bonfires line a road below.\n\
The road continues on ***+.\n", {0} },
	/* 82 */
	{ "You are flying over a clearing.",
		{ 88, 72, 74, 87, 68, 1, 93, 1 },
"There is a dock here (big enough for a seaplane) leading to a clearing.\n\
The still waters of the lagoon reflects our orange turbo thrusters.****\n", {0} },
	/* 83 */
	{ "You are flying over the shore.",
		{ 94, 75, 95, 96, 68, 1, 0, 1 },
"Rocky lava flows have overtaken the beach here.****\n", {0} },
	/* 84 */
	{ "You are flying in a wide valley.",
		{ 95, 97, 86, 75, 68, 1, 98, 1 },
"This is a shallow valley yet the floor is obscured by a thick mist.\n\
The valley opens into blackness +. The mist grows thicker +.**\n", {0} },
	/* 85 */
	{ "You are flying near the shore.",
		{ 96, 77, 75, 99, 68, 1, 0, 1 },
"Very tall trees growing in neatly planted rows march off from the \n\
water here towards the hills and down to the flat lands *+.**\n", {0} },
	/* 86 */
	{ "You are flying around the very tip of the island.",
		{ 95, 79, 90, 84, 68, 1, 0, 1 },
"Sheer cliffs rise several hundred feet to a tree covered summit. Far below,\n\
the grey sea gnaws voraciously at the roots of these cliffs. The island bends\n\
around +** and +.\n", {0} },
	/* 87 */
	{ "You are flying along the coastline.",
		{ 99, 82, 88, 100, 68, 1, 101, 1 },
"This is a narrow strip of sand lined with very few trees. The stars above\n\
flicker through wisps of clouds.  The beach continues on -.* There\n\
are some lights +.*\n", {0} },
	/* 88 */
	{ "You are flying over several cottages and buildings",
		{ 99, 82, 77, 87, 68, 1, 103, 1 },
"Directly below is small ornate house lit up with spot lights and decorative\n\
bulbs.  A bell tower and a spurting fountain are nearby.  Small dirt roads go\n\
+ and +.**\n", {0} },
	/* 89 */
	{ "You are in a field of sugar cane.",
		{ 109, 110, 111, 112, 77, 0, 0, 0 },
"These strong, thick canes give little shelter but many cuts and scrapes.\n\
There are some large trees ***+.\n", {0} },
	/* 90 */
	{ "You are flying over the ocean.",
		{ 95, 78, 90, 86, 68, 1, 0, 1 },
"The water is a placid ebony.****\n", {0} },
	/* 91 */
	{ "You are on the coast road.",
		{ 113, 114, 115, 116, 79, 0, 0, 0 },
"The road winds close to the shore here.  The trees on either side press close\n\
in the darkness and seem to be watching us.**  The road continues -\n\
and -.\n", {0} },
	/* 92 */
	{ "You are on the main street of the village.",
		{ 117, 118, 119, 120, 81, 0, 0, 0 },
"The natives are having a festive luau here.  Beautiful dancers gyrate in the\n\
torchlight to the rhythm of wooden drums.  A suckling pig is sizzling in a\n\
bed of coals and ti leaves are spread with poi and tropical fruits.  Several\n\
natives have come over to you and want you to join in the festivities.\n\
There is a light burning in a bungalow +.***\n", {0} },
	/* 93 */
	{ "You are at the sea=plane dock.",
		{ 121, 122, 123, 124, 82, 0, 0, 0 },
"The clearing is deserted.  The grass is wet with the evening dew +.*\n\
There is something set up +.*\n", {0} },
	/* 94 */
	{ "You are flying over the ocean.",
		{ 94, 83, 95, 96, 68, 1, 0, 1 },
"The black waves surge off shore here.  The ocean becomes much calmer +.***\n", {0} },
	/* 95 */
	{ "You are flying along the coast.",
		{ 94, 84, 86, 83, 68, 1, 0, 1 },
"The land is very low here with a river running into the sea +. There\n\
is a wide valley opening up +, but a strange mist is flowing out of it.\n\
The very tip of the island is +.*\n", {0} },
	/* 96 */
	{ "You are flying along the coast.",
		{ 94, 85, 83, 99, 68, 1, 0, 1 },
"The coast here is cluttered with rocky outcroppings.****\n", {0} },
	/* 97 */
	{ "You are lost in a sea of fog.",
		{ 97, 104, 97, 97, 97, 1, 0, 1 },
"What have you gotten us into?\n\
I can't see a thing! ****\n", {0} },
	/* 98 */
	{ "You are on a gravel wash.",
		{ 125, 126, 127, 128, 84, 0, 0, 0 },
"It is very dark here.  A cool breeze is blowing from +.  No moonlight can\n\
reach below a thick canopy of fog above.  The sound of cascading water is\n\
coming from +.**\n", {0} },
	/* 99 */
	{ "You are flying over a wide beach.",
		{ 96, 88, 85, 87, 68, 1, 105, 1 },
"There are some lighted buildings *+.  Some trees are growing +.*\n", {0} },
	/* 100 */
	{ "You are flying over the ocean.",
		{ 100, 100, 87, 100, 68, 1, 0, 1 },
"The black waves surge and splash against the rocky shore.****\n", {0} },
	/* 101 */
	{ "You are on a narrow strip of sand.",
		{ 129, 130, 131, 0, 87, 0, 0, 0 },
"Rather coarse sand makes this beach very steep and only a few meters wide.\n\
A fresh ocean breeze is rustling the ferns **+.*\n", {0} },
	/* 102 */
	{ "This is Fern Canyon.",
		{ 0, 0, 132, 133, 76, 0, 0, 0 },
"Delicate waving ferns flourish here, suckled by warm water dripping from \n\
every fissure and crevice in the solid rock walls.  The stars above sparkle\n\
through a thin mist.  The canyon winds **-, and -.\n", {0} },
	/* 103 */
	{ "This is the front lawn.",
		{ 134, 135, 136, 137, 88, 0, 0, 0 },
"There is a small fountain lighted with green and yellow bulbs here where\n\
the driveway meets the lawn.  Across the driveway, +, is an ornate white\n\
house lit with gas lamps.  A bell tower here is awash in pale blue.*  There\n\
is a road + which turns into the driveway.*\n", {0} },
	/* 104 */
	{ "You have just crossed the crest of a mountain.",
		{ 97, 79, 86, 71, 68, 1, 0, 1 },
"The fog vanished mysteriously as we flew over the crest.*\n\
Far + I can see the ocean sparkling in the moonlight.**\n", {0} },
	/* 105 */
	{ "You are on a sandy beach.",
		{ 138, 139, 140, 0, 99, 0, 0, 0 },
"Fine coral sand, a fresh sea breeze, and dramatic surf add to this beach's\n\
appeal.**  Stone steps lead to a lighted path in the gardens +.*\n", {0} },
	/* 106 */
	{ "You are among palm trees near the shore.",
		{ 141, 80, 142, 143, 73, 0, 0, 0 },
"Arching coconut palms laden with fruit provide a canopy for the glistening\n\
white sand and sparse, dew covered grasses growing here. The forest grows\n\
denser +.  Crickets are chirping loudly here.  The ocean is +.**\n", {0} },
	/* 107 */
	{ "You are walking along the beach.",
		{ 144, 0, 145, 80, 73, 0, 0, 0 },
"The warm tropical waters nuzzle your ankles as you walk. Above is a gorgeous\n\
starscape.  The battlestar must be up there somewhere.  The slope of the sand\n\
is so gentle that the surf only slides up the sand.** There are some rocks\n\
+.*\n", {0} },
	/* 108 */
	{ "You are walking along the beach.",
		{ 146, 0, 80, 147, 73, 0, 0, 0 },
"The tide is out very far tonight, and it is possible to explore hidden rocks\n\
and caves not ordinarily accessible.  Rich beds of seaweed have been exposed\n\
to the cool night air.****\n", {0} },
	/* 109 */
	{ "You are in a papaya grove.",
		{ 148, 89, 149, 150, 77, 0, 0, 0 },
"Slender trees with their large seven lobed leaves bulge with succulent fruit.\n\
There are some tall trees +.***\n", {0} },
	/* 110 */
	{ "You are in a field of pineapple.",
		{ 89, 151, 152, 153, 77, 0, 0, 0 },
"The sharp dagger like pineapple leaves can pierce the flesh and hold fast\n\
a skewered victim with tiny barbs.* The field ends +.**\n", {0} },
	/* 111 */
	{ "You are in a field of kiwi plants.",
		{ 149, 154, 155, 89, 77, 0, 0, 0 },
"Round hairy fruit hang from staked vines here. There are some trees +\n\
and +. The field ends in a road +.*\n", {0} },
	/* 112 */
	{ "You are in a large grove of coconuts.",
		{ 150, 153, 89, 156, 77, 0, 0, 0 },
"These trees are much taller than any growing near the shore and the shadows\n\
are also deeper.  It's hard to keep my sense of direction.****\n", {0} },
	/* 113 */
	{ "You are in the woods.",
		{ 157, 91, 158, 116, 79, 0, 0, 0 },
"Tropical undergrowth makes the going rough here. Sword ferns give no strong\n\
foot hold and the dangling vines would gladly throttle one. The darkness is\n\
so intense here that we stand in utter blackness.****\n", {0} },
	/* 114 */
	{ "You are at the shore.",
		{ 91, 0, 159, 145, 79, 0, 160, 0 },
"The low minus tide tonight might make it possible to climb down to a\n\
small cave entrance below.  Large rocks would usually churn the waves\n\
asunder.***  The beach goes -.\n", {0} },
	/* 115 */
	{ "You are on the coast road.",
		{ 158, 161, 162, 91, 79, 0, 0, 0 },
"The road is beginning to turn slightly -.  I can hear the surf +.  The road\n\
continues into the dark forest +.*\n", {0} },
	/* 116 */
	{ "The road winds deeper into the trees.",
		{ 163, 142, 91, 164, 79, 0, 0, 0 },
"Only narrow moonbeams filter through the dense foliage above. The moist rich\n\
earth has nurtured a myriad of slugs, snakes, and spiders to grow here. The\n\
road continues - and *- into the shadows.*\n", {0} },
	/* 117 */
	{ "This is the front porch of the bungalow.",
		{ 165, 92, 0, 0, 81, 0, 0, 0 },
"The veranda is lit by a small yellow bug light.  The door leads -.\n\
The stone walk down to the luau is lined with burning torches +.  That\n\
roast pig smells good.**\n", {0} },
	/* 118 */
	{ "You are on a path leading to the lagoon.",
		{ 92, 166, 167, 168, 81, 0, 0, 0 },
"This path winds through the underbrush and towards the lagoon *+.  The\n\
broad faced moon peeps though the branches above.  The sound of drums echos\n\
in the woods.**\n", {0} },
	/* 119 */
	{ "This is a dirt road.",
		{ 169, 118, 170, 92, 81, 0, 0, 0 },
"**The road continues on - here for some distance. A bonfire and party light\n\
up the night sky +.\n", {0} },
	/* 120 */
	{ "You are on a dirt road.",
		{ 171, 118, 92, 172, 81, 0, 0, 0 },
"**There is a village +. A huge bonfire licks at the trees, and a celebration\n\
of some sort is going on there.  The smell of luscious cooking is tantalizing\n\
my flared nostrils.  The road continues +.\n", {0} },
	/* 121 */
	{ "You are on a dirt road.",
		{ 173, 93, 174, 175, 82, 0, 0, 0 },
"This is a wide grassy clearing bedewed with droplets of evening mist.  The\n\
trees alongside the road moan and whisper as we pass.  They seem annoyed at\n\
our presence.  **The road continues - and -.\n", {0} },
	/* 122 */
	{ "You are at the seaplane dock.",
		{ 93, 0, 176, 177, 82, 0, 0, 0 },
"Not a living thing stirs the calm surface of the lagoon.  The wooden planks\n\
creak unnaturally as we tread on them.  The dock reaches a clearing +.\n\
A dark trail leads around the lagoon **+.\n", {0} },
	/* 123 */
	{ "There are some tables on the lawn here.",
		{ 121, 122, 123, 93, 82, 0, 0, 0 },
"Some tables are strewn on the wet lawn.****\n", {0} },
	/* 124 */
	{ "You are nosing around in the bushes.",
		{ 124, 124, 93, 124, 82, 0, 0, 0 },
"There is little here but some old beer cans. It is damp and dirty in here.\n\
I think I stepped in something unpleasant. It would be best to go **-.*\n", {0} },
	/* 125 */
	{ "You are walking in a dry stream bed.",
		{ 178, 98, 179, 0, 84, 0, 0, 0 },
"The large cobblestones are difficult to walk on.  No starlight reaches\n\
below a black canopy of fog seemingly engulfing the whole island.  A dirt\n\
path along the wash is **+.  The high bank is impossible to climb +.\n", {0} },
	/* 126 */
	{ "You are at the thermal pools.",
		{ 98, 0, 180, 181, 84, 0, 0, 0 },
"Odd spluttering and belching water splashes up around the rocks here.\n\
A spectacular waterfall nearby tumbles down as a river of effervescent\n\
bubbles.  The air is quite warm and a cave entrance ***+ spews steam.\n", {0} },
	/* 127 */
	{ "You are in the woods.",
		{ 127, 180, 182, 98, 84, 0, 0, 0 },
"It is pitch black in the forest here and my pant leg is caught on something.\n\
There may be poison oak here.  What was that?  A lantern just flickered by in\n\
the dark!  The sound of rushing water is coming from *+.**\n", {0} },
	/* 128 */
	{ "You are on a dirt trail.",
		{ 179, 181, 98, 0, 84, 0, 0, 0 },
"The trail seems to start here and head towards the forest +.**  High,\n\
dark cliffs border the trail +.  Some crickets are chirping\n\
noisily.\n", {0} },
	/* 129 */
	{ "You are  walking along the beach.",
		{ 183, 101, 184, 0, 87, 0, 0, 0 },
"The surf is rather tame tonight.  The beach continues + and +.**\n", {0} },
	/* 130 */
	{ "You are walking along the beach.",
		{ 101, 185, 186, 0, 87, 0, 0, 0 },
"This is not a very nice beach. The coarse sand hurts my feet.****\n", {0} },
	/* 131 */
	{ "You are walking through some ferns.",
		{ 184, 186, 187, 101, 87, 0, 0, 0 },
"This is a wide field growing only ferns and small shrubs.**  In the dark\n\
it would be all to easy to stumble into a venomous snake.  The ocean is\n\
*+.\n", {0} },
	/* 132 */
	{ "You are in a narrow canyon.",
		{ 0, 0, 188, 102, 76, 0, 0, 0 },
"The steep sides here squeeze a little freshet through a gauntlet like\n\
series of riffles and pools.  The cool mountain air is refreshing.****\n", {0} },
	/* 133 */
	{ "The canyon is much wider here.",
		{ 0, 0, 102, 189, 76, 0, 0, 0 },
"The sheer rock walls rise 10 meters into darkness. A slender waterfall\n\
careens away from the face of the rock high above and showers the gravel\n\
floor with sparkling raindrops.**  The canyon continues -\n\
and -.\n", {0} },
	/* 134 */
	{ "You are on the front porch of the cottage.",
		{ 190, 103, 0, 0, 0, 0, 0, 0 },
"The veranda is deserted.  A table and chair are the only things on the porch.\n\
Inside the house is a parlor lighted with an elegant chandelier.  The door\n\
leads -.  The lawn and fountain are +.**\n", {0} },
	/* 135 */
	{ "You are in a palm grove.",
		{ 103, 191, 192, 105, 88, 0, 0, 0 },
"Crickets are chirping in the cool night air.****\n", {0} },
	/* 136 */
	{ "You are on a dirt road.",
		{ 193, 192, 245, 103, 88, 0, 0, 0 },
"There are many bright lights +.  The road cleaves the darkness +.\n\
A small dirt road goes -, and a driveway peels off +.\n", {0} },
	/* 137 */
	{ "You are in a field of small shrubs.",
		{ 184, 186, 103, 187, 88, 0, 0, 0 },
"**Pine and other coniferous saplings are growing here.  The rich brown\n\
soil is well watered.  Across a large lawn +, there is a small cottage lighted\n\
with spot lights and gas lamps.  A cool land breeze is blowing.*\n", {0} },
	/* 138 */
	{ "The beach is pretty rocky here.",
		{ 194, 105, 195, 0, 96, 0, 0, 0 },
"The tide is very low tonight.  The beach is nicer *+.**\n", {0} },
	/* 139 */
	{ "The beach is almost 10 meters wide here.",
		{ 105, 183, 196, 0, 99, 0, 0, 0 },
"The sand has become more coarse and the beach steeper.****\n", {0} },
	/* 140 */
	{ "You are in the gardens.",
		{ 195, 196, 197, 105, 99, 0, 0, 0 },
"Shadowy expanses of lawn and leaf have been groomed and manicured here.\n\
The night sky is glowing with a full moon.**  A lighted path leads -.\n\
Stone steps lead down to the beach +.\n", {0} },
	/* 141 */
	{ "You are on the coast road.",
		{ 198, 106, 163, 199, 73, 0, 0, 0 },
"The forest is dense on either side.  The trees seem to be actually squeezing\n\
together to keep us from passing.  A feeling of enmity is in the air.**\n\
The road continues - and -.\n", {0} },
	/* 142 */
	{ "You are in the forest.",
		{ 116, 107, 91, 106, 73, 0, 0, 0 },
"I suppose there are trees and ferns all around, but it is too dark to see.****\n", {0} },
	/* 143 */
	{ "You are in the forest.",
		{ 199, 108, 106, 146, 73, 0, 0, 0 },
"There are shadowy trees and ferns all around.****\n", {0} },
	/* 144 */
	{ "You are in a copse.",
		{ 142, 107, 145, 80, 0, 0, 0, 0 },
"This is a secret hidden thicket only noticeable from the beach.  In the\n\
moonlight, I can tell that someone has been digging here recently.****\n", {0} },
	/* 145 */
	{ "You are at the tide pools.",
		{ 91, 0, 114, 107, 79, 0, 0, 0 },
"These rocks and pools are the home for many sea anemones and crustaceans.\n\
They are exposed because of the low tide.  There is a beach ***+.\n", {0} },
	/* 146 */
	{ "You are in the forest.",
		{ 199, 108, 143, 0, 73, 0, 0, 0 },
"This is a shallow depression sheltered from the wind by a thick growth of \n\
thorny shrubs.  It looks like someone is camping here.  There is a fire pit\n\
with warm, crackling flames and coals here.*  The beach is +.*  The thorny\n\
shrubs block the way -.\n", {0} },
	/* 147 */
	{ "You are at the mouth of the lagoon.",
		{ 200, 0, 108, 201, 74, 0, 0, 0 },
"The beach ends here where the coral reef rises to form a wide lagoon.\n\
A path winds around the lagoon to the -.* The beach continues\n\
on -. Only water lies +.\n", {0} },
	/* 148 */
	{ "You are in a breadfruit grove.",
		{ 202, 109, 203, 204, 77, 0, 0, 0 },
"The tall trees bend leisurely in the breeze, holding many round breadfruits\n\
close to their large serrated leaves.  There are coconut palms +,\n\
*+, and +.\n", {0} },
	/* 149 */
	{ "You are in a grove of mango trees.",
		{ 203, 111, 205, 109, 77, 0, 0, 0 },
"The trees are not tall enough to obscure the view and the bright moonlight\n\
makes it fairly easy to see.****\n", {0} },
	/* 150 */
	{ "You are in a grove of coconut palms.",
		{ 204, 112, 109, 206, 77, 0, 0, 0 },
"All I can see around us are trees and ominous shapes darting in and out of the\n\
shadows.****\n", {0} },
	/* 151 */
	{ "You are in a coconut grove.",
		{ 110, 207, 208, 209, 77, 0, 0, 0 },
"There are countless trees here.****\n", {0} },
	/* 152 */
	{ "You are in a field of pineapple.",
		{ 154, 208, 210, 110, 77, 0, 0, 0 },
"The sharp leaves are cutting me to ribbons. There is a road **+.*\n", {0} },
	/* 153 */
	{ "You are in a coconut grove.",
		{ 112, 209, 110, 211, 77, 0, 0, 0 },
"There is a field of something **+.*\n", {0} },
	/* 154 */
	{ "You are on the edge of a kiwi and pineapple field.",
		{ 111, 152, 155, 110, 77, 0, 0, 0 },
"An irrigation ditch separates the two fields here. There is a road **+.*\n", {0} },
	/* 155 */
	{ "This is a dirt road.",
		{ 205, 210, 212, 111, 77, 0, 0, 0 },
"The road runs - and - here.  It is very dark in the forest.**\n", {0} },
	/* 156 */
	{ "You are in a palm grove.",
		{ 206, 211, 112, 213, 77, 0, 0, 0 },
"There are trees all around us.****\n", {0} },
	/* 157 */
	{ "You are on the edge of a small clearing.",
		{ 157, 113, 157, 157, 79, 0, 0, 0 },
"The ground is rather marshy here and the darkness is intense.  A swarm of\n\
ravenous mosquitoes has descended upon you and has sent you quaking to your\n\
knees.****\n", {0} },
	/* 158 */
	{ "You are in the woods.",
		{ 158, 115, 215, 113, 79, 0, 0, 0 },
"You have walked a long way and found only spider webs. ****\n", {0} },
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
"This little thicket is hidden from the road in the shadows of the forest.\n\
From here we have a clear view of any traffic along the road. A great hollow\n\
tree stuffed with something is nearby.  The road is +.***\n", {0} },
	/* 162 */
	{ "You are on the coast road.",
		{ 215, 214, 0, 115, 86, 0, 0, 0 },
"The road turns abruptly - here, wandering deeper into the black forest.***\n", {0} },
	/* 163 */
	{ "You are on a dirt road.",
		{ 216, 116, 113, 141, 79, 0, 0, 0 },
"We are walking through a tunnel of unfriendly trees and shrubs.  The tall\n\
ones bend over the roadway and reach down with their branches to grab us.\n\
Broad leafed plants at the roadside whisper in the darkness.  Something\n\
just darted across the road and into the bushes *+.  Let's go *-.\n", {0} },
	/* 164 */
	{ "You have discovered a hidden thicket near the road.",
		{ 163, 142, 116, 106, 73, 0, 0, 0 },
"I would think it best to stay on the road.  The forest seems very unfriendly\n\
at night.  The road is **+.*\n", {0} },
	/* 165 */
	{ "You are in the living room.",
		{ 0, 117, 217, 218, 0, 0, 0, 0 },
"A decorative entry with fresh flowers and wall to wall carpeting leads into\n\
the living room here where a couch and two chairs converge with an end table.\n\
*The exit is +.* The bedroom is +.\n", {0} },
	/* 166 */
	{ "You are at the lagoon.",
		{ 118, 0, 167, 168, 81, 0, 0, 0 },
"A small beach here is deserted except for some fishing nets.  It is very\n\
peaceful at the lagoon at night.  The sound of native drums is carried on\n\
the night breeze.  There are paths leading off into darkness +,\n\
*+, and +.\n", {0} },
	/* 167 */
	{ "You are at the lagoon.",
		{ 118, 0, 170, 166, 81, 0, 0, 0 },
"The grass near the water is moist with the refreshing evening dew.  Far away,\n\
drums reverberate in the forest.**  The path continues + and +.\n", {0} },
	/* 168 */
	{ "You are at the lagoon.",
		{ 118, 0, 166, 172, 81, 0, 0, 0 },
"The path meanders through shadows of tussocks of grass, ferns, and thorny\n\
bushes here and continues on **- and -.\n", {0} },
	/* 169 */
	{ "You are in the woods.",
		{ 219, 119, 220, 92, 81, 0, 0, 0 },
"There are plenty of ferns and thorny bushes here! Spider webs and probing\n\
branches snare us as we stumble along in the pitch black night.****\n", {0} },
	/* 170 */
	{ "You are on a dirt road.",
		{ 220, 167, 199, 119, 74, 0, 0, 0 },
"The road winds rather close to a large lagoon here and many sedges and tall\n\
loom in the darkness *+. The road continues - and -.\n", {0} },
	/* 171 */
	{ "You are in the woods beside the road.",
		{ 221, 120, 92, 222, 81, 0, 0, 0 },
"The forest grows darker +. The road is +.**\n", {0} },
	/* 172 */
	{ "The road crosses the lagoon here.",
		{ 222, 0, 120, 174, 81, 0, 0, 0 },
"Strange mists rising from the water engulf a rickety old enclosed bridge here.\n\
Spider webs catch our hair as we pass through its rotting timbers.  I felt\n\
something drop on my neck.  The road delves into the accursed forest\n\
**+ and +.\n", {0} },
	/* 173 */
	{ "You are in a coconut palm grove.",
		{ 223, 121, 224, 225, 82, 0, 0, 0 },
"The tall palms are planted about 30 feet apart and the starry sky is clearly\n\
visible above.  A low growing grass carpets the ground all around.  The grove\n\
continues +.***\n", {0} },
	/* 174 */
	{ "You are walking along a dirt road.",
		{ 224, 176, 172, 121, 82, 0, 0, 0 },
"You are near misty patch of the roadway **+.  The road continues -.\n", {0} },
	/* 175 */
	{ "You are on a dirt road.",
		{ 225, 177, 121, 226, 82, 0, 0, 0 },
"The road turns abruptly - here, splitting a grove of palm trees.* In the\n\
starlight I can also discern that the road continues - toward the lagoon.*\n", {0} },
	/* 176 */
	{ "You are on a trail running around the lagoon.",
		{ 172, 0, 0, 122, 82, 0, 0, 0 },
"The dark waters brush the trail here and the path crosses an old  bridge\n\
+.  There is deep water + and +. The trail continues -.\n", {0} },
	/* 177 */
	{ "This is the mouth of the lagoon.",
		{ 175, 0, 122, 227, 82, 0, 0, 0 },
"The coral reef wraps around a natural bay here to create a wide lagoon which\n\
winds tortuously inland.**  A trail goes around the lagoon +.\n\
The beach is -.\n", {0} },
	/* 178 */
	{ "You are in a dry stream bed.",
		{ 0, 125, 0, 0, 84, 0, 0, 0 },
"The dry wash drains over a tall precipice here into a turbid morass below. The\n\
most noisome stench imaginable is wafting up to defile our nostrils. Above,\n\
the blackness is intense and a strange mist engulfs the island.*  Let's go\n\
-.**\n", {0} },
	/* 179 */
	{ "You are on a dirt path along the wash.",
		{ 0, 128, 125, 228, 84, 0, 0, 0 },
"The trail winds along the gravel wash and delves into the forest ***+.\n", {0} },
	/* 180 */
	{ "The thermal pools flow into a stream here.",
		{ 127, 0, 229, 126, 84, 0, 0, 0 },
"The gurgling hot waters pour over boulders into a swiftly flowing\n\
stream **+. The pools are +.\n", {0} },
	/* 181 */
	{ "You are at the entrance to a cave.",
		{ 128, 230, 126, 0, 84, 0, 0, 0 },
"A torch lights the entrance to the cave.  Deep inside I can see shadows moving.\n\
A path goes + from here.  The entrance is +.**\n", {0} },
	/* 182 */
	{ "You are in the woods.",
		{ 182, 229, 182, 127, 84, 0, 0, 0 },
"Thorns tangle your every effort to proceed.*  The sound of rushing water is\n\
+.**\n", {0} },
	/* 183 */
	{ "You are walking along the beach.",
		{ 139, 129, 184, 0, 99, 0, 0, 0 },
"Some dunes here progress inland and make it impossible to get very far in that\n\
direction.  The beach continues - and -.*  The ocean is +.\n", {0} },
	/* 184 */
	{ "You are in the dunes.",
		{ 183, 101, 184, 129, 87, 0, 0, 0 },
"The endless rolling and pitching sand dunes are enough to make one very queasy!\n\
The sand is cool and the stars are bright at the ocean.  The only way I'm going\n\
is ***+.\n", {0} },
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
vegetation +.**  The ocean is +.\n", {0} },
	/* 187 */
	{ "You are in the woods.",
		{ 131, 131, 137, 131, 87, 0, 0, 0 },
"Young trees and tall shrubs grow densely together here.\n\
They grow thicker **+.*\n", {0} },
	/* 188 */
	{ "The canyon is no wider than a foot here.",
		{ 0, 0, 0, 132, 0, 0, 0, 0 },
"The freshet is gushing through the narrow trough, but the canyon has grown\n\
too narrow to follow it any farther.***  I guess we'll have to go -.\n", {0} },
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
night air pours in through open shutters *+.  There doesn't seem \n\
to be anybody around.  A large immaculate oaken desk is visible in the\n\
study and it even has a old fashioned telephone to complete the decor.**\n", {0} },
	/* 191 */
	{ "You are in a palm grove.",
		{ 135, 191, 233, 191, 88, 0, 0, 0 },
"Grassy rows of dew covered palms stretch as far as I can see.**\n\
There is a road +.*\n", {0} },
	/* 192 */
	{ "You are on a dirt road.",
		{ 136, 233, 234, 135, 88, 0, 0, 0 },
"The road winds through a coconut palm grove here. It continues on - \n\
and -.**\n", {0} },
	/* 193 */
	{ "The road leads to several large buildings here.",
		{ 235, 136, 236, 237, 88, 0, 0, 0 },
"There is a lighted clubhouse +,* a large barn and stable +, and a\n\
garage of similar construct to the barn +.\n", {0} },
	/* 194 */
	{ "This part of the beach is impassable.",
		{ 0, 138, 0, 0, 96, 0, 0, 0 },
"The see is calm tonight.  The beach goes *-.**\n", {0} },
	/* 195 */
	{ "You are in the gardens.",
		{ 195, 140, 197, 138, 96, 0, 0, 0 },
"Dew beaded grass sparkles in the moonlight.  Tiny lamps beside the path light\n\
the way to the ocean ***+.\n", {0} },
	/* 196 */
	{ "You are in the gardens.",
		{ 140, 183, 197, 139, 99, 0, 0, 0 },
"Beautiful flowers and shrubs surround a lighted goldfish pond.****\n", {0} },
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
"Two roads come together in the darkness here. One runs -,* the other \n\
runs - and -.\n", {0} },
	/* 200 */
	{ "You are on a dirt path around the lagoon.",
		{ 170, 147, 146, 0, 74, 0, 0, 0 },
"The still waters reflect bending palms and a starry sky. It looks like\n\
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
"The coast road ends here in a lookout with a view of the ocean.\n\
Far below, the waves crash against the rocks.\n\
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
"The darkness is intense, but there seems to be a clearing +.***\n", {0} },
	/* 220 */
	{ "You are in the woods near the road.",
		{ 219, 170, 239, 169, 81, 0, 0, 0 },
"*As far as I can tell, there are two roads + and +.*\n", {0} },
	/* 221 */
	{ "You are in the woods.",
		{ 207, 171, 219, 222, 81, 0, 0, 0 },
"The spider webs thin out and the forest is clearer +.***\n", {0} },
	/* 222 */
	{ "You are on the lagoon's inland finger.",
		{ 0, 172, 171, 172, 81, 0, 0, 0 },
"It is impossible to follow the lagoon any farther inland because of sharp\n\
and very painful sedges.* The road is +.**\n", {0} },
	/* 223 */
	{ "You are in a grassy coconut grove.",
		{ 240, 173, 224, 241, 82, 0, 0, 0 },
"The tall palms provide a ghostly canopy for the sandy ground covering.****\n", {0} },
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
"The trail goes **-.*\n", {0} },
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
stone door in the wall +.  A wooden sign in the dust warns in old elven\n\
runes, \"GSRF KDIRE NLVEMP!\".**\n", {0} },
	/* 232 */
	{ "You are at the edge of a huge chasm.",
		{ 0, 0, 189, 0, 76, 0, 0, 0 },
"Several hundred feet down I can see the glimmer of placid water. The\n\
rivulets drain over the edge and trickle down into the depths. It is \n\
impossible to climb down.**  The canyon continues -.*\n", {0} },
	/* 233 */
	{ "You are on a dirt road.",
		{ 192, 241, 240, 191, 88, 0, 0, 0 },
"The road winds through a coconut grove here. The road continues on into the\n\
shadows - and -.**\n", {0} },
	/* 234 */
	{ "You are in a coconut palm grove near the road.",
		{ 193, 233, 213, 192, 88, 0, 0, 0 },
"***The road is +.\n", {0} },
	/* 235 */
	{ "You are at the clubhouse.",
		{ 0, 193, 0, 0, 0, 0, 0, 0 },
"The clubhouse is built over the most inland part of the lagoon.  Tropical\n\
bananas and fragrant frangipani grow along the grassy shore.  Walking across\n\
the short wooden bridge, we enter. Along one wall is a bar crowded with people.\n\
The restaurant and disco dance floor are filled to capacity.  A rock group\n\
electrocutes itself to the satisfaction of the audience.****\n", {0} },
	/* 236 */
	{ "You are in the stables.",
		{ 0, 0, 0, 193, 0, 0, 0, 0 },
"Neighing horses snacking on hay and oats fill the stalls on both sides of\n\
the barn.  It is rather warm in here but that is not the most offensive\n\
part.****\n", {0} },
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
"The road leads to a formal garden laced with lighted stone walks and tropical\n\
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
"Falling 10 agonizing meters into darkness, only droplets of the stream must\n\
be left to dance off the floor below.  There is no way down, even with a\n\
strong rope. ****\n", {0} },
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
"The ceiling here sparkles with iridescent gems and minerals. Colorful starfish\n\
and sea anemones cling to the slippery walls and floor.  The passage continues\n\
+.***\n", {0} },
	/* 247 */
	{ "The walls are very close together here.",
		{ 248, 246, 0, 0, 0, 0, 0, 0 },
"I can barely squeeze through the jagged opening. Slimy sea weeds provide\n\
no footing at all. This tunnel seems to be an ancient lava tube.  There is\n\
a large room +.***\n", {0} },
	/* 248 */
	{ "You are in the cathedral room.",
		{ 249, 247, 250, 251, 0, 0, 0, 0 },
"Your light casts ghostly shadows on the walls but cannot pierce the \n\
engulfing darkness overhead. The sound of water dripping echoes in the void.\n\
*I can see no passages leading out of this room.*** \n", {0} },
	/* 249 */
	{ "You are walking through a very round tunnel.",
		{ 252, 248, 0, 0, 252, 1, 0, 0 },
"The round walls of this tunnel are amazingly smooth to the touch. A little\n\
trickle of water flows down the center.  The tunnel climbs steadily +.\n\
There is a large room +.**\n", {0} },
	/* 250 */
	{ "You are in the cathedral anteroom.",
		{ 0, 0, 0, 248, 253, 1, 0, 0 },
"This small chamber with a flat stone floor is to one side of the cathedral \n\
room. We appear to be at the bottom of a tall narrow shaft. There are many \n\
puddles of water here. A staircase hewn from solid rock and black lava \n\
leads up.***  The cathedral room is -.\n", {0} },
	/* 251 */
	{ "You are in a wide chamber.",
		{ 0, 0, 248, 254, 0, 0, 0, 0 },
"Water is sprinkling from the ceiling here. A shallow pool populated by a \n\
myriad of blind white creatures sparkles in your light. Tiny shrimp and\n\
crabs scurry away, frightened by the blinding rays.**  The cave \n\
continues + and +.\n", {0} },
	/* 252 */
	{ "You are at the top of a sloping passage.",
		{ 0, 249, 255, 256, 257, 1, 249, 0 },
"There is much algae growing here, both green and brown specimens. I suspect\n\
that we are near the high tide zone, but no light can get in here. The walls\n\
glisten with shiny minerals.**  A hallway here runs + and -.\n", {0} },
	/* 253 */
	{ "You are in an elaborately tiled room.",
		{ 0, 0, 258, 0, 0, 0, 250, 0 },
"Large colorful tiles plate the floor and walls.  The ceiling is a mosaic\n\
of gems set in gold.  Hopefully it is only our footsteps that are echoing in\n\
this hollow chamber.**  The room continues -.  A stone staircase leads\n\
down.*\n", {0} },
	/* 254 */
	{ "You are at a dead end.",
		{ 0, 0, 251, 0, 0, 0, 0, 0 },
"The walls here are alive with dark mussels.  They click their shells menacingly\n\
if we disturb them.**  The only exit is +.*\n", {0} },
	/* 255 */
	{ "The tunnel is very low here.",
		{ 0, 0, 259, 252, 0, 0, 0, 0 },
"You practically have to crawl on your knees to pass through this opening.  The\n\
air is stiflingly damp, but you can't hear any sounds of water dripping.**\n\
The crawlspace continues -.  The tunnel seems wider +.\n", {0} },
	/* 256 */
	{ "This is the supply room.",
		{ 0, 0, 252, 0, 0, 0, 0, 0 },
"Picks and shovels line the walls here, as well as hard hats, boxes of\n\
dynamite, and a cartload of very high grade gold and silver ore.** \n\
A tunnel leads off +.*\n", {0} },
	/* 257 */
	{ "You have found a secret entrance to the catacombs",
		{ 0, 0, 0, 0, 216, 1, 252, 0 },
"Below is a wet, seaweed covered floor.  Above is a way out.****\n", {0} },
	/* 258 */
	{ "You are in the catacombs.",
		{ 0, 0, 260, 253, 0, 0, 0, 0 },
"Ornate tombs and piles of treasure line the walls.  Long spears with many\n\
blades, fine swords and coats of mail, heaps of coins, jewelry, pottery, \n\
and golden statues are tribute past kings and queens.** The catacombs\n\
continue - and -.\n", {0} },
	/* 259 */
	{ "You are crawling on your stomach.",
		{ 0, 0, 261, 255, 0, 0, 0, 0 },
"The passage is quite narrow and jagged, but the rock is no longer lava.\n\
It appears to be a form of granite.**  The crawlspace continues -, \n\
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
		{ 0, 0, 0, 259, 0, 0, 262, 0 },
"A ladder goes down into darkness here.***  A small crawlspace goes -.\n", {0} },
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
be nearly at sea level.  A ladder leads up.****\n", {0} },
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
The trail winds - and -.**\n", {0} },
	/* 268 */
	{ "You are in a rather large chamber.",
		{ 267, 0, 0, 269, 0, 0, 269, 0 },
"Beds of ferns and palm leaves make several cozy nests along the walls. In the\n\
center of the room is a throne of gold and silver.***  A passageway leads\n\
down and +.\n", {0} },
	/* 269 */
	{ "You are walking along the edge of a huge abyss.",
		{ 0, 0, 268, 0, 268, 1, 270, 0 },
"Steam is rising in great clouds from the immeasurable depths.  A very narrow\n\
trail winds down.**  There is a tunnel -.*\n", {0} },
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
"At the top of the pit, a single star can be seen in the night sky.  There\n\
doesn't appear to be any way to get out without a rope.  I don't remember\n\
how we got here.****\n", {0} }
};
