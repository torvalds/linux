#!/bin/sh

for FILE in `grep '^[ \t]*#[ \t]*include[ \t]*<' $2 | cut -f2 -d\< | cut -f1 -d\> | egrep ^linux\|^asm` ; do
    if [ ! -r $1/$FILE ]; then
	echo $2 requires $FILE, which does not exist in exported headers
	exit 1
    fi
done
# FIXME: List dependencies into $3
touch $3
