/* SPDX-License-Identifier: GPL-2.0 */

/* Try to choose an implementation variant via Kconfig */
#ifdef CONFIG_CRC32_SLICEBY8
# define CRC_LE_BITS 64
# define CRC_BE_BITS 64
#endif
#ifdef CONFIG_CRC32_SLICEBY4
# define CRC_LE_BITS 32
# define CRC_BE_BITS 32
#endif
#ifdef CONFIG_CRC32_SARWATE
# define CRC_LE_BITS 8
# define CRC_BE_BITS 8
#endif
#ifdef CONFIG_CRC32_BIT
# define CRC_LE_BITS 1
# define CRC_BE_BITS 1
#endif

/*
 * How many bits at a time to use.  Valid values are 1, 2, 4, 8, 32 and 64.
 * For less performance-sensitive, use 4 or 8 to save table size.
 * For larger systems choose same as CPU architecture as default.
 * This works well on X86_64, SPARC64 systems. This may require some
 * elaboration after experiments with other architectures.
 */
#ifndef CRC_LE_BITS
#  ifdef CONFIG_64BIT
#  define CRC_LE_BITS 64
#  else
#  define CRC_LE_BITS 32
#  endif
#endif
#ifndef CRC_BE_BITS
#  ifdef CONFIG_64BIT
#  define CRC_BE_BITS 64
#  else
#  define CRC_BE_BITS 32
#  endif
#endif

/*
 * Little-endian CRC computation.  Used with serial bit streams sent
 * lsbit-first.  Be sure to use cpu_to_le32() to append the computed CRC.
 */
#if CRC_LE_BITS > 64 || CRC_LE_BITS < 1 || CRC_LE_BITS == 16 || \
	CRC_LE_BITS & CRC_LE_BITS-1
# error "CRC_LE_BITS must be one of {1, 2, 4, 8, 32, 64}"
#endif

/*
 * Big-endian CRC computation.  Used with serial bit streams sent
 * msbit-first.  Be sure to use cpu_to_be32() to append the computed CRC.
 */
#if CRC_BE_BITS > 64 || CRC_BE_BITS < 1 || CRC_BE_BITS == 16 || \
	CRC_BE_BITS & CRC_BE_BITS-1
# error "CRC_BE_BITS must be one of {1, 2, 4, 8, 32, 64}"
#endif
