/* SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-2-Clause) */
#ifndef LIBFDT_H
#define LIBFDT_H
/*
 * libfdt - Flat Device Tree manipulation
 * Copyright (C) 2006 David Gibson, IBM Corporation.
 */

#include "libfdt_env.h"
#include "fdt.h"

#define FDT_FIRST_SUPPORTED_VERSION	0x02
#define FDT_LAST_SUPPORTED_VERSION	0x11

/* Error codes: informative error codes */
#define FDT_ERR_NOTFOUND	1
	/* FDT_ERR_NOTFOUND: The requested yesde or property does yest exist */
#define FDT_ERR_EXISTS		2
	/* FDT_ERR_EXISTS: Attempted to create a yesde or property which
	 * already exists */
#define FDT_ERR_NOSPACE		3
	/* FDT_ERR_NOSPACE: Operation needed to expand the device
	 * tree, but its buffer did yest have sufficient space to
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
	 * yest permitted. */
#define FDT_ERR_BADSTATE	7
	/* FDT_ERR_BADSTATE: Function was passed an incomplete device
	 * tree created by the sequential-write functions, which is
	 * yest sufficiently complete for the requested operation. */

/* Error codes: codes for bad device tree blobs */
#define FDT_ERR_TRUNCATED	8
	/* FDT_ERR_TRUNCATED: FDT or a sub-block is improperly
	 * terminated (overflows, goes outside allowed bounds, or
	 * isn't properly terminated).  */
#define FDT_ERR_BADMAGIC	9
	/* FDT_ERR_BADMAGIC: Given "device tree" appears yest to be a
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
	 * yesdes, or subyesdes preceding properties). */
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
	 * is yest NUL-terminated within the length of its value. */

#define FDT_ERR_BADOVERLAY	16
	/* FDT_ERR_BADOVERLAY: The device tree overlay, while
	 * correctly structured, canyest be applied due to some
	 * unexpected or missing value, property or yesde. */

#define FDT_ERR_NOPHANDLES	17
	/* FDT_ERR_NOPHANDLES: The device tree doesn't have any
	 * phandle available anymore without causing an overflow */

#define FDT_ERR_BADFLAGS	18
	/* FDT_ERR_BADFLAGS: The function was passed a flags field that
	 * contains invalid flags or an invalid combination of flags. */

#define FDT_ERR_MAX		18

/* constants */
#define FDT_MAX_PHANDLE 0xfffffffe
	/* Valid values for phandles range from 1 to 2^32-2. */

/**********************************************************************/
/* Low-level functions (you probably don't need these)                */
/**********************************************************************/

#ifndef SWIG /* This function is yest useful in Python */
const void *fdt_offset_ptr(const void *fdt, int offset, unsigned int checklen);
#endif
static inline void *fdt_offset_ptr_w(void *fdt, int offset, int checklen)
{
	return (void *)(uintptr_t)fdt_offset_ptr(fdt, offset, checklen);
}

uint32_t fdt_next_tag(const void *fdt, int offset, int *nextoffset);

/*
 * Alignment helpers:
 *     These helpers access words from a device tree blob.  They're
 *     built to work even with unaligned pointers on platforms (ike
 *     ARM) that don't like unaligned loads and stores
 */

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
	uint8_t *bp = property;

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
	uint8_t *bp = property;

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

int fdt_next_yesde(const void *fdt, int offset, int *depth);

/**
 * fdt_first_subyesde() - get offset of first direct subyesde
 *
 * @fdt:	FDT blob
 * @offset:	Offset of yesde to check
 * @return offset of first subyesde, or -FDT_ERR_NOTFOUND if there is yesne
 */
int fdt_first_subyesde(const void *fdt, int offset);

/**
 * fdt_next_subyesde() - get offset of next direct subyesde
 *
 * After first calling fdt_first_subyesde(), call this function repeatedly to
 * get direct subyesdes of a parent yesde.
 *
 * @fdt:	FDT blob
 * @offset:	Offset of previous subyesde
 * @return offset of next subyesde, or -FDT_ERR_NOTFOUND if there are yes more
 * subyesdes
 */
int fdt_next_subyesde(const void *fdt, int offset);

/**
 * fdt_for_each_subyesde - iterate over all subyesdes of a parent
 *
 * @yesde:	child yesde (int, lvalue)
 * @fdt:	FDT blob (const void *)
 * @parent:	parent yesde (int)
 *
 * This is actually a wrapper around a for loop and would be used like so:
 *
 *	fdt_for_each_subyesde(yesde, fdt, parent) {
 *		Use yesde
 *		...
 *	}
 *
 *	if ((yesde < 0) && (yesde != -FDT_ERR_NOTFOUND)) {
 *		Error handling
 *	}
 *
 * Note that this is implemented as a macro and @yesde is used as
 * iterator in the loop. The parent variable be constant or even a
 * literal.
 *
 */
#define fdt_for_each_subyesde(yesde, fdt, parent)		\
	for (yesde = fdt_first_subyesde(fdt, parent);	\
	     yesde >= 0;					\
	     yesde = fdt_next_subyesde(fdt, yesde))

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
 */
size_t fdt_header_size_(uint32_t version);
static inline size_t fdt_header_size(const void *fdt)
{
	return fdt_header_size_(fdt_version(fdt));
}

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
 *     -FDT_ERR_NOSPACE, bufsize is insufficient to contain the device tree
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
 * device tree. This will igyesre badly formatted phandles, or phandles
 * with a value of 0 or -1.
 *
 * This function is deprecated in favour of fdt_find_max_phandle().
 *
 * returns:
 *      the highest phandle on success
 *      0, if yes phandle was found in the device tree
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
 * Returns:
 *   0 on success or a negative error-code on failure
 */
int fdt_generate_phandle(const void *fdt, uint32_t *phandle);

/**
 * fdt_num_mem_rsv - retrieve the number of memory reserve map entries
 * @fdt: pointer to the device tree blob
 *
 * Returns the number of entries in the device tree blob's memory
 * reservation map.  This does yest include the terminating 0,0 entry
 * or any other (0,0) entries reserved for expansion.
 *
 * returns:
 *     the number of entries
 */
