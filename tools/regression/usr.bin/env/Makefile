# $FreeBSD$

TESTPGM?=TestProgramNotSpecifed

all:
	@echo "Note that the 'env' command uses its own regression suite,"
	@echo "which uses a single data file and a script written in ruby."
	@echo "By default it will test /usr/bin/env"
	@echo
	@ruby regress-sb.rb --rgdata=${.CURDIR}/regress-env.rgdata

#   A version which allows the user to specify which executable of `env'
#   should be tested, e.g.:    make testenv TESTPROG=/usr/bin/env-rel6
#   This will probably need a bit more thought...
testenv:
	@ruby regress-sb.rb --rgdata=${.CURDIR}/regress-env.rgdata \
	 --testpgm=${TESTPGM}
