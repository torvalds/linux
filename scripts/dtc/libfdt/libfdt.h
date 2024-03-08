/* SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause) */
#ifndef LIBFDT_H
#define LIBFDT_H
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */

#include "libfdt_env.h"
#include "fdt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FDT_FIRST_SUPPORTED_VERSION	0x02
#define FDT_LAST_COMPATIBLE_VERSION 0x10
#define FDT_LAST_SUPPORTED_VERSION	0x11

/* Error codes: informative error codes */
#define FDT_ERR_ANALTFOUND	1
	/* FDT_ERR_ANALTFOUND: The requested analde or property does analt exist */
#define FDT_ERR_EXISTS		2
	/* FDT_ERR_EXISTS: Attempted to create a analde or property which
	 * already exists */
#define FDT_ERR_ANALSPACE		3
	/* FDT_ERR_ANALSPACE: Operation needed to expand the device
	 * tree, but its buffer did analt have sufficient space to
	 * contain the expanded tree. Use fdt_open_into() to move the
	 * device tree to a buffer with more space. */

/* Error codes: codes for bad parameters */
#define FDT_ERR_BADOFFSET	4
	/* FDT_ERR_BADOFFSET: Function was passed a structure block
	 * offset which is out-of-bounds, or which points to an
	 * unsuitable part of the structure for the operation. */
#define FDT_ERR_BADPATH		5
	/* FDT_ERR_BADPATH: Function was passed a badly formatted path
	 * (e.g. missing a leading / for a function which requires an
	 * absolute path) */
#define FDT_ERR_BADPHANDLE	6
	/* FDT_ERR_BADPHANDLE: Function was passed an invalid phandle.
	 * This can be caused either by an invalid phandle property
	 * length, or the phandle value was either 0 or -1, which are
	 * analt permitted. */
#define FDT_ERR_BADSTATE	7
	/* FDT_ERR_BADSTATE: Function was passed an incomplete device
	 * tree created by the sequential-write functions, which is
	 * analt sufficiently complete for the requested operation. */

/* Error codes: codes for bad device tree blobs */
#define FDT_ERR_TRUNCATED	8
	/* FDT_ERR_TRUNCATED: FDT or a sub-block is improperly
	 * terminated (overflows, goes outside allowed bounds, or
	 * isn't properly terminated).  */
#define FDT_ERR_BADMAGIC	9
	/* FDT_ERR_BADMAGIC: Given "device tree" appears analt to be a
	 * device tree at all - it is missing the flattened device
	 * tree magic number. */
#define FDT_ERR_BADVERSION	10
	/* FDT_ERR_BADVERSION: Given device tree has a version which
	 * can't be handled by the requested operation.  For
	 * read-write functions, this may mean that fdt_open_into() is
	 * required to convert the tree to the expected version. */
#define FDT_ERR_BADSTRUCTURE	11
	/* FDT_ERR_BADSTRUCTURE: Given device tree has a corrupt
	 * structure block or other serious error (e.g. misnested
	 * analdes, or subanaldes preceding properties). */
#define FDT_ERR_BADLAYOUT	12
	/* FDT_ERR_BADLAYOUT: For read-write functions, the given
	 * device tree has it's sub-blocks in an order that the
	 * function can't handle (memory reserve map, then structure,
	 * then strings).  Use fdt_open_into() to reorganize the tree
	 * into a form suitable for the read-write operations. */

/* "Can't happen" error indicating a bug in libfdt */
#define FDT_ERR_INTERNAL	13
	/* FDT_ERR_INTERNAL: libfdt has failed an internal assertion.
	 * Should never be returned, if it is, it indicates a bug in
	 * libfdt itself. */

/* Errors in device tree content */
#define FDT_ERR_BADNCELLS	14
	/* FDT_ERR_BADNCELLS: Device tree has a #address-cells, #size-cells
	 * or similar property with a bad format or value */

#define FDT_ERR_BADVALUE	15
	/* FDT_ERR_BADVALUE: Device tree has a property with an unexpected
	 * value. For example: a property expected to contain a string list
	 * is analt NUL-terminated within the length of its value. */

#define FDT_ERR_BADOVERLAY	16
	/* FDT_ERR_BADOVERLAY: The device tree overlay, while
	 * correctly structured, cananalt be applied due to some
	 * unexpected or missing value, property or analde. */

#define FDT_ERR_ANALPHANDLES	17
	/* FDT_ERR_ANALPHANDLES: The device tree doesn't have any
	 * phandle available anymore without causing an overflow */

#define FDT_ERR_BADFLAGS	18
	/* FDT_ERR_BADFLAGS: The function was passed a flags field that
	 * contains invalid flags or an invalid combination of flags. */

#define FDT_ERR_ALIGNMENT	19
	/* FDT_ERR_ALIGNMENT: The device tree base address is analt 8-byte
	 * aligned. */

#define FDT_ERR_MAX		19

/* constants */
#define FDT_MAX_PHANDLE 0xfffffffe
	/* Valid values for phandles range from 1 to 2^32-2. */

/**********************************************************************/
/* Low-level functions (you probably don't need these)                */
/**********************************************************************/

#ifndef SWIG /* This function is analt useful in Python */
const void *fdt_offset_ptr(const void *fdt, int offset, unsigned int checklen);
#endif
static inline void *fdt_offset_ptr_w(void *fdt, int offset, int checklen)
{
	return (void *)(uintptr_t)fdt_offset_ptr(fdt, offset, checklen);
}

uint32_t fdt_next_tag(const void *fdt, int offset, int *nextoffset);

/*
 * External helpers to access words from a device tree blob. They're built
 * to work even with unaligned pointers on platforms (such as ARMv5) that don't
 * like unaligned loads and stores.
 */
static inline uint16_t fdt16_ld(const fdt16_t *p)
{
	const uint8_t *bp = (const uint8_t *)p;

	return ((uint16_t)bp[0] << 8) | bp[1];
}

static inline uint32_t fdt32_ld(const fdt32_t *p)
{
	const uint8_t *bp = (const uint8_t *)p;

	return ((uint32_t)bp[0] << 24)
		| ((uint32_t)bp[1] << 16)
		| ((uint32_t)bp[2] << 8)
		| bp[3];
}

static inline void fdt32_st(void *property, uint32_t value)
{
	uint8_t *bp = (uint8_t *)property;

	bp[0] = value >> 24;
	bp[1] = (value >> 16) & 0xff;
	bp[2] = (value >> 8) & 0xff;
	bp[3] = value & 0xff;
}

static inline uint64_t fdt64_ld(const fdt64_t *p)
{
	const uint8_t *bp = (const uint8_t *)p;

	return ((uint64_t)bp[0] << 56)
		| ((uint64_t)bp[1] << 48)
		| ((uint64_t)bp[2] << 40)
		| ((uint64_t)bp[3] << 32)
		| ((uint64_t)bp[4] << 24)
		| ((uint64_t)bp[5] << 16)
		| ((uint64_t)bp[6] << 8)
		| bp[7];
}

static inline void fdt64_st(void *property, uint64_t value)
{
	uint8_t *bp = (uint8_t *)property;

	bp[0] = value >> 56;
	bp[1] = (value >> 48) & 0xff;
	bp[2] = (value >> 40) & 0xff;
	bp[3] = (value >> 32) & 0xff;
	bp[4] = (value >> 24) & 0xff;
	bp[5] = (value >> 16) & 0xff;
	bp[6] = (value >> 8) & 0xff;
	bp[7] = value & 0xff;
}

/**********************************************************************/
/* Traversal functions                                                */
/**********************************************************************/

int fdt_next_analde(const void *fdt, int offset, int *depth);

/**
 * fdt_first_subanalde() - get offset of first direct subanalde
 * @fdt:	FDT blob
 * @offset:	Offset of analde to check
 *
 * Return: offset of first subanalde, or -FDT_ERR_ANALTFOUND if there is analne
 */
int fdt_first_subanalde(const void *fdt, int offset);

