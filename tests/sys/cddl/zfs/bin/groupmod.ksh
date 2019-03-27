#! /usr/local/bin/ksh93 -p

# $FreeBSD$

a=
g=
for i in $*
do
	a="$a $g"
	g=$i
done
	
/usr/sbin/pw groupmod $g $a
