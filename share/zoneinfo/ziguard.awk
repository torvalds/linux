# Convert tzdata source into vanguard or rearguard form.

# Contributed by Paul Eggert.  This file is in the public domain.

# This is not a general-purpose converter; it is designed for current tzdata.
# It just converts from current source to main, vanguard, and rearguard forms.
# Although it might be nice for it to be idempotent, or to be useful
# for converting back and forth between formats,
# it does not do these nonessential tasks now.
#
# This script can convert from main to vanguard form and vice versa.
# There is no need to convert rearguard to other forms.
#
# When converting to vanguard form, the output can use the line
# "Zone GMT 0 - GMT" which TZUpdater 2.3.2 mistakenly rejects.
#
# When converting to vanguard form, the output can use negative SAVE
# values.
#
# When converting to rearguard form, the output uses only nonnegative
# SAVE values.  The idea is for the output data to simulate the behavior
# of the input data as best it can within the constraints of the
# rearguard format.

# Given a FIELD like "-0:30", return a minute count like -30.
function get_minutes(field, \
		     sign, hours, minutes)
{
  sign = field ~ /^-/ ? -1 : 1
  hours = +field
  if (field ~ /:/) {
    minutes = field
    sub(/[^:]*:/, "", minutes)
  }
  return 60 * hours + sign * minutes
}

# Given an OFFSET, which is a minute count like 300 or 330,
# return a %z-style abbreviation like "+05" or "+0530".
function offset_abbr(offset, \
		     hours, minutes, sign)
{
  hours = int(offset / 60)
  minutes = offset % 60
  if (minutes) {
    return sprintf("%+.4d", hours * 100 + minutes);
  } else {
    return sprintf("%+.2d", hours)
  }
}

# Round TIMESTAMP (a +-hh:mm:ss.dddd string) to the nearest second.
function round_to_second(timestamp, \
			 hh, mm, ss, seconds, dot_dddd, subseconds)
{
  dot_dddd = timestamp
  if (!sub(/^[+-]?[0-9]+:[0-9]+:[0-9]+\./, ".", dot_dddd))
    return timestamp
  hh = mm = ss = timestamp
  sub(/^[-+]?[0-9]+:[0-9]+:/, "", ss)
  sub(/^[-+]?[0-9]+:/, "", mm)
  sub(/^[-+]?/, "", hh)
  seconds = 3600 * hh + 60 * mm + ss
  subseconds = +dot_dddd
  seconds += 0.5 < subseconds || ((subseconds == 0.5) && (seconds % 2));
  return sprintf("%s%d:%.2d:%.2d", timestamp ~ /^-/ ? "-" : "", \
		 seconds / 3600, seconds / 60 % 60, seconds % 60)
}

