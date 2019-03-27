# $FreeBSD$

.include <bsd.init.mk>

.PATH:	${GOOGLETEST_SRCROOT}/include ${GOOGLETEST_SRCROOT}/src

LIB_CXX=	gtest
SHLIB_MAJOR=	0
PRIVATELIB=	true

CXXFLAGS+=	-I${GOOGLETEST_SRCROOT}/include
CXXFLAGS+=	-I${GOOGLETEST_SRCROOT}

INCSGROUPS=	MAIN_INCS INTERNAL_INCS INTERNAL_CUSTOM_INCS

MAIN_INCSDIR=	${INCLUDEDIR}${PRIVATELIB:D/private/${LIB}}
MAIN_INCS+=	gtest/gtest-death-test.h
MAIN_INCS+=	gtest/gtest-message.h
MAIN_INCS+=	gtest/gtest-param-test.h
MAIN_INCS+=	gtest/gtest-printers.h
MAIN_INCS+=	gtest/gtest-spi.h
MAIN_INCS+=	gtest/gtest-test-part.h
MAIN_INCS+=	gtest/gtest-typed-test.h
MAIN_INCS+=	gtest/gtest.h
MAIN_INCS+=	gtest/gtest_pred_impl.h
MAIN_INCS+=	gtest/gtest_prod.h

INTERNAL_INCSDIR=	${MAIN_INCSDIR}/internal
INTERNAL_INCS+=	gtest/internal/gtest-death-test-internal.h
INTERNAL_INCS+=	gtest/internal/gtest-filepath.h
INTERNAL_INCS+=	gtest/internal/gtest-internal.h
INTERNAL_INCS+=	gtest/internal/gtest-linked_ptr.h
INTERNAL_INCS+=	gtest/internal/gtest-param-util-generated.h
INTERNAL_INCS+=	gtest/internal/gtest-param-util.h
INTERNAL_INCS+=	gtest/internal/gtest-port-arch.h
INTERNAL_INCS+=	gtest/internal/gtest-port.h
INTERNAL_INCS+=	gtest/internal/gtest-string.h
INTERNAL_INCS+=	gtest/internal/gtest-tuple.h
INTERNAL_INCS+=	gtest/internal/gtest-type-util.h

INTERNAL_CUSTOM_INCSDIR= ${INTERNAL_INCSDIR}/custom
INTERNAL_CUSTOM_INCS+=	gtest/internal/custom/gtest-port.h
INTERNAL_CUSTOM_INCS+=	gtest/internal/custom/gtest-printers.h
INTERNAL_CUSTOM_INCS+=	gtest/internal/custom/gtest.h

SRCS+=		gtest-all.cc

LIBADD+=	pthread

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.lib.mk>
