/*	$OpenBSD: bytestring.h,v 1.6 2024/12/05 19:57:37 tb Exp $	*/
/*
 * Copyright (c) 2014, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef OPENSSL_HEADER_BYTESTRING_H
#define OPENSSL_HEADER_BYTESTRING_H

#include <sys/types.h>
#include <stdint.h>

__BEGIN_HIDDEN_DECLS

/*
 * Bytestrings are used for parsing and building TLS and ASN.1 messages.
 *
 * A "CBS" (CRYPTO ByteString) represents a string of bytes in memory and
 * provides utility functions for safely parsing length-prefixed structures
 * like TLS and ASN.1 from it.
 *
 * A "CBB" (CRYPTO ByteBuilder) is a memory buffer that grows as needed and
 * provides utility functions for building length-prefixed messages.
 */

/* CRYPTO ByteString */
typedef struct cbs_st {
	const uint8_t *data;
	size_t initial_len;
	size_t len;
} CBS;

/*
 * CBS_init sets |cbs| to point to |data|. It does not take ownership of
 * |data|.
 */
void CBS_init(CBS *cbs, const uint8_t *data, size_t len);

/*
 * CBS_skip advances |cbs| by |len| bytes. It returns one on success and zero
 * otherwise.
 */
int CBS_skip(CBS *cbs, size_t len);

/*
 * CBS_data returns a pointer to the contents of |cbs|.
 */
const uint8_t *CBS_data(const CBS *cbs);

/*
 * CBS_len returns the number of bytes remaining in |cbs|.
 */
size_t CBS_len(const CBS *cbs);

/*
 * CBS_offset returns the current offset into the original data of |cbs|.
 */
size_t CBS_offset(const CBS *cbs);

/*
 * CBS_stow copies the current contents of |cbs| into |*out_ptr| and
 * |*out_len|. If |*out_ptr| is not NULL, the contents are freed with
 * free. It returns one on success and zero on allocation failure. On
 * success, |*out_ptr| should be freed with free. If |cbs| is empty,
 * |*out_ptr| will be NULL.
 */
int CBS_stow(const CBS *cbs, uint8_t **out_ptr, size_t *out_len);

/*
 * CBS_strdup copies the current contents of |cbs| into |*out_ptr| as a
 * NUL-terminated C string. If |*out_ptr| is not NULL, the contents are freed
 * with free. It returns one on success and zero on failure. On success,
 * |*out_ptr| should be freed with free. If |cbs| contains NUL bytes,
 * CBS_strdup will fail.
 */
int CBS_strdup(const CBS *cbs, char **out_ptr);

/*
 * CBS_write_bytes writes all of the remaining data from |cbs| into |dst|
 * if it is at most |dst_len| bytes.  If |copied| is not NULL, it will be set
 * to the amount copied. It returns one on success and zero otherwise.
 */
int CBS_write_bytes(const CBS *cbs, uint8_t *dst, size_t dst_len,
    size_t *copied);

/*
 * CBS_contains_zero_byte returns one if the current contents of |cbs| contains
 * a NUL byte and zero otherwise.
 */
int CBS_contains_zero_byte(const CBS *cbs);

/*
 * CBS_mem_equal compares the current contents of |cbs| with the |len| bytes
 * starting at |data|. If they're equal, it returns one, otherwise zero. If the
 * lengths match, it uses a constant-time comparison.
 */
int CBS_mem_equal(const CBS *cbs, const uint8_t *data, size_t len);

/*
 * CBS_get_u8 sets |*out| to the next uint8_t from |cbs| and advances |cbs|. It
 * returns one on success and zero on error.
 */
int CBS_get_u8(CBS *cbs, uint8_t *out);

/*
 * CBS_get_u16 sets |*out| to the next, big-endian uint16_t from |cbs| and
 * advances |cbs|. It returns one on success and zero on error.
 */
int CBS_get_u16(CBS *cbs, uint16_t *out);

/*
 * CBS_get_u24 sets |*out| to the next, big-endian 24-bit value from |cbs| and
 * advances |cbs|. It returns one on success and zero on error.
 */
int CBS_get_u24(CBS *cbs, uint32_t *out);

/*
 * CBS_get_u32 sets |*out| to the next, big-endian uint32_t value from |cbs|
 * and advances |cbs|. It returns one on success and zero on error.
 */
int CBS_get_u32(CBS *cbs, uint32_t *out);

/*
 * CBS_get_u64 sets |*out| to the next, big-endian uint64_t value from |cbs|
 * and advances |cbs|. It returns one on success and zero on error.
 */
