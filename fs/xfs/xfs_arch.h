/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_ARCH_H__
#define __XFS_ARCH_H__

#ifndef XFS_BIG_INUMS
# error XFS_BIG_INUMS must be defined true or false
#endif

#ifdef __KERNEL__

#include <asm/byteorder.h>

#ifdef __BIG_ENDIAN
#define	XFS_NATIVE_HOST	1
#else
#undef XFS_NATIVE_HOST
#endif

#else /* __KERNEL__ */

#if __BYTE_ORDER == __BIG_ENDIAN
#define	XFS_NATIVE_HOST	1
#else
#undef XFS_NATIVE_HOST
#endif

#ifdef XFS_NATIVE_HOST
#define cpu_to_be16(val)	((__be16)(val))
#define cpu_to_be32(val)	((__be32)(val))
#define cpu_to_be64(val)	((__be64)(val))
#define be16_to_cpu(val)	((__uint16_t)(val))
#define be32_to_cpu(val)	((__uint32_t)(val))
#define be64_to_cpu(val)	((__uint64_t)(val))
#else
#define cpu_to_be16(val)	(__swab16((__uint16_t)(val)))
#define cpu_to_be32(val)	(__swab32((__uint32_t)(val)))
#define cpu_to_be64(val)	(__swab64((__uint64_t)(val)))
#define be16_to_cpu(val)	(__swab16((__be16)(val)))
#define be32_to_cpu(val)	(__swab32((__be32)(val)))
#define be64_to_cpu(val)	(__swab64((__be64)(val)))
#endif

#endif	/* __KERNEL__ */

/* do we need conversion? */
#define ARCH_NOCONVERT 1
#ifdef XFS_NATIVE_HOST
# define ARCH_CONVERT	ARCH_NOCONVERT
#else
# define ARCH_CONVERT	0
#endif

/* generic swapping macros */

#ifndef HAVE_SWABMACROS
#define INT_SWAP16(type,var) ((typeof(type))(__swab16((__u16)(var))))
#define INT_SWAP32(type,var) ((typeof(type))(__swab32((__u32)(var))))
#define INT_SWAP64(type,var) ((typeof(type))(__swab64((__u64)(var))))
#endif

#define INT_SWAP(type, var) \
    ((sizeof(type) == 8) ? INT_SWAP64(type,var) : \
    ((sizeof(type) == 4) ? INT_SWAP32(type,var) : \
    ((sizeof(type) == 2) ? INT_SWAP16(type,var) : \
    (var))))

/*
 * get and set integers from potentially unaligned locations
 */

#define INT_GET_UNALIGNED_16_BE(pointer) \
   ((__u16)((((__u8*)(pointer))[0] << 8) | (((__u8*)(pointer))[1])))
#define INT_SET_UNALIGNED_16_BE(pointer,value) \
    { \
	((__u8*)(pointer))[0] = (((value) >> 8) & 0xff); \
	((__u8*)(pointer))[1] = (((value)     ) & 0xff); \
    }

/* define generic INT_ macros */

#define INT_GET(reference,arch) \
    (((arch) == ARCH_NOCONVERT) \
	? \
	    (reference) \
	: \
	    INT_SWAP((reference),(reference)) \
    )

/* does not return a value */
#define INT_SET(reference,arch,valueref) \
    (__builtin_constant_p(valueref) ? \
	(void)( (reference) = ( ((arch) != ARCH_NOCONVERT) ? (INT_SWAP((reference),(valueref))) : (valueref)) ) : \
	(void)( \
	    ((reference) = (valueref)), \
	    ( ((arch) != ARCH_NOCONVERT) ? (reference) = INT_SWAP((reference),(reference)) : 0 ) \
	) \
    )

/* does not return a value */
#define INT_MOD_EXPR(reference,arch,code) \
    (((arch) == ARCH_NOCONVERT) \
	? \
	    (void)((reference) code) \
	: \
	    (void)( \
		(reference) = INT_GET((reference),arch) , \
		((reference) code), \
		INT_SET(reference, arch, reference) \
	    ) \
    )

