/*
 * dname.h -- Domain name handling.
 *
 * Copyright (c) 2001-2006, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

#ifndef DNAME_H
#define DNAME_H

#include <assert.h>
#include <stdio.h>

#include "buffer.h"
#include "region-allocator.h"
#include "dns.h" /* for MAXDOMAINLEN */

#if defined(NAMEDB_UPPERCASE) || defined(USE_NAMEDB_UPPERCASE)
#define DNAME_NORMALIZE        toupper
#else
#define DNAME_NORMALIZE        tolower
#endif


/*
 * Domain names stored in memory add some additional information to be
 * able to quickly index and compare by label.
 */
typedef struct dname dname_type;
struct dname
{
	/*
	 * The size (in bytes) of the domain name in wire format.
	 */
	uint8_t name_size;

	/*
	 * The number of labels in this domain name (including the
	 * root label).
	 */
	uint8_t label_count;

	/*
	  uint8_t label_offsets[label_count];
	  uint8_t name[name_size];
	*/
};


/*
 * Construct a new domain name based on NAME in wire format.  NAME
 * cannot contain compression pointers.
 *
 * Pre: NAME != NULL.
 */
const dname_type *dname_make(region_type *region, const uint8_t *name,
			     int normalize);

/*
 * Construct a new domain name based on wire format dname stored at
 * PACKET's current position.  Compression pointers are followed.  The
 * PACKET's current position is changed to the end of the wire format
 * dname or set to just after the first compression pointer.
 */
const dname_type *dname_make_from_packet(region_type *region,
					 buffer_type *packet,
					 int allow_pointers,
					 int normalize);

/*
 * parse wireformat from packet (following pointers) into the
 * given buffer. Returns length in buffer or 0 on error.
 * buffer must be MAXDOMAINLEN+1 long.
 */
int dname_make_wire_from_packet(uint8_t *buf,
				buffer_type *packet,
				int allow_pointers);

/*
 * Construct a new domain name based on the ASCII representation NAME.
 * If ORIGIN is not NULL and NAME is not terminated by a "." the
 * ORIGIN is appended to the result.  NAME can contain escape
 * sequences.
 *
 * Returns NULL on failure.  Otherwise a newly allocated domain name
 * is returned.
 *
 * Pre: name != NULL.
 */
const dname_type *dname_parse(region_type *region, const char *name);

/*
 * parse ascii string to wireformat domain name (without compression ptrs)
 * returns 0 on failure, the length of the wireformat on success.
 * the result is stored in the wirefmt which must be at least MAXDOMAINLEN
 * in size. On failure, the wirefmt can be altered.
 */
int dname_parse_wire(uint8_t* wirefmt, const char* name);

/*
 * Return NULL if DNAME is NULL or a copy of DNAME otherwise.
 */
const dname_type *dname_copy(region_type *region, const dname_type *dname);


/*
 * Copy the most significant LABEL_COUNT labels from dname.
 */
const dname_type *dname_partial_copy(region_type *region,
				     const dname_type *dname,
				     uint8_t label_count);


/*
 * The origin of DNAME.
 */
const dname_type *dname_origin(region_type *region, const dname_type *dname);

/*
 * Return true if LEFT is a subdomain of RIGHT.
 */
int dname_is_subdomain(const dname_type *left, const dname_type *right);


/*
 * Offsets into NAME for each label starting with the most
 * significant label (the root label, followed by the TLD,
 * etc).
 */
static inline const uint8_t *
dname_label_offsets(const dname_type *dname)
{
	return (const uint8_t *) ((const char *) dname + sizeof(dname_type));
}


/*
 * The actual name in wire format (a sequence of label, each
 * prefixed by a length byte, terminated by a zero length
 * label).
 */
static inline const uint8_t *
dname_name(const dname_type *dname)
{
	return (const uint8_t *) ((const char *) dname
				  + sizeof(dname_type)
				  + dname->label_count * sizeof(uint8_t));
}


/*
 * Return the label for DNAME specified by LABEL_INDEX.  The first
 * label (LABEL_INDEX == 0) is the root label, the next label is the
 * TLD, etc.
 *
 * Pre: dname != NULL && label_index < dname->label_count.
 */
static inline const uint8_t *
dname_label(const dname_type *dname, uint8_t label)
{
	uint8_t label_index;

	assert(dname != NULL);
	assert(label < dname->label_count);

	label_index = dname_label_offsets(dname)[label];
	assert(label_index < dname->name_size);

	return dname_name(dname) + label_index;
}


/*
 * Compare two domain names.  The comparison defines a lexicographical
 * ordering based on the domain name's labels, starting with the most
 * significant label.
 *
 * Return < 0 if LEFT < RIGHT, 0 if LEFT == RIGHT, and > 0 if LEFT >
 * RIGHT.  The comparison is case sensitive.
 *
 * Pre: left != NULL && right != NULL
 * left and right are dname_type*.
 */
int dname_compare(const void *left, const void *right);


/*
 * Compare two labels.  The comparison defines a lexicographical
 * ordering based on the characters in the labels.
 *
 * Return < 0 if LEFT < RIGHT, 0 if LEFT == RIGHT, and > 0 if LEFT >
 * RIGHT.  The comparison is case sensitive.
 *
 * Pre: left != NULL && right != NULL
 *      label_is_normal(left) && label_is_normal(right)
 */