BEGIN {
  dataform_type["vanguard"] = 1
  dataform_type["main"] = 1
  dataform_type["rearguard"] = 1

  if (PACKRATLIST) {
    while (getline <PACKRATLIST) {
      if ($0 ~ /^#/) continue
      packratlist[$3] = 1
    }
  }

  # The command line should set DATAFORM.
  if (!dataform_type[DATAFORM]) exit 1
}

$1 == "#PACKRATLIST" && $2 == PACKRATLIST {
  sub(/^#PACKRATLIST[\t ]+[^\t ]+[\t ]+/, "")
}

/^Zone/ { zone = $2 }

DATAFORM != "main" {
  in_comment = $0 ~ /^#/
  uncomment = comment_out = 0

  # If this line should differ due to Czechoslovakia using negative SAVE values,
  # uncomment the desired version and comment out the undesired one.
  if (zone == "Europe/Prague" && $0 ~ /^#?[\t ]+[01]:00[\t ]/ \
      && $0 ~ /1947 Feb 23/) {
    if (($(in_comment + 2) != "-") == (DATAFORM != "rearguard")) {
      uncomment = in_comment
    } else {
      comment_out = !in_comment
    }
  }

  # If this line should differ due to Ireland using negative SAVE values,
  # uncomment the desired version and comment out the undesired one.
  Rule_Eire = $0 ~ /^#?Rule[\t ]+Eire[\t ]/
  Zone_Dublin_post_1968 \
    = (zone == "Europe/Dublin" && $0 ~ /^#?[\t ]+[01]:00[\t ]/ \
       && (!$(in_comment + 4) || 1968 < $(in_comment + 4)))
  if (Rule_Eire || Zone_Dublin_post_1968) {
    if ((Rule_Eire \
	 || (Zone_Dublin_post_1968 && $(in_comment + 3) == "IST/GMT"))	\
	== (DATAFORM != "rearguard")) {
      uncomment = in_comment
    } else {
      comment_out = !in_comment
    }
  }

  # If this line should differ due to Namibia using negative SAVE values,
  # uncomment the desired version and comment out the undesired one.
  Rule_Namibia = $0 ~ /^#?Rule[\t ]+Namibia[\t ]/
  Zone_using_Namibia_rule \
    = (zone == "Africa/Windhoek" && $0 ~ /^#?[\t ]+[12]:00[\t ]/ \
       && ($(in_comment + 2) == "Namibia" \
	   || ($(in_comment + 2) == "-" && $(in_comment + 3) == "CAT" \
	       && ((1994 <= $(in_comment + 4) && $(in_comment + 4) <= 2017) \
		   || in_comment + 3 == NF))))
  if (Rule_Namibia || Zone_using_Namibia_rule) {
    if ((Rule_Namibia \
	 ? ($9 ~ /^-/ || ($9 == 0 && $10 == "CAT")) \
	 : $(in_comment + 1) == "2:00" && $(in_comment + 2) == "Namibia") \
	== (DATAFORM != "rearguard")) {
      uncomment = in_comment
    } else {
      comment_out = !in_comment
    }
  }

  # If this line should differ due to Portugal benefiting from %z if supported,
  # comment out the undesired version and uncomment the desired one.
  if ($0 ~ /^#?[\t ]+-[12]:00[\t ]+((Port|W-Eur)[\t ]+[%+-]|-[\t ]+(%z|-01)[\t ]+1982 Mar 28)/) {
    if (($0 ~ /%z/) == (DATAFORM == "rearguard")) {
      comment_out = !in_comment
    } else {
      uncomment = in_comment
    }
  }

  # In vanguard form, use the line "Zone GMT 0 - GMT" instead of
  # "Zone Etc/GMT 0 - GMT" and adjust Link lines accordingly.
  # This works around a bug in TZUpdater 2.3.2.
  if (/^#?(Zone|Link)[\t ]+(Etc\/)?GMT[\t ]/) {
    if (($2 == "GMT") == (DATAFORM == "vanguard")) {
      uncomment = in_comment
    } else {
      comment_out = !in_comment
    }
  }

  if (uncomment) {
    sub(/^#/, "")
  }
  if (comment_out) {
    sub(/^/, "#")
  }

  # Prefer explicit abbreviations in rearguard form, %z otherwise.
  if (DATAFORM == "rearguard") {
    if ($0 ~ /^[^#]*%z/) {
      stdoff_column = 2 * ($0 ~ /^Zone/) + 1
      rules_column = stdoff_column + 1
      stdoff = get_minutes($stdoff_column)
      rules = $rules_column
      stdabbr = offset_abbr(stdoff)
      if (rules == "-") {
	abbr = stdabbr
      } else {
	dstabbr_only = rules ~ /^[+0-9-]/
	if (dstabbr_only) {
	  dstoff = get_minutes(rules)
	} else {
	  # The DST offset is normally an hour, but there are special cases.
	  if (rules == "Morocco" && NF == 3) {
	    dstoff = -60
	  } else if (rules == "NBorneo") {
	    dstoff = 20
	  } else if (((rules == "Cook" || rules == "LH") && NF == 3) \
		     || (rules == "Uruguay" \
			 && $0 ~ /[\t ](1942 Dec 14|1960|1970|1974 Dec 22)$/)) {
	    dstoff = 30
	  } else if (rules == "Uruguay" && $0 ~ /[\t ]1974 Mar 10$/) {
	    dstoff = 90
	  } else {
	    dstoff = 60
	  }
	}
	dstabbr = offset_abbr(stdoff + dstoff)
	if (dstabbr_only) {
	  abbr = dstabbr
	} else {
	  abbr = stdabbr "/" dstabbr
	}
      }
      sub(/%z/, abbr)
    }
  } else {
    sub(/^(Zone[\t ]+[^\t ]+)?[\t ]+[^\t ]+[\t ]+[^\t ]+[\t ]+[-+][^\t ]+/, \
	"&CHANGE-TO-%z")
    sub(/-00CHANGE-TO-%z/, "-00")
    sub(/[-+][^\t ]+CHANGE-TO-/, "")
  }

  # Normally, prefer whole seconds.  However, prefer subseconds
  # if generating vanguard form and the otherwise-undocumented
  # VANGUARD_SUBSECONDS environment variable is set.
  # This relies on #STDOFF comment lines in the data.
  # It is for hypothetical clients that support UT offsets that are
  # not integer multiples of one second (e.g., Europe/Lisbon, 1884 to 1912).
  # No known clients need this currently, and this experimental
  # feature may be changed or withdrawn in future releases.
  if ($1 == "#STDOFF") {
    stdoff = $2
    rounded_stdoff = round_to_second(stdoff)
    if (DATAFORM == "vanguard" && ENVIRON["VANGUARD_SUBSECONDS"]) {
      stdoff_subst[0] = rounded_stdoff
      stdoff_subst[1] = stdoff
    } else {
      stdoff_subst[0] = stdoff
      stdoff_subst[1] = rounded_stdoff
    }
  } else if (stdoff_subst[0]) {
    stdoff_column = 2 * ($0 ~ /^Zone/) + 1
    stdoff_column_val = $stdoff_column
    if (stdoff_column_val == stdoff_subst[0]) {
      sub(stdoff_subst[0], stdoff_subst[1])
    } else if (stdoff_column_val != stdoff_subst[1]) {
      stdoff_subst[0] = 0
    }
  }

  # In rearguard form, change the Japan rule line with "Sat>=8 25:00"
  # to "Sun>=9 1:00", to cater to zic before 2007 and to older Java.
  if ($0 ~ /^Rule/ && $2 == "Japan") {
    if (DATAFORM == "rearguard") {
      if ($7 == "Sat>=8" && $8 == "25:00") {
	sub(/Sat>=8/, "Sun>=9")
	sub(/25:00/, " 1:00")
      }
    } else {
      if ($7 == "Sun>=9" && $8 == "1:00") {
	sub(/Sun>=9/, "Sat>=8")
	sub(/ 1:00/, "25:00")
      }
    }
  }

  # In rearguard form, change the Morocco lines with negative SAVE values
  # to use positive SAVE values.
  if ($2 == "Morocco") {
    if ($0 ~ /^Rule/) {
      if ($4 ~ /^201[78]$/ && $6 == "Oct") {
	if (DATAFORM == "rearguard") {
	  sub(/\t2018\t/, "\t2017\t")
	} else {
	  sub(/\t2017\t/, "\t2018\t")
	}
      }

      if (2019 <= $3) {
	if ($8 == "2:00") {
	  if (DATAFORM == "rearguard") {
	    sub(/\t0\t/, "\t1:00\t")
	  } else {
	    sub(/\t1:00\t/, "\t0\t")
	  }
	} else {
	  if (DATAFORM == "rearguard") {
	    sub(/\t-1:00\t/, "\t0\t")
	  } else {
	    sub(/\t0\t/, "\t-1:00\t")
	  }
	}
      }
    }
    if ($1 ~ /^[+0-9-]/ && NF == 3) {
      if (DATAFORM == "rearguard") {
	sub(/1:00\tMorocco/, "0:00\tMorocco")
	sub(/\t\+01\/\+00$/, "\t+00/+01")
      } else {
	sub(/0:00\tMorocco/, "1:00\tMorocco")
	sub(/\t\+00\/+01$/, "\t+01/+00")
      }
    }
  }
}

/^Zone/ {
  packrat_ignored = FILENAME == PACKRATDATA && PACKRATLIST && !packratlist[$2];
}
{
  if (packrat_ignored && $0 !~ /^Rule/) {
    sub(/^/, "#")
  }
}

# Return a link line resulting by changing OLDLINE to link to TARGET
# from LINKNAME, instead of linking to OLDTARGET from LINKNAME.
# Align data columns the same as they were in OLDLINE.
# Also, replace any existing white space followed by comment with COMMENT.
function make_linkline(oldline, target, linkname, oldtarget, comment, \
		       oldprefix, oldprefixlen, oldtargettabs, \
		       replsuffix, targettabs)
{
  oldprefix = "Link\t" oldtarget "\t"
  oldprefixlen = length(oldprefix)
  if (substr(oldline, 1, oldprefixlen) == oldprefix) {
    # Use tab stops to preserve LINKNAME's column.
    replsuffix = substr(oldline, oldprefixlen + 1)
    sub(/[\t ]*#.*/, "", replsuffix)
    oldtargettabs = int(length(oldtarget) / 8) + 1
    targettabs = int(length(target) / 8) + 1
    for (; targettabs < oldtargettabs; targettabs++) {
      replsuffix = "\t" replsuffix
    }
    for (; oldtargettabs < targettabs && replsuffix ~ /^\t/; targettabs--) {
      replsuffix = substr(replsuffix, 2)
    }
  } else {
    # Odd format line; don't bother lining up its replacement nicely.
    replsuffix = linkname
  }
  return "Link\t" target "\t" replsuffix comment
}

/^Link/ && $4 == "#=" && DATAFORM == "vanguard" {
  $0 = make_linkline($0, $5, $3, $2)
}

# If a Link line is followed by a Link or Zone line for the same data, comment
# out the Link line.  This can happen if backzone overrides a Link
# with a Zone or a different Link.
/^Zone/ {
  sub(/^Link/, "#Link", line[linkline[$2]])
}
/^Link/ {
  sub(/^Link/, "#Link", line[linkline[$3]])
  linkline[$3] = NR
  linktarget[$3] = $2
}

{ line[NR] = $0 }

function cut_link_chains_short( \
			       l, linkname, t, target)
{
  for (linkname in linktarget) {
    target = linktarget[linkname]
    t = linktarget[target]
    if (t) {
      # TARGET is itself a link name.  Replace the line "Link TARGET LINKNAME"
      # with "Link T LINKNAME #= TARGET", where T is at the end of the chain
      # of links that LINKNAME points to.
      while ((u = linktarget[t])) {
	t = u
      }
      l = linkline[linkname]
      line[l] = make_linkline(line[l], t, linkname, target, "\t#= " target)
    }
  }
}

END {
  if (DATAFORM != "vanguard") {
    cut_link_chains_short()
  }
  for (i = 1; i <= NR; i++)
    print line[i]
}
