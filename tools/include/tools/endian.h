#ifndef _TOOLS_ENDIAN_H
#define _TOOLS_ENDIAN_H

#include <byteswap.h>

#if __BYTE_ORDER == __LITTLE_ENDIAN

#ifndef htole16
#define htole16(x) (x)
#endif
#ifndef htole32
#define htole32(x) (x)
#endif
#ifndef htole64
#define htole64(x) (x)
#endif

#else /* __BYTE_ORDER */

#ifndef htole16
#define htole16(x) __bswap_16(x)
#endif
#ifndef htole32
#define htole32(x) __bswap_32(x)
#endif
#ifndef htole64
#define htole64(x) __bswap_64(x)
#endif

#endif

#endif /* _TOOLS_ENDIAN_H */
