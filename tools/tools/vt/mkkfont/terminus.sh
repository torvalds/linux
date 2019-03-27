#!/bin/sh
for i in 12 14 16 20 22 24 28 32
do
	zcat ../fontcvt/terminus-u${i}.vfnt.gz | ./mkkfont > terminus-u${i}.c
done
