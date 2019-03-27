# $FreeBSD$
#
# Test the -V option
#

FOO=	foo
FOOBAR=	${FOO}bar

test1:
	@echo "-V FOOBAR"
	@${MAKE} -V FOOBAR

test2:
	@echo '-V "$${FOOBAR}"'
	@${MAKE} -V '$${FOOBAR}'
