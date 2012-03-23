/*
 * There are multiple 16-bit CRC polynomials in common use, but this is
 * *the* standard CRC-32 polynomial, first popularized by Ethernet.
 * x^32+x^26+x^23+x^22+x^16+x^12+x^11+x^10+x^8+x^7+x^5+x^4+x^2+x^1+x^0
 */
#define CRCPOLY_LE 0xedb88320
#define CRCPOLY_BE 0x04c11db7

/* How many bits at a time to use.  Valid values are 1, 2, 4, 8, and 32. */
/* For less performance-sensitive, use 4 or 8 */
#ifndef CRC_LE_BITS
# define CRC_LE_BITS 32
#endif
#ifndef CRC_BE_BITS
# define CRC_BE_BITS 32
#endif

/*
 * Little-endian CRC computation.  Used with serial bit streams sent
 * lsbit-first.  Be sure to use cpu_to_le32() to append the computed CRC.
 */
#if CRC_LE_BITS > 32 || CRC_LE_BITS < 1 || CRC_LE_BITS == 16 || \
	CRC_LE_BITS & CRC_LE_BITS-1
# error "CRC_LE_BITS must be one of {1, 2, 4, 8, 32}"
#endif

/*
 * Big-endian CRC computation.  Used with serial bit streams sent
 * msbit-first.  Be sure to use cpu_to_be32() to append the computed CRC.
 */
#if CRC_BE_BITS > 32 || CRC_BE_BITS < 1 || CRC_BE_BITS == 16 || \
	CRC_BE_BITS & CRC_BE_BITS-1
# error "CRC_BE_BITS must be one of {1, 2, 4, 8, 32}"
#endif