int CBS_get_u64(CBS *cbs, uint64_t *out);

/*
 * CBS_get_last_u8 sets |*out| to the last uint8_t from |cbs| and shortens
 * |cbs|. It returns one on success and zero on error.
 */
int CBS_get_last_u8(CBS *cbs, uint8_t *out);

/*
 * CBS_get_bytes sets |*out| to the next |len| bytes from |cbs| and advances
 * |cbs|. It returns one on success and zero on error.
 */
int CBS_get_bytes(CBS *cbs, CBS *out, size_t len);

/*
 * CBS_get_u8_length_prefixed sets |*out| to the contents of an 8-bit,
 * length-prefixed value from |cbs| and advances |cbs| over it. It returns one
 * on success and zero on error.
 */
int CBS_get_u8_length_prefixed(CBS *cbs, CBS *out);

/*
 * CBS_get_u16_length_prefixed sets |*out| to the contents of a 16-bit,
 * big-endian, length-prefixed value from |cbs| and advances |cbs| over it. It
 * returns one on success and zero on error.
 */
int CBS_get_u16_length_prefixed(CBS *cbs, CBS *out);

/*
 * CBS_get_u24_length_prefixed sets |*out| to the contents of a 24-bit,
 * big-endian, length-prefixed value from |cbs| and advances |cbs| over it. It
 * returns one on success and zero on error.
 */
int CBS_get_u24_length_prefixed(CBS *cbs, CBS *out);

/*
 * CBS_peek_u8 sets |*out| to the next uint8_t from |cbs|, but does not advance
 * |cbs|. It returns one on success and zero on error.
 */
int CBS_peek_u8(CBS *cbs, uint8_t *out);

/*
 * CBS_peek_u16 sets |*out| to the next, big-endian uint16_t from |cbs|, but
 * does not advance |cbs|. It returns one on success and zero on error.
 */
int CBS_peek_u16(CBS *cbs, uint16_t *out);

/*
 * CBS_peek_u24 sets |*out| to the next, big-endian 24-bit value from |cbs|, but
 * does not advance |cbs|. It returns one on success and zero on error.
 */
int CBS_peek_u24(CBS *cbs, uint32_t *out);

/*
 * CBS_peek_u32 sets |*out| to the next, big-endian uint32_t value from |cbs|,
 * but does not advance |cbs|. It returns one on success and zero on error.
 */
int CBS_peek_u32(CBS *cbs, uint32_t *out);

/*
 * CBS_peek_last_u8 sets |*out| to the last uint8_t from |cbs|, but does not
 * shorten |cbs|. It returns one on success and zero on error.
 */
int CBS_peek_last_u8(CBS *cbs, uint8_t *out);


/* Parsing ASN.1 */

/*
 * While an identifier can be multiple octets, this library only handles the
 * single octet variety currently.  This limits support up to tag number 30
 * since tag number 31 is a reserved value to indicate multiple octets.
 */

/* Bits 8 and 7: class tag type: See X.690 section 8.1.2.2. */
#define CBS_ASN1_UNIVERSAL		0x00
#define CBS_ASN1_APPLICATION		0x40
#define CBS_ASN1_CONTEXT_SPECIFIC	0x80
#define CBS_ASN1_PRIVATE		0xc0

/* Bit 6: Primitive or constructed: See X.690 section 8.1.2.3. */
#define CBS_ASN1_PRIMITIVE	0x00
#define CBS_ASN1_CONSTRUCTED	0x20

/*
 * Bits 5 to 1 are the tag number.  See X.680 section 8.6 for tag numbers of
 * the universal class.
 */

/*
 * Common universal identifier octets.
 * See X.690 section 8.1 and X.680 section 8.6 for universal tag numbers.
 *
 * Note: These definitions are the cause of some of the strange behavior in
 * CBS's bs_ber.c.
 *
 * In BER, it is the sender's option to use primitive or constructed for
 * bitstring (X.690 section 8.6.1) and octetstring (X.690 section 8.7.1).
 *
 * In DER, bitstring and octetstring are required to be primitive
 * (X.690 section 10.2).
 */
