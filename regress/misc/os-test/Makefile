#	$OpenBSD: Makefile,v 1.2 2020/12/16 20:37:16 bluhm Exp $

# The following port must be installed:
#
# os-test    test suites for POSIX operating systems

# Test corner cases of few IO and a bunch of UDP network system calls
# and check result.  The test suite https://sortix.org/os-test/ is   
# used as a package and run as regress.  A html file with results is
# generated.

.if ! (make(clean) || make(cleandir) || make(obj))
.if ! exists(/usr/local/bin/os-test)
regress:
	@echo Install misc/os-test port for additional tests.
	@echo SKIPPED
.endif
.endif

regress:
	/usr/local/bin/os-test

clean:
	rm -f uname.out os-test.html
	rm -rf io io.expect udp udp.expect

.include <bsd.regress.mk>
