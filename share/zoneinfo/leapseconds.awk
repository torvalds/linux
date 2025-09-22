# Generate zic format 'leapseconds' from NIST/IERS format 'leap-seconds.list'.

# This file is in the public domain.

# This program uses awk arithmetic.  POSIX requires awk to support
# exact integer arithmetic only through 10**10, which means for NTP
# timestamps this program works only to the year 2216, which is the
# year 1900 plus 10**10 seconds.  However, in practice
# POSIX-conforming awk implementations invariably use IEEE-754 double
# and so support exact integers through 2**53.  By the year 2216,
# POSIX will almost surely require at least 2**53 for awk, so for NTP
# timestamps this program should be good until the year 285,428,681
# (the year 1900 plus 2**53 seconds).  By then leap seconds will be
# long obsolete, as the Earth will likely slow down so much that
# there will be more than 25 hours per day and so some other scheme
# will be needed.

BEGIN {
  print "# Allowance for leap seconds added to each time zone file."
  print ""
  print "# This file is in the public domain."
  print ""
  print "# This file is generated automatically from the data in the public-domain"
  print "# NIST/IERS format leap-seconds.list file, which can be copied from"
  print "# <https://hpiers.obspm.fr/iers/bul/bulc/ntp/leap-seconds.list>"
  print "# or, in a variant with different comments, from"
  print "# <ftp://ftp.boulder.nist.gov/pub/time/leap-seconds.list>."
  print "# For more about leap-seconds.list, please see"
  print "# The NTP Timescale and Leap Seconds"
  print "# <https://www.eecis.udel.edu/~mills/leap.html>."
  print ""
  print "# The rules for leap seconds are specified in Annex 1 (Time scales) of:"
  print "# Standard-frequency and time-signal emissions."
  print "# International Telecommunication Union - Radiocommunication Sector"
  print "# (ITU-R) Recommendation TF.460-6 (02/2002)"
  print "# <https://www.itu.int/rec/R-REC-TF.460-6-200202-I/>."
  print "# The International Earth Rotation and Reference Systems Service (IERS)"
  print "# periodically uses leap seconds to keep UTC to within 0.9 s of UT1"
  print "# (a proxy for Earth's angle in space as measured by astronomers)"
  print "# and publishes leap second data in a copyrighted file"
  print "# <https://hpiers.obspm.fr/iers/bul/bulc/Leap_Second.dat>."
  print "# See: Levine J. Coordinated Universal Time and the leap second."
  print "# URSI Radio Sci Bull. 2016;89(4):30-6. doi:10.23919/URSIRSB.2016.7909995"
  print "# <https://ieeexplore.ieee.org/document/7909995>."
  print ""
  print "# There were no leap seconds before 1972, as no official mechanism"
  print "# accounted for the discrepancy between atomic time (TAI) and the earth's"
  print "# rotation.  The first (\"1 Jan 1972\") data line in leap-seconds.list"
  print "# does not denote a leap second; it denotes the start of the current definition"
  print "# of UTC."
  print ""
  print "# All leap-seconds are Stationary (S) at the given UTC time."
  print "# The correction (+ or -) is made at the given time, so in the unlikely"
  print "# event of a negative leap second, a line would look like this:"
  print "# Leap	YEAR	MON	DAY	23:59:59	-	S"
  print "# Typical lines look like this:"
  print "# Leap	YEAR	MON	DAY	23:59:60	+	S"

  monthabbr[ 1] = "Jan"
  monthabbr[ 2] = "Feb"
  monthabbr[ 3] = "Mar"
  monthabbr[ 4] = "Apr"
  monthabbr[ 5] = "May"
  monthabbr[ 6] = "Jun"
  monthabbr[ 7] = "Jul"
  monthabbr[ 8] = "Aug"
  monthabbr[ 9] = "Sep"
  monthabbr[10] = "Oct"
  monthabbr[11] = "Nov"
  monthabbr[12] = "Dec"

  sstamp_init()
}

# In case the input has CRLF form a la NIST.
{ sub(/\r$/, "") }

/^#[ \t]*[Uu]pdated through/ || /^#[ \t]*[Ff]ile expires on/ {
    last_lines = last_lines $0 "\n"
}

/^#[$][ \t]/ { updated = $2 }
/^#[@][ \t]/ { expires = $2 }

/^[ \t]*#/ { next }

{
    NTP_timestamp = $1
    TAI_minus_UTC = $2
    if (old_TAI_minus_UTC) {
	if (old_TAI_minus_UTC < TAI_minus_UTC) {
	    sign = "23:59:60\t+"
	} else {
	    sign = "23:59:59\t-"
	}
	sstamp_to_ymdhMs(NTP_timestamp - 1, ss_NTP)
	printf "Leap\t%d\t%s\t%d\t%s\tS\n", \
	  ss_year, monthabbr[ss_month], ss_mday, sign
    }
    old_TAI_minus_UTC = TAI_minus_UTC
}

