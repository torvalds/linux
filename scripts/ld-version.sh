#!/usr/bin/awk -f
# extract linker version number from stdin and turn into single number
	{
	gsub(".*\\)", "");
	gsub(".*version ", "");
	gsub("-.*", "");
	split($1,a, ".");
	print a[1]*100000000 + a[2]*1000000 + a[3]*10000;
	exit
	}
