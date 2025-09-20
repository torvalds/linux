#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+
#
# Create a spreadsheet from torture-test Kconfig options and kernel boot
# parameters.  Run this in the directory containing the scenario files.
#
# Usage: config2csv path.csv [ "scenario1 scenario2 ..." ]
#
# By default, this script will take the list of scenarios from the CFLIST
# file in that directory, otherwise it will consider only the scenarios
# specified on the command line.  It will examine each scenario's file
# and also its .boot file, if present, and create a column in the .csv
# output file.  Note that "CFLIST" is a synonym for all the scenarios in the
# CFLIST file, which allows easy comparison of those scenarios with selected
# scenarios such as BUSTED that are normally omitted from CFLIST files.

csvout=${1}
if test -z "$csvout"
then
	echo "Need .csv output file as first argument."
	exit 1
fi
shift
defaultconfigs="`tr '\012' ' ' < CFLIST`"
if test "$#" -eq 0
then
	scenariosarg=$defaultconfigs
else
	scenariosarg=$*
fi
scenarios="`echo $scenariosarg | sed -e "s/\<CFLIST\>/$defaultconfigs/g"`"

T=`mktemp -d /tmp/config2latex.sh.XXXXXX`
trap 'rm -rf $T' 0

cat << '---EOF---' >> $T/p.awk
END	{
---EOF---
for i in $scenarios
do
	echo '	s["'$i'"] = 1;' >> $T/p.awk
	grep -v '^#' < $i | grep -v '^ *$' > $T/p
	if test -r $i.boot
	then
		tr -s ' ' '\012' < $i.boot | grep -v '^#' >> $T/p
	fi
	sed -e 's/^[^=]*$/&=?/' < $T/p |
	sed -e 's/^\([^=]*\)=\(.*\)$/\tp["\1:'"$i"'"] = "\2";\n\tc["\1"] = 1;/' >> $T/p.awk
done
cat << '---EOF---' >> $T/p.awk
	ns = asorti(s, ss);
	nc = asorti(c, cs);
	for (j = 1; j <= ns; j++)
		printf ",\"%s\"", ss[j];
	printf "\n";
	for (i = 1; i <= nc; i++) {
		printf "\"%s\"", cs[i];
		for (j = 1; j <= ns; j++) {
			printf ",\"%s\"", p[cs[i] ":" ss[j]];
		}
		printf "\n";
	}
}
---EOF---
awk -f $T/p.awk < /dev/null > $T/p.csv
cp $T/p.csv $csvout