int fdt_num_mem_rsv(const void *fdt);

/**
 * fdt_get_mem_rsv - retrieve one memory reserve map entry
 * @fdt: pointer to the device tree blob
 * @address, @size: pointers to 64-bit variables
 *
 * On success, *address and *size will contain the address and size of
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
 * fdt_subyesde_offset_namelen - find a subyesde based on substring
 * @fdt: pointer to the device tree blob
 * @parentoffset: structure block offset of a yesde
 * @name: name of the subyesde to locate
 * @namelen: number of characters of name to consider
 *
 * Identical to fdt_subyesde_offset(), but only examine the first
 * namelen characters of name for matching the subyesde name.  This is
 * useful for finding subyesdes based on a portion of a larger string,
 * such as a full path.
 */
#ifndef SWIG /* Not available in Python */
int fdt_subyesde_offset_namelen(const void *fdt, int parentoffset,
			       const char *name, int namelen);
#endif
/**
 * fdt_subyesde_offset - find a subyesde of a given yesde
 * @fdt: pointer to the device tree blob
 * @parentoffset: structure block offset of a yesde
 * @name: name of the subyesde to locate
 *
 * fdt_subyesde_offset() finds a subyesde of the yesde at structure block
 * offset parentoffset with the given name.  name may include a unit
 * address, in which case fdt_subyesde_offset() will find the subyesde
 * with that unit address, or the unit address may be omitted, in
 * which case fdt_subyesde_offset() will find an arbitrary subyesde
 * whose name excluding unit address matches the given name.
 *
 * returns:
 *	structure block offset of the requested subyesde (>=0), on success
 *	-FDT_ERR_NOTFOUND, if the requested subyesde does yest exist
 *	-FDT_ERR_BADOFFSET, if parentoffset did yest point to an FDT_BEGIN_NODE
 *		tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings.
 */
int fdt_subyesde_offset(const void *fdt, int parentoffset, const char *name);

/**
 * fdt_path_offset_namelen - find a tree yesde by its full path
 * @fdt: pointer to the device tree blob
 * @path: full path of the yesde to locate
 * @namelen: number of characters of path to consider
 *
 * Identical to fdt_path_offset(), but only consider the first namelen
 * characters of path as the path name.
 */
#ifndef SWIG /* Not available in Python */
int fdt_path_offset_namelen(const void *fdt, const char *path, int namelen);
#endif

/**
 * fdt_path_offset - find a tree yesde by its full path
 * @fdt: pointer to the device tree blob
 * @path: full path of the yesde to locate
 *
 * fdt_path_offset() finds a yesde of a given path in the device tree.
 * Each path component may omit the unit address portion, but the
 * results of this are undefined if any such path component is
 * ambiguous (that is if there are multiple yesdes at the relevant
 * level matching the given component, differentiated only by unit
 * address).
 *
 * returns:
 *	structure block offset of the yesde with the requested path (>=0), on
 *		success
 *	-FDT_ERR_BADPATH, given path does yest begin with '/' or is invalid
 *	-FDT_ERR_NOTFOUND, if the requested yesde does yest exist
 *      -FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings.
 */
int fdt_path_offset(const void *fdt, const char *path);

/**
 * fdt_get_name - retrieve the name of a given yesde
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: structure block offset of the starting yesde
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * fdt_get_name() retrieves the name (including unit address) of the
 * device tree yesde at structure block offset yesdeoffset.  If lenp is
 * yesn-NULL, the length of this name is also returned, in the integer
 * pointed to by lenp.
 *
 * returns:
 *	pointer to the yesde's name, on success
 *		If lenp is yesn-NULL, *lenp contains the length of that name
 *			(>=0)
 *	NULL, on error
 *		if lenp is yesn-NULL *lenp contains an error code (<0):
 *		-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE
 *			tag
 *		-FDT_ERR_BADMAGIC,
 *		-FDT_ERR_BADVERSION,
 *		-FDT_ERR_BADSTATE, standard meanings
 */
const char *fdt_get_name(const void *fdt, int yesdeoffset, int *lenp);

/**
 * fdt_first_property_offset - find the offset of a yesde's first property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: structure block offset of a yesde
 *
 * fdt_first_property_offset() finds the first property of the yesde at
 * the given structure block offset.
 *
 * returns:
 *	structure block offset of the property (>=0), on success
 *	-FDT_ERR_NOTFOUND, if the requested yesde has yes properties
 *	-FDT_ERR_BADOFFSET, if yesdeoffset did yest point to an FDT_BEGIN_NODE tag
 *      -FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings.
 */
int fdt_first_property_offset(const void *fdt, int yesdeoffset);

/**
 * fdt_next_property_offset - step through a yesde's properties
 * @fdt: pointer to the device tree blob
 * @offset: structure block offset of a property
 *
 * fdt_next_property_offset() finds the property immediately after the
 * one at the given structure block offset.  This will be a property
 * of the same yesde as the given property.
 *
 * returns:
 *	structure block offset of the next property (>=0), on success
 *	-FDT_ERR_NOTFOUND, if the given property is the last in its yesde
 *	-FDT_ERR_BADOFFSET, if yesdeoffset did yest point to an FDT_PROP tag
 *      -FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings.
 */
int fdt_next_property_offset(const void *fdt, int offset);

/**
 * fdt_for_each_property_offset - iterate over all properties of a yesde
 *
 * @property_offset:	property offset (int, lvalue)
 * @fdt:		FDT blob (const void *)
 * @yesde:		yesde offset (int)
 *
 * This is actually a wrapper around a for loop and would be used like so:
 *
 *	fdt_for_each_property_offset(property, fdt, yesde) {
 *		Use property
 *		...
 *	}
 *
 *	if ((property < 0) && (property != -FDT_ERR_NOTFOUND)) {
 *		Error handling
 *	}
 *
 * Note that this is implemented as a macro and property is used as
 * iterator in the loop. The yesde variable can be constant or even a
 * literal.
 */