#define CBS_ASN1_BOOLEAN     (CBS_ASN1_UNIVERSAL | CBS_ASN1_PRIMITIVE | 0x1)
#define CBS_ASN1_INTEGER     (CBS_ASN1_UNIVERSAL | CBS_ASN1_PRIMITIVE | 0x2)
#define CBS_ASN1_BITSTRING   (CBS_ASN1_UNIVERSAL | CBS_ASN1_PRIMITIVE | 0x3)
#define CBS_ASN1_OCTETSTRING (CBS_ASN1_UNIVERSAL | CBS_ASN1_PRIMITIVE | 0x4)
#define CBS_ASN1_OBJECT      (CBS_ASN1_UNIVERSAL | CBS_ASN1_PRIMITIVE | 0x6)
#define CBS_ASN1_ENUMERATED  (CBS_ASN1_UNIVERSAL | CBS_ASN1_PRIMITIVE | 0xa)
#define CBS_ASN1_SEQUENCE    (CBS_ASN1_UNIVERSAL | CBS_ASN1_CONSTRUCTED | 0x10)
#define CBS_ASN1_SET         (CBS_ASN1_UNIVERSAL | CBS_ASN1_CONSTRUCTED | 0x11)

/*
 * CBS_get_asn1 sets |*out| to the contents of DER-encoded, ASN.1 element (not
 * including tag and length bytes) and advances |cbs| over it. The ASN.1
 * element must match |tag_value|. It returns one on success and zero
 * on error.
 *
 * Tag numbers greater than 30 are not supported (i.e. short form only).
 */
int CBS_get_asn1(CBS *cbs, CBS *out, unsigned int tag_value);

/*
 * CBS_get_asn1_element acts like |CBS_get_asn1| but |out| will include the
 * ASN.1 header bytes too.
 */
int CBS_get_asn1_element(CBS *cbs, CBS *out, unsigned int tag_value);

/*
 * CBS_peek_asn1_tag looks ahead at the next ASN.1 tag and returns one
 * if the next ASN.1 element on |cbs| would have tag |tag_value|. If
 * |cbs| is empty or the tag does not match, it returns zero. Note: if
 * it returns one, CBS_get_asn1 may still fail if the rest of the
 * element is malformed.
 */
int CBS_peek_asn1_tag(const CBS *cbs, unsigned int tag_value);

/*
 * CBS_get_any_asn1_element sets |*out| to contain the next ASN.1 element from
 * |*cbs| (including header bytes) and advances |*cbs|. It sets |*out_tag| to
 * the tag number and |*out_header_len| to the length of the ASN.1 header.
 * Each of |out|, |out_tag|, and |out_header_len| may be NULL to ignore
 * the value.
 *
 * Tag numbers greater than 30 are not supported (i.e. short form only).
 */
int CBS_get_any_asn1_element(CBS *cbs, CBS *out, unsigned int *out_tag,
    size_t *out_header_len);

/*
 * CBS_get_asn1_uint64 gets an ASN.1 INTEGER from |cbs| using |CBS_get_asn1|
 * and sets |*out| to its value. It returns one on success and zero on error,
 * where error includes the integer being negative, or too large to represent
 * in 64 bits.
 */
int CBS_get_asn1_uint64(CBS *cbs, uint64_t *out);

/*
 * CBS_get_optional_asn1 gets an optional explicitly-tagged element
 * from |cbs| tagged with |tag| and sets |*out| to its contents. If
 * present, it sets |*out_present| to one, otherwise zero. It returns
 * one on success, whether or not the element was present, and zero on
 * decode failure.
 */
int CBS_get_optional_asn1(CBS *cbs, CBS *out, int *out_present,
    unsigned int tag);

/*
 * CBS_get_optional_asn1_octet_string gets an optional
 * explicitly-tagged OCTET STRING from |cbs|. If present, it sets
 * |*out| to the string and |*out_present| to one. Otherwise, it sets
 * |*out| to empty and |*out_present| to zero. |out_present| may be
 * NULL. It returns one on success, whether or not the element was
 * present, and zero on decode failure.
 */
int CBS_get_optional_asn1_octet_string(CBS *cbs, CBS *out, int *out_present,
    unsigned int tag);

/*
 * CBS_get_optional_asn1_uint64 gets an optional explicitly-tagged
 * INTEGER from |cbs|. If present, it sets |*out| to the
 * value. Otherwise, it sets |*out| to |default_value|. It returns one
 * on success, whether or not the element was present, and zero on
 * decode failure.
 */
int CBS_get_optional_asn1_uint64(CBS *cbs, uint64_t *out, unsigned int tag,
    uint64_t default_value);

/*
 * CBS_get_optional_asn1_bool gets an optional, explicitly-tagged BOOLEAN from
 * |cbs|. If present, it sets |*out| to either zero or one, based on the
 * boolean. Otherwise, it sets |*out| to |default_value|. It returns one on
 * success, whether or not the element was present, and zero on decode
 * failure.
 */
int CBS_get_optional_asn1_bool(CBS *cbs, int *out, unsigned int tag,
    int default_value);


