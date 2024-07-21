/* SPDX-License-Identifier: 0BSD */

/*
 * Definitions for handling the .xz file format
 *
 * Author: Lasse Collin <lasse.collin@tukaani.org>
 */

#ifndef XZ_STREAM_H
#define XZ_STREAM_H

#if defined(__KERNEL__) && !XZ_INTERNAL_CRC32
#	include <linux/crc32.h>
#	undef crc32
#	define xz_crc32(buf, size, crc) \
		(~crc32_le(~(uint32_t)(crc), buf, size))
#endif

/*
 * See the .xz file format specification at
 * https://tukaani.org/xz/xz-file-format.txt
 * to understand the container format.
 */

#define STREAM_HEADER_SIZE 12

#define HEADER_MAGIC "\3757zXZ"
#define HEADER_MAGIC_SIZE 6

#define FOOTER_MAGIC "YZ"
#define FOOTER_MAGIC_SIZE 2

/*
 * Variable-length integer can hold a 63-bit unsigned integer or a special
 * value indicating that the value is unknown.
 *
 * Experimental: vli_type can be defined to uint32_t to save a few bytes
 * in code size (no effect on speed). Doing so limits the uncompressed and
 * compressed size of the file to less than 256 MiB and may also weaken
 * error detection slightly.
 */
typedef uint64_t vli_type;

#define VLI_MAX ((vli_type)-1 / 2)
#define VLI_UNKNOWN ((vli_type)-1)

/* Maximum encoded size of a VLI */
#define VLI_BYTES_MAX (sizeof(vli_type) * 8 / 7)

/* Integrity Check types */
enum xz_check {
	XZ_CHECK_NONE = 0,
	XZ_CHECK_CRC32 = 1,
	XZ_CHECK_CRC64 = 4,
	XZ_CHECK_SHA256 = 10
};

/* Maximum possible Check ID */
#define XZ_CHECK_MAX 15

#endif
