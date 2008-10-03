#include <asm/byteorder.h>

#ifndef CONFIG_MTD_CFI_ADV_OPTIONS

#define CFI_HOST_ENDIAN

#else

#ifdef CONFIG_MTD_CFI_NOSWAP
#define CFI_HOST_ENDIAN
#endif

#ifdef CONFIG_MTD_CFI_LE_BYTE_SWAP
#define CFI_LITTLE_ENDIAN
#endif

#ifdef CONFIG_MTD_CFI_BE_BYTE_SWAP
#define CFI_BIG_ENDIAN
#endif

#endif

#if defined(CFI_LITTLE_ENDIAN)
#define cpu_to_cfi8(x) (x)
#define cfi8_to_cpu(x) (x)
#define cpu_to_cfi16(x) cpu_to_le16(x)
#define cpu_to_cfi32(x) cpu_to_le32(x)
#define cpu_to_cfi64(x) cpu_to_le64(x)
#define cfi16_to_cpu(x) le16_to_cpu(x)
#define cfi32_to_cpu(x) le32_to_cpu(x)
#define cfi64_to_cpu(x) le64_to_cpu(x)
#elif defined (CFI_BIG_ENDIAN)
#define cpu_to_cfi8(x) (x)
#define cfi8_to_cpu(x) (x)
#define cpu_to_cfi16(x) cpu_to_be16(x)
#define cpu_to_cfi32(x) cpu_to_be32(x)
#define cpu_to_cfi64(x) cpu_to_be64(x)
#define cfi16_to_cpu(x) be16_to_cpu(x)
#define cfi32_to_cpu(x) be32_to_cpu(x)
#define cfi64_to_cpu(x) be64_to_cpu(x)
#elif defined (CFI_HOST_ENDIAN)
#define cpu_to_cfi8(x) (x)
#define cfi8_to_cpu(x) (x)
#define cpu_to_cfi16(x) (x)
#define cpu_to_cfi32(x) (x)
#define cpu_to_cfi64(x) (x)
#define cfi16_to_cpu(x) (x)
#define cfi32_to_cpu(x) (x)
#define cfi64_to_cpu(x) (x)
#else
#error No CFI endianness defined
#endif