int label_compare(const uint8_t *left, const uint8_t *right);


/*
 * Returns the number of labels that match in LEFT and RIGHT, starting
 * with the most significant label.  Because the root label always
 * matches, the result will always be >= 1.
 *
 * Pre: left != NULL && right != NULL
 */
uint8_t dname_label_match_count(const dname_type *left,
				const dname_type *right);


/*
 * The total size (in bytes) allocated to store DNAME.
 *
 * Pre: dname != NULL
 */
static inline size_t
dname_total_size(const dname_type *dname)
{
	return (sizeof(dname_type)
		+ ((((size_t)dname->label_count) + ((size_t)dname->name_size))
		   * sizeof(uint8_t)));
}


/*
 * Is LABEL a normal LABEL (not a pointer or reserved)?
 *
 * Pre: label != NULL;
 */
static inline int
label_is_normal(const uint8_t *label)
{
	assert(label);
	return (label[0] & 0xc0) == 0;
}


/*
 * Is LABEL a pointer?
 *
 * Pre: label != NULL;
 */
static inline int
label_is_pointer(const uint8_t *label)
{
	assert(label);
	return (label[0] & 0xc0) == 0xc0;
}


/*
 * LABEL's pointer location.
 *
 * Pre: label != NULL && label_is_pointer(label)
 */
static inline uint16_t
label_pointer_location(const uint8_t *label)
{
	assert(label);
	assert(label_is_pointer(label));
	return ((uint16_t) (label[0] & ~0xc0) << 8) | (uint16_t) label[1];
}


/*
 * Length of LABEL.
 *
 * Pre: label != NULL && label_is_normal(label)
 */
static inline uint8_t
label_length(const uint8_t *label)
{
	assert(label);
	assert(label_is_normal(label));
	return label[0];
}


/*
 * The data of LABEL.
 *
 * Pre: label != NULL && label_is_normal(label)
 */
static inline const uint8_t *
label_data(const uint8_t *label)
{
	assert(label);
	assert(label_is_normal(label));
	return label + 1;
}


/*
 * Is LABEL the root label?
 *
 * Pre: label != NULL
 */
static inline int
label_is_root(const uint8_t *label)
{
	assert(label);
	return label[0] == 0;
}


/*
 * Is LABEL the wildcard label?
 *
 * Pre: label != NULL
 */
static inline int
label_is_wildcard(const uint8_t *label)
{
	assert(label);
	return label[0] == 1 && label[1] == '*';
}


/*
 * The next label of LABEL.
 *
 * Pre: label != NULL
 *      label_is_normal(label)
 *      !label_is_root(label)
 */
static inline const uint8_t *
label_next(const uint8_t *label)
{
	assert(label);
	assert(label_is_normal(label));
	assert(!label_is_root(label));
	return label + label_length(label) + 1;
}


/*
 * Convert DNAME to its string representation.  The result points to a
 * static buffer that is overwritten the next time this function is
 * invoked.
 *
 * If ORIGIN is provided and DNAME is a subdomain of ORIGIN the dname
 * will be represented relative to ORIGIN.
 *
 * Pre: dname != NULL
 */
const char *dname_to_string(const dname_type *dname,
			    const dname_type *origin);

/*
 * Convert DNAME to its string representation.  The result if written
 * to the provided buffer buf, which must be at least 5 times
 * MAXDOMAINNAMELEN.
 *
 * If ORIGIN is provided and DNAME is a subdomain of ORIGIN the dname
 * will be represented relative to ORIGIN.
 *
 * Pre: dname != NULL
 */
const char *dname_to_string_buf(const dname_type *dname,
                                const dname_type *origin,
                                char buf[MAXDOMAINLEN * 5]);

/*
 * Create a dname containing the single label specified by STR
 * followed by the root label.
 */
const dname_type *dname_make_from_label(region_type *region,
					const uint8_t *label,
					const size_t length);


/*
 * Concatenate two dnames.
 */
const dname_type *dname_concatenate(region_type *region,
				    const dname_type *left,
				    const dname_type *right);


/*
 * Perform DNAME substitution on a name, replace src with dest.
 * Name must be a subdomain of src. The returned name is a subdomain of dest.
 * Returns NULL if the result domain name is too long.
 */
const dname_type *dname_replace(region_type* region,
				const dname_type* name,
				const dname_type* src,
				const dname_type* dest);

/** Convert uncompressed wireformat dname to a string */
char* wiredname2str(const uint8_t* dname);
/** convert uncompressed label to string */
char* wirelabel2str(const uint8_t* label);
/** check if two uncompressed dnames of the same total length are equal */
int dname_equal_nocase(uint8_t* a, uint8_t* b, uint16_t len);

/* Test is the name is a subdomain of the other name. Equal names return true.
 * Subdomain d of d2 returns true, otherwise false. The names are in
 * wireformat, uncompressed. Does not perform canonicalization, it is case
 * sensitive. */
int is_dname_subdomain_of_case(const uint8_t* d, unsigned int len,
	const uint8_t* d2, unsigned int len2);

#endif /* DNAME_H */
