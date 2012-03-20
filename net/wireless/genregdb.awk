#!/usr/bin/awk -f
#
# genregdb.awk -- generate regdb.c from db.txt
#
# Actually, it reads from stdin (presumed to be db.txt) and writes
# to stdout (presumed to be regdb.c), but close enough...
#
# Copyright 2009 John W. Linville <linville@tuxdriver.com>
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

BEGIN {
	active = 0
	rules = 0;
	print "/*"
	print " * DO NOT EDIT -- file generated from data in db.txt"
	print " */"
	print ""
	print "#include <linux/nl80211.h>"
	print "#include <net/cfg80211.h>"
	print "#include \"regdb.h\""
	print ""
	regdb = "const struct ieee80211_regdomain *reg_regdb[] = {\n"
}

/^[ \t]*#/ {
	# Ignore
}

!active && /^[ \t]*$/ {
	# Ignore
}

!active && /country/ {
	country=$2
	sub(/:/, "", country)
	printf "static const struct ieee80211_regdomain regdom_%s = {\n", country
	printf "\t.alpha2 = \"%s\",\n", country
	printf "\t.reg_rules = {\n"
	active = 1
	regdb = regdb "\t&regdom_" country ",\n"
}

active && /^[ \t]*\(/ {
	start = $1
	sub(/\(/, "", start)
	end = $3
	bw = $5
	sub(/\),/, "", bw)
	gain = $6
	sub(/\(/, "", gain)
	sub(/,/, "", gain)
	power = $7
	sub(/\)/, "", power)
	sub(/,/, "", power)
	# power might be in mW...
	units = $8
	sub(/\)/, "", units)
	sub(/,/, "", units)
	if (units == "mW") {
		if (power == 100) {
			power = 20
		} else if (power == 200) {
			power = 23
		} else if (power == 500) {
			power = 27
		} else if (power == 1000) {
			power = 30
		} else {
			print "Unknown power value in database!"
		}
	}
	flagstr = ""
	for (i=8; i<=NF; i++)
		flagstr = flagstr $i
	split(flagstr, flagarray, ",")
	flags = ""
	for (arg in flagarray) {
		if (flagarray[arg] == "NO-OFDM") {
			flags = flags "\n\t\t\tNL80211_RRF_NO_OFDM | "
		} else if (flagarray[arg] == "NO-CCK") {
			flags = flags "\n\t\t\tNL80211_RRF_NO_CCK | "
		} else if (flagarray[arg] == "NO-INDOOR") {
			flags = flags "\n\t\t\tNL80211_RRF_NO_INDOOR | "
		} else if (flagarray[arg] == "NO-OUTDOOR") {
			flags = flags "\n\t\t\tNL80211_RRF_NO_OUTDOOR | "
		} else if (flagarray[arg] == "DFS") {
			flags = flags "\n\t\t\tNL80211_RRF_DFS | "
		} else if (flagarray[arg] == "PTP-ONLY") {
			flags = flags "\n\t\t\tNL80211_RRF_PTP_ONLY | "
		} else if (flagarray[arg] == "PTMP-ONLY") {
			flags = flags "\n\t\t\tNL80211_RRF_PTMP_ONLY | "
		} else if (flagarray[arg] == "PASSIVE-SCAN") {
			flags = flags "\n\t\t\tNL80211_RRF_PASSIVE_SCAN | "
		} else if (flagarray[arg] == "NO-IBSS") {
			flags = flags "\n\t\t\tNL80211_RRF_NO_IBSS | "
		}
	}
	flags = flags "0"
	printf "\t\tREG_RULE(%d, %d, %d, %d, %d, %s),\n", start, end, bw, gain, power, flags
	rules++
}

active && /^[ \t]*$/ {
	active = 0
	printf "\t},\n"
	printf "\t.n_reg_rules = %d\n", rules
	printf "};\n\n"
	rules = 0;
}

END {
	print regdb "};"
	print ""
	print "int reg_regdb_size = ARRAY_SIZE(reg_regdb);"
}
