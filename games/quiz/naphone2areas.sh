#!/bin/sh

# na.phone:
# Area Code : City : State/Province : State/Province Abbrev.

# areas:
# Area Code : State/Province|State/Province Abbrev. : City

if [ X"$1" == X"" ]; then
	exit 1
fi

awk '{
	split($0, a, ":");

	if (a[1] ~ /^[0-9]+$/) {
		if (last != a[1]) {
			if (a[4] == "")
				print a[1] ":" a[3] ":" a[2];
			else
				print a[1] ":" a[3] "|" a[4] ":" a[2];
		}
	}
	last = a[1];
}' $1