/* does not return a value */
#define INT_MOD(reference,arch,delta) \
    (void)( \
	INT_MOD_EXPR(reference,arch,+=(delta)) \
    )

/*
 * INT_COPY - copy a value between two locations with the
 *	      _same architecture_ but _potentially different sizes_
 *
 *	    if the types of the two parameters are equal or they are
 *		in native architecture, a simple copy is done
 *
 *	    otherwise, architecture conversions are done
 *
 */

/* does not return a value */
#define INT_COPY(dst,src,arch) \
    ( \
	((sizeof(dst) == sizeof(src)) || ((arch) == ARCH_NOCONVERT)) \
	    ? \
		(void)((dst) = (src)) \
	    : \
		INT_SET(dst, arch, INT_GET(src, arch)) \
    )

/*
 * INT_XLATE - copy a value in either direction between two locations
 *	       with different architectures
 *
 *		    dir < 0	- copy from memory to buffer (native to arch)
 *		    dir > 0	- copy from buffer to memory (arch to native)
 */

/* does not return a value */
#define INT_XLATE(buf,mem,dir,arch) {\
    ASSERT(dir); \
    if (dir>0) { \
	(mem)=INT_GET(buf, arch); \
    } else { \
	INT_SET(buf, arch, mem); \
    } \
}

/*
 * In directories inode numbers are stored as unaligned arrays of unsigned
 * 8bit integers on disk.
 *
 * For v1 directories or v2 directories that contain inode numbers that
 * do not fit into 32bit the array has eight members, but the first member
 * is always zero:
 *
 *  |unused|48-55|40-47|32-39|24-31|16-23| 8-15| 0- 7|
 *
 * For v2 directories that only contain entries with inode numbers that fit
 * into 32bits a four-member array is used:
 *
 *  |24-31|16-23| 8-15| 0- 7|
 */ 

#define XFS_GET_DIR_INO4(di) \
	(((__u32)(di).i[0] << 24) | ((di).i[1] << 16) | ((di).i[2] << 8) | ((di).i[3]))

#define XFS_PUT_DIR_INO4(from, di) \
do { \
	(di).i[0] = (((from) & 0xff000000ULL) >> 24); \
	(di).i[1] = (((from) & 0x00ff0000ULL) >> 16); \
	(di).i[2] = (((from) & 0x0000ff00ULL) >> 8); \
	(di).i[3] = ((from) & 0x000000ffULL); \
} while (0)

#define XFS_DI_HI(di) \
	(((__u32)(di).i[1] << 16) | ((di).i[2] << 8) | ((di).i[3]))
#define XFS_DI_LO(di) \
	(((__u32)(di).i[4] << 24) | ((di).i[5] << 16) | ((di).i[6] << 8) | ((di).i[7]))

#define XFS_GET_DIR_INO8(di)        \
	(((xfs_ino_t)XFS_DI_LO(di) & 0xffffffffULL) | \
	 ((xfs_ino_t)XFS_DI_HI(di) << 32))

#define XFS_PUT_DIR_INO8(from, di) \
do { \
	(di).i[0] = 0; \
	(di).i[1] = (((from) & 0x00ff000000000000ULL) >> 48); \
	(di).i[2] = (((from) & 0x0000ff0000000000ULL) >> 40); \
	(di).i[3] = (((from) & 0x000000ff00000000ULL) >> 32); \
	(di).i[4] = (((from) & 0x00000000ff000000ULL) >> 24); \
	(di).i[5] = (((from) & 0x0000000000ff0000ULL) >> 16); \
	(di).i[6] = (((from) & 0x000000000000ff00ULL) >> 8); \
	(di).i[7] = ((from) & 0x00000000000000ffULL); \
} while (0)
	
#endif	/* __XFS_ARCH_H__ */
