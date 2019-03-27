#!/bin/sh
#
#	@(#)makedb.sh	8.1 (Berkeley) 6/4/93

awk '{i++; print $0; print i;}' /usr/share/dict/words > WORDS
ls /bin /usr/bin /usr/ucb /etc | egrep '^(...|....|.....|......)$' | \
sort | uniq | \
awk '{
	printf "%s\n", $0
	for (i = 0; i < 1000; i++)
		printf "%s+", $0
	printf "\n"
}' > LONG.DATA
