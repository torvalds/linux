#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

cd Documentation/

# Check entries that should be removed

obsolete=""
for i in $(tail -n +12 00-INDEX |grep -E '^[a-zA-Z0-9]+'); do
	if [ ! -e $i ]; then
		obsolete="$obsolete $i"
	fi
done

# Check directory entries that should be added
search=""
dir=""
for i in $(find . -maxdepth 1 -type d); do
	if [ "$i" != "." ]; then
		new=$(echo $i|perl -ne 's,./(.*),$1/,; print $_')
		search="$search $new"
	fi
done

for i in $search; do
	if [ "$(grep -P "^$i" 00-INDEX)" == "" ]; then
		dir="$dir $i"
	fi
done

# Check file entries that should be added
search=""
file=""
for i in $(find . -maxdepth 1 -type f); do
	if [ "$i" != "./.gitignore" ]; then
		new=$(echo $i|perl -ne 's,./(.*),$1,; print $_')
		search="$search $new"
	fi
done

for i in $search; do
	if [ "$(grep -P "^$i\$" 00-INDEX)" == "" ]; then
		file="$file $i"
	fi
done

# Output its findings

echo -e "Documentation/00-INDEX check results:\n"

if [ "$obsolete" != "" ]; then
	echo -e "- Should remove those entries:\n\t$obsolete\n"
else
	echo -e "- No obsolete entries\n"
fi

if [ "$dir" != "" ]; then
	echo -e "- Should document those directories:\n\t$dir\n"
else
	echo -e "- No new directories to add\n"
fi

if [ "$file" != "" ]; then
	echo -e "- Should document those files:\n\t$file"
else
	echo "- No new files to add"
fi
