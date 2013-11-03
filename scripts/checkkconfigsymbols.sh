#!/bin/sh
# Find Kconfig variables used in source code but never defined in Kconfig
# Copyright (C) 2007, Paolo 'Blaisorblade' Giarrusso <blaisorblade@yahoo.it>

# Tested with dash.
paths="$@"
[ -z "$paths" ] && paths=.

# Doing this once at the beginning saves a lot of time, on a cache-hot tree.
Kconfigs="`find . -name 'Kconfig' -o -name 'Kconfig*[^~]'`"

printf "File list \tundefined symbol used\n"
find $paths -name '*.[chS]' -o -name 'Makefile' -o -name 'Makefile*[^~]'| while read i
do
	# Output the bare Kconfig variable and the filename; the _MODULE part at
	# the end is not removed here (would need perl an not-hungry regexp for that).
	sed -ne 's!^.*\<\(UML_\)\?CONFIG_\([0-9A-Za-z_]\+\).*!\2 '$i'!p' < $i
done | \
# Smart "sort|uniq" implemented in awk and tuned to collect the names of all
# files which use a given symbol
awk '{map[$1, count[$1]++] = $2; }
END {
	for (combIdx in map) {
		split(combIdx, separate, SUBSEP);
		# The value may have been removed.
		if (! ( (separate[1], separate[2]) in map ) )
			continue;
		symb=separate[1];
		printf "%s ", symb;
		#Use gawk extension to delete the names vector
		delete names;
		#Portably delete the names vector
		#split("", names);
		for (i=0; i < count[symb]; i++) {
			names[map[symb, i]] = 1;
			# Unfortunately, we may still encounter symb, i in the
			# outside iteration.
			delete map[symb, i];
		}
		i=0;
		for (name in names) {
			if (i > 0)
				printf ", %s", name;
			else
				printf "%s", name;
			i++;
		}
		printf "\n";
	}
}' |
while read symb files; do
	# Remove the _MODULE suffix when checking the variable name. This should
	# be done only on tristate symbols, actually, but Kconfig parsing is
	# beyond the purpose of this script.
	symb_bare=`echo $symb | sed -e 's/_MODULE//'`
	if ! grep -q "\<$symb_bare\>" $Kconfigs; then
		printf "$files: \t$symb\n"
	fi
done|sort