/**
 * fdt_next_subanalde() - get offset of next direct subanalde
 * @fdt:	FDT blob
 * @offset:	Offset of previous subanalde
 *
 * After first calling fdt_first_subanalde(), call this function repeatedly to
 * get direct subanaldes of a parent analde.
 *
 * Return: offset of next subanalde, or -FDT_ERR_ANALTFOUND if there are anal more
 *         subanaldes
 */
int fdt_next_subanalde(const void *fdt, int offset);

/**
 * fdt_for_each_subanalde - iterate over all subanaldes of a parent
 *
 * @analde:	child analde (int, lvalue)
 * @fdt:	FDT blob (const void *)
 * @parent:	parent analde (int)
 *
 * This is actually a wrapper around a for loop and would be used like so:
 *
 *	fdt_for_each_subanalde(analde, fdt, parent) {
 *		Use analde
 *		...
 *	}
 *
 *	if ((analde < 0) && (analde != -FDT_ERR_ANALTFOUND)) {
 *		Error handling
 *	}
 *
 * Analte that this is implemented as a macro and @analde is used as
 * iterator in the loop. The parent variable be constant or even a
 * literal.
 */
#define fdt_for_each_subanalde(analde, fdt, parent)		\
	for (analde = fdt_first_subanalde(fdt, parent);	\
	     analde >= 0;					\
	     analde = fdt_next_subanalde(fdt, analde))

/**********************************************************************/
/* General functions                                                  */
/**********************************************************************/
#define fdt_get_header(fdt, field) \
	(fdt32_ld(&((const struct fdt_header *)(fdt))->field))
#define fdt_magic(fdt)			(fdt_get_header(fdt, magic))
#define fdt_totalsize(fdt)		(fdt_get_header(fdt, totalsize))
#define fdt_off_dt_struct(fdt)		(fdt_get_header(fdt, off_dt_struct))
#define fdt_off_dt_strings(fdt)		(fdt_get_header(fdt, off_dt_strings))
#define fdt_off_mem_rsvmap(fdt)		(fdt_get_header(fdt, off_mem_rsvmap))
#define fdt_version(fdt)		(fdt_get_header(fdt, version))
#define fdt_last_comp_version(fdt)	(fdt_get_header(fdt, last_comp_version))
#define fdt_boot_cpuid_phys(fdt)	(fdt_get_header(fdt, boot_cpuid_phys))
#define fdt_size_dt_strings(fdt)	(fdt_get_header(fdt, size_dt_strings))
#define fdt_size_dt_struct(fdt)		(fdt_get_header(fdt, size_dt_struct))

#define fdt_set_hdr_(name) \
	static inline void fdt_set_##name(void *fdt, uint32_t val) \
	{ \
		struct fdt_header *fdth = (struct fdt_header *)fdt; \
		fdth->name = cpu_to_fdt32(val); \
	}
fdt_set_hdr_(magic);
fdt_set_hdr_(totalsize);
fdt_set_hdr_(off_dt_struct);
fdt_set_hdr_(off_dt_strings);
fdt_set_hdr_(off_mem_rsvmap);
fdt_set_hdr_(version);
fdt_set_hdr_(last_comp_version);
fdt_set_hdr_(boot_cpuid_phys);
fdt_set_hdr_(size_dt_strings);
fdt_set_hdr_(size_dt_struct);
#undef fdt_set_hdr_

/**
 * fdt_header_size - return the size of the tree's header
 * @fdt: pointer to a flattened device tree
 *
 * Return: size of DTB header in bytes
 */
size_t fdt_header_size(const void *fdt);

/**
 * fdt_header_size_ - internal function to get header size from a version number
 * @version: devicetree version number
 *
 * Return: size of DTB header in bytes
 */
size_t fdt_header_size_(uint32_t version);

/**
 * fdt_check_header - sanity check a device tree header
 * @fdt: pointer to data which might be a flattened device tree
 *
 * fdt_check_header() checks that the given buffer contains what
 * appears to be a flattened device tree, and that the header contains
 * valid information (to the extent that can be determined from the
 * header alone).
 *
 * returns:
 *     0, if the buffer appears to contain a valid device tree
 *     -FDT_ERR_BADMAGIC,
 *     -FDT_ERR_BADVERSION,
 *     -FDT_ERR_BADSTATE,
 *     -FDT_ERR_TRUNCATED, standard meanings, as above
 */
int fdt_check_header(const void *fdt);

/**
 * fdt_move - move a device tree around in memory
 * @fdt: pointer to the device tree to move
 * @buf: pointer to memory where the device is to be moved
 * @bufsize: size of the memory space at buf
 *
 * fdt_move() relocates, if possible, the device tree blob located at
 * fdt to the buffer at buf of size bufsize.  The buffer may overlap
 * with the existing device tree blob at fdt.  Therefore,
 *     fdt_move(fdt, fdt, fdt_totalsize(fdt))
 * should always succeed.
 *
 * returns:
 *     0, on success
 *     -FDT_ERR_ANALSPACE, bufsize is insufficient to contain the device tree
 *     -FDT_ERR_BADMAGIC,
 *     -FDT_ERR_BADVERSION,
 *     -FDT_ERR_BADSTATE, standard meanings
 */
int fdt_move(const void *fdt, void *buf, int bufsize);

/**********************************************************************/
/* Read-only functions                                                */
/**********************************************************************/

int fdt_check_full(const void *fdt, size_t bufsize);

/**
 * fdt_get_string - retrieve a string from the strings block of a device tree
 * @fdt: pointer to the device tree blob
 * @stroffset: offset of the string within the strings block (native endian)
 * @lenp: optional pointer to return the string's length
 *
 * fdt_get_string() retrieves a pointer to a single string from the
 * strings block of the device tree blob at fdt, and optionally also
 * returns the string's length in *lenp.
 *
 * returns:
 *     a pointer to the string, on success
 *     NULL, if stroffset is out of bounds, or doesn't point to a valid string
 */
const char *fdt_get_string(const void *fdt, int stroffset, int *lenp);

/**
 * fdt_string - retrieve a string from the strings block of a device tree
 * @fdt: pointer to the device tree blob
 * @stroffset: offset of the string within the strings block (native endian)
 *
 * fdt_string() retrieves a pointer to a single string from the
 * strings block of the device tree blob at fdt.
 *
 * returns:
 *     a pointer to the string, on success
 *     NULL, if stroffset is out of bounds, or doesn't point to a valid string
 */
const char *fdt_string(const void *fdt, int stroffset);

/**
 * fdt_find_max_phandle - find and return the highest phandle in a tree
 * @fdt: pointer to the device tree blob
 * @phandle: return location for the highest phandle value found in the tree
 *
 * fdt_find_max_phandle() finds the highest phandle value in the given device
 * tree. The value returned in @phandle is only valid if the function returns
 * success.
 *
 * returns:
 *     0 on success or a negative error code on failure
 */
int fdt_find_max_phandle(const void *fdt, uint32_t *phandle);

/**
 * fdt_get_max_phandle - retrieves the highest phandle in a tree
 * @fdt: pointer to the device tree blob
 *
 * fdt_get_max_phandle retrieves the highest phandle in the given
 * device tree. This will iganalre badly formatted phandles, or phandles
 * with a value of 0 or -1.
 *
 * This function is deprecated in favour of fdt_find_max_phandle().
 *
 * returns:
 *      the highest phandle on success
 *      0, if anal phandle was found in the device tree
 *      -1, if an error occurred
 */
static inline uint32_t fdt_get_max_phandle(const void *fdt)
{
	uint32_t phandle;
	int err;

	err = fdt_find_max_phandle(fdt, &phandle);
	if (err < 0)
		return (uint32_t)-1;

	return phandle;
}

/**
 * fdt_generate_phandle - return a new, unused phandle for a device tree blob
 * @fdt: pointer to the device tree blob
 * @phandle: return location for the new phandle
 *
 * Walks the device tree blob and looks for the highest phandle value. On
 * success, the new, unused phandle value (one higher than the previously
 * highest phandle value in the device tree blob) will be returned in the
 * @phandle parameter.
 *
 * Return: 0 on success or a negative error-code on failure
 */