END {
    print ""

    if (expires) {
      sstamp_to_ymdhMs(expires, ss_NTP)

      print "# UTC timestamp when this leap second list expires."
      print "# Any additional leap seconds will come after this."
      if (! EXPIRES_LINE) {
	print "# This Expires line is commented out for now,"
	print "# so that pre-2020a zic implementations do not reject this file."
      }
      printf "%sExpires %.4d\t%s\t%.2d\t%.2d:%.2d:%.2d\n", \
	EXPIRES_LINE ? "" : "#", \
	ss_year, monthabbr[ss_month], ss_mday, ss_hour, ss_min, ss_sec
    } else {
      print "# (No Expires line, since the expires time is unknown.)"
    }

    # The difference between the NTP and POSIX epochs is 70 years
    # (including 17 leap days), each 24 hours of 60 minutes of 60
    # seconds each.
    epoch_minus_NTP = ((1970 - 1900) * 365 + 17) * 24 * 60 * 60

    print ""
    print "# POSIX timestamps for the data in this file:"
    if (updated) {
      sstamp_to_ymdhMs(updated, ss_NTP)
      printf "#updated %d (%.4d-%.2d-%.2d %.2d:%.2d:%.2d UTC)\n", \
	updated - epoch_minus_NTP, \
	ss_year, ss_month, ss_mday, ss_hour, ss_min, ss_sec
    } else {
      print "#(updated time unknown)"
    }
    if (expires) {
      sstamp_to_ymdhMs(expires, ss_NTP)
      printf "#expires %d (%.4d-%.2d-%.2d %.2d:%.2d:%.2d UTC)\n", \
	expires - epoch_minus_NTP, \
	ss_year, ss_month, ss_mday, ss_hour, ss_min, ss_sec
    } else {
      print "#(expires time unknown)"
    }
    printf "\n%s", last_lines
}

# sstamp_to_ymdhMs - convert seconds timestamp to date and time
#
# Call as:
#
#    sstamp_to_ymdhMs(sstamp, epoch_days)
#
# where:
#
#    sstamp - is the seconds timestamp.
#    epoch_days - is the timestamp epoch in Gregorian days since 1600-03-01.
#	ss_NTP is appropriate for an NTP sstamp.
#
# Both arguments should be nonnegative integers.
# On return, the following variables are set based on sstamp:
#
#    ss_year	- Gregorian calendar year
#    ss_month	- month of the year (1-January to 12-December)
#    ss_mday	- day of the month (1-31)
#    ss_hour	- hour (0-23)
#    ss_min	- minute (0-59)
#    ss_sec	- second (0-59)
#    ss_wday	- day of week (0-Sunday to 6-Saturday)
#
# The function sstamp_init should be called prior to using sstamp_to_ymdhMs.

function sstamp_init()
{
  # Days in month N, where March is month 0 and January month 10.
  ss_mon_days[ 0] = 31
  ss_mon_days[ 1] = 30
  ss_mon_days[ 2] = 31
  ss_mon_days[ 3] = 30
  ss_mon_days[ 4] = 31
  ss_mon_days[ 5] = 31
  ss_mon_days[ 6] = 30
  ss_mon_days[ 7] = 31
  ss_mon_days[ 8] = 30
  ss_mon_days[ 9] = 31
  ss_mon_days[10] = 31

  # Counts of days in a Gregorian year, quad-year, century, and quad-century.
  ss_year_days = 365
  ss_quadyear_days = ss_year_days * 4 + 1
  ss_century_days = ss_quadyear_days * 25 - 1
  ss_quadcentury_days = ss_century_days * 4 + 1

  # Standard day epochs, suitable for epoch_days.
  # ss_MJD = 94493
  # ss_POSIX = 135080
  ss_NTP = 109513
}

function sstamp_to_ymdhMs(sstamp, epoch_days, \
			  quadcentury, century, quadyear, year, month, day)
{
  ss_hour = int(sstamp / 3600) % 24
  ss_min = int(sstamp / 60) % 60
  ss_sec = sstamp % 60

  # Start with a count of days since 1600-03-01 Gregorian.
  day = epoch_days + int(sstamp / (24 * 60 * 60))

  # Compute a year-month-day date with days of the month numbered
  # 0-30, months (March-February) numbered 0-11, and years that start
  # start March 1 and end after the last day of February.  A quad-year
  # starts on March 1 of a year evenly divisible by 4 and ends after
  # the last day of February 4 years later.  A century starts on and
  # ends before March 1 in years evenly divisible by 100.
  # A quad-century starts on and ends before March 1 in years divisible
  # by 400.  While the number of days in a quad-century is a constant,
  # the number of days in each other time period can vary by 1.
  # Any variation is in the last day of the time period (there might
  # or might not be a February 29) where it is easy to deal with.

  quadcentury = int(day / ss_quadcentury_days)
  day -= quadcentury * ss_quadcentury_days
  ss_wday = (day + 3) % 7
  century = int(day / ss_century_days)
  century -= century == 4
  day -= century * ss_century_days
  quadyear = int(day / ss_quadyear_days)
  day -= quadyear * ss_quadyear_days
  year = int(day / ss_year_days)
  year -= year == 4
  day -= year * ss_year_days
  for (month = 0; month < 11; month++) {
    if (day < ss_mon_days[month])
      break
    day -= ss_mon_days[month]
  }

  # Convert the date to a conventional day of month (1-31),
  # month (1-12, January-December) and Gregorian year.
  ss_mday = day + 1
  if (month <= 9) {
    ss_month = month + 3
  } else {
    ss_month = month - 9
    year++
  }
  ss_year = 1600 + quadcentury * 400 + century * 100 + quadyear * 4 + year
}
