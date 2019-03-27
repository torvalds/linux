#!/bin/sh
# $FreeBSD$

for i in 6:12 8:14 8:16 10:18 10:20 11:22 12:24 14:28 16:32
do
	C=`echo $i | cut -f 1 -d :`
	R=`echo $i | cut -f 2 -d :`
	./vtfontcvt \
		-w $C -h $R \
		~/terminus-font-4.36/ter-u${R}n.bdf \
		~/terminus-font-4.36/ter-u${R}b.bdf \
		terminus-u${R}.vfnt
	gzip -9nf terminus-u${R}.vfnt
done