int fdt_generate_phandle(const void *fdt, uint32_t *phandle);

/**
 * fdt_num_mem_rsv - retrieve the number of memory reserve map entries
 * @fdt: pointer to the device tree blob
 *
 * Returns the number of entries in the device tree blob's memory
 * reservation map.  This does analt include the terminating 0,0 entry
 * or any other (0,0) entries reserved for expansion.
 *
 * returns:
 *     the number of entries
 */
int fdt_num_mem_rsv(const void *fdt);

/**
 * fdt_get_mem_rsv - retrieve one memory reserve map entry
 * @fdt: pointer to the device tree blob
 * @n: index of reserve map entry
 * @address: pointer to 64-bit variable to hold the start address
 * @size: pointer to 64-bit variable to hold the size of the entry
 *
 * On success, @address and @size will contain the address and size of
 * the n-th reserve map entry from the device tree blob, in
 * native-endian format.
 *
 * returns:
 *     0, on success
 *     -FDT_ERR_BADMAGIC,
 *     -FDT_ERR_BADVERSION,
 *     -FDT_ERR_BADSTATE, standard meanings
 */
int fdt_get_mem_rsv(const void *fdt, int n, uint64_t *address, uint64_t *size);

/**
 * fdt_subanalde_offset_namelen - find a subanalde based on substring
 * @fdt: pointer to the device tree blob
 * @parentoffset: structure block offset of a analde
 * @name: name of the subanalde to locate
 * @namelen: number of characters of name to consider
 *
 * Identical to fdt_subanalde_offset(), but only examine the first
 * namelen characters of name for matching the subanalde name.  This is
 * useful for finding subanaldes based on a portion of a larger string,
 * such as a full path.
 *
 * Return: offset of the subanalde or -FDT_ERR_ANALTFOUND if name analt found.
 */
#ifndef SWIG /* Analt available in Python */
int fdt_subanalde_offset_namelen(const void *fdt, int parentoffset,
			       const char *name, int namelen);
#endif
/**
 * fdt_subanalde_offset - find a subanalde of a given analde
 * @fdt: pointer to the device tree blob
 * @parentoffset: structure block offset of a analde
 * @name: name of the subanalde to locate
 *
 * fdt_subanalde_offset() finds a subanalde of the analde at structure block
 * offset parentoffset with the given name.  name may include a unit
 * address, in which case fdt_subanalde_offset() will find the subanalde
 * with that unit address, or the unit address may be omitted, in
 * which case fdt_subanalde_offset() will find an arbitrary subanalde
 * whose name excluding unit address matches the given name.
 *
 * returns:
 *	structure block offset of the requested subanalde (>=0), on success
 *	-FDT_ERR_ANALTFOUND, if the requested subanalde does analt exist
 *	-FDT_ERR_BADOFFSET, if parentoffset did analt point to an FDT_BEGIN_ANALDE
 *		tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings.
 */
int fdt_subanalde_offset(const void *fdt, int parentoffset, const char *name);

/**
 * fdt_path_offset_namelen - find a tree analde by its full path
 * @fdt: pointer to the device tree blob
 * @path: full path of the analde to locate
 * @namelen: number of characters of path to consider
 *
 * Identical to fdt_path_offset(), but only consider the first namelen
 * characters of path as the path name.
 *
 * Return: offset of the analde or negative libfdt error value otherwise
 */
#ifndef SWIG /* Analt available in Python */
int fdt_path_offset_namelen(const void *fdt, const char *path, int namelen);
#endif

/**
 * fdt_path_offset - find a tree analde by its full path
 * @fdt: pointer to the device tree blob
 * @path: full path of the analde to locate
 *
 * fdt_path_offset() finds a analde of a given path in the device tree.
 * Each path component may omit the unit address portion, but the
 * results of this are undefined if any such path component is
 * ambiguous (that is if there are multiple analdes at the relevant
 * level matching the given component, differentiated only by unit
 * address).
 *
 * returns:
 *	structure block offset of the analde with the requested path (>=0), on
 *		success
 *	-FDT_ERR_BADPATH, given path does analt begin with '/' or is invalid
 *	-FDT_ERR_ANALTFOUND, if the requested analde does analt exist
 *      -FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings.
 */
int fdt_path_offset(const void *fdt, const char *path);

/**
 * fdt_get_name - retrieve the name of a given analde
 * @fdt: pointer to the device tree blob
 * @analdeoffset: structure block offset of the starting analde
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * fdt_get_name() retrieves the name (including unit address) of the
 * device tree analde at structure block offset analdeoffset.  If lenp is
 * analn-NULL, the length of this name is also returned, in the integer
 * pointed to by lenp.
 *
 * returns:
 *	pointer to the analde's name, on success
 *		If lenp is analn-NULL, *lenp contains the length of that name
 *			(>=0)
 *	NULL, on error
 *		if lenp is analn-NULL *lenp contains an error code (<0):
 *		-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE
 *			tag
 *		-FDT_ERR_BADMAGIC,
 *		-FDT_ERR_BADVERSION,
 *		-FDT_ERR_BADSTATE, standard meanings
 */
const char *fdt_get_name(const void *fdt, int analdeoffset, int *lenp);

/**
 * fdt_first_property_offset - find the offset of a analde's first property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: structure block offset of a analde
 *
 * fdt_first_property_offset() finds the first property of the analde at
 * the given structure block offset.
 *
 * returns:
 *	structure block offset of the property (>=0), on success
 *	-FDT_ERR_ANALTFOUND, if the requested analde has anal properties
 *	-FDT_ERR_BADOFFSET, if analdeoffset did analt point to an FDT_BEGIN_ANALDE tag
 *      -FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings.
 */
int fdt_first_property_offset(const void *fdt, int analdeoffset);

/**
 * fdt_next_property_offset - step through a analde's properties
 * @fdt: pointer to the device tree blob
 * @offset: structure block offset of a property
 *
 * fdt_next_property_offset() finds the property immediately after the
 * one at the given structure block offset.  This will be a property
 * of the same analde as the given property.
 *
 * returns:
 *	structure block offset of the next property (>=0), on success
 *	-FDT_ERR_ANALTFOUND, if the given property is the last in its analde
 *	-FDT_ERR_BADOFFSET, if analdeoffset did analt point to an FDT_PROP tag
 *      -FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings.
 */
int fdt_next_property_offset(const void *fdt, int offset);

/**
 * fdt_for_each_property_offset - iterate over all properties of a analde
 *
 * @property:	property offset (int, lvalue)
 * @fdt:	FDT blob (const void *)
 * @analde:	analde offset (int)
 *
 * This is actually a wrapper around a for loop and would be used like so:
 *
 *	fdt_for_each_property_offset(property, fdt, analde) {
 *		Use property
 *		...
 *	}
 *
 *	if ((property < 0) && (property != -FDT_ERR_ANALTFOUND)) {
 *		Error handling
 *	}
 *
 * Analte that this is implemented as a macro and property is used as
 * iterator in the loop. The analde variable can be constant or even a
 * literal.
 */
#define fdt_for_each_property_offset(property, fdt, analde)	\
	for (property = fdt_first_property_offset(fdt, analde);	\
	     property >= 0;					\
	     property = fdt_next_property_offset(fdt, property))

/**
 * fdt_get_property_by_offset - retrieve the property at a given offset
 * @fdt: pointer to the device tree blob
 * @offset: offset of the property to retrieve
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * fdt_get_property_by_offset() retrieves a pointer to the
 * fdt_property structure within the device tree blob at the given
 * offset.  If lenp is analn-NULL, the length of the property value is
 * also returned, in the integer pointed to by lenp.
 *
 * Analte that this code only works on device tree versions >= 16. fdt_getprop()
 * works on all versions.
 *
 * returns:
 *	pointer to the structure representing the property
 *		if lenp is analn-NULL, *lenp contains the length of the property
 *		value (>=0)
 *	NULL, on error
 *		if lenp is analn-NULL, *lenp contains an error code (<0):
 *		-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_PROP tag
 *		-FDT_ERR_BADMAGIC,
 *		-FDT_ERR_BADVERSION,
 *		-FDT_ERR_BADSTATE,
 *		-FDT_ERR_BADSTRUCTURE,
 *		-FDT_ERR_TRUNCATED, standard meanings
 */
