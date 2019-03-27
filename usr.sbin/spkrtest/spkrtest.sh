#!/bin/sh

#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2002  The FreeBSD Project
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#

#
# Inspired on spkrtest.pl, rewritten from scratch to remove perl dependency
# $VER: spkrtest 0.3 (9.5.2002) Riccardo "VIC" Torrini <riccardo@torrini.org>
# $FreeBSD$
#

cleanExit() {
	rm -f ${choices}
	exit ${1:-0}
}

trap 'cleanExit 1' 1 2 3 5 15	# HUP, INT, QUIT, TRAP, TERM

choices=${TMP:-/tmp}/_spkrtest_choices.$$
speaker=/dev/speaker

test -w ${speaker}
if [ $? -ne 0 ]
then
	echo "You have no write access to $speaker or the speaker device is"
	echo "not enabled in kernel. Cannot play any melody! See spkr(4)."
	sleep 2
	cleanExit 1
fi

/usr/bin/dialog --title "Speaker test" --checklist \
	"Please select the melodies you wish to play (space for select)" \
	0 0 0 \
	reveille "Reveille" OFF \
	contact "Contact theme from Close Encounters" OFF \
	dance "Lord of the Dance (aka Simple Gifts)" OFF \
	loony "Loony Toons theme" OFF \
	sinister "Standard villain's entrance music" OFF \
	rightstuff "A trope from 'The Right Stuff' score by Bill Conti" OFF \
	toccata "Opening bars of Bach's Toccata and Fugue in D Minor" OFF \
	startrek "Opening bars of the theme from Star Trek Classic" OFF \
		2> ${choices} || cleanExit 0

echo ""
tunes="`cat ${choices} | tr -d '\"'`"
for tune in ${tunes:-DEFAULT}
do
	case ${tune:-NULL} in
	DEFAULT)
		title="(default melody)"
		music="ec"
		;;
	reveille)
		title="Reveille"
		music="t255l8c.f.afc~c.f.afc~c.f.afc.f.a..f.~c.f.afc~c.f.afc~c.f.afc~c.f.."
		;;
	contact)
		title="Contact theme from Close Encounters"
		music="<cd<a#~<a#>f"
		;;
	dance)
		title="Lord of the Dance (aka Simple Gifts)"
		music="t240<cfcfgagaa#b#>dc<a#a.~fg.gaa#.agagegc.~cfcfgagaa#b#>dc<a#a.~fg.gga.agfgfgf."
		;;
	loony)
		title="Loony Toons theme"
		music="t255cf8f8edc<a>~cf8f8edd#e~ce8cdce8cd.<a>c8c8c#def8af8"
		;;
	sinister)
		title="standard villain's entrance music"
		music="mst200o2ola.l8bc.~a.~>l2d#"
		;;
	rightstuff)
		title="a trope from 'The Right Stuff' score by Bill Conti"
		music="olcega.a8f>cd2bgc.c8dee2"
		;;
	toccata)
		title="opening bars of Bach's Toccata and Fugue in D Minor"
		music="msl16oldcd4mll8pcb-agf+4.g4p4<msl16dcd4mll8pa.a+f+4p16g4"
		;;
	startrek)
		title="opening bars of the theme from Star Trek Classic"
		music="l2b.f+.p16a.c+.p l4mn<b.>e8a2mspg+e8c+f+8b2"
		;;
	esac
	echo "Title: ${title}"
	echo ${music} > ${speaker}
	sleep 1
done
cleanExit 0
