#!/bin/sh
# This file is in the public domain
# $FreeBSD$

set -e

sh reduce.sh

OPLIST=`sh listallopts.sh`

ODIR=/usr/obj/`pwd`
RDIR=${ODIR}/_.result
export ODIR RDIR

table_td () (

	awk -v R=$1 -v T=$2 -v M=$4 '
	BEGIN	{
		t= R "-" T
	}
	$1 == t {
		if ($3 == 0 && $5 == 0 && $7 == 0) {
			printf "<TD align=center COLSPAN=5>no effect</TD>"
		} else {
			if ($3 == 0) {
				printf "<TD align=right>+%d</TD>", $3
			} else {
				printf "<TD align=right>"
				printf "<A HREF=\"%s/%s.mtree.add.txt\">+%d</A>", M, t, $3
				printf "</TD>"
			}
			if ($5 == 0) {
				printf "<TD align=right>-%d</TD>", $5
			} else {
				printf "<TD align=right>"
				printf "<A HREF=\"%s/%s.mtree.sub.txt\">-%d</A>", M, t, $5
				printf "</TD>"
			}
			if ($7 == 0) {
				printf "<TD align=right>*%d</TD>", $7
			} else {
				printf "<TD align=right>"
				printf "<A HREF=\"%s/%s.mtree.chg.txt\">*%d</A>", M, t, $7
				printf "</TD>"
			}
			printf "<TD align=right>%d</TD>", $9
			printf "<TD align=right>%d</TD>", -$11
		}
		printf "\n"
		d = 1
		}
	END	{
		if (d != 1) {
			printf "<TD COLSPAN=5></TD>"
		}
	}
	' $3/stats
	mkdir -p $HDIR/$4
	cp $3/r*.txt $HDIR/$4 || true
)

HDIR=${ODIR}/HTML
rm -rf ${HDIR}
mkdir -p ${HDIR}
H=${HDIR}/index.html

echo '<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN" "http://www.w3.org/TR/html4/loose.dtd">
<HTML>' > $H

echo '<HEAD>
<META http-equiv="Content-Type" content="text/html; charset=iso-8859-1">
<TITLE>FreeBSD Build Options Survey</TITLE>
</HEAD>
<BODY bgcolor="#FFFFFF">
' >> $H

echo '
<H2>The table is explained at the bottom</H2>
<HR>
' >> $H

echo '<TABLE  border="1" cellspacing="0">' >> $H

echo "<TR>" >> $H
echo "<TH ROWSPAN=2>src.conf</TH>" >> $H
echo "<TH ROWSPAN=2>MK_FOO</TH>" >> $H
echo "<TH ROWSPAN=2></TH>" >> $H
echo "<TH COLSPAN=5>BuildWorld</TH>" >> $H
echo "<TH ROWSPAN=2></TH>" >> $H
echo "<TH COLSPAN=5>InstallWorld</TH>" >> $H
echo "<TH ROWSPAN=2></TH>" >> $H
echo "<TH COLSPAN=5>World</TH>" >> $H
echo "</TR>" >> $H

echo "<TR>" >> $H
for i in bw iw w
do
	echo "<TH>A</TH>" >> $H
	echo "<TH>D</TH>" >> $H
	echo "<TH>C</TH>" >> $H
	echo "<TH>KB</TH>" >> $H
	echo "<TH>Delta</TH>" >> $H
done
echo "</TR>" >> $H

majcol ( ) (
	echo "<TD></TD>" >> $H
	if [ ! -f $3/$1/done ] ; then
		echo "<TD align=center COLSPAN=5>no data yet</TD>" >> $H
	elif [ -f $3/$1/_.success ] ; then
		table_td $2 $1 $3 $4 >> $H
	else
		echo "<TD align=center COLSPAN=5>failed</TD>" >> $H
	fi
)


for o in $OPLIST
do
	md=`echo "${o}=foo" | md5`
	m=${RDIR}/$md
	if [ ! -d $m ] ; then
		continue
	fi
	if [ ! -f $m/stats ] ; then
		continue
	fi
	echo "=== mkhtml ${d}_${o}"

	echo "<TR>" >> $H
	echo "<TD><PRE>" >> $H
	cat $m/src.conf >> $H
	echo "</PRE></TD>" >> $H
	echo "<TD><PRE>" >> $H
	if [ -f $m/bw/_.sc ] ; then
		comm -13 ${RDIR}/Ref/_.sc $m/bw/_.sc >> $H
	fi
	echo "</PRE></TD>" >> $H

	majcol bw r $m $md
	majcol iw r $m $md
	majcol w  r $m $md
	echo "</TR>" >> $H
done
echo "</TABLE>" >> $H
echo '
<HR>
<H2>How to read this table</H2>
<P>
The table has five major columns.

<OL>
<LI><P><B>src.conf</B></P>
<P>The name of the option being tested</P>
<P>
All options are tested both in their WITH_FOO and WITHOUT_FOO variants
but if the option has no effect (ie: is the default) it will not appear
in the table
</P>
</LI>

<LI><P><B>MK_FOO</B></P>
<P>Internal build flags affected by this option </P>
</LI>

<LI><P><B>Buildworld</B></P>
<P>What happens when the option is given to buildworld but not installworld</P>
<PRE>Ie:
	make buildworld WITH_FOO=yes
	make installworld 
</PRE>
</LI>

<LI><P><B>Installworld</B></P>
<P>What happens when the option is given to installworld but not buildworld</P>
<PRE>Ie:
	make buildworld 
	make installworld WITH_FOO=yes
</PRE>
</LI>

<LI><P><B>World</B></P>
<P>What happens when the option is given to both buildworld and installworld</P>
<PRE>Ie:
	make buildworld WITH_FOO=yes
	make installworld WITH_FOO=yes
</PRE>
</LI>
</OL>

<P>Inside each of the last three major columns there are five subcolumns</P>
<OL>
<LI><P><B>A</B></P>
<P>Number of added files/directories (relative to the option not be given</P>
<P>If non-zero, the number links to a list of the added files/directories</P>
</LI>
<LI><P><B>D</B></P>
<P>Number of deleted files/directories (relative to the option not be given</P>
<P>If non-zero, the number links to a list of the files not installed files/directories</P>
</LI>
<LI><P><B>C</B></P>
<P>Number of changed files/directories (relative to the option not be given</P>
<P>If non-zero, the number links to a list of the files/directories which are differnet (two lines each)</P>
</LI>
<LI><P><B>KB</B></P>
<P>Size of installed operating system in kilobytes</P>
<LI><P><B>Delta</B></P>
<P>Size change in kilobytes relative to the option not be given</P>
</LI>
</OL>

<HR>' >> $H
echo '
<p>
    <a href="http://validator.w3.org/check?uri=referer"><img
        src="http://www.w3.org/Icons/valid-html401"
        alt="Valid HTML 4.01 Transitional" height="31" width="88"></a>
</p>

' >> $H
echo "</HTML>" >> $H