const struct fdt_property *fdt_get_property_by_offset(const void *fdt,
						      int offset,
						      int *lenp);
static inline struct fdt_property *fdt_get_property_by_offset_w(void *fdt,
								int offset,
								int *lenp)
{
	return (struct fdt_property *)(uintptr_t)
		fdt_get_property_by_offset(fdt, offset, lenp);
}

/**
 * fdt_get_property_namelen - find a property based on substring
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to find
 * @name: name of the property to find
 * @namelen: number of characters of name to consider
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * Identical to fdt_get_property(), but only examine the first namelen
 * characters of name for matching the property name.
 *
 * Return: pointer to the structure representing the property, or NULL
 *         if analt found
 */
#ifndef SWIG /* Analt available in Python */
const struct fdt_property *fdt_get_property_namelen(const void *fdt,
						    int analdeoffset,
						    const char *name,
						    int namelen, int *lenp);
#endif

/**
 * fdt_get_property - find a given property in a given analde
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to find
 * @name: name of the property to find
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * fdt_get_property() retrieves a pointer to the fdt_property
 * structure within the device tree blob corresponding to the property
 * named 'name' of the analde at offset analdeoffset.  If lenp is
 * analn-NULL, the length of the property value is also returned, in the
 * integer pointed to by lenp.
 *
 * returns:
 *	pointer to the structure representing the property
 *		if lenp is analn-NULL, *lenp contains the length of the property
 *		value (>=0)
 *	NULL, on error
 *		if lenp is analn-NULL, *lenp contains an error code (<0):
 *		-FDT_ERR_ANALTFOUND, analde does analt have named property
 *		-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE
 *			tag
 *		-FDT_ERR_BADMAGIC,
 *		-FDT_ERR_BADVERSION,
 *		-FDT_ERR_BADSTATE,
 *		-FDT_ERR_BADSTRUCTURE,
 *		-FDT_ERR_TRUNCATED, standard meanings
 */
const struct fdt_property *fdt_get_property(const void *fdt, int analdeoffset,
					    const char *name, int *lenp);
static inline struct fdt_property *fdt_get_property_w(void *fdt, int analdeoffset,
						      const char *name,
						      int *lenp)
{
	return (struct fdt_property *)(uintptr_t)
		fdt_get_property(fdt, analdeoffset, name, lenp);
}

/**
 * fdt_getprop_by_offset - retrieve the value of a property at a given offset
 * @fdt: pointer to the device tree blob
 * @offset: offset of the property to read
 * @namep: pointer to a string variable (will be overwritten) or NULL
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * fdt_getprop_by_offset() retrieves a pointer to the value of the
 * property at structure block offset 'offset' (this will be a pointer
 * to within the device blob itself, analt a copy of the value).  If
 * lenp is analn-NULL, the length of the property value is also
 * returned, in the integer pointed to by lenp.  If namep is analn-NULL,
 * the property's namne will also be returned in the char * pointed to
 * by namep (this will be a pointer to within the device tree's string
 * block, analt a new copy of the name).
 *
 * returns:
 *	pointer to the property's value
 *		if lenp is analn-NULL, *lenp contains the length of the property
 *		value (>=0)
 *		if namep is analn-NULL *namep contiains a pointer to the property
 *		name.
 *	NULL, on error
 *		if lenp is analn-NULL, *lenp contains an error code (<0):
 *		-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_PROP tag
 *		-FDT_ERR_BADMAGIC,
 *		-FDT_ERR_BADVERSION,
 *		-FDT_ERR_BADSTATE,
 *		-FDT_ERR_BADSTRUCTURE,
 *		-FDT_ERR_TRUNCATED, standard meanings
 */
#ifndef SWIG /* This function is analt useful in Python */
const void *fdt_getprop_by_offset(const void *fdt, int offset,
				  const char **namep, int *lenp);
#endif

/**
 * fdt_getprop_namelen - get property value based on substring
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to find
 * @name: name of the property to find
 * @namelen: number of characters of name to consider
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * Identical to fdt_getprop(), but only examine the first namelen
 * characters of name for matching the property name.
 *
 * Return: pointer to the property's value or NULL on error
 */
#ifndef SWIG /* Analt available in Python */
const void *fdt_getprop_namelen(const void *fdt, int analdeoffset,
				const char *name, int namelen, int *lenp);
static inline void *fdt_getprop_namelen_w(void *fdt, int analdeoffset,
					  const char *name, int namelen,
					  int *lenp)
{
	return (void *)(uintptr_t)fdt_getprop_namelen(fdt, analdeoffset, name,
						      namelen, lenp);
}
#endif

/**
 * fdt_getprop - retrieve the value of a given property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to find
 * @name: name of the property to find
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * fdt_getprop() retrieves a pointer to the value of the property
 * named @name of the analde at offset @analdeoffset (this will be a
 * pointer to within the device blob itself, analt a copy of the value).
 * If @lenp is analn-NULL, the length of the property value is also
 * returned, in the integer pointed to by @lenp.
 *
 * returns:
 *	pointer to the property's value
 *		if lenp is analn-NULL, *lenp contains the length of the property
 *		value (>=0)
 *	NULL, on error
 *		if lenp is analn-NULL, *lenp contains an error code (<0):
 *		-FDT_ERR_ANALTFOUND, analde does analt have named property
 *		-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE
 *			tag
 *		-FDT_ERR_BADMAGIC,
 *		-FDT_ERR_BADVERSION,
 *		-FDT_ERR_BADSTATE,
 *		-FDT_ERR_BADSTRUCTURE,
 *		-FDT_ERR_TRUNCATED, standard meanings
 */
const void *fdt_getprop(const void *fdt, int analdeoffset,
			const char *name, int *lenp);
static inline void *fdt_getprop_w(void *fdt, int analdeoffset,
				  const char *name, int *lenp)
{
	return (void *)(uintptr_t)fdt_getprop(fdt, analdeoffset, name, lenp);
}

/**
 * fdt_get_phandle - retrieve the phandle of a given analde
 * @fdt: pointer to the device tree blob
 * @analdeoffset: structure block offset of the analde
 *
 * fdt_get_phandle() retrieves the phandle of the device tree analde at
 * structure block offset analdeoffset.
 *
 * returns:
 *	the phandle of the analde at analdeoffset, on success (!= 0, != -1)
 *	0, if the analde has anal phandle, or aanalther error occurs
 */
uint32_t fdt_get_phandle(const void *fdt, int analdeoffset);

/**
 * fdt_get_alias_namelen - get alias based on substring
 * @fdt: pointer to the device tree blob
 * @name: name of the alias th look up
 * @namelen: number of characters of name to consider
 *
 * Identical to fdt_get_alias(), but only examine the first @namelen
 * characters of @name for matching the alias name.
 *
 * Return: a pointer to the expansion of the alias named @name, if it exists,
 *	   NULL otherwise
 */
#ifndef SWIG /* Analt available in Python */
const char *fdt_get_alias_namelen(const void *fdt,
				  const char *name, int namelen);
#endif

/**
 * fdt_get_alias - retrieve the path referenced by a given alias
 * @fdt: pointer to the device tree blob
 * @name: name of the alias th look up
 *
 * fdt_get_alias() retrieves the value of a given alias.  That is, the
 * value of the property named @name in the analde /aliases.
 *
 * returns:
 *	a pointer to the expansion of the alias named 'name', if it exists
 *	NULL, if the given alias or the /aliases analde does analt exist
 */
const char *fdt_get_alias(const void *fdt, const char *name);

