# $FreeBSD$

.include <bsd.init.mk>

.PATH:	${GOOGLEMOCK_SRCROOT}/src ${GOOGLEMOCK_SRCROOT}/test

GTESTS+=	gmock-actions_test
GTESTS+=	gmock-cardinalities_test
GTESTS+=	gmock_ex_test
GTESTS+=	gmock-generated-actions_test
GTESTS+=	gmock-generated-function-mockers_test
GTESTS+=	gmock-generated-internal-utils_test
GTESTS+=	gmock-generated-matchers_test
GTESTS+=	gmock-internal-utils_test
GTESTS+=	gmock-matchers_test
GTESTS+=	gmock-more-actions_test
GTESTS+=	gmock-nice-strict_test
GTESTS+=	gmock-port_test
GTESTS+=	gmock-spec-builders_test
GTESTS+=	gmock_link_test
GTESTS+=	gmock_test

CXXFLAGS+=	-I${GOOGLEMOCK_SRCROOT}/include
CXXFLAGS+=	-I${GOOGLEMOCK_SRCROOT}
CXXFLAGS+=	-I${GOOGLETEST_SRCROOT}/include
CXXFLAGS+=	-I${GOOGLETEST_SRCROOT}

SRCS.gmock_link_test=		\
	gmock_link_test.cc	\
	gmock_link2_test.cc

LIBADD=		gmock_main gmock gtest

# The next release will resolve a number of build warnings issues.
NO_WERROR=

.include <bsd.test.mk>
