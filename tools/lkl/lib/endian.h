#ifndef _LKL_LIB_ENDIAN_H
#define _LKL_LIB_ENDIAN_H

#ifndef __MINGW32__
#ifdef __FreeBSD__
#include <sys/endian.h>
#else
#include <endian.h>
#endif  /* __FreeBSD__ */
#else  /* !__MINGW32__ */
#include <winsock.h>
#define le32toh(x) (x)
#define le16toh(x) (x)
#define htole32(x) (x)
#define htole16(x) (x)
#define le64toh(x) (x)
#define htobe32(x) htonl(x)
#define htobe16(x) htons(x)
#define be32toh(x) ntohl(x)
#define be16toh(x) ntohs(x)
#endif  /* __MINGW32__ */

#define htonl(x) htobe32(x)
#define htons(x) htobe16(x)
#define ntohl(x) be32toh(x)
#define ntohs(x) be16toh(x)

#endif /* _LKL_LIB_ENDIAN_H */