/*
 * CRYPTO ByteBuilder.
 *
 * |CBB| objects allow one to build length-prefixed serialisations. A |CBB|
 * object is associated with a buffer and new buffers are created with
 * |CBB_init|. Several |CBB| objects can point at the same buffer when a
 * length-prefix is pending, however only a single |CBB| can be 'current' at
 * any one time. For example, if one calls |CBB_add_u8_length_prefixed| then
 * the new |CBB| points at the same buffer as the original. But if the original
 * |CBB| is used then the length prefix is written out and the new |CBB| must
 * not be used again.
 *
 * If one needs to force a length prefix to be written out because a |CBB| is
 * going out of scope, use |CBB_flush|.
 */

struct cbb_buffer_st {
	uint8_t *buf;

	/* The number of valid bytes. */
	size_t len;

	/* The size of buf. */
	size_t cap;

	/*
	 * One iff |buf| is owned by this object. If not then |buf| cannot be
	 * resized.
	 */
	char can_resize;
};

typedef struct cbb_st {
	struct cbb_buffer_st *base;

	/*
	 * offset is the offset from the start of |base->buf| to the position of any
	 * pending length-prefix.
	 */
	size_t offset;

	/* child points to a child CBB if a length-prefix is pending. */
	struct cbb_st *child;

	/*
	 * pending_len_len contains the number of bytes in a pending length-prefix,
	 * or zero if no length-prefix is pending.
	 */
	uint8_t pending_len_len;

	char pending_is_asn1;

	/*
	 * is_top_level is true iff this is a top-level |CBB| (as opposed to a child
	 * |CBB|). Top-level objects are valid arguments for |CBB_finish|.
	 */
	char is_top_level;
} CBB;

/*
 * CBB_init initialises |cbb| with |initial_capacity|. Since a |CBB| grows as
 * needed, the |initial_capacity| is just a hint. It returns one on success or
 * zero on error.
 */
int CBB_init(CBB *cbb, size_t initial_capacity);

/*
 * CBB_init_fixed initialises |cbb| to write to |len| bytes at |buf|. Since
 * |buf| cannot grow, trying to write more than |len| bytes will cause CBB
 * functions to fail. It returns one on success or zero on error.
 */
int CBB_init_fixed(CBB *cbb, uint8_t *buf, size_t len);

/*
 * CBB_cleanup frees all resources owned by |cbb| and other |CBB| objects
 * writing to the same buffer. This should be used in an error case where a
 * serialisation is abandoned.
 */
void CBB_cleanup(CBB *cbb);

/*
 * CBB_finish completes any pending length prefix and sets |*out_data| to a
 * malloced buffer and |*out_len| to the length of that buffer. The caller
 * takes ownership of the buffer and, unless the buffer was fixed with
 * |CBB_init_fixed|, must call |free| when done.
 *
 * It can only be called on a "top level" |CBB|, i.e. one initialised with
 * |CBB_init| or |CBB_init_fixed|. It returns one on success and zero on
 * error.
 */
int CBB_finish(CBB *cbb, uint8_t **out_data, size_t *out_len);

/*
 * CBB_flush causes any pending length prefixes to be written out and any child
 * |CBB| objects of |cbb| to be invalidated. It returns one on success or zero
 * on error.
 */
int CBB_flush(CBB *cbb);

/*
 * CBB_discard_child discards the current unflushed child of |cbb|. Neither the
 * child's contents nor the length prefix will be included in the output.
 */
void CBB_discard_child(CBB *cbb);

/*
 * CBB_add_u8_length_prefixed sets |*out_contents| to a new child of |cbb|. The
 * data written to |*out_contents| will be prefixed in |cbb| with an 8-bit
 * length. It returns one on success or zero on error.
 */
int CBB_add_u8_length_prefixed(CBB *cbb, CBB *out_contents);

/*
 * CBB_add_u16_length_prefixed sets |*out_contents| to a new child of |cbb|.
 * The data written to |*out_contents| will be prefixed in |cbb| with a 16-bit,
 * big-endian length. It returns one on success or zero on error.
 */
int CBB_add_u16_length_prefixed(CBB *cbb, CBB *out_contents);

/*
 * CBB_add_u24_length_prefixed sets |*out_contents| to a new child of |cbb|.
 * The data written to |*out_contents| will be prefixed in |cbb| with a 24-bit,
 * big-endian length. It returns one on success or zero on error.
 */
int CBB_add_u24_length_prefixed(CBB *cbb, CBB *out_contents);

