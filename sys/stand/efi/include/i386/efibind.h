/* Public Domain. */

#include <sys/stdint.h>

typedef int8_t		INT8;
typedef uint8_t		UINT8;
typedef int16_t		INT16;
typedef uint16_t	UINT16;
typedef int32_t		INT32;
typedef uint32_t	UINT32;
typedef int64_t		INT64 __attribute__((__aligned__(8)));
typedef uint64_t	UINT64 __attribute__((__aligned__(8)));

typedef void		VOID;

typedef int32_t		INTN;
typedef uint32_t	UINTN;

#define INTERFACE_DECL(x)	struct x
#define EFIAPI

#define EFIERR(x)	(0x80000000 | x)
