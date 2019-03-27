# $FreeBSD$

# Common flags to build lua related files

CFLAGS+=	-I${LUASRC} -I${LDRSRC} -I${LIBLUASRC}
CFLAGS+=	-DLUA_FLOAT_TYPE=LUA_FLOAT_INT64