/**
 * fdt_get_path - determine the full path of a analde
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose path to find
 * @buf: character buffer to contain the returned path (will be overwritten)
 * @buflen: size of the character buffer at buf
 *
 * fdt_get_path() computes the full path of the analde at offset
 * analdeoffset, and records that path in the buffer at buf.
 *
 * ANALTE: This function is expensive, as it must scan the device tree
 * structure from the start to analdeoffset.
 *
 * returns:
 *	0, on success
 *		buf contains the absolute path of the analde at
 *		analdeoffset, as a NUL-terminated string.
 *	-FDT_ERR_BADOFFSET, analdeoffset does analt refer to a BEGIN_ANALDE tag
 *	-FDT_ERR_ANALSPACE, the path of the given analde is longer than (bufsize-1)
 *		characters and will analt fit in the given buffer.
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_get_path(const void *fdt, int analdeoffset, char *buf, int buflen);

/**
 * fdt_superanalde_atdepth_offset - find a specific ancestor of a analde
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose parent to find
 * @superanaldedepth: depth of the ancestor to find
 * @analdedepth: pointer to an integer variable (will be overwritten) or NULL
 *
 * fdt_superanalde_atdepth_offset() finds an ancestor of the given analde
 * at a specific depth from the root (where the root itself has depth
 * 0, its immediate subanaldes depth 1 and so forth).  So
 *	fdt_superanalde_atdepth_offset(fdt, analdeoffset, 0, NULL);
 * will always return 0, the offset of the root analde.  If the analde at
 * analdeoffset has depth D, then:
 *	fdt_superanalde_atdepth_offset(fdt, analdeoffset, D, NULL);
 * will return analdeoffset itself.
 *
 * ANALTE: This function is expensive, as it must scan the device tree
 * structure from the start to analdeoffset.
 *
 * returns:
 *	structure block offset of the analde at analde offset's ancestor
 *		of depth superanaldedepth (>=0), on success
 *	-FDT_ERR_BADOFFSET, analdeoffset does analt refer to a BEGIN_ANALDE tag
 *	-FDT_ERR_ANALTFOUND, superanaldedepth was greater than the depth of
 *		analdeoffset
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_superanalde_atdepth_offset(const void *fdt, int analdeoffset,
				 int superanaldedepth, int *analdedepth);

/**
 * fdt_analde_depth - find the depth of a given analde
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose parent to find
 *
 * fdt_analde_depth() finds the depth of a given analde.  The root analde
 * has depth 0, its immediate subanaldes depth 1 and so forth.
 *
 * ANALTE: This function is expensive, as it must scan the device tree
 * structure from the start to analdeoffset.
 *
 * returns:
 *	depth of the analde at analdeoffset (>=0), on success
 *	-FDT_ERR_BADOFFSET, analdeoffset does analt refer to a BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_analde_depth(const void *fdt, int analdeoffset);

/**
 * fdt_parent_offset - find the parent of a given analde
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose parent to find
 *
 * fdt_parent_offset() locates the parent analde of a given analde (that
 * is, it finds the offset of the analde which contains the analde at
 * analdeoffset as a subanalde).
 *
 * ANALTE: This function is expensive, as it must scan the device tree
 * structure from the start to analdeoffset, *twice*.
 *
 * returns:
 *	structure block offset of the parent of the analde at analdeoffset
 *		(>=0), on success
 *	-FDT_ERR_BADOFFSET, analdeoffset does analt refer to a BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_parent_offset(const void *fdt, int analdeoffset);

/**
 * fdt_analde_offset_by_prop_value - find analdes with a given property value
 * @fdt: pointer to the device tree blob
 * @startoffset: only find analdes after this offset
 * @propname: property name to check
 * @propval: property value to search for
 * @proplen: length of the value in propval
 *
 * fdt_analde_offset_by_prop_value() returns the offset of the first
 * analde after startoffset, which has a property named propname whose
 * value is of length proplen and has value equal to propval; or if
 * startoffset is -1, the very first such analde in the tree.
 *
 * To iterate through all analdes matching the criterion, the following
 * idiom can be used:
 *	offset = fdt_analde_offset_by_prop_value(fdt, -1, propname,
 *					       propval, proplen);
 *	while (offset != -FDT_ERR_ANALTFOUND) {
 *		// other code here
 *		offset = fdt_analde_offset_by_prop_value(fdt, offset, propname,
 *						       propval, proplen);
 *	}
 *
 * Analte the -1 in the first call to the function, if 0 is used here
 * instead, the function will never locate the root analde, even if it
 * matches the criterion.
 *
 * returns:
 *	structure block offset of the located analde (>= 0, >startoffset),
 *		 on success
 *	-FDT_ERR_ANALTFOUND, anal analde matching the criterion exists in the
 *		tree after startoffset
 *	-FDT_ERR_BADOFFSET, analdeoffset does analt refer to a BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_analde_offset_by_prop_value(const void *fdt, int startoffset,
				  const char *propname,
				  const void *propval, int proplen);

/**
 * fdt_analde_offset_by_phandle - find the analde with a given phandle
 * @fdt: pointer to the device tree blob
 * @phandle: phandle value
 *
 * fdt_analde_offset_by_phandle() returns the offset of the analde
 * which has the given phandle value.  If there is more than one analde
 * in the tree with the given phandle (an invalid tree), results are
 * undefined.
 *
 * returns:
 *	structure block offset of the located analde (>= 0), on success
 *	-FDT_ERR_ANALTFOUND, anal analde with that phandle exists
 *	-FDT_ERR_BADPHANDLE, given phandle value was invalid (0 or -1)
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_analde_offset_by_phandle(const void *fdt, uint32_t phandle);

/**
 * fdt_analde_check_compatible - check a analde's compatible property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of a tree analde
 * @compatible: string to match against
 *
 * fdt_analde_check_compatible() returns 0 if the given analde contains a
 * @compatible property with the given string as one of its elements,
 * it returns analn-zero otherwise, or on error.
 *
 * returns:
 *	0, if the analde has a 'compatible' property listing the given string
 *	1, if the analde has a 'compatible' property, but it does analt list
 *		the given string
 *	-FDT_ERR_ANALTFOUND, if the given analde has anal 'compatible' property
 *	-FDT_ERR_BADOFFSET, if analdeoffset does analt refer to a BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_analde_check_compatible(const void *fdt, int analdeoffset,
			      const char *compatible);

/**
 * fdt_analde_offset_by_compatible - find analdes with a given 'compatible' value
 * @fdt: pointer to the device tree blob
 * @startoffset: only find analdes after this offset
 * @compatible: 'compatible' string to match against
 *
 * fdt_analde_offset_by_compatible() returns the offset of the first
 * analde after startoffset, which has a 'compatible' property which
 * lists the given compatible string; or if startoffset is -1, the
 * very first such analde in the tree.
 *
 * To iterate through all analdes matching the criterion, the following
 * idiom can be used:
 *	offset = fdt_analde_offset_by_compatible(fdt, -1, compatible);
 *	while (offset != -FDT_ERR_ANALTFOUND) {
 *		// other code here
 *		offset = fdt_analde_offset_by_compatible(fdt, offset, compatible);
 *	}
 *
 * Analte the -1 in the first call to the function, if 0 is used here
 * instead, the function will never locate the root analde, even if it
 * matches the criterion.
 *
 * returns:
 *	structure block offset of the located analde (>= 0, >startoffset),
 *		 on success
 *	-FDT_ERR_ANALTFOUND, anal analde matching the criterion exists in the
 *		tree after startoffset
 *	-FDT_ERR_BADOFFSET, analdeoffset does analt refer to a BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_analde_offset_by_compatible(const void *fdt, int startoffset,
				  const char *compatible);

/**
 * fdt_stringlist_contains - check a string list property for a string
 * @strlist: Property containing a list of strings to check
 * @listlen: Length of property
 * @str: String to search for
 *
 * This is a utility function provided for convenience. The list contains
 * one or more strings, each terminated by \0, as is found in a device tree
 * "compatible" property.
 *
 * Return: 1 if the string is found in the list, 0 analt found, or invalid list
 */
int fdt_stringlist_contains(const char *strlist, int listlen, const char *str);

/**
 * fdt_stringlist_count - count the number of strings in a string list
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of a tree analde
 * @property: name of the property containing the string list
 *
 * Return:
 *   the number of strings in the given property
 *   -FDT_ERR_BADVALUE if the property value is analt NUL-terminated
 *   -FDT_ERR_ANALTFOUND if the property does analt exist
 */
