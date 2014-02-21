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

function parse_country_head() {
	country=$2
	sub(/:/, "", country)
	printf "static const struct ieee80211_regdomain regdom_%s = {\n", country
	printf "\t.alpha2 = \"%s\",\n", country
	if ($NF ~ /DFS-ETSI/)
		printf "\t.dfs_region = NL80211_DFS_ETSI,\n"
	else if ($NF ~ /DFS-FCC/)
		printf "\t.dfs_region = NL80211_DFS_FCC,\n"
	else if ($NF ~ /DFS-JP/)
		printf "\t.dfs_region = NL80211_DFS_JP,\n"
	printf "\t.reg_rules = {\n"
	active = 1
	regdb = regdb "\t&regdom_" country ",\n"
}

function parse_reg_rule()
{
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
	dfs_cac = $9
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
	} else {
		dfs_cac = $8
	}
	sub(/,/, "", dfs_cac)
	sub(/\(/, "", dfs_cac)
	sub(/\)/, "", dfs_cac)
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
			flags = flags "\n\t\t\tNL80211_RRF_NO_IR | "
		} else if (flagarray[arg] == "NO-IBSS") {
			flags = flags "\n\t\t\tNL80211_RRF_NO_IR | "
		} else if (flagarray[arg] == "NO-IR") {
			flags = flags "\n\t\t\tNL80211_RRF_NO_IR | "
		} else if (flagarray[arg] == "AUTO-BW") {
			flags = flags "\n\t\t\tNL80211_RRF_AUTO_BW | "
		}

	}
	flags = flags "0"
	printf "\t\tREG_RULE_EXT(%d, %d, %d, %d, %d, %d, %s),\n", start, end, bw, gain, power, dfs_cac, flags
	rules++
}

function print_tail_country()
{
	active = 0
	printf "\t},\n"
	printf "\t.n_reg_rules = %d\n", rules
	printf "};\n\n"
	rules = 0;
}

/^[ \t]*#/ {
	# Ignore
}

!active && /^[ \t]*$/ {
	# Ignore
}

!active && /country/ {
	parse_country_head()
}

active && /^[ \t]*\(/ {
	parse_reg_rule()
}

active && /^[ \t]*$/ {
	print_tail_country()
}

END {
	if (active)
		print_tail_country()
	print regdb "};"
	print ""
	print "int reg_regdb_size = ARRAY_SIZE(reg_regdb);"
}
