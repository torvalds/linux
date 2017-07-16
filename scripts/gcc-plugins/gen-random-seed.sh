#!/bin/sh

if [ ! -f "$1" ]; then
	SEED=`od -A n -t x8 -N 32 /dev/urandom | tr -d ' \n'`
	echo "const char *randstruct_seed = \"$SEED\";" > "$1"
	HASH=`echo -n "$SEED" | sha256sum | cut -d" " -f1 | tr -d ' \n'`
	echo "#define RANDSTRUCT_HASHED_SEED \"$HASH\"" > "$2"
fi