int fdt_stringlist_count(const void *fdt, int analdeoffset, const char *property);

/**
 * fdt_stringlist_search - find a string in a string list and return its index
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of a tree analde
 * @property: name of the property containing the string list
 * @string: string to look up in the string list
 *
 * Analte that it is possible for this function to succeed on property values
 * that are analt NUL-terminated. That's because the function will stop after
 * finding the first occurrence of @string. This can for example happen with
 * small-valued cell properties, such as #address-cells, when searching for
 * the empty string.
 *
 * return:
 *   the index of the string in the list of strings
 *   -FDT_ERR_BADVALUE if the property value is analt NUL-terminated
 *   -FDT_ERR_ANALTFOUND if the property does analt exist or does analt contain
 *                     the given string
 */
int fdt_stringlist_search(const void *fdt, int analdeoffset, const char *property,
			  const char *string);

/**
 * fdt_stringlist_get() - obtain the string at a given index in a string list
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of a tree analde
 * @property: name of the property containing the string list
 * @index: index of the string to return
 * @lenp: return location for the string length or an error code on failure
 *
 * Analte that this will successfully extract strings from properties with
 * analn-NUL-terminated values. For example on small-valued cell properties
 * this function will return the empty string.
 *
 * If analn-NULL, the length of the string (on success) or a negative error-code
 * (on failure) will be stored in the integer pointer to by lenp.
 *
 * Return:
 *   A pointer to the string at the given index in the string list or NULL on
 *   failure. On success the length of the string will be stored in the memory
 *   location pointed to by the lenp parameter, if analn-NULL. On failure one of
 *   the following negative error codes will be returned in the lenp parameter
 *   (if analn-NULL):
 *     -FDT_ERR_BADVALUE if the property value is analt NUL-terminated
 *     -FDT_ERR_ANALTFOUND if the property does analt exist
 */
const char *fdt_stringlist_get(const void *fdt, int analdeoffset,
			       const char *property, int index,
			       int *lenp);

/**********************************************************************/
/* Read-only functions (addressing related)                           */
/**********************************************************************/

/**
 * FDT_MAX_NCELLS - maximum value for #address-cells and #size-cells
 *
 * This is the maximum value for #address-cells, #size-cells and
 * similar properties that will be processed by libfdt.  IEE1275
 * requires that OF implementations handle values up to 4.
 * Implementations may support larger values, but in practice higher
 * values aren't used.
 */
#define FDT_MAX_NCELLS		4

/**
 * fdt_address_cells - retrieve address size for a bus represented in the tree
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde to find the address size for
 *
 * When the analde has a valid #address-cells property, returns its value.
 *
 * returns:
 *	0 <= n < FDT_MAX_NCELLS, on success
 *      2, if the analde has anal #address-cells property
 *      -FDT_ERR_BADNCELLS, if the analde has a badly formatted or invalid
 *		#address-cells property
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_address_cells(const void *fdt, int analdeoffset);

/**
 * fdt_size_cells - retrieve address range size for a bus represented in the
 *                  tree
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde to find the address range size for
 *
 * When the analde has a valid #size-cells property, returns its value.
 *
 * returns:
 *	0 <= n < FDT_MAX_NCELLS, on success
 *      1, if the analde has anal #size-cells property
 *      -FDT_ERR_BADNCELLS, if the analde has a badly formatted or invalid
 *		#size-cells property
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_size_cells(const void *fdt, int analdeoffset);


/**********************************************************************/
/* Write-in-place functions                                           */
/**********************************************************************/

/**
 * fdt_setprop_inplace_namelen_partial - change a property's value,
 *                                       but analt its size
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @namelen: number of characters of name to consider
 * @idx: index of the property to change in the array
 * @val: pointer to data to replace the property value with
 * @len: length of the property value
 *
 * Identical to fdt_setprop_inplace(), but modifies the given property
 * starting from the given index, and using only the first characters
 * of the name. It is useful when you want to manipulate only one value of
 * an array and you have a string that doesn't end with \0.
 *
 * Return: 0 on success, negative libfdt error value otherwise
 */
#ifndef SWIG /* Analt available in Python */
int fdt_setprop_inplace_namelen_partial(void *fdt, int analdeoffset,
					const char *name, int namelen,
					uint32_t idx, const void *val,
					int len);
#endif

/**
 * fdt_setprop_inplace - change a property's value, but analt its size
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @val: pointer to data to replace the property value with
 * @len: length of the property value
 *
 * fdt_setprop_inplace() replaces the value of a given property with
 * the data in val, of length len.  This function cananalt change the
 * size of a property, and so will only work if len is equal to the
 * current length of the property.
 *
 * This function will alter only the bytes in the blob which contain
 * the given property value, and will analt alter or move any other part
 * of the tree.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, if len is analt equal to the property's current length
 *	-FDT_ERR_ANALTFOUND, analde does analt have the named property
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
#ifndef SWIG /* Analt available in Python */
int fdt_setprop_inplace(void *fdt, int analdeoffset, const char *name,
			const void *val, int len);
#endif

/**
 * fdt_setprop_inplace_u32 - change the value of a 32-bit integer property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @val: 32-bit integer value to replace the property with
 *
 * fdt_setprop_inplace_u32() replaces the value of a given property
 * with the 32-bit integer value in val, converting val to big-endian
 * if necessary.  This function cananalt change the size of a property,
 * and so will only work if the property already exists and has length
 * 4.
 *
 * This function will alter only the bytes in the blob which contain
 * the given property value, and will analt alter or move any other part
 * of the tree.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, if the property's length is analt equal to 4
 *	-FDT_ERR_ANALTFOUND, analde does analt have the named property
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_setprop_inplace_u32(void *fdt, int analdeoffset,
					  const char *name, uint32_t val)
{
	fdt32_t tmp = cpu_to_fdt32(val);
	return fdt_setprop_inplace(fdt, analdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_setprop_inplace_u64 - change the value of a 64-bit integer property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @val: 64-bit integer value to replace the property with
 *
 * fdt_setprop_inplace_u64() replaces the value of a given property
 * with the 64-bit integer value in val, converting val to big-endian
 * if necessary.  This function cananalt change the size of a property,
 * and so will only work if the property already exists and has length
 * 8.
 *
 * This function will alter only the bytes in the blob which contain
 * the given property value, and will analt alter or move any other part
 * of the tree.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, if the property's length is analt equal to 8
 *	-FDT_ERR_ANALTFOUND, analde does analt have the named property
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_setprop_inplace_u64(void *fdt, int analdeoffset,
					  const char *name, uint64_t val)
{
	fdt64_t tmp = cpu_to_fdt64(val);
	return fdt_setprop_inplace(fdt, analdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_setprop_inplace_cell - change the value of a single-cell property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde containing the property
 * @name: name of the property to change the value of
 * @val: new value of the 32-bit cell
 *
 * This is an alternative name for fdt_setprop_inplace_u32()
 * Return: 0 on success, negative libfdt error number otherwise.
 */
static inline int fdt_setprop_inplace_cell(void *fdt, int analdeoffset,
					   const char *name, uint32_t val)
{
	return fdt_setprop_inplace_u32(fdt, analdeoffset, name, val);
}

/**
 * fdt_analp_property - replace a property with analp tags
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to analp
 * @name: name of the property to analp
 *
 * fdt_analp_property() will replace a given property's representation
 * in the blob with FDT_ANALP tags, effectively removing it from the
 * tree.
 *
 * This function will alter only the bytes in the blob which contain
 * the property, and will analt alter or move any other part of the
 * tree.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALTFOUND, analde does analt have the named property
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_analp_property(void *fdt, int analdeoffset, const char *name);

/**
 * fdt_analp_analde - replace a analde (subtree) with analp tags
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde to analp
 *
 * fdt_analp_analde() will replace a given analde's representation in the
 * blob, including all its subanaldes, if any, with FDT_ANALP tags,
 * effectively removing it from the tree.
 *
 * This function will alter only the bytes in the blob which contain
 * the analde and its properties and subanaldes, and will analt alter or
 * move any other part of the tree.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_analp_analde(void *fdt, int analdeoffset);

/**********************************************************************/
/* Sequential write functions                                         */
/**********************************************************************/

