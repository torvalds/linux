/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

/******* EXPOSED TYPES ********************************************************/
/*
* Contains the parsed contents of a dictionary
* This includes Huffman and FSE tables used for decoding and data on offsets
*/
typedef struct dictionary_s dictionary_t;
/******* END EXPOSED TYPES ****************************************************/

/******* DECOMPRESSION FUNCTIONS **********************************************/
/// Zstandard decompression functions.
/// `dst` must point to a space at least as large as the reconstructed output.
size_t ZSTD_decompress(void *const dst, const size_t dst_len,
                    const void *const src, const size_t src_len);

/// If `dict != NULL` and `dict_len >= 8`, does the same thing as
/// `ZSTD_decompress` but uses the provided dict
size_t ZSTD_decompress_with_dict(void *const dst, const size_t dst_len,
                              const void *const src, const size_t src_len,
                              dictionary_t* parsed_dict);

/// Get the decompressed size of an input stream so memory can be allocated in
/// advance
/// Returns -1 if the size can't be determined
/// Assumes decompression of a single frame
size_t ZSTD_get_decompressed_size(const void *const src, const size_t src_len);
/******* END DECOMPRESSION FUNCTIONS ******************************************/

/******* DICTIONARY MANAGEMENT ***********************************************/
/*
 * Return a valid dictionary_t pointer for use with dictionary initialization
 * or decompression
 */
dictionary_t* create_dictionary();

/*
 * Parse a provided dictionary blob for use in decompression
 * `src` -- must point to memory space representing the dictionary
 * `src_len` -- must provide the dictionary size
 * `dict` -- will contain the parsed contents of the dictionary and
 *        can be used for decompression
 */
void parse_dictionary(dictionary_t *const dict, const void *src,
                             size_t src_len);

/*
 * Free internal Huffman tables, FSE tables, and dictionary content
 */
void free_dictionary(dictionary_t *const dict);
/******* END DICTIONARY MANAGEMENT *******************************************/