#define fdt_for_each_property_offset(property, fdt, yesde)	\
	for (property = fdt_first_property_offset(fdt, yesde);	\
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
 * offset.  If lenp is yesn-NULL, the length of the property value is
 * also returned, in the integer pointed to by lenp.
 *
 * Note that this code only works on device tree versions >= 16. fdt_getprop()
 * works on all versions.
 *
 * returns:
 *	pointer to the structure representing the property
 *		if lenp is yesn-NULL, *lenp contains the length of the property
 *		value (>=0)
 *	NULL, on error
 *		if lenp is yesn-NULL, *lenp contains an error code (<0):
 *		-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_PROP tag
 *		-FDT_ERR_BADMAGIC,
 *		-FDT_ERR_BADVERSION,
 *		-FDT_ERR_BADSTATE,
 *		-FDT_ERR_BADSTRUCTURE,
 *		-FDT_ERR_TRUNCATED, standard meanings
 */
const struct fdt_property *fdt_get_property_by_offset(const void *fdt,
						      int offset,
						      int *lenp);

/**
 * fdt_get_property_namelen - find a property based on substring
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to find
 * @name: name of the property to find
 * @namelen: number of characters of name to consider
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * Identical to fdt_get_property(), but only examine the first namelen
 * characters of name for matching the property name.
 */
#ifndef SWIG /* Not available in Python */
const struct fdt_property *fdt_get_property_namelen(const void *fdt,
						    int yesdeoffset,
						    const char *name,
						    int namelen, int *lenp);
#endif

/**
 * fdt_get_property - find a given property in a given yesde
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to find
 * @name: name of the property to find
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * fdt_get_property() retrieves a pointer to the fdt_property
 * structure within the device tree blob corresponding to the property
 * named 'name' of the yesde at offset yesdeoffset.  If lenp is
 * yesn-NULL, the length of the property value is also returned, in the
 * integer pointed to by lenp.
 *
 * returns:
 *	pointer to the structure representing the property
 *		if lenp is yesn-NULL, *lenp contains the length of the property
 *		value (>=0)
 *	NULL, on error
 *		if lenp is yesn-NULL, *lenp contains an error code (<0):
 *		-FDT_ERR_NOTFOUND, yesde does yest have named property
 *		-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE
 *			tag
 *		-FDT_ERR_BADMAGIC,
 *		-FDT_ERR_BADVERSION,
 *		-FDT_ERR_BADSTATE,
 *		-FDT_ERR_BADSTRUCTURE,
 *		-FDT_ERR_TRUNCATED, standard meanings
 */
const struct fdt_property *fdt_get_property(const void *fdt, int yesdeoffset,
					    const char *name, int *lenp);
static inline struct fdt_property *fdt_get_property_w(void *fdt, int yesdeoffset,
						      const char *name,
						      int *lenp)
{
	return (struct fdt_property *)(uintptr_t)
		fdt_get_property(fdt, yesdeoffset, name, lenp);
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
 * to within the device blob itself, yest a copy of the value).  If
 * lenp is yesn-NULL, the length of the property value is also
 * returned, in the integer pointed to by lenp.  If namep is yesn-NULL,
 * the property's namne will also be returned in the char * pointed to
 * by namep (this will be a pointer to within the device tree's string
 * block, yest a new copy of the name).
 *
 * returns:
 *	pointer to the property's value
 *		if lenp is yesn-NULL, *lenp contains the length of the property
 *		value (>=0)
 *		if namep is yesn-NULL *namep contiains a pointer to the property
 *		name.
 *	NULL, on error
 *		if lenp is yesn-NULL, *lenp contains an error code (<0):
 *		-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_PROP tag
 *		-FDT_ERR_BADMAGIC,
 *		-FDT_ERR_BADVERSION,
 *		-FDT_ERR_BADSTATE,
 *		-FDT_ERR_BADSTRUCTURE,
 *		-FDT_ERR_TRUNCATED, standard meanings
 */
#ifndef SWIG /* This function is yest useful in Python */
const void *fdt_getprop_by_offset(const void *fdt, int offset,
				  const char **namep, int *lenp);
#endif

/**
 * fdt_getprop_namelen - get property value based on substring
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to find
 * @name: name of the property to find
 * @namelen: number of characters of name to consider
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * Identical to fdt_getprop(), but only examine the first namelen
 * characters of name for matching the property name.
 */
#ifndef SWIG /* Not available in Python */
const void *fdt_getprop_namelen(const void *fdt, int yesdeoffset,
				const char *name, int namelen, int *lenp);
static inline void *fdt_getprop_namelen_w(void *fdt, int yesdeoffset,
					  const char *name, int namelen,
					  int *lenp)
{
	return (void *)(uintptr_t)fdt_getprop_namelen(fdt, yesdeoffset, name,
						      namelen, lenp);
}
#endif

/**
 * fdt_getprop - retrieve the value of a given property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to find
 * @name: name of the property to find
 * @lenp: pointer to an integer variable (will be overwritten) or NULL
 *
 * fdt_getprop() retrieves a pointer to the value of the property
 * named 'name' of the yesde at offset yesdeoffset (this will be a
 * pointer to within the device blob itself, yest a copy of the value).
 * If lenp is yesn-NULL, the length of the property value is also
 * returned, in the integer pointed to by lenp.
 *
 * returns:
 *	pointer to the property's value
 *		if lenp is yesn-NULL, *lenp contains the length of the property
 *		value (>=0)
 *	NULL, on error
 *		if lenp is yesn-NULL, *lenp contains an error code (<0):
 *		-FDT_ERR_NOTFOUND, yesde does yest have named property
 *		-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE
 *			tag
 *		-FDT_ERR_BADMAGIC,
 *		-FDT_ERR_BADVERSION,
 *		-FDT_ERR_BADSTATE,
 *		-FDT_ERR_BADSTRUCTURE,
 *		-FDT_ERR_TRUNCATED, standard meanings
 */
const void *fdt_getprop(const void *fdt, int yesdeoffset,
			const char *name, int *lenp);
static inline void *fdt_getprop_w(void *fdt, int yesdeoffset,
				  const char *name, int *lenp)
{
	return (void *)(uintptr_t)fdt_getprop(fdt, yesdeoffset, name, lenp);
}

/**
 * fdt_get_phandle - retrieve the phandle of a given yesde
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: structure block offset of the yesde
 *
 * fdt_get_phandle() retrieves the phandle of the device tree yesde at
 * structure block offset yesdeoffset.
 *
 * returns:
 *	the phandle of the yesde at yesdeoffset, on success (!= 0, != -1)
 *	0, if the yesde has yes phandle, or ayesther error occurs
 */
uint32_t fdt_get_phandle(const void *fdt, int yesdeoffset);

/**
 * fdt_get_alias_namelen - get alias based on substring
 * @fdt: pointer to the device tree blob
 * @name: name of the alias th look up
 * @namelen: number of characters of name to consider
 *
 * Identical to fdt_get_alias(), but only examine the first namelen
 * characters of name for matching the alias name.
 */
#ifndef SWIG /* Not available in Python */
const char *fdt_get_alias_namelen(const void *fdt,
				  const char *name, int namelen);
#endif

/**
 * fdt_get_alias - retrieve the path referenced by a given alias
 * @fdt: pointer to the device tree blob
 * @name: name of the alias th look up
 *
 * fdt_get_alias() retrieves the value of a given alias.  That is, the
 * value of the property named 'name' in the yesde /aliases.
 *
 * returns:
 *	a pointer to the expansion of the alias named 'name', if it exists
 *	NULL, if the given alias or the /aliases yesde does yest exist
 */
const char *fdt_get_alias(const void *fdt, const char *name);

/**
 * fdt_get_path - determine the full path of a yesde
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose path to find
 * @buf: character buffer to contain the returned path (will be overwritten)
 * @buflen: size of the character buffer at buf
 *
 * fdt_get_path() computes the full path of the yesde at offset
 * yesdeoffset, and records that path in the buffer at buf.
 *
 * NOTE: This function is expensive, as it must scan the device tree
 * structure from the start to yesdeoffset.
 *
 * returns:
 *	0, on success
 *		buf contains the absolute path of the yesde at
 *		yesdeoffset, as a NUL-terminated string.
 *	-FDT_ERR_BADOFFSET, yesdeoffset does yest refer to a BEGIN_NODE tag
 *	-FDT_ERR_NOSPACE, the path of the given yesde is longer than (bufsize-1)
 *		characters and will yest fit in the given buffer.
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_get_path(const void *fdt, int yesdeoffset, char *buf, int buflen);

/**
 * fdt_superyesde_atdepth_offset - find a specific ancestor of a yesde
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose parent to find
 * @superyesdedepth: depth of the ancestor to find
 * @yesdedepth: pointer to an integer variable (will be overwritten) or NULL
 *
 * fdt_superyesde_atdepth_offset() finds an ancestor of the given yesde
 * at a specific depth from the root (where the root itself has depth
 * 0, its immediate subyesdes depth 1 and so forth).  So
 *	fdt_superyesde_atdepth_offset(fdt, yesdeoffset, 0, NULL);
 * will always return 0, the offset of the root yesde.  If the yesde at
 * yesdeoffset has depth D, then:
 *	fdt_superyesde_atdepth_offset(fdt, yesdeoffset, D, NULL);
 * will return yesdeoffset itself.
 *
 * NOTE: This function is expensive, as it must scan the device tree
 * structure from the start to yesdeoffset.
 *
 * returns:
 *	structure block offset of the yesde at yesde offset's ancestor
 *		of depth superyesdedepth (>=0), on success
 *	-FDT_ERR_BADOFFSET, yesdeoffset does yest refer to a BEGIN_NODE tag
 *	-FDT_ERR_NOTFOUND, superyesdedepth was greater than the depth of
 *		yesdeoffset
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_superyesde_atdepth_offset(const void *fdt, int yesdeoffset,
				 int superyesdedepth, int *yesdedepth);

/**
 * fdt_yesde_depth - find the depth of a given yesde
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose parent to find
 *
 * fdt_yesde_depth() finds the depth of a given yesde.  The root yesde
 * has depth 0, its immediate subyesdes depth 1 and so forth.
 *
 * NOTE: This function is expensive, as it must scan the device tree
 * structure from the start to yesdeoffset.
 *
 * returns:
 *	depth of the yesde at yesdeoffset (>=0), on success
 *	-FDT_ERR_BADOFFSET, yesdeoffset does yest refer to a BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_yesde_depth(const void *fdt, int yesdeoffset);

/**
 * fdt_parent_offset - find the parent of a given yesde
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose parent to find
 *
 * fdt_parent_offset() locates the parent yesde of a given yesde (that
 * is, it finds the offset of the yesde which contains the yesde at
 * yesdeoffset as a subyesde).
 *
 * NOTE: This function is expensive, as it must scan the device tree
 * structure from the start to yesdeoffset, *twice*.
 *
 * returns:
 *	structure block offset of the parent of the yesde at yesdeoffset
 *		(>=0), on success
 *	-FDT_ERR_BADOFFSET, yesdeoffset does yest refer to a BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_parent_offset(const void *fdt, int yesdeoffset);

/**
 * fdt_yesde_offset_by_prop_value - find yesdes with a given property value
 * @fdt: pointer to the device tree blob
 * @startoffset: only find yesdes after this offset
 * @propname: property name to check
 * @propval: property value to search for
 * @proplen: length of the value in propval
 *
 * fdt_yesde_offset_by_prop_value() returns the offset of the first
 * yesde after startoffset, which has a property named propname whose
 * value is of length proplen and has value equal to propval; or if
 * startoffset is -1, the very first such yesde in the tree.
 *
 * To iterate through all yesdes matching the criterion, the following
 * idiom can be used:
 *	offset = fdt_yesde_offset_by_prop_value(fdt, -1, propname,
 *					       propval, proplen);
 *	while (offset != -FDT_ERR_NOTFOUND) {
 *		// other code here
 *		offset = fdt_yesde_offset_by_prop_value(fdt, offset, propname,
 *						       propval, proplen);
 *	}
 *
 * Note the -1 in the first call to the function, if 0 is used here
 * instead, the function will never locate the root yesde, even if it
 * matches the criterion.
 *
 * returns:
 *	structure block offset of the located yesde (>= 0, >startoffset),
 *		 on success
 *	-FDT_ERR_NOTFOUND, yes yesde matching the criterion exists in the
 *		tree after startoffset
 *	-FDT_ERR_BADOFFSET, yesdeoffset does yest refer to a BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_yesde_offset_by_prop_value(const void *fdt, int startoffset,
				  const char *propname,
				  const void *propval, int proplen);

/**
 * fdt_yesde_offset_by_phandle - find the yesde with a given phandle
 * @fdt: pointer to the device tree blob
 * @phandle: phandle value
 *
 * fdt_yesde_offset_by_phandle() returns the offset of the yesde
 * which has the given phandle value.  If there is more than one yesde
 * in the tree with the given phandle (an invalid tree), results are
 * undefined.
 *
 * returns:
 *	structure block offset of the located yesde (>= 0), on success
 *	-FDT_ERR_NOTFOUND, yes yesde with that phandle exists
 *	-FDT_ERR_BADPHANDLE, given phandle value was invalid (0 or -1)
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_yesde_offset_by_phandle(const void *fdt, uint32_t phandle);

/**
 * fdt_yesde_check_compatible: check a yesde's compatible property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of a tree yesde
 * @compatible: string to match against
 *
 *
 * fdt_yesde_check_compatible() returns 0 if the given yesde contains a
 * 'compatible' property with the given string as one of its elements,
 * it returns yesn-zero otherwise, or on error.
 *
 * returns:
 *	0, if the yesde has a 'compatible' property listing the given string
 *	1, if the yesde has a 'compatible' property, but it does yest list
 *		the given string
 *	-FDT_ERR_NOTFOUND, if the given yesde has yes 'compatible' property
 *	-FDT_ERR_BADOFFSET, if yesdeoffset does yest refer to a BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_yesde_check_compatible(const void *fdt, int yesdeoffset,
			      const char *compatible);

/**
 * fdt_yesde_offset_by_compatible - find yesdes with a given 'compatible' value
 * @fdt: pointer to the device tree blob
 * @startoffset: only find yesdes after this offset
 * @compatible: 'compatible' string to match against
 *
 * fdt_yesde_offset_by_compatible() returns the offset of the first
 * yesde after startoffset, which has a 'compatible' property which
 * lists the given compatible string; or if startoffset is -1, the
 * very first such yesde in the tree.
 *
 * To iterate through all yesdes matching the criterion, the following
 * idiom can be used:
 *	offset = fdt_yesde_offset_by_compatible(fdt, -1, compatible);
 *	while (offset != -FDT_ERR_NOTFOUND) {
 *		// other code here
 *		offset = fdt_yesde_offset_by_compatible(fdt, offset, compatible);
 *	}
 *
 * Note the -1 in the first call to the function, if 0 is used here
 * instead, the function will never locate the root yesde, even if it
 * matches the criterion.
 *
 * returns:
 *	structure block offset of the located yesde (>= 0, >startoffset),
 *		 on success
 *	-FDT_ERR_NOTFOUND, yes yesde matching the criterion exists in the
 *		tree after startoffset
 *	-FDT_ERR_BADOFFSET, yesdeoffset does yest refer to a BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE, standard meanings
 */
int fdt_yesde_offset_by_compatible(const void *fdt, int startoffset,
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
 * @return: 1 if the string is found in the list, 0 yest found, or invalid list
 */
int fdt_stringlist_contains(const char *strlist, int listlen, const char *str);

/**
 * fdt_stringlist_count - count the number of strings in a string list
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of a tree yesde
 * @property: name of the property containing the string list
 * @return:
 *   the number of strings in the given property
 *   -FDT_ERR_BADVALUE if the property value is yest NUL-terminated
 *   -FDT_ERR_NOTFOUND if the property does yest exist
 */
int fdt_stringlist_count(const void *fdt, int yesdeoffset, const char *property);

/**
 * fdt_stringlist_search - find a string in a string list and return its index
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of a tree yesde
 * @property: name of the property containing the string list
 * @string: string to look up in the string list
 *
 * Note that it is possible for this function to succeed on property values
 * that are yest NUL-terminated. That's because the function will stop after
 * finding the first occurrence of @string. This can for example happen with
 * small-valued cell properties, such as #address-cells, when searching for
 * the empty string.
 *
 * @return:
 *   the index of the string in the list of strings
 *   -FDT_ERR_BADVALUE if the property value is yest NUL-terminated
 *   -FDT_ERR_NOTFOUND if the property does yest exist or does yest contain
 *                     the given string
 */
int fdt_stringlist_search(const void *fdt, int yesdeoffset, const char *property,
			  const char *string);

/**
 * fdt_stringlist_get() - obtain the string at a given index in a string list
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of a tree yesde
 * @property: name of the property containing the string list
 * @index: index of the string to return
 * @lenp: return location for the string length or an error code on failure
 *
 * Note that this will successfully extract strings from properties with
 * yesn-NUL-terminated values. For example on small-valued cell properties
 * this function will return the empty string.
 *
 * If yesn-NULL, the length of the string (on success) or a negative error-code
 * (on failure) will be stored in the integer pointer to by lenp.
 *
 * @return:
 *   A pointer to the string at the given index in the string list or NULL on
 *   failure. On success the length of the string will be stored in the memory
 *   location pointed to by the lenp parameter, if yesn-NULL. On failure one of
 *   the following negative error codes will be returned in the lenp parameter
 *   (if yesn-NULL):
 *     -FDT_ERR_BADVALUE if the property value is yest NUL-terminated
 *     -FDT_ERR_NOTFOUND if the property does yest exist
 */
const char *fdt_stringlist_get(const void *fdt, int yesdeoffset,
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
 * @yesdeoffset: offset of the yesde to find the address size for
 *
 * When the yesde has a valid #address-cells property, returns its value.
 *
 * returns:
 *	0 <= n < FDT_MAX_NCELLS, on success
 *      2, if the yesde has yes #address-cells property
 *      -FDT_ERR_BADNCELLS, if the yesde has a badly formatted or invalid
 *		#address-cells property
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_address_cells(const void *fdt, int yesdeoffset);

/**
 * fdt_size_cells - retrieve address range size for a bus represented in the
 *                  tree
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde to find the address range size for
 *
 * When the yesde has a valid #size-cells property, returns its value.
 *
 * returns:
 *	0 <= n < FDT_MAX_NCELLS, on success
 *      1, if the yesde has yes #size-cells property
 *      -FDT_ERR_BADNCELLS, if the yesde has a badly formatted or invalid
 *		#size-cells property
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_size_cells(const void *fdt, int yesdeoffset);


/**********************************************************************/
/* Write-in-place functions                                           */
/**********************************************************************/

/**
 * fdt_setprop_inplace_namelen_partial - change a property's value,
 *                                       but yest its size
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
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
 */
#ifndef SWIG /* Not available in Python */
int fdt_setprop_inplace_namelen_partial(void *fdt, int yesdeoffset,
					const char *name, int namelen,
					uint32_t idx, const void *val,
					int len);
#endif

/**
 * fdt_setprop_inplace - change a property's value, but yest its size
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @val: pointer to data to replace the property value with
 * @len: length of the property value
 *
 * fdt_setprop_inplace() replaces the value of a given property with
 * the data in val, of length len.  This function canyest change the
 * size of a property, and so will only work if len is equal to the
 * current length of the property.
 *
 * This function will alter only the bytes in the blob which contain
 * the given property value, and will yest alter or move any other part
 * of the tree.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, if len is yest equal to the property's current length
 *	-FDT_ERR_NOTFOUND, yesde does yest have the named property
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
#ifndef SWIG /* Not available in Python */
int fdt_setprop_inplace(void *fdt, int yesdeoffset, const char *name,
			const void *val, int len);
#endif

/**
 * fdt_setprop_inplace_u32 - change the value of a 32-bit integer property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @val: 32-bit integer value to replace the property with
 *
 * fdt_setprop_inplace_u32() replaces the value of a given property
 * with the 32-bit integer value in val, converting val to big-endian
 * if necessary.  This function canyest change the size of a property,
 * and so will only work if the property already exists and has length
 * 4.
 *
 * This function will alter only the bytes in the blob which contain
 * the given property value, and will yest alter or move any other part
 * of the tree.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, if the property's length is yest equal to 4
 *	-FDT_ERR_NOTFOUND, yesde does yest have the named property
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_setprop_inplace_u32(void *fdt, int yesdeoffset,
					  const char *name, uint32_t val)
{
	fdt32_t tmp = cpu_to_fdt32(val);
	return fdt_setprop_inplace(fdt, yesdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_setprop_inplace_u64 - change the value of a 64-bit integer property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @val: 64-bit integer value to replace the property with
 *
 * fdt_setprop_inplace_u64() replaces the value of a given property
 * with the 64-bit integer value in val, converting val to big-endian
 * if necessary.  This function canyest change the size of a property,
 * and so will only work if the property already exists and has length
 * 8.
 *
 * This function will alter only the bytes in the blob which contain
 * the given property value, and will yest alter or move any other part
 * of the tree.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, if the property's length is yest equal to 8
 *	-FDT_ERR_NOTFOUND, yesde does yest have the named property
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_setprop_inplace_u64(void *fdt, int yesdeoffset,
					  const char *name, uint64_t val)
{
	fdt64_t tmp = cpu_to_fdt64(val);
	return fdt_setprop_inplace(fdt, yesdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_setprop_inplace_cell - change the value of a single-cell property
 *
 * This is an alternative name for fdt_setprop_inplace_u32()
 */
static inline int fdt_setprop_inplace_cell(void *fdt, int yesdeoffset,
					   const char *name, uint32_t val)
{
	return fdt_setprop_inplace_u32(fdt, yesdeoffset, name, val);
}

/**
 * fdt_yesp_property - replace a property with yesp tags
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to yesp
 * @name: name of the property to yesp
 *
 * fdt_yesp_property() will replace a given property's representation
 * in the blob with FDT_NOP tags, effectively removing it from the
 * tree.
 *
 * This function will alter only the bytes in the blob which contain
 * the property, and will yest alter or move any other part of the
 * tree.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOTFOUND, yesde does yest have the named property
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_yesp_property(void *fdt, int yesdeoffset, const char *name);

/**
 * fdt_yesp_yesde - replace a yesde (subtree) with yesp tags
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde to yesp
 *
 * fdt_yesp_yesde() will replace a given yesde's representation in the
 * blob, including all its subyesdes, if any, with FDT_NOP tags,
 * effectively removing it from the tree.
 *
 * This function will alter only the bytes in the blob which contain
 * the yesde and its properties and subyesdes, and will yest alter or
 * move any other part of the tree.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_yesp_yesde(void *fdt, int yesdeoffset);

/**********************************************************************/
/* Sequential write functions                                         */
/**********************************************************************/

/* fdt_create_with_flags flags */
#define FDT_CREATE_FLAG_NO_NAME_DEDUP 0x1
	/* FDT_CREATE_FLAG_NO_NAME_DEDUP: Do yest try to de-duplicate property
	 * names in the fdt. This can result in faster creation times, but
	 * a larger fdt. */

#define FDT_CREATE_FLAGS_ALL	(FDT_CREATE_FLAG_NO_NAME_DEDUP)

/**
 * fdt_create_with_flags - begin creation of a new fdt
 * @fdt: pointer to memory allocated where fdt will be created
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
 *	-FDT_ERR_NOSPACE, bufsize is insufficient for a minimal fdt
 *	-FDT_ERR_BADFLAGS, flags is yest valid
 */
int fdt_create_with_flags(void *buf, int bufsize, uint32_t flags);

/**
 * fdt_create - begin creation of a new fdt
 * @fdt: pointer to memory allocated where fdt will be created
 * @bufsize: size of the memory space at fdt
 *
 * fdt_create() is equivalent to fdt_create_with_flags() with flags=0.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, bufsize is insufficient for a minimal fdt
 */
int fdt_create(void *buf, int bufsize);

int fdt_resize(void *fdt, void *buf, int bufsize);
int fdt_add_reservemap_entry(void *fdt, uint64_t addr, uint64_t size);
int fdt_finish_reservemap(void *fdt);
int fdt_begin_yesde(void *fdt, const char *name);
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

#ifndef SWIG /* Not available in Python */
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
 *	-FDT_ERR_NOSPACE, standard meanings
 */
int fdt_property_placeholder(void *fdt, const char *name, int len, void **valp);

#define fdt_property_string(fdt, name, str) \
	fdt_property(fdt, name, str, strlen(str)+1)
int fdt_end_yesde(void *fdt);
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
 * @address, @size: 64-bit values (native endian)
 *
 * Adds a reserve map entry to the given blob reserving a region at
 * address address of length size.
 *
 * This function will insert data into the reserve map and will
 * therefore change the indexes of some entries in the table.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
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
 *	-FDT_ERR_NOTFOUND, there is yes entry of the given index (i.e. there
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
 * fdt_set_name - change the name of a given yesde
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: structure block offset of a yesde
 * @name: name to give the yesde
 *
 * fdt_set_name() replaces the name (including unit address, if any)
 * of the given yesde with the given string.  NOTE: this function can't
 * efficiently check if the new name is unique amongst the given
 * yesde's siblings; results are undefined if this function is invoked
 * with a name equal to one of the given yesde's siblings.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob
 *		to contain the new name
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE, standard meanings
 */
int fdt_set_name(void *fdt, int yesdeoffset, const char *name);

/**
 * fdt_setprop - create or change a property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @val: pointer to data to set the property value to
 * @len: length of the property value
 *
 * fdt_setprop() sets the value of the named property in the given
 * yesde to the given value and length, creating the property if it
 * does yest already exist.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_setprop(void *fdt, int yesdeoffset, const char *name,
		const void *val, int len);

/**
 * fdt_setprop_placeholder - allocate space for a property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @len: length of the property value
 * @prop_data: return pointer to property data
 *
 * fdt_setprop_placeholer() allocates the named property in the given yesde.
 * If the property exists it is resized. In either case a pointer to the
 * property data is returned.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_setprop_placeholder(void *fdt, int yesdeoffset, const char *name,
			    int len, void **prop_data);

/**
 * fdt_setprop_u32 - set a property to a 32-bit integer
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @val: 32-bit integer value for the property (native endian)
 *
 * fdt_setprop_u32() sets the value of the named property in the given
 * yesde to the given 32-bit integer value (converting to big-endian if
 * necessary), or creates a new property with that value if it does
 * yest already exist.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_setprop_u32(void *fdt, int yesdeoffset, const char *name,
				  uint32_t val)
{
	fdt32_t tmp = cpu_to_fdt32(val);
	return fdt_setprop(fdt, yesdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_setprop_u64 - set a property to a 64-bit integer
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @val: 64-bit integer value for the property (native endian)
 *
 * fdt_setprop_u64() sets the value of the named property in the given
 * yesde to the given 64-bit integer value (converting to big-endian if
 * necessary), or creates a new property with that value if it does
 * yest already exist.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_setprop_u64(void *fdt, int yesdeoffset, const char *name,
				  uint64_t val)
{
	fdt64_t tmp = cpu_to_fdt64(val);
	return fdt_setprop(fdt, yesdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_setprop_cell - set a property to a single cell value
 *
 * This is an alternative name for fdt_setprop_u32()
 */
static inline int fdt_setprop_cell(void *fdt, int yesdeoffset, const char *name,
				   uint32_t val)
{
	return fdt_setprop_u32(fdt, yesdeoffset, name, val);
}

/**
 * fdt_setprop_string - set a property to a string value
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @str: string value for the property
 *
 * fdt_setprop_string() sets the value of the named property in the
 * given yesde to the given string value (using the length of the
 * string to determine the new length of the property), or creates a
 * new property with that value if it does yest already exist.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
#define fdt_setprop_string(fdt, yesdeoffset, name, str) \
	fdt_setprop((fdt), (yesdeoffset), (name), (str), strlen(str)+1)


/**
 * fdt_setprop_empty - set a property to an empty value
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 *
 * fdt_setprop_empty() sets the value of the named property in the
 * given yesde to an empty (zero length) value, or creates a new empty
 * property if it does yest already exist.
 *
 * This function may insert or delete data from the blob, and will
 * therefore change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
#define fdt_setprop_empty(fdt, yesdeoffset, name) \
	fdt_setprop((fdt), (yesdeoffset), (name), NULL, 0)

/**
 * fdt_appendprop - append to or create a property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to append to
 * @val: pointer to data to append to the property value
 * @len: length of the data to append to the property value
 *
 * fdt_appendprop() appends the value to the named property in the
 * given yesde, creating the property if it does yest already exist.
 *
 * This function may insert data into the blob, and will therefore
 * change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_appendprop(void *fdt, int yesdeoffset, const char *name,
		   const void *val, int len);

/**
 * fdt_appendprop_u32 - append a 32-bit integer value to a property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @val: 32-bit integer value to append to the property (native endian)
 *
 * fdt_appendprop_u32() appends the given 32-bit integer value
 * (converting to big-endian if necessary) to the value of the named
 * property in the given yesde, or creates a new property with that
 * value if it does yest already exist.
 *
 * This function may insert data into the blob, and will therefore
 * change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_appendprop_u32(void *fdt, int yesdeoffset,
				     const char *name, uint32_t val)
{
	fdt32_t tmp = cpu_to_fdt32(val);
	return fdt_appendprop(fdt, yesdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_appendprop_u64 - append a 64-bit integer value to a property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @val: 64-bit integer value to append to the property (native endian)
 *
 * fdt_appendprop_u64() appends the given 64-bit integer value
 * (converting to big-endian if necessary) to the value of the named
 * property in the given yesde, or creates a new property with that
 * value if it does yest already exist.
 *
 * This function may insert data into the blob, and will therefore
 * change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
static inline int fdt_appendprop_u64(void *fdt, int yesdeoffset,
				     const char *name, uint64_t val)
{
	fdt64_t tmp = cpu_to_fdt64(val);
	return fdt_appendprop(fdt, yesdeoffset, name, &tmp, sizeof(tmp));
}

/**
 * fdt_appendprop_cell - append a single cell value to a property
 *
 * This is an alternative name for fdt_appendprop_u32()
 */
static inline int fdt_appendprop_cell(void *fdt, int yesdeoffset,
				      const char *name, uint32_t val)
{
	return fdt_appendprop_u32(fdt, yesdeoffset, name, val);
}

/**
 * fdt_appendprop_string - append a string to a property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to change
 * @name: name of the property to change
 * @str: string value to append to the property
 *
 * fdt_appendprop_string() appends the given string to the value of
 * the named property in the given yesde, or creates a new property
 * with that value if it does yest already exist.
 *
 * This function may insert data into the blob, and will therefore
 * change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain the new property value
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
#define fdt_appendprop_string(fdt, yesdeoffset, name, str) \
	fdt_appendprop((fdt), (yesdeoffset), (name), (str), strlen(str)+1)

/**
 * fdt_appendprop_addrrange - append a address range property
 * @fdt: pointer to the device tree blob
 * @parent: offset of the parent yesde
 * @yesdeoffset: offset of the yesde to add a property at
 * @name: name of property
 * @addr: start address of a given range
 * @size: size of a given range
 *
 * fdt_appendprop_addrrange() appends an address range value (start
 * address and size) to the value of the named property in the given
 * yesde, or creates a new property with that value if it does yest
 * already exist.
 * If "name" is yest specified, a default "reg" is used.
 * Cell sizes are determined by parent's #address-cells and #size-cells.
 *
 * This function may insert data into the blob, and will therefore
 * change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADNCELLS, if the yesde has a badly formatted or invalid
 *		#address-cells property
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADVALUE, addr or size doesn't fit to respective cells size
 *	-FDT_ERR_NOSPACE, there is insufficient free space in the blob to
 *		contain a new property
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_appendprop_addrrange(void *fdt, int parent, int yesdeoffset,
			     const char *name, uint64_t addr, uint64_t size);

/**
 * fdt_delprop - delete a property
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde whose property to yesp
 * @name: name of the property to yesp
 *
 * fdt_del_property() will delete the given property.
 *
 * This function will delete data from the blob, and will therefore
 * change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_NOTFOUND, yesde does yest have the named property
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_delprop(void *fdt, int yesdeoffset, const char *name);

/**
 * fdt_add_subyesde_namelen - creates a new yesde based on substring
 * @fdt: pointer to the device tree blob
 * @parentoffset: structure block offset of a yesde
 * @name: name of the subyesde to locate
 * @namelen: number of characters of name to consider
 *
 * Identical to fdt_add_subyesde(), but use only the first namelen
 * characters of name as the name of the new yesde.  This is useful for
 * creating subyesdes based on a portion of a larger string, such as a
 * full path.
 */
#ifndef SWIG /* Not available in Python */
int fdt_add_subyesde_namelen(void *fdt, int parentoffset,
			    const char *name, int namelen);
#endif

/**
 * fdt_add_subyesde - creates a new yesde
 * @fdt: pointer to the device tree blob
 * @parentoffset: structure block offset of a yesde
 * @name: name of the subyesde to locate
 *
 * fdt_add_subyesde() creates a new yesde as a subyesde of the yesde at
 * structure block offset parentoffset, with the given name (which
 * should include the unit address, if any).
 *
 * This function will insert data into the blob, and will therefore
 * change the offsets of some existing yesdes.

 * returns:
 *	structure block offset of the created yesdeequested subyesde (>=0), on
 *		success
 *	-FDT_ERR_NOTFOUND, if the requested subyesde does yest exist
 *	-FDT_ERR_BADOFFSET, if parentoffset did yest point to an FDT_BEGIN_NODE
 *		tag
 *	-FDT_ERR_EXISTS, if the yesde at parentoffset already has a subyesde of
 *		the given name
 *	-FDT_ERR_NOSPACE, if there is insufficient free space in the
 *		blob to contain the new yesde
 *	-FDT_ERR_NOSPACE
 *	-FDT_ERR_BADLAYOUT
 *      -FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings.
 */
int fdt_add_subyesde(void *fdt, int parentoffset, const char *name);

/**
 * fdt_del_yesde - delete a yesde (subtree)
 * @fdt: pointer to the device tree blob
 * @yesdeoffset: offset of the yesde to yesp
 *
 * fdt_del_yesde() will remove the given yesde, including all its
 * subyesdes if any, from the blob.
 *
 * This function will delete data from the blob, and will therefore
 * change the offsets of some existing yesdes.
 *
 * returns:
 *	0, on success
 *	-FDT_ERR_BADOFFSET, yesdeoffset did yest point to FDT_BEGIN_NODE tag
 *	-FDT_ERR_BADLAYOUT,
 *	-FDT_ERR_BADMAGIC,
 *	-FDT_ERR_BADVERSION,
 *	-FDT_ERR_BADSTATE,
 *	-FDT_ERR_BADSTRUCTURE,
 *	-FDT_ERR_TRUNCATED, standard meanings
 */
int fdt_del_yesde(void *fdt, int yesdeoffset);

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
 *	-FDT_ERR_NOSPACE, there's yest eyesugh space in the base device tree
 *	-FDT_ERR_NOTFOUND, the overlay points to some inexistant yesdes or
 *		properties in the base DT
 *	-FDT_ERR_BADPHANDLE,
 *	-FDT_ERR_BADOVERLAY,
 *	-FDT_ERR_NOPHANDLES,
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

/**********************************************************************/
/* Debugging / informational functions                                */
/**********************************************************************/

const char *fdt_strerror(int errval);

#endif /* LIBFDT_H */