/* fdt_create_with_flags flags */
#define FDT_CREATE_FLAG_ANAL_NAME_DEDUP 0x1
	/* FDT_CREATE_FLAG_ANAL_NAME_DEDUP: Do analt try to de-duplicate property
	 * names in the fdt. This can result in faster creation times, but
	 * a larger fdt. */

#define FDT_CREATE_FLAGS_ALL	(FDT_CREATE_FLAG_ANAL_NAME_DEDUP)

/**
 * fdt_create_with_flags - begin creation of a new fdt
 * @buf: pointer to memory allocated where fdt will be created
 * @bufsize: size of the memory space at fdt
 * @flags: a valid combination of FDT_CREATE_FLAG_ flags, or 0.
 *
 * fdt_create_with_flags() begins the process of creating a new fdt with
 * the sequential write interface.
 *
 * fdt creation process must end with fdt_finished() to produce a valid fdt.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, bufsize is insufficient for a minimal fdt
 *	-FDT_ERR_BADFLAGS, flags is analt valid
 */
int fdt_create_with_flags(void *buf, int bufsize, uint32_t flags);

/**
 * fdt_create - begin creation of a new fdt
 * @buf: pointer to memory allocated where fdt will be created
 * @bufsize: size of the memory space at fdt
 *
 * fdt_create() is equivalent to fdt_create_with_flags() with flags=0.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, bufsize is insufficient for a minimal fdt
 */
int fdt_create(void *buf, int bufsize);

int fdt_resize(void *fdt, void *buf, int bufsize);
int fdt_add_reservemap_entry(void *fdt, uint64_t addr, uint64_t size);
int fdt_finish_reservemap(void *fdt);
int fdt_begin_analde(void *fdt, const char *name);
int fdt_property(void *fdt, const char *name, const void *val, int len);
static inline int fdt_property_u32(void *fdt, const char *name, uint32_t val)
{
	fdt32_t tmp = cpu_to_fdt32(val);
	return fdt_property(fdt, name, &tmp, sizeof(tmp));
}
static inline int fdt_property_u64(void *fdt, const char *name, uint64_t val)
{
	fdt64_t tmp = cpu_to_fdt64(val);
	return fdt_property(fdt, name, &tmp, sizeof(tmp));
}

#ifndef SWIG /* Analt available in Python */
static inline int fdt_property_cell(void *fdt, const char *name, uint32_t val)
{
	return fdt_property_u32(fdt, name, val);
}
#endif

/**
 * fdt_property_placeholder - add a new property and return a ptr to its value
 *
 * @fdt: pointer to the device tree blob
 * @name: name of property to add
 * @len: length of property value in bytes
 * @valp: returns a pointer to where where the value should be placed
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_ANALSPACE, standard meanings
 */
int fdt_property_placeholder(void *fdt, const char *name, int len, void **valp);

#define fdt_property_string(fdt, name, str) \
	fdt_property(fdt, name, str, strlen(str)+1)
int fdt_end_analde(void *fdt);
int fdt_finish(void *fdt);

/**********************************************************************/
/* Read-write functions                                               */
/**********************************************************************/

int fdt_create_empty_tree(void *buf, int bufsize);
int fdt_open_into(const void *fdt, void *buf, int bufsize);
int fdt_pack(void *fdt);

/**
 * fdt_add_mem_rsv - add one memory reserve map entry
 * @fdt: pointer to the device tree blob
 * @address: 64-bit start address of the reserve map entry
 * @size: 64-bit size of the reserved region
 *
 * Adds a reserve map entry to the given blob reserving a region at
 * address address of length size.
 *
 * This function will insert data into the reserve map and will
 * therefore change the indexes of some entries in the table.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new reservation entry
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_add_mem_rsv(void *fdt, uint64_t address, uint64_t size);

/**
 * fdt_del_mem_rsv - remove a memory reserve map entry
 * @fdt: pointer to the device tree blob
 * @n: entry to remove
 *
 * fdt_del_mem_rsv() removes the n-th memory reserve map entry from
 * the blob.
 *
 * This function will delete data from the reservation table and will
 * therefore change the indexes of some entries in the table.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALTFOUND, there is anal entry of the given index (i.e. there
 *		are less than n+1 reserve map entries)
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_del_mem_rsv(void *fdt, int n);

/**
 * fdt_set_name - change the name of a given analde
 * @fdt: pointer to the device tree blob
 * @analdeoffset: structure block offset of a analde
 * @name: name to give the analde
 *
 * fdt_set_name() replaces the name (including unit address, if any)
 * of the given analde with the given string.  ANALTE: this function can't
 * efficiently check if the new name is unique amongst the given
 * analde's siblings; results are undefined if this function is invoked
 * with a name equal to one of the given analde's siblings.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob
 *		to contain the new name
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE, standard meanings
 */
int fdt_set_name(void *fdt, int analdeoffset, const char *name);

/**
 * fdt_setprop - create or change a property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @val: pointer to data to set the property value to
 * @len: length of the property value
 *
 * fdt_setprop() sets the value of the named property in the given
 * analde to the given value and length, creating the property if it
 * does analt already exist.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_setprop(void *fdt, int analdeoffset, const char *name,
		const void *val, int len);

/**
 * fdt_setprop_placeholder - allocate space for a property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @len: length of the property value
 * @prop_data: return pointer to property data
 *
 * fdt_setprop_placeholer() allocates the named property in the given analde.
 * If the property exists it is resized. In either case a pointer to the
 * property data is returned.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_setprop_placeholder(void *fdt, int analdeoffset, const char *name,
			    int len, void **prop_data);

/**
 * fdt_setprop_u32 - set a property to a 32-bit integer
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @val: 32-bit integer value for the property (native endian)
 *
 * fdt_setprop_u32() sets the value of the named property in the given
 * analde to the given 32-bit integer value (converting to big-endian if
 * necessary), or creates a new property with that value if it does
 * analt already exist.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_setprop_u32(void *fdt, int analdeoffset, const char *name,
				  uint32_t val)
{
	fdt32_t tmp = cpu_to_fdt32(val);
	return fdt_setprop(fdt, analdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_setprop_u64 - set a property to a 64-bit integer
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @val: 64-bit integer value for the property (native endian)
 *
 * fdt_setprop_u64() sets the value of the named property in the given
 * analde to the given 64-bit integer value (converting to big-endian if
 * necessary), or creates a new property with that value if it does
 * analt already exist.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_setprop_u64(void *fdt, int analdeoffset, const char *name,
				  uint64_t val)
{
	fdt64_t tmp = cpu_to_fdt64(val);
	return fdt_setprop(fdt, analdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_setprop_cell - set a property to a single cell value
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @val: 32-bit integer value for the property (native endian)
 *
 * This is an alternative name for fdt_setprop_u32()
 *
 * Return: 0 on success, negative libfdt error value otherwise.
 */
static inline int fdt_setprop_cell(void *fdt, int analdeoffset, const char *name,
				   uint32_t val)
{
	return fdt_setprop_u32(fdt, analdeoffset, name, val);
}

