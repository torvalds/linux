/*
 * parseutil.h - parse utilities for string and wire conversion
 *
 * (c) NLnet Labs, 2004
 *
 * See the file LICENSE for the license
 */
/**
 * \file
 *
 * Utility functions for parsing, base32(DNS variant) and base64 encoding
 * and decoding, Hex, Time units, Escape codes.
 */

#ifndef LDNS_PARSEUTIL_H
#define LDNS_PARSEUTIL_H
struct tm;

/** 
 *  A general purpose lookup table
 *  
 *  Lookup tables are arrays of (id, name) pairs,
 *  So you can for instance lookup the RCODE 3, which is "NXDOMAIN",
 *  and vice versa. The lookup tables themselves are defined wherever needed,
 *  for instance in host2str.c
 */
struct sldns_struct_lookup_table {
        int id;
        const char *name;
};
typedef struct sldns_struct_lookup_table sldns_lookup_table;

/**
 * Looks up the table entry by name, returns NULL if not found.
 * \param[in] table the lookup table to search in
 * \param[in] name what to search for
 * \return the item found
 */
sldns_lookup_table *sldns_lookup_by_name(sldns_lookup_table table[],
                                       const char *name);
/**
 * Looks up the table entry by id, returns NULL if not found.
 * \param[in] table the lookup table to search in
 * \param[in] id what to search for
 * \return the item found
 */
sldns_lookup_table *sldns_lookup_by_id(sldns_lookup_table table[], int id);

/**
 * Convert TM to seconds since epoch (midnight, January 1st, 1970).
 * Like timegm(3), which is not always available.
 * \param[in] tm a struct tm* with the date
 * \return the seconds since epoch
 */
time_t sldns_mktime_from_utc(const struct tm *tm);

/**
 * The function interprets time as the number of seconds since epoch
 * with respect to now using serial arithmetics (rfc1982).
 * That number of seconds is then converted to broken-out time information.
 * This is especially useful when converting the inception and expiration
 * fields of RRSIG records.
 *
 * \param[in] time number of seconds since epoch (midnight, January 1st, 1970)
 *            to be interpreted as a serial arithmetics number relative to now.
 * \param[in] now number of seconds since epoch (midnight, January 1st, 1970)
 *            to which the time value is compared to determine the final value.
 * \param[out] result the struct with the broken-out time information
 * \return result on success or NULL on error
 */
struct tm * sldns_serial_arithmetics_gmtime_r(int32_t time, time_t now, struct tm *result);

/**
 * converts a ttl value (like 5d2h) to a long.
 * \param[in] nptr the start of the string
 * \param[out] endptr points to the last char in case of error
 * \param[out] overflow returns if the string causes integer overflow error,
 * 	       the number is too big, string of digits too long.
 * \return the convert duration value
 */
uint32_t sldns_str2period(const char *nptr, const char **endptr, int* overflow);

/**
 * Returns the int value of the given (hex) digit
 * \param[in] ch the hex char to convert
 * \return the converted decimal value
 */
int sldns_hexdigit_to_int(char ch);

/**
 * calculates the size needed to store the result of b64_ntop
 */
size_t sldns_b64_ntop_calculate_size(size_t srcsize);

int sldns_b64_ntop(uint8_t const *src, size_t srclength,
	char *target, size_t targsize);
int sldns_b64url_ntop(uint8_t const *src, size_t srclength, char *target,
	size_t targsize);

/**
 * calculates the size needed to store the result of sldns_b64_pton
 */
size_t sldns_b64_pton_calculate_size(size_t srcsize);
int sldns_b64_pton(char const *src, uint8_t *target, size_t targsize);
int sldns_b64url_pton(char const *src, size_t srcsize, uint8_t *target,
	size_t targsize);
int sldns_b64_contains_nonurl(char const *src, size_t srcsize);

/**
 * calculates the size needed to store the result of b32_ntop
 */
size_t sldns_b32_ntop_calculate_size(size_t src_data_length);

size_t sldns_b32_ntop_calculate_size_no_padding(size_t src_data_length);

int sldns_b32_ntop(const uint8_t* src_data, size_t src_data_length,
	char* target_text_buffer, size_t target_text_buffer_size);

int sldns_b32_ntop_extended_hex(const uint8_t* src_data, size_t src_data_length,
	char* target_text_buffer, size_t target_text_buffer_size);

/**
 * calculates the size needed to store the result of b32_pton
 */
size_t sldns_b32_pton_calculate_size(size_t src_text_length);

int sldns_b32_pton(const char* src_text, size_t src_text_length,
	uint8_t* target_data_buffer, size_t target_data_buffer_size);

int sldns_b32_pton_extended_hex(const char* src_text, size_t src_text_length,
	uint8_t* target_data_buffer, size_t target_data_buffer_size);

/*
 * Checks whether the escaped value at **s is an octal value or
 * a 'normally' escaped character (and not eos)
 *
 * @param ch_p: the parsed character
 * @param str_p: the string. moved along for characters read.
 * The string pointer at *s is increased by either 0 (on error), 1 (on
 * normal escapes), or 3 (on octals)
 *
 * @return 0 on error
 */
int sldns_parse_escape(uint8_t *ch_p, const char** str_p);

/** 
 * Parse one character, with escape codes,
 * @param ch_p: the parsed character
 * @param str_p: the string. moved along for characters read.
 * @return 0 on error
 */
int sldns_parse_char(uint8_t *ch_p, const char** str_p);

#endif /* LDNS_PARSEUTIL_H */
