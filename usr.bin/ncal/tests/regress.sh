# $FreeBSD$

CAL_BIN="ncal"
CAL="${CAL_BIN} -C"
NCAL="${CAL_BIN} -N"
YEARS="2008 2009 2010 2011"
ONEYEAR="2009"

echo 1..89

REGRESSION_START($1)

#
# The first tests are layout tests, to make sure that the output is still the
# same despite varying months.
#

# Full year calendars

for y in ${YEARS}; do
	# Regular calendar, Month days, No-highlight
	REGRESSION_TEST(`r-y${y}-md-nhl', `$NCAL -h ${y}')
	# Backwards calendar, Month days, No-highlight
	REGRESSION_TEST(`b-y${y}-md-nhl', `$CAL -h ${y}')
	# Regular calendar, Julian days, No-highlight
	REGRESSION_TEST(`r-y${y}-jd-nhl', `$NCAL -jh ${y}')
	# Backwards calendar, Julian days, No-highlight
	REGRESSION_TEST(`b-y${y}-jd-nhl', `$CAL -jh ${y}')
done

# 3 month calendars

for m in $(jot -w %02d 12); do
	# Regular calendar, Month days, No-highlight
	REGRESSION_TEST(`r-3m${ONEYEAR}${m}-md-nhl',
	    `$NCAL -h3 ${m} ${ONEYEAR}')
	# Backwards calendar, Month days, No-highlight
	REGRESSION_TEST(`b-3m${ONEYEAR}${m}-md-nhl', `$CAL -h3 ${m} ${ONEYEAR}')
	# Regular calendar, Julian days, No-highlight
	REGRESSION_TEST(`r-3m${ONEYEAR}${m}-jd-nhl',
	    `$NCAL -jh3 ${m} ${ONEYEAR}')
	# Backwards calendar, Julian days, No-highlight
	REGRESSION_TEST(`b-3m${ONEYEAR}${m}-jd-nhl', `$CAL -jh3 ${m} ${ONEYEAR}')
done

#
# The next tests are combinations of the various arguments.
#

# These should fail
REGRESSION_TEST(`f-3y-nhl',  `$NCAL -3 -y 2>&1')
REGRESSION_TEST(`f-3A-nhl',  `$NCAL -3 -A 3 2>&1')
REGRESSION_TEST(`f-3B-nhl',  `$NCAL -3 -B 3 2>&1')
REGRESSION_TEST(`f-3gy-nhl', `$NCAL -3 2008 2>&1')
REGRESSION_TEST(`f-3AB-nhl', `$NCAL -3 -A 3 -B 3 2>&1')
REGRESSION_TEST(`f-mgm-nhl', `$NCAL -m 3 2 2008 2>&1')
REGRESSION_TEST(`f-ym-nhl',  `$NCAL -y -m 2 2>&1')
REGRESSION_TEST(`f-ygm-nhl', `$NCAL -y 2 2008 2>&1')
REGRESSION_TEST(`f-yA-nhl',  `$NCAL -y -A 3 2>&1')
REGRESSION_TEST(`f-yB-nhl',  `$NCAL -y -B 3 2>&1')
REGRESSION_TEST(`f-yAB-nhl', `$NCAL -y -A 3 -B 3 2>&1')

# These should be successful

REGRESSION_TEST(`s-b-3-nhl',    `$CAL -d 2008.03 -3')
REGRESSION_TEST(`s-b-A-nhl',    `$CAL -d 2008.03 -A 1')
REGRESSION_TEST(`s-b-B-nhl',    `$CAL -d 2008.03 -B 1')
REGRESSION_TEST(`s-b-AB-nhl',   `$CAL -d 2008.03 -A 1 -B 1')
REGRESSION_TEST(`s-b-m-nhl',    `$CAL -d 2008.03 -m 1')
REGRESSION_TEST(`s-b-mgy-nhl',  `$CAL -d 2008.03 -m 1 2007')
REGRESSION_TEST(`s-b-gmgy-nhl', `$CAL -d 2008.03 1 2007')
REGRESSION_TEST(`s-r-3-nhl',    `$NCAL -d 2008.03 -3')
REGRESSION_TEST(`s-r-A-nhl',    `$NCAL -d 2008.03 -A 1')
REGRESSION_TEST(`s-r-B-nhl',    `$NCAL -d 2008.03 -B 1')
REGRESSION_TEST(`s-r-AB-nhl',   `$NCAL -d 2008.03 -A 1 -B 1')
REGRESSION_TEST(`s-r-m-nhl',    `$NCAL -d 2008.03 -m 1')
REGRESSION_TEST(`s-r-mgy-nhl',  `$NCAL -d 2008.03 -m 1 2007')
REGRESSION_TEST(`s-r-gmgy-nhl', `$NCAL -d 2008.03 1 2007')

REGRESSION_END()
