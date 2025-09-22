/* $OpenBSD: expat_config.h,v 1.6 2024/02/11 00:56:28 bluhm Exp $ */

/* quick and dirty conf for OpenBSD */

#define HAVE_ARC4RANDOM 1
#define HAVE_ARC4RANDOM_BUF 1
#define XML_CONTEXT_BYTES 1024
#define XML_DTD 1
#define XML_GE 1
#define XML_NS 1

#include <endian.h>
#if BYTE_ORDER == LITTLE_ENDIAN
#define BYTEORDER 1234
#elif BYTE_ORDER == BIG_ENDIAN
#define BYTEORDER 4321
#else
#error "unknown byte order"
#endif
