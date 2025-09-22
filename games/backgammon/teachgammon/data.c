/*	$OpenBSD: data.c,v 1.8 2017/01/20 01:55:25 tb Exp $	*/

/*
 * Copyright (c) 1980, 1993
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

#include <sys/types.h>

#include "tutor.h"

int     maxmoves = 23;

const char   *const text0[] = {
	"To start the game, I roll a 3 and you roll a 1.  This means",
	"that I get to start first.  I move 8-5,6-5 since this makes a",
	"new point and helps to trap your back men on 1.  You should be",
	"able to do a similar move with your roll.",
	"",
	0
};

const char   *const text1[] = {
	"Now you shall see a move using doubles.  I just rolled double",
	"5's.  I will move two men from position 13 to position 3.  The",
	"notation for this is 13-8,13-8,8-3,8-3.  You will also roll dou-",
	"bles, but you will be able to make a much stronger move.",
	"",
	0
};

const char   *const text2[] = {
	"Excellent!  As you can see, you are beginning to develop a wall",
	"which is trapping my men on position 24.  Also, moving your back",
	"men forward not only improves your board position safely, but it",
	"thwarts my effort to make a wall.",
	"",
	"My roll now is 5 6.  Normally, I would use that roll to move from",
	"position 24 to position 13 (24-18-13), but your new point prevents",
	"that.  Instead, I am forced to move from 13 to 2, where my man is",
	"open but cannot be hit.",
	"",
	0
};

const char   *const text3[] = {
	"As you can see, although you left a man open, it is a rela-",
	"tively safe move to an advantageous position, which might help",
	"you make a point later.  Only two rolls (4 5 or 5 4) will allow",
	"me to hit you.  With an unprecedented amount of luck, I happen",
	"to roll a 4 5 and hit you as just mentioned.",
	"",
	0
};

const char   *const text4[] = {
	"You're pretty lucky yourself, you know.  I follow by rolling 2 3",
	"and moving 25-22,24-22, forming a new point.",
	"",
	0
};

const char   *const text5[] = {
	"Not a spectacular move, but a safe one.  I follow by rolling 6 1.",
	"I decide to use this roll to move 22-16,16-15.  It leaves me with",
	"one man still open, but the blot is farther back on the board, and",
	"would suffer less of a loss by being hit.",
	"",
	0
};

const char   *const text6[] = {
	"By moving your two men from 17 to 20, you lessen my chance of",
	"getting my man off the board.  In fact, the odds are 5 to 4",
	"against me getting off.  I roll with the odds and helplessly",
	"receive a 3 5.",
	"",
	0
};

const char   *const text7[] = {
	"Note that the blot on 7 cannot be hit unless I get off the bar",
	"and have a 1 or a 6 left over, and doing so will leave two of",
	"my men open.  Also, the blot on 16 cannot be hit at all!  With",
	"a sigh of frustration, I roll double 6's and remain immobile.",
	"",
	0
};

const char   *const text8[] = {
	"See, you did not get hit and, you got to `cover up' your open men.",
	"Quite an accomplishment.  Finally, I get off the bar by rolling",
	"6 2 and moving 25-23,23-17.",
	"",
	0
};

const char   *const text9[] = {
	"My venture off the bar did not last long.  However, I got lucky",
	"and rolled double 1's, allowing me to move 25-24,24-23,15-14,15-14.",
	"",
	0
};

const char   *const text10[] = {
	"You are improving your position greatly and safely, and are well",
	"on the way to winning the game.  I roll a 6 2 and squeak past",
	"your back man.  Now the game becomes a race to the finish.",
	"",
	0
};

const char   *const text11[] = {
	"Now that it is merely a race, you are trying to get as many men",
	"as possible into the inner table, so you can start removing them.",
	"I roll a 3 4 and move my two men farthest back to position 11",
	"(15-11,14-11).",
	"",
	0
};

const char   *const text12[] = {
	"The race is still on, and you seem to be doing all right.",
	"I roll 6 1 and move 14-8,13-12.",
	"",
	0
};

const char   *const text13[] = {
	"Notice that you get to remove men the instant you have all of",
	"them at your inner table, even if it is the middle of a turn.",
	"I roll 1 2 and move 13-11,12-11.",
	"",
	0
};

const char   *const text14[] = {
	"Although you could have removed a man, this move illustrates two",
	"points:  1) You never have to remove men, and 2) You should try",
	"to spread out your men on your inner table.  Since you have one",
	"man on each position, you should be able to remove at least two",
	"men next turn.  I roll 2 5 and move 8-6,11-6.",
	"",
	0
};

const char   *const text15[] = {
	"This time you were able to remove men.  I roll 3 4 and move",
	"11-7,11-8.  The race continues.",
	"",
	0
};

const char   *const text16[] = {
	"More holes are opening up in your inner table, but you are",
	"still very much ahead.  If we were doubling, you would have",
	"doubled long ago.  I roll 2 6 and move 8-6,11-5.",
	"",
	0
};

const char   *const text17[] = {
	"It pays to spread out your men.  I roll 3 5 and move 7-4,8-3.",
	"",
	0
};

const char   *const text18[] = {
	"You can only remove some men, but you spread out more and",
	"more, in order to be able to remove men more efficiently.",
	"I roll double 3's, which help, but not that much.  I move",
	"8-5,3-0,3-0,3-0.",
	"",
	0
};

const char   *const text19[] = {
	"I roll 1 4 and move 5-4,4-0.",
	"",
	0
};

const char   *const text20[] = {
	"You are now nicely spread out to win a game.  I roll 5 6 and",
	"move 5-0,6-0.",
	"",
	0
};

const char   *const text21[] = {
	"Any minute now.  Just a few short steps from victory.  I roll",
	"2 4 and move 6-4,4-0.",
	"",
	0
};

const char   *const text22[] = {
	"It looks pretty hopeless for me, but I play on, rolling 1 3 and",
	"moving 4-3,3-0.",
	"",
	0
};

const char   *const text23[] = {
	"Congratulations!  You just won a game of backgammon against the",
	"computer!  This ends the tutorial; next is a real game.  Remember",
	"when you start playing that doubling will be enabled, which",
	"will add another factor to the game...  Good luck!!",
	"",
	0
};

const char   *const ans0[] = {
	"Never seen",
	"",
	0
	};

const char   *const ans1[] = {
	"No, that's still not it.  Here's the move that you should have made:",
	"17/4,19/2.  That makes a new point at position 21.  I will make the",
	"move for you now, and continue with my own move after that.",
	"",
	0
	};

const char   *const ans2[] = {
	"Okay, I'll tell you the move that I consider best:  1/66,12/66.  All",
	"points, no blots.  I'll make that move now and continue on.",
	"",
	0
	};

const char   *const ans3[] = {
	"That's not it.  The move I will make for you is 12-14-15.",
	"",
	0
	};

const char   *const ans4[] = {
	"It's clear that the only move you can do is 0-2-7.",
	"",
	0
	};

const char   *const ans5[] = {
	"Not optimal.  Your best move is 7-11-12.",
	"",
	0
	};

const char   *const ans6[] = {
	"There are many passable moves, but the best is 17/33,19/33, which",
	"I will do for you now.",
	"",
	0
	};

const char   *const ans7[] = {
	"That's still not the move I consider best.  I'll force you to play",
	"7/5,12/4 and justify it later.",
	"",
	0
	};

const char   *const ans8[] = {
	"No.  It's pretty clear that 7-10-16, which removes your blots, is best.",
	"",
	0
	};

const char   *const ans9[] = {
	"No.  Why not 12-17-18?",
	"",
	0
	};

const char   *const ans10[] = {
	"No.  Your best move is 12/46.",
	"",
	0
	};

const char   *const ans11[] = {
	"No.  Your best move is 16/3,18/1.",
	"",
	0
	};

const char   *const ans12[] = {
	"No.  Your best move is 16/35.",
	"",
	0
	};

const char   *const ans13[] = {
	"No.  Your best move is 18/444,21/4.",
	"",
	0
	};

const char   *const ans14[] = {
	"No.  Your best move is 22/12.",
	"",
	0
	};

const char   *const ans15[] = {
	"No.  Your best move is 19-H,22-H.",
	"",
	0
	};

const char   *const ans16[] = {
	"No.  Your best move is 20/5,23/2.",
	"",
	0
	};

const char   *const ans17[] = {
	"No.  Your best move is 19-H,24-H.",
	"",
	0
	};

const char   *const ans18[] = {
	"No.  I think your best move is 20/5,21/2.  I'll move that now.",
	"",
	0
	};

const char   *const ans19[] = {
	"No.  Your best move is 19/16.",
	"",
	0
	};

const char   *const ans20[] = {
	"No.  Your best move is 22/3,23/2.",
	"",
	0
	};

const char   *const ans21[] = {
	"No.  Your best move is 20/1,21-H.",
	"",
	0
	};

const char   *const ans22[] = {
	"No.  Your best move is 19-H,21-23.",
	"",
	0
	};

const char   *const ans23[] = {
	"No.  Your best move is 22-H,23-H.",
	"",
	0
	};

const struct situatn  test[] = {
	{
		{0,2,0,0,0,0,-5,0,-3,0,0,0,5,-5,0,0,0,3,0,5,0,0,0,0,-2,0},
		3, 1, {8,6,0,0}, {5,5,0,0}, 4, 2, {text0}, {ans0}
	},
	{
		{0,2,0,0,0,-2,-4,0,-2,0,0,0,5,-5,0,0,0,2,0,4,0,2,0,0,-2,0},
		5, 5, {13,13,8,8}, {8,8,3,3}, 6, 6, {text1}, {ans1}
	},
	{
		{0,0,0,-2,0,-2,-4,2,-2,0,0,0,3,-3,0,0,0,2,2,4,0,2,0,0,-2,0},
		6, 5, {13,8,0,0}, {8,2,0,0}, 1, 2, {text2}, {ans2}
	},
	{
		{0,0,-1,-2,0,-2,-4,2,-2,0,0,0,2,-2,0,1,0,2,2,4,0,2,0,0,-2,0},
		4, 5, {24,20,0,0}, {20,15,0,0}, 2, 5, {text3}, {ans3}
	},
	{
		{0,0,0,-2,0,-2,-4,3,-2,0,0,0,2,-2,0,-1,0,2,2,4,0,2,0,0,-1,-1},
		2, 3, {25,24,0,0}, {22,22,0,0}, 4, 1, {text4}, {ans4}
	},
	{
		{0,0,0,-2,0,-2,-4,2,-2,0,0,0,3,-2,0,-1,0,2,2,4,0,2,-2,0,0,0},
		6, 1, {22,16,0,0}, {16,15,0,0}, 3, 3, {text5}, {ans5}
	},
	{
		{0,0,0,-2,0,-2,-4,2,-2,0,0,0,3,-2,0,-2,0,0,2,2,2,2,2,0,0,-1},
		3, 5, {0,0,0,0}, {0,0,0,0}, 5, 4, {text6}, {ans6}
	},
	{
		{0,0,0,-2,0,-2,-4,1,-2,0,0,0,3,-2,0,-2,1,0,2,2,2,2,2,0,0,-1},
		6, 6, {0,0,0,0}, {0,0,0,0}, 3, 6, {text7}, {ans7}
	},
	{
		{0,0,0,-2,0,-2,-4,0,-2,0,0,0,3,-2,0,-2,2,0,2,2,2,2,2,0,0,-1},
		2, 6, {25,23,0,0}, {23,17,0,0}, 5, 1, {text8}, {ans8}
	},
	{
		{0,0,0,-2,0,-2,-4,0,-2,0,0,0,2,-2,0,-2,2,0,3,2,2,2,2,0,0,-1},
		1, 1, {25,24,15,15}, {24,23,14,14}, 4, 6, {text9}, {ans9}
	},
	{
		{0,0,0,-2,0,-2,-4,0,-2,0,0,0,0,-2,-2,0,3,0,4,2,2,2,2,-1,0,0},
		6, 2, {23,17,0,0}, {17,15,0,0}, 1, 3, {text10}, {ans10}
	},
	{
		{0,0,0,-2,0,-2,-4,0,-2,0,0,0,0,-2,-2,-1,2,0,3,4,2,2,2,0,0,0},
		4, 3, {15,14,0,0}, {11,11,0,0}, 5, 3, {text11}, {ans11}
	},
	{
		{0,0,0,-2,0,-2,-4,0,-2,0,0,-2,0,-2,-1,0,0,0,3,5,2,3,2,0,0,0},
		6, 1, {14,13,0,0}, {8,12,0,0}, 4, 4, {text12}, {ans12}
	},
	{
		{0,0,0,-2,0,-2,-4,0,-3,0,0,-2,-1,-1,0,0,0,0,0,5,2,2,5,0,0,0},
		2, 1, {13,12,0,0}, {11,11,0,0}, 2, 1, {text13}, {ans13}
	},
	{
		{0,0,0,-2,0,-2,-4,0,-3,0,0,-4,0,0,0,0,0,0,0,5,2,2,3,1,1,0},
		2, 5, {8,11,0,0}, {6,6,0,0}, 6, 3, {text14}, {ans14}
	},
	{
		{0,0,0,-2,0,-2,-6,0,-2,0,0,-3,0,0,0,0,0,0,0,4,2,2,2,1,1,0},
		4, 3, {11,11,0,0}, {7,8,0,0}, 2, 5, {text15}, {ans15}
	},
	{
		{0,0,0,-2,0,-2,-6,-1,-3,0,0,-1,0,0,0,0,0,0,0,4,1,2,2,0,1,0},
		2, 6, {8,11,0,0}, {6,5,0,0}, 6, 1, {text16}, {ans16}
	},
	{
		{0,0,0,-2,0,-3,-7,-1,-2,0,0,0,0,0,0,0,0,0,0,3,1,2,2,0,0,0},
		5, 3, {8,7,0,0}, {3,4,0,0}, 5, 2, {text17}, {ans17}
	},
	{
		{0,0,0,-3,-1,-3,-7,0,-1,0,0,0,0,0,0,0,0,0,0,3,0,1,2,1,0,0},
		3, 3, {8,3,3,3}, {5,0,0,0}, 1, 6, {text18}, {ans18}
	},
	{
		{0,0,0,0,-1,-4,-7,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,2,1,0,0},
		1, 4, {4,5,0,0}, {0,4,0,0}, 2, 3, {text19}, {ans19}
	},
	{
		{0,0,0,0,-1,-3,-7,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,0,0},
		5, 6, {6,5,0,0}, {0,0,0,0}, 1, 4, {text20}, {ans20}
	},
	{
		{0,0,0,0,-1,-2,-6,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,0,0,0},
		2, 4, {4,6,0,0}, {0,4,0,0}, 6, 2, {text21}, {ans21}
	},
	{
		{0,0,0,0,-1,-2,-5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0},
		3, 1, {4,3,0,0}, {3,0,0,0}, 4, 3, {text22}, {ans22}
	},
	{
		{0,0,0,0,0,-2,-5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
		0, 0, {0,0,0,0}, {0,0,0,0}, 0, 0, {text23}, {ans23}
	}
};
