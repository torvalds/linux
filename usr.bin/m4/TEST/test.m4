#	$OpenBSD: test.m4,v 1.3 2003/06/03 02:56:11 millert Exp $
#	$NetBSD: test.m4,v 1.4 1995/09/28 05:38:05 tls Exp $
#
# Copyright (c) 1989, 1993
#	The Regents of the University of California.  All rights reserved.
#
# This code is derived from software contributed to Berkeley by
# Ozan Yigit.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. Neither the name of the University nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#
#	@(#)test.m4	8.1 (Berkeley) 6/6/93
#

# test file for mp (not comprehensive)
#
# v7 m4 does not have `decr'.
#
define(DECR,`eval($1-1)')
#
# include string macros
#
include(string.m4)
#
# create some fortrash strings for an even uglier language
#
string(TEXT, "text")
string(DATA, "data")
string(BEGIN, "begin")
string(END, "end")
string(IF, "if")
string(THEN, "then")
string(ELSE, "else")
string(CASE, "case")
string(REPEAT, "repeat")
string(WHILE, "while")
string(DEFAULT, "default")
string(UNTIL, "until")
string(FUNCTION, "function")
string(PROCEDURE, "procedure")
string(EXTERNAL, "external")
string(FORWARD, "forward")
string(TYPE, "type")
string(VAR, "var")
string(CONST, "const")
string(PROGRAM, "program")
string(INPUT, "input")
string(OUTPUT, "output")
#
divert(2)
diversion #1
divert(3)
diversion #2
divert(4)
diversion #3
divert(5)
diversion #4
divert(0)
define(abc,xxx)
ifdef(`abc',defined,undefined)
#
# v7 m4 does this wrong. The right output is 
# 	this is A vEry lon sEntEnCE
# see m4 documentation for translit.
#
translit(`this is a very long sentence', abcdefg, ABCDEF)
#
# include towers-of-hanoi
#
include(hanoi.m4)
#
# some reasonable set of disks
#
hanoi(6)
#
# include ackermann's function
#
include(ack.m4)
#
# something like (3,3) will blow away un*x m4.
#
ack(2,3)
#
# include a square_root function for fixed nums
#
include(sqroot.m4)
#
# some square roots.
#
square_root(15)
square_root(100)
square_root(-4)
square_root(21372)
#
# some textual material for enjoyment.
#
[taken from the 'Clemson University Computer Newsletter',
 September 1981, pp. 6-7]
     
I am a wizard in the magical Kingdom of Transformation and I
slay dragons for a living.  Actually, I am a systems programmer.
One of the problems with systems programming is explaining to
non-computer enthusiasts what that is.  All of the terms I use to
describe my job are totally meaningless to them.  Usually my response
to questions about my work is to say as little as possible.  For
instance, if someone asks what happened at work this week, I say
"Nothing much" and then I change the subject.
     
With the assistance of my brother, a mechanical engineer, I have devised
an analogy that everyone can understand.  The analogy describes the
"Kingdom of Transformation" where travelers wander and are magically
transformed.  This kingdom is the computer and the travelers are information.
The purpose of the computer is to change information to a more meaningful
forma.  The law of conservation applies here:  The computer never creates
and never intentionally destroys data.  With no further ado, let us travel
to the Kingdom of Transformation:
     
In a land far, far away, there is a magical kingdom called the Kingdom of
Transformation.  A king rules over this land and employs a Council of
Wizardry.  The main purpose of this kingdom is to provide a way for
neighboring kingdoms to transform citizens into more useful citizens.  This
is done by allowing the citizens to enter the kingdom at one of its ports
and to travel any of the many routes in the kingdom.  They are magically
transformed along the way.  The income of the Kingdom of Transformation
comes from the many toll roads within its boundaries.
     
The Kingdom of Transformation was created when several kingdoms got
together and discovered a mutual need for new talents and abilities for
citizens.  They employed CTK, Inc. (Creators of Transformation, Inc.) to
create this kingdom.  CTK designed the country, its transportation routes,
and its laws of transformation, and created the major highway system.
     
Hazards
=======
     
Because magic is not truly controllable, CTK invariably, but unknowingly,
creates dragons.  Dragons are huge fire-breathing beasts which sometimes
injure or kill travelers.  Fortunately, they do not travel, but always
remain near their den.
     
Other hazards also exist which are potentially harmful.  As the roads
become older and more weatherbeaten, pot-holes will develop, trees will
fall on travelers, etc.  CTK maintenance men are called to fix these
problems.
     
Wizards
=======
     
The wizards play a major role in creating and maintaining the kingdom but
get little credit for their work because it is performed secretly.  The
wizards do not wan the workers or travelers to learn their incantations
because many laws would be broken and chaos would result.
     
CTK's grand design is always general enough to be applicable in many
different situations.  As a result, it is often difficult to use.  The
first duty of the wizards is to tailor the transformation laws so as to be
more beneficial and easier to use in their particular environment.
     
After creation of the kingdom, a major duty of the wizards is to search for
and kill dragons.  If travelers do not return on time or if they return
injured, the ruler of the country contacts the wizards.  If the wizards
determine that the injury or death occurred due to the traveler's
negligence, they provide the traveler's country with additional warnings.
If not, they must determine if the cause was a road hazard or a dragon.  If
the suspect a road hazard, they call in a CTK maintenance man to locate the
hazard and to eliminate it, as in repairing the pothole in the road.  If
they think that cause was a dragon, then they must find and slay it.
     
The most difficult part of eliminating a dragon is finding it.  Sometimes
the wizard magically knows where the dragon's lair it, but often the wizard
must send another traveler along the same route and watch to see where he
disappears.  This sounds like a failsafe method for finding dragons (and a
suicide mission for thr traveler) but the second traveler does not always
disappear.  Some dragons eat any traveler who comes too close; others are
very picky.
     
The wizards may call in CTK who designed the highway system and
transformation laws to help devise a way to locate the dragon.  CTK also
helps provide the right spell or incantation to slay the dragon. (There is
no general spell to slay dragons; each dragon must be eliminated with a
different spell.)
     
Because neither CTK nor wizards are perfect, spells to not always work
correctly.  At best, nothing happens when the wrong spell is uttered.  At
worst, the dragon becomes a much larger dragon or multiplies into several
smaller ones.  In either case, new spells must be found.
     
If all existing dragons are quiet (i.e. have eaten sufficiently), wizards
have time to do other things.  They hide in castles and practice spells and
incatations.  They also devise shortcuts for travelers and new laws of
transformation.
     
Changes in the Kingdom
======================
     
As new transformation kingdoms are created and old ones are maintained,
CTK, Inc. is constantly learning new things.  It learns ways to avoid
creating some of the dragons that they have previously created.  It also
discovers new and better laws of transformation.  As a result, CTK will
periodically create a new grand design which is far better than the old.
The wizards determine when is a good time to implement this new design.
This is when the tourist season is slow or when no important travelers
(VIPs) are to arrive.  The kingdom must be closed for the actual
implementation and is leter reopened as a new and better place to go.
     
A final question you might ask is what happens when the number of tourists
becomes too great for the kingdom to handle in a reasonable period of time
(i.e., the tourist lines at the ports are too long).  The Kingdom of
Transformation has three options: (1) shorten the paths that a tourist must
travel, or (2) convince CTK to develop a faster breed of horses so that the
travelers can finish sooner, or (3) annex more territories so that the
kingdom can handle more travelers.
     
Thus ends the story of the Kingdom of Transformation.  I hope this has
explained my job to you:  I slay dragons for a living.

#
#should do an automatic undivert..
#
