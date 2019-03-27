# $FreeBSD$

.include <bsd.init.mk>

LIB=		clang_rt.ubsan_standalone_cxx-${CRTARCH}

CXXFLAGS+=	-frtti

SRCS+=		ubsan/ubsan_handlers_cxx.cc
SRCS+=		ubsan/ubsan_type_hash.cc
SRCS+=		ubsan/ubsan_type_hash_itanium.cc

.include <bsd.lib.mk>
