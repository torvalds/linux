#!/bin/sh

for FILE in `grep '^#include <' $2 | cut -f2 -d\< | cut -f1 -d\> | egrep ^linux\|^asm` ; do
    if [ ! -r $1/$FILE ]; then
	echo $2 requires $FILE, which does not exist
	exit 1
    fi
done