/*
 * CBB_add_u32_length_prefixed sets |*out_contents| to a new child of |cbb|.
 * The data written to |*out_contents| will be prefixed in |cbb| with a 32-bit,
 * big-endian length. It returns one on success or zero on error.
 */
int CBB_add_u32_length_prefixed(CBB *cbb, CBB *out_contents);

/*
 * CBB_add_asn sets |*out_contents| to a |CBB| into which the contents of an
 * ASN.1 object can be written. The |tag| argument will be used as the tag for
 * the object. Passing in |tag| number 31 will return in an error since only
 * single octet identifiers are supported. It returns one on success or zero
 * on error.
 */
int CBB_add_asn1(CBB *cbb, CBB *out_contents, unsigned int tag);

/*
 * CBB_add_bytes appends |len| bytes from |data| to |cbb|. It returns one on
 * success and zero otherwise.
 */
int CBB_add_bytes(CBB *cbb, const uint8_t *data, size_t len);

/*
 * CBB_add_space appends |len| bytes to |cbb| and sets |*out_data| to point to
 * the beginning of that space. The caller must then write |len| bytes of
 * actual contents to |*out_data|. It returns one on success and zero
 * otherwise.
 */
int CBB_add_space(CBB *cbb, uint8_t **out_data, size_t len);

/*
 * CBB_add_u8 appends an 8-bit number from |value| to |cbb|. It returns one on
 * success and zero otherwise.
 */
int CBB_add_u8(CBB *cbb, size_t value);

/*
 * CBB_add_u8 appends a 16-bit, big-endian number from |value| to |cbb|. It
 * returns one on success and zero otherwise.
 */
int CBB_add_u16(CBB *cbb, size_t value);

/*
 * CBB_add_u24 appends a 24-bit, big-endian number from |value| to |cbb|. It
 * returns one on success and zero otherwise.
 */
int CBB_add_u24(CBB *cbb, size_t value);

/*
 * CBB_add_u32 appends a 32-bit, big-endian number from |value| to |cbb|. It
 * returns one on success and zero otherwise.
 */
int CBB_add_u32(CBB *cbb, size_t value);

/*
 * CBB_add_u64 appends a 64-bit, big-endian number from |value| to |cbb|. It
 * returns one on success and zero otherwise.
 */
int CBB_add_u64(CBB *cbb, uint64_t value);

/*
 * CBB_add_asn1_uint64 writes an ASN.1 INTEGER into |cbb| using |CBB_add_asn1|
 * and writes |value| in its contents. It returns one on success and zero on
 * error.
 */
int CBB_add_asn1_uint64(CBB *cbb, uint64_t value);

#ifdef LIBRESSL_INTERNAL
/*
 * CBS_dup sets |out| to point to cbs's |data| and |len|.  It results in two
 * CBS that point to the same buffer.
 */
void CBS_dup(const CBS *cbs, CBS *out);

/*
 * cbs_get_any_asn1_element sets |*out| to contain the next ASN.1 element from
 * |*cbs| (including header bytes) and advances |*cbs|. It sets |*out_tag| to
 * the tag number and |*out_header_len| to the length of the ASN.1 header. If
 * strict mode is disabled and the element has indefinite length then |*out|
 * will only contain the header. Each of |out|, |out_tag|, and
 * |out_header_len| may be NULL to ignore the value.
 *
 * Tag numbers greater than 30 are not supported (i.e. short form only).
 */
int cbs_get_any_asn1_element_internal(CBS *cbs, CBS *out, unsigned int *out_tag,
    size_t *out_header_len, int strict);

/*
 * CBS_asn1_indefinite_to_definite reads an ASN.1 structure from |in|. If it
 * finds indefinite-length elements that otherwise appear to be valid DER, it
 * attempts to convert the DER-like data to DER and sets |*out| and
 * |*out_length| to describe a malloced buffer containing the DER data.
 * Additionally, |*in| will be advanced over the ASN.1 data.
 *
 * If it doesn't find any indefinite-length elements then it sets |*out| to
 * NULL and |*in| is unmodified.
 *
 * This is NOT a conversion from BER to DER.  There are many restrictions when
 * dealing with DER data.  This is only concerned with one: indefinite vs.
 * definite form. However, this suffices to handle the PKCS#7 and PKCS#12 output
 * from NSS.
 *
 * It returns one on success and zero otherwise.
 */
int CBS_asn1_indefinite_to_definite(CBS *in, uint8_t **out, size_t *out_len);
#endif /* LIBRESSL_INTERNAL */

__END_HIDDEN_DECLS

#endif  /* OPENSSL_HEADER_BYTESTRING_H */