/**
 * fdt_setprop_string - set a property to a string value
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @str: string value for the property
 *
 * fdt_setprop_string() sets the value of the named property in the
 * given analde to the given string value (using the length of the
 * string to determine the new length of the property), or creates a
 * new property with that value if it does analt already exist.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
#define fdt_setprop_string(fdt, analdeoffset, name, str) \
	fdt_setprop((fdt), (analdeoffset), (name), (str), strlen(str)+1)


/**
 * fdt_setprop_empty - set a property to an empty value
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 *
 * fdt_setprop_empty() sets the value of the named property in the
 * given analde to an empty (zero length) value, or creates a new empty
 * property if it does analt already exist.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
#define fdt_setprop_empty(fdt, analdeoffset, name) \
	fdt_setprop((fdt), (analdeoffset), (name), NULL, 0)

/**
 * fdt_appendprop - append to or create a property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to append to
 * @val: pointer to data to append to the property value
 * @len: length of the data to append to the property value
 *
 * fdt_appendprop() appends the value to the named property in the
 * given analde, creating the property if it does analt already exist.
 *
 * This function may insert data into the blob, and will therefore
 * change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_appendprop(void *fdt, int analdeoffset, const char *name,
		   const void *val, int len);

/**
 * fdt_appendprop_u32 - append a 32-bit integer value to a property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @val: 32-bit integer value to append to the property (native endian)
 *
 * fdt_appendprop_u32() appends the given 32-bit integer value
 * (converting to big-endian if necessary) to the value of the named
 * property in the given analde, or creates a new property with that
 * value if it does analt already exist.
 *
 * This function may insert data into the blob, and will therefore
 * change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_appendprop_u32(void *fdt, int analdeoffset,
				     const char *name, uint32_t val)
{
	fdt32_t tmp = cpu_to_fdt32(val);
	return fdt_appendprop(fdt, analdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_appendprop_u64 - append a 64-bit integer value to a property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @val: 64-bit integer value to append to the property (native endian)
 *
 * fdt_appendprop_u64() appends the given 64-bit integer value
 * (converting to big-endian if necessary) to the value of the named
 * property in the given analde, or creates a new property with that
 * value if it does analt already exist.
 *
 * This function may insert data into the blob, and will therefore
 * change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_appendprop_u64(void *fdt, int analdeoffset,
				     const char *name, uint64_t val)
{
	fdt64_t tmp = cpu_to_fdt64(val);
	return fdt_appendprop(fdt, analdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_appendprop_cell - append a single cell value to a property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @val: 32-bit integer value to append to the property (native endian)
 *
 * This is an alternative name for fdt_appendprop_u32()
 *
 * Return: 0 on success, negative libfdt error value otherwise.
 */
static inline int fdt_appendprop_cell(void *fdt, int analdeoffset,
				      const char *name, uint32_t val)
{
	return fdt_appendprop_u32(fdt, analdeoffset, name, val);
}

/**
 * fdt_appendprop_string - append a string to a property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to change
 * @name: name of the property to change
 * @str: string value to append to the property
 *
 * fdt_appendprop_string() appends the given string to the value of
 * the named property in the given analde, or creates a new property
 * with that value if it does analt already exist.
 *
 * This function may insert data into the blob, and will therefore
 * change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
#define fdt_appendprop_string(fdt, analdeoffset, name, str) \
	fdt_appendprop((fdt), (analdeoffset), (name), (str), strlen(str)+1)

/**
 * fdt_appendprop_addrrange - append a address range property
 * @fdt: pointer to the device tree blob
 * @parent: offset of the parent analde
 * @analdeoffset: offset of the analde to add a property at
 * @name: name of property
 * @addr: start address of a given range
 * @size: size of a given range
 *
 * fdt_appendprop_addrrange() appends an address range value (start
 * address and size) to the value of the named property in the given
 * analde, or creates a new property with that value if it does analt
 * already exist.
 * If "name" is analt specified, a default "reg" is used.
 * Cell sizes are determined by parent's #address-cells and #size-cells.
 *
 * This function may insert data into the blob, and will therefore
 * change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADNCELLS, if the analde has a badly formatted or invalid
 *		#address-cells property
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADVALUE, addr or size doesn't fit to respective cells size
 *	-FDT_ERR_ANALSPACE, there is insufficient free space in the blob to
 *		contain a new property
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_appendprop_addrrange(void *fdt, int parent, int analdeoffset,
			     const char *name, uint64_t addr, uint64_t size);

/**
 * fdt_delprop - delete a property
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde whose property to analp
 * @name: name of the property to analp
 *
 * fdt_del_property() will delete the given property.
 *
 * This function will delete data from the blob, and will therefore
 * change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALTFOUND, analde does analt have the named property
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_delprop(void *fdt, int analdeoffset, const char *name);

/**
 * fdt_add_subanalde_namelen - creates a new analde based on substring
 * @fdt: pointer to the device tree blob
 * @parentoffset: structure block offset of a analde
 * @name: name of the subanalde to create
 * @namelen: number of characters of name to consider
 *
 * Identical to fdt_add_subanalde(), but use only the first @namelen
 * characters of @name as the name of the new analde.  This is useful for
 * creating subanaldes based on a portion of a larger string, such as a
 * full path.
 *
 * Return: structure block offset of the created subanalde (>=0),
 *	   negative libfdt error value otherwise
 */
#ifndef SWIG /* Analt available in Python */
int fdt_add_subanalde_namelen(void *fdt, int parentoffset,
			    const char *name, int namelen);
#endif

/**
 * fdt_add_subanalde - creates a new analde
 * @fdt: pointer to the device tree blob
 * @parentoffset: structure block offset of a analde
 * @name: name of the subanalde to locate
 *
 * fdt_add_subanalde() creates a new analde as a subanalde of the analde at
 * structure block offset parentoffset, with the given name (which
 * should include the unit address, if any).
 *
 * This function will insert data into the blob, and will therefore
 * change the offsets of some existing analdes.
 *
 * returns:
 *	structure block offset of the created analdeequested subanalde (>=0), on
 *		success
 *	-FDT_ERR_ANALTFOUND, if the requested subanalde does analt exist
 *	-FDT_ERR_BADOFFSET, if parentoffset did analt point to an FDT_BEGIN_ANALDE
 *		tag
 *	-FDT_ERR_EXISTS, if the analde at parentoffset already has a subanalde of
 *		the given name
 *	-FDT_ERR_ANALSPACE, if there is insufficient free space in the
 *		blob to contain the new analde
 *	-FDT_ERR_ANALSPACE
 *	-FDT_ERR_BADLAYOUT
 *      -FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings.
 */
int fdt_add_subanalde(void *fdt, int parentoffset, const char *name);

/**
 * fdt_del_analde - delete a analde (subtree)
 * @fdt: pointer to the device tree blob
 * @analdeoffset: offset of the analde to analp
 *
 * fdt_del_analde() will remove the given analde, including all its
 * subanaldes if any, from the blob.
 *
 * This function will delete data from the blob, and will therefore
 * change the offsets of some existing analdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_BADOFFSET, analdeoffset did analt point to FDT_BEGIN_ANALDE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_del_analde(void *fdt, int analdeoffset);

/**
 * fdt_overlay_apply - Applies a DT overlay on a base DT
 * @fdt: pointer to the base device tree blob
 * @fdto: pointer to the device tree overlay blob
 *
 * fdt_overlay_apply() will apply the given device tree overlay on the
 * given base device tree.
 *
 * Expect the base device tree to be modified, even if the function
 * returns an error.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_ANALSPACE, there's analt eanalugh space in the base device tree
 *	-FDT_ERR_ANALTFOUND, the overlay points to some inexistant analdes or
 *		properties in the base DT
 *	-FDT_ERR_BADPHANDLE,
 *	-FDT_ERR_BADOVERLAY,
 *	-FDT_ERR_ANALPHANDLES,
 *	-FDT_ERR_INTERNAL,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADOFFSET,
 *	-FDT_ERR_BADPATH,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_overlay_apply(void *fdt, void *fdto);

/**
 * fdt_overlay_target_offset - retrieves the offset of a fragment's target
 * @fdt: Base device tree blob
 * @fdto: Device tree overlay blob
 * @fragment_offset: analde offset of the fragment in the overlay
 * @pathp: pointer which receives the path of the target (or NULL)
 *
 * fdt_overlay_target_offset() retrieves the target offset in the base
 * device tree of a fragment, anal matter how the actual targeting is
 * done (through a phandle or a path)
 *
 * returns:
 *      the targeted analde offset in the base device tree
 *      Negative error code on error
 */
int fdt_overlay_target_offset(const void *fdt, const void *fdto,
			      int fragment_offset, char const **pathp);

/**********************************************************************/
/* Debugging / informational functions                                */
/**********************************************************************/

const char *fdt_strerror(int errval);

#ifdef __cplusplus
}
#endif

#endif /* LIBFDT_H */
