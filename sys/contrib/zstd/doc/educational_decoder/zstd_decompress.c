/*
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

/// Zstandard educational decoder implementation
/// See https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zstd_decompress.h"

/******* UTILITY MACROS AND TYPES *********************************************/
// Max block size decompressed size is 128 KB and literal blocks can't be
// larger than their block
#define MAX_LITERALS_SIZE ((size_t)128 * 1024)

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

/// This decoder calls exit(1) when it encounters an error, however a production
/// library should propagate error codes
#define ERROR(s)                                                               \
    do {                                                                       \
        fprintf(stderr, "Error: %s\n", s);                                     \
        exit(1);                                                               \
    } while (0)
#define INP_SIZE()                                                             \
    ERROR("Input buffer smaller than it should be or input is "                \
          "corrupted")
#define OUT_SIZE() ERROR("Output buffer too small for output")
#define CORRUPTION() ERROR("Corruption detected while decompressing")
#define BAD_ALLOC() ERROR("Memory allocation error")
#define IMPOSSIBLE() ERROR("An impossibility has occurred")

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
/******* END UTILITY MACROS AND TYPES *****************************************/

/******* IMPLEMENTATION PRIMITIVE PROTOTYPES **********************************/
/// The implementations for these functions can be found at the bottom of this
/// file.  They implement low-level functionality needed for the higher level
/// decompression functions.

/*** IO STREAM OPERATIONS *************/

/// ostream_t/istream_t are used to wrap the pointers/length data passed into
/// ZSTD_decompress, so that all IO operations are safely bounds checked
/// They are written/read forward, and reads are treated as little-endian
/// They should be used opaquely to ensure safety
typedef struct {
    u8 *ptr;
    size_t len;
} ostream_t;

typedef struct {
    const u8 *ptr;
    size_t len;

    // Input often reads a few bits at a time, so maintain an internal offset
    int bit_offset;
} istream_t;

/// The following two functions are the only ones that allow the istream to be
/// non-byte aligned

/// Reads `num` bits from a bitstream, and updates the internal offset
static inline u64 IO_read_bits(istream_t *const in, const int num_bits);
/// Backs-up the stream by `num` bits so they can be read again
static inline void IO_rewind_bits(istream_t *const in, const int num_bits);
/// If the remaining bits in a byte will be unused, advance to the end of the
/// byte
static inline void IO_align_stream(istream_t *const in);

/// Write the given byte into the output stream
static inline void IO_write_byte(ostream_t *const out, u8 symb);

/// Returns the number of bytes left to be read in this stream.  The stream must
/// be byte aligned.
static inline size_t IO_istream_len(const istream_t *const in);

/// Advances the stream by `len` bytes, and returns a pointer to the chunk that
/// was skipped.  The stream must be byte aligned.
static inline const u8 *IO_get_read_ptr(istream_t *const in, size_t len);
/// Advances the stream by `len` bytes, and returns a pointer to the chunk that
/// was skipped so it can be written to.
static inline u8 *IO_get_write_ptr(ostream_t *const out, size_t len);

/// Advance the inner state by `len` bytes.  The stream must be byte aligned.
static inline void IO_advance_input(istream_t *const in, size_t len);

/// Returns an `ostream_t` constructed from the given pointer and length.
static inline ostream_t IO_make_ostream(u8 *out, size_t len);
/// Returns an `istream_t` constructed from the given pointer and length.
static inline istream_t IO_make_istream(const u8 *in, size_t len);

/// Returns an `istream_t` with the same base as `in`, and length `len`.
/// Then, advance `in` to account for the consumed bytes.
/// `in` must be byte aligned.
static inline istream_t IO_make_sub_istream(istream_t *const in, size_t len);
/*** END IO STREAM OPERATIONS *********/

/*** BITSTREAM OPERATIONS *************/
/// Read `num` bits (up to 64) from `src + offset`, where `offset` is in bits,
/// and return them interpreted as a little-endian unsigned integer.
static inline u64 read_bits_LE(const u8 *src, const int num_bits,
                               const size_t offset);

/// Read bits from the end of a HUF or FSE bitstream.  `offset` is in bits, so
/// it updates `offset` to `offset - bits`, and then reads `bits` bits from
/// `src + offset`.  If the offset becomes negative, the extra bits at the
/// bottom are filled in with `0` bits instead of reading from before `src`.
static inline u64 STREAM_read_bits(const u8 *src, const int bits,
                                   i64 *const offset);
/*** END BITSTREAM OPERATIONS *********/

/*** BIT COUNTING OPERATIONS **********/
/// Returns the index of the highest set bit in `num`, or `-1` if `num == 0`
static inline int highest_set_bit(const u64 num);
/*** END BIT COUNTING OPERATIONS ******/

/*** HUFFMAN PRIMITIVES ***************/
// Table decode method uses exponential memory, so we need to limit depth
#define HUF_MAX_BITS (16)

// Limit the maximum number of symbols to 256 so we can store a symbol in a byte
#define HUF_MAX_SYMBS (256)

/// Structure containing all tables necessary for efficient Huffman decoding
typedef struct {
    u8 *symbols;
    u8 *num_bits;
    int max_bits;
} HUF_dtable;

/// Decode a single symbol and read in enough bits to refresh the state
static inline u8 HUF_decode_symbol(const HUF_dtable *const dtable,
                                   u16 *const state, const u8 *const src,
                                   i64 *const offset);
/// Read in a full state's worth of bits to initialize it
static inline void HUF_init_state(const HUF_dtable *const dtable,
                                  u16 *const state, const u8 *const src,
                                  i64 *const offset);

/// Decompresses a single Huffman stream, returns the number of bytes decoded.
/// `src_len` must be the exact length of the Huffman-coded block.
static size_t HUF_decompress_1stream(const HUF_dtable *const dtable,
                                     ostream_t *const out, istream_t *const in);
/// Same as previous but decodes 4 streams, formatted as in the Zstandard
/// specification.
/// `src_len` must be the exact length of the Huffman-coded block.
static size_t HUF_decompress_4stream(const HUF_dtable *const dtable,
                                     ostream_t *const out, istream_t *const in);

/// Initialize a Huffman decoding table using the table of bit counts provided
static void HUF_init_dtable(HUF_dtable *const table, const u8 *const bits,
                            const int num_symbs);
/// Initialize a Huffman decoding table using the table of weights provided
/// Weights follow the definition provided in the Zstandard specification
static void HUF_init_dtable_usingweights(HUF_dtable *const table,
                                         const u8 *const weights,
                                         const int num_symbs);

/// Free the malloc'ed parts of a decoding table
static void HUF_free_dtable(HUF_dtable *const dtable);

/// Deep copy a decoding table, so that it can be used and free'd without
/// impacting the source table.
static void HUF_copy_dtable(HUF_dtable *const dst, const HUF_dtable *const src);
/*** END HUFFMAN PRIMITIVES ***********/

/*** FSE PRIMITIVES *******************/
/// For more description of FSE see
/// https://github.com/Cyan4973/FiniteStateEntropy/

// FSE table decoding uses exponential memory, so limit the maximum accuracy
#define FSE_MAX_ACCURACY_LOG (15)
// Limit the maximum number of symbols so they can be stored in a single byte
#define FSE_MAX_SYMBS (256)

/// The tables needed to decode FSE encoded streams
typedef struct {
    u8 *symbols;
    u8 *num_bits;
    u16 *new_state_base;
    int accuracy_log;
} FSE_dtable;

/// Return the symbol for the current state
static inline u8 FSE_peek_symbol(const FSE_dtable *const dtable,
                                 const u16 state);
/// Read the number of bits necessary to update state, update, and shift offset
/// back to reflect the bits read
static inline void FSE_update_state(const FSE_dtable *const dtable,
                                    u16 *const state, const u8 *const src,
                                    i64 *const offset);

/// Combine peek and update: decode a symbol and update the state
static inline u8 FSE_decode_symbol(const FSE_dtable *const dtable,
                                   u16 *const state, const u8 *const src,
                                   i64 *const offset);

/// Read bits from the stream to initialize the state and shift offset back
static inline void FSE_init_state(const FSE_dtable *const dtable,
                                  u16 *const state, const u8 *const src,
                                  i64 *const offset);

/// Decompress two interleaved bitstreams (e.g. compressed Huffman weights)
/// using an FSE decoding table.  `src_len` must be the exact length of the
/// block.
static size_t FSE_decompress_interleaved2(const FSE_dtable *const dtable,
                                          ostream_t *const out,
                                          istream_t *const in);

/// Initialize a decoding table using normalized frequencies.
static void FSE_init_dtable(FSE_dtable *const dtable,
                            const i16 *const norm_freqs, const int num_symbs,
                            const int accuracy_log);

/// Decode an FSE header as defined in the Zstandard format specification and
/// use the decoded frequencies to initialize a decoding table.
static void FSE_decode_header(FSE_dtable *const dtable, istream_t *const in,
                                const int max_accuracy_log);

/// Initialize an FSE table that will always return the same symbol and consume
/// 0 bits per symbol, to be used for RLE mode in sequence commands
static void FSE_init_dtable_rle(FSE_dtable *const dtable, const u8 symb);

/// Free the malloc'ed parts of a decoding table
static void FSE_free_dtable(FSE_dtable *const dtable);

/// Deep copy a decoding table, so that it can be used and free'd without
/// impacting the source table.
static void FSE_copy_dtable(FSE_dtable *const dst, const FSE_dtable *const src);
/*** END FSE PRIMITIVES ***************/

/******* END IMPLEMENTATION PRIMITIVE PROTOTYPES ******************************/

/******* ZSTD HELPER STRUCTS AND PROTOTYPES ***********************************/

/// A small structure that can be reused in various places that need to access
/// frame header information
typedef struct {
    // The size of window that we need to be able to contiguously store for
    // references
    size_t window_size;
    // The total output size of this compressed frame
    size_t frame_content_size;

    // The dictionary id if this frame uses one
    u32 dictionary_id;

    // Whether or not the content of this frame has a checksum
    int content_checksum_flag;
    // Whether or not the output for this frame is in a single segment
    int single_segment_flag;
} frame_header_t;

/// The context needed to decode blocks in a frame
typedef struct {
    frame_header_t header;

    // The total amount of data available for backreferences, to determine if an
    // offset too large to be correct
    size_t current_total_output;

    const u8 *dict_content;
    size_t dict_content_len;

    // Entropy encoding tables so they can be repeated by future blocks instead
    // of retransmitting
    HUF_dtable literals_dtable;
    FSE_dtable ll_dtable;
    FSE_dtable ml_dtable;
    FSE_dtable of_dtable;

    // The last 3 offsets for the special "repeat offsets".
    u64 previous_offsets[3];
} frame_context_t;

/// The decoded contents of a dictionary so that it doesn't have to be repeated
/// for each frame that uses it
struct dictionary_s {
    // Entropy tables
    HUF_dtable literals_dtable;
    FSE_dtable ll_dtable;
    FSE_dtable ml_dtable;
    FSE_dtable of_dtable;

    // Raw content for backreferences
    u8 *content;
    size_t content_size;

    // Offset history to prepopulate the frame's history
    u64 previous_offsets[3];

    u32 dictionary_id;
};

/// A tuple containing the parts necessary to decode and execute a ZSTD sequence
/// command
typedef struct {
    u32 literal_length;
    u32 match_length;
    u32 offset;
} sequence_command_t;

/// The decoder works top-down, starting at the high level like Zstd frames, and
/// working down to lower more technical levels such as blocks, literals, and
/// sequences.  The high-level functions roughly follow the outline of the
/// format specification:
/// https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md

/// Before the implementation of each high-level function declared here, the
/// prototypes for their helper functions are defined and explained

/// Decode a single Zstd frame, or error if the input is not a valid frame.
/// Accepts a dict argument, which may be NULL indicating no dictionary.
/// See
/// https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md#frame-concatenation
static void decode_frame(ostream_t *const out, istream_t *const in,
                         const dictionary_t *const dict);

// Decode data in a compressed block
static void decompress_block(frame_context_t *const ctx, ostream_t *const out,
                             istream_t *const in);

// Decode the literals section of a block
static size_t decode_literals(frame_context_t *const ctx, istream_t *const in,
                              u8 **const literals);

// Decode the sequences part of a block
static size_t decode_sequences(frame_context_t *const ctx, istream_t *const in,
                               sequence_command_t **const sequences);

// Execute the decoded sequences on the literals block
static void execute_sequences(frame_context_t *const ctx, ostream_t *const out,
                              const u8 *const literals,
                              const size_t literals_len,
                              const sequence_command_t *const sequences,
                              const size_t num_sequences);

// Copies literals and returns the total literal length that was copied
static u32 copy_literals(const size_t seq, istream_t *litstream,
                         ostream_t *const out);

// Given an offset code from a sequence command (either an actual offset value
// or an index for previous offset), computes the correct offset and udpates
// the offset history
static size_t compute_offset(sequence_command_t seq, u64 *const offset_hist);

// Given an offset, match length, and total output, as well as the frame
// context for the dictionary, determines if the dictionary is used and
// executes the copy operation
static void execute_match_copy(frame_context_t *const ctx, size_t offset,
                              size_t match_length, size_t total_output,
                              ostream_t *const out);

/******* END ZSTD HELPER STRUCTS AND PROTOTYPES *******************************/

size_t ZSTD_decompress(void *const dst, const size_t dst_len,
                       const void *const src, const size_t src_len) {
    dictionary_t* uninit_dict = create_dictionary();
    size_t const decomp_size = ZSTD_decompress_with_dict(dst, dst_len, src,
                                                         src_len, uninit_dict);
    free_dictionary(uninit_dict);
    return decomp_size;
}

size_t ZSTD_decompress_with_dict(void *const dst, const size_t dst_len,
                                 const void *const src, const size_t src_len,
                                 dictionary_t* parsed_dict) {

    istream_t in = IO_make_istream(src, src_len);
    ostream_t out = IO_make_ostream(dst, dst_len);

    // "A content compressed by Zstandard is transformed into a Zstandard frame.
    // Multiple frames can be appended into a single file or stream. A frame is
    // totally independent, has a defined beginning and end, and a set of
    // parameters which tells the decoder how to decompress it."

    /* this decoder assumes decompression of a single frame */
    decode_frame(&out, &in, parsed_dict);

    return out.ptr - (u8 *)dst;
}

/******* FRAME DECODING ******************************************************/

static void decode_data_frame(ostream_t *const out, istream_t *const in,
                              const dictionary_t *const dict);
static void init_frame_context(frame_context_t *const context,
                               istream_t *const in,
                               const dictionary_t *const dict);
static void free_frame_context(frame_context_t *const context);
static void parse_frame_header(frame_header_t *const header,
                               istream_t *const in);
static void frame_context_apply_dict(frame_context_t *const ctx,
                                     const dictionary_t *const dict);

static void decompress_data(frame_context_t *const ctx, ostream_t *const out,
                            istream_t *const in);

static void decode_frame(ostream_t *const out, istream_t *const in,
                         const dictionary_t *const dict) {
    const u32 magic_number = IO_read_bits(in, 32);
    // Zstandard frame
    //
    // "Magic_Number
    //
    // 4 Bytes, little-endian format. Value : 0xFD2FB528"
    if (magic_number == 0xFD2FB528U) {
        // ZSTD frame
        decode_data_frame(out, in, dict);

        return;
    }

    // not a real frame or a skippable frame
    ERROR("Tried to decode non-ZSTD frame");
}

/// Decode a frame that contains compressed data.  Not all frames do as there
/// are skippable frames.
/// See
/// https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md#general-structure-of-zstandard-frame-format
static void decode_data_frame(ostream_t *const out, istream_t *const in,
                              const dictionary_t *const dict) {
    frame_context_t ctx;

    // Initialize the context that needs to be carried from block to block
    init_frame_context(&ctx, in, dict);

    if (ctx.header.frame_content_size != 0 &&
        ctx.header.frame_content_size > out->len) {
        OUT_SIZE();
    }

    decompress_data(&ctx, out, in);

    free_frame_context(&ctx);
}

/// Takes the information provided in the header and dictionary, and initializes
/// the context for this frame
static void init_frame_context(frame_context_t *const context,
                               istream_t *const in,
                               const dictionary_t *const dict) {
    // Most fields in context are correct when initialized to 0
    memset(context, 0, sizeof(frame_context_t));

    // Parse data from the frame header
    parse_frame_header(&context->header, in);

    // Set up the offset history for the repeat offset commands
    context->previous_offsets[0] = 1;
    context->previous_offsets[1] = 4;
    context->previous_offsets[2] = 8;

    // Apply details from the dict if it exists
    frame_context_apply_dict(context, dict);
}

static void free_frame_context(frame_context_t *const context) {
    HUF_free_dtable(&context->literals_dtable);

    FSE_free_dtable(&context->ll_dtable);
    FSE_free_dtable(&context->ml_dtable);
    FSE_free_dtable(&context->of_dtable);

    memset(context, 0, sizeof(frame_context_t));
}

static void parse_frame_header(frame_header_t *const header,
                               istream_t *const in) {
    // "The first header's byte is called the Frame_Header_Descriptor. It tells
    // which other fields are present. Decoding this byte is enough to tell the
    // size of Frame_Header.
    //
    // Bit number   Field name
    // 7-6  Frame_Content_Size_flag
    // 5    Single_Segment_flag
    // 4    Unused_bit
    // 3    Reserved_bit
    // 2    Content_Checksum_flag
    // 1-0  Dictionary_ID_flag"
    const u8 descriptor = IO_read_bits(in, 8);

    // decode frame header descriptor into flags
    const u8 frame_content_size_flag = descriptor >> 6;
    const u8 single_segment_flag = (descriptor >> 5) & 1;
    const u8 reserved_bit = (descriptor >> 3) & 1;
    const u8 content_checksum_flag = (descriptor >> 2) & 1;
    const u8 dictionary_id_flag = descriptor & 3;

    if (reserved_bit != 0) {
        CORRUPTION();
    }

    header->single_segment_flag = single_segment_flag;
    header->content_checksum_flag = content_checksum_flag;

    // decode window size
    if (!single_segment_flag) {
        // "Provides guarantees on maximum back-reference distance that will be
        // used within compressed data. This information is important for
        // decoders to allocate enough memory.
        //
        // Bit numbers  7-3         2-0
        // Field name   Exponent    Mantissa"
        u8 window_descriptor = IO_read_bits(in, 8);
        u8 exponent = window_descriptor >> 3;
        u8 mantissa = window_descriptor & 7;

        // Use the algorithm from the specification to compute window size
        // https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md#window_descriptor
        size_t window_base = (size_t)1 << (10 + exponent);
        size_t window_add = (window_base / 8) * mantissa;
        header->window_size = window_base + window_add;
    }

    // decode dictionary id if it exists
    if (dictionary_id_flag) {
        // "This is a variable size field, which contains the ID of the
        // dictionary required to properly decode the frame. Note that this
        // field is optional. When it's not present, it's up to the caller to
        // make sure it uses the correct dictionary. Format is little-endian."
        const int bytes_array[] = {0, 1, 2, 4};
        const int bytes = bytes_array[dictionary_id_flag];

        header->dictionary_id = IO_read_bits(in, bytes * 8);
    } else {
        header->dictionary_id = 0;
    }

    // decode frame content size if it exists
    if (single_segment_flag || frame_content_size_flag) {
        // "This is the original (uncompressed) size. This information is
        // optional. The Field_Size is provided according to value of
        // Frame_Content_Size_flag. The Field_Size can be equal to 0 (not
        // present), 1, 2, 4 or 8 bytes. Format is little-endian."
        //
        // if frame_content_size_flag == 0 but single_segment_flag is set, we
        // still have a 1 byte field
        const int bytes_array[] = {1, 2, 4, 8};
        const int bytes = bytes_array[frame_content_size_flag];

        header->frame_content_size = IO_read_bits(in, bytes * 8);
        if (bytes == 2) {
            // "When Field_Size is 2, the offset of 256 is added."
            header->frame_content_size += 256;
        }
    } else {
        header->frame_content_size = 0;
    }

    if (single_segment_flag) {
        // "The Window_Descriptor byte is optional. It is absent when
        // Single_Segment_flag is set. In this case, the maximum back-reference
        // distance is the content size itself, which can be any value from 1 to
        // 2^64-1 bytes (16 EB)."
        header->window_size = header->frame_content_size;
    }
}

/// A dictionary acts as initializing values for the frame context before
/// decompression, so we implement it by applying it's predetermined
/// tables and content to the context before beginning decompression
static void frame_context_apply_dict(frame_context_t *const ctx,
                                     const dictionary_t *const dict) {
    // If the content pointer is NULL then it must be an empty dict
    if (!dict || !dict->content)
        return;

    // If the requested dictionary_id is non-zero, the correct dictionary must
    // be present
    if (ctx->header.dictionary_id != 0 &&
        ctx->header.dictionary_id != dict->dictionary_id) {
        ERROR("Wrong dictionary provided");
    }

    // Copy the dict content to the context for references during sequence
    // execution
    ctx->dict_content = dict->content;
    ctx->dict_content_len = dict->content_size;

    // If it's a formatted dict copy the precomputed tables in so they can
    // be used in the table repeat modes
    if (dict->dictionary_id != 0) {
        // Deep copy the entropy tables so they can be freed independently of
        // the dictionary struct
        HUF_copy_dtable(&ctx->literals_dtable, &dict->literals_dtable);
        FSE_copy_dtable(&ctx->ll_dtable, &dict->ll_dtable);
        FSE_copy_dtable(&ctx->of_dtable, &dict->of_dtable);
        FSE_copy_dtable(&ctx->ml_dtable, &dict->ml_dtable);

        // Copy the repeated offsets
        memcpy(ctx->previous_offsets, dict->previous_offsets,
               sizeof(ctx->previous_offsets));
    }
}

/// Decompress the data from a frame block by block
static void decompress_data(frame_context_t *const ctx, ostream_t *const out,
                            istream_t *const in) {
    // "A frame encapsulates one or multiple blocks. Each block can be
    // compressed or not, and has a guaranteed maximum content size, which
    // depends on frame parameters. Unlike frames, each block depends on
    // previous blocks for proper decoding. However, each block can be
    // decompressed without waiting for its successor, allowing streaming
    // operations."
    int last_block = 0;
    do {
        // "Last_Block
        //
        // The lowest bit signals if this block is the last one. Frame ends
        // right after this block.
        //
        // Block_Type and Block_Size
        //
        // The next 2 bits represent the Block_Type, while the remaining 21 bits
        // represent the Block_Size. Format is little-endian."
        last_block = IO_read_bits(in, 1);
        const int block_type = IO_read_bits(in, 2);
        const size_t block_len = IO_read_bits(in, 21);

        switch (block_type) {
        case 0: {
            // "Raw_Block - this is an uncompressed block. Block_Size is the
            // number of bytes to read and copy."
            const u8 *const read_ptr = IO_get_read_ptr(in, block_len);
            u8 *const write_ptr = IO_get_write_ptr(out, block_len);

            // Copy the raw data into the output
            memcpy(write_ptr, read_ptr, block_len);

            ctx->current_total_output += block_len;
            break;
        }
        case 1: {
            // "RLE_Block - this is a single byte, repeated N times. In which
            // case, Block_Size is the size to regenerate, while the
            // "compressed" block is just 1 byte (the byte to repeat)."
            const u8 *const read_ptr = IO_get_read_ptr(in, 1);
            u8 *const write_ptr = IO_get_write_ptr(out, block_len);

            // Copy `block_len` copies of `read_ptr[0]` to the output
            memset(write_ptr, read_ptr[0], block_len);

            ctx->current_total_output += block_len;
            break;
        }
        case 2: {
            // "Compressed_Block - this is a Zstandard compressed block,
            // detailed in another section of this specification. Block_Size is
            // the compressed size.

            // Create a sub-stream for the block
            istream_t block_stream = IO_make_sub_istream(in, block_len);
            decompress_block(ctx, out, &block_stream);
            break;
        }
        case 3:
            // "Reserved - this is not a block. This value cannot be used with
            // current version of this specification."
            CORRUPTION();
            break;
        default:
            IMPOSSIBLE();
        }
    } while (!last_block);

    if (ctx->header.content_checksum_flag) {
        // This program does not support checking the checksum, so skip over it
        // if it's present
        IO_advance_input(in, 4);
    }
}
/******* END FRAME DECODING ***************************************************/

/******* BLOCK DECOMPRESSION **************************************************/
static void decompress_block(frame_context_t *const ctx, ostream_t *const out,
                             istream_t *const in) {
    // "A compressed block consists of 2 sections :
    //
    // Literals_Section
    // Sequences_Section"


    // Part 1: decode the literals block
    u8 *literals = NULL;
    const size_t literals_size = decode_literals(ctx, in, &literals);

    // Part 2: decode the sequences block
    sequence_command_t *sequences = NULL;
    const size_t num_sequences =
        decode_sequences(ctx, in, &sequences);

    // Part 3: combine literals and sequence commands to generate output
    execute_sequences(ctx, out, literals, literals_size, sequences,
                      num_sequences);
    free(literals);
    free(sequences);
}
/******* END BLOCK DECOMPRESSION **********************************************/

/******* LITERALS DECODING ****************************************************/
static size_t decode_literals_simple(istream_t *const in, u8 **const literals,
                                     const int block_type,
                                     const int size_format);
static size_t decode_literals_compressed(frame_context_t *const ctx,
                                         istream_t *const in,
                                         u8 **const literals,
                                         const int block_type,
                                         const int size_format);
static void decode_huf_table(HUF_dtable *const dtable, istream_t *const in);
static void fse_decode_hufweights(ostream_t *weights, istream_t *const in,
                                    int *const num_symbs);

static size_t decode_literals(frame_context_t *const ctx, istream_t *const in,
                              u8 **const literals) {
    // "Literals can be stored uncompressed or compressed using Huffman prefix
    // codes. When compressed, an optional tree description can be present,
    // followed by 1 or 4 streams."
    //
    // "Literals_Section_Header
    //
    // Header is in charge of describing how literals are packed. It's a
    // byte-aligned variable-size bitfield, ranging from 1 to 5 bytes, using
    // little-endian convention."
    //
    // "Literals_Block_Type
    //
    // This field uses 2 lowest bits of first byte, describing 4 different block
    // types"
    //
    // size_format takes between 1 and 2 bits
    int block_type = IO_read_bits(in, 2);
    int size_format = IO_read_bits(in, 2);

    if (block_type <= 1) {
        // Raw or RLE literals block
        return decode_literals_simple(in, literals, block_type,
                                      size_format);
    } else {
        // Huffman compressed literals
        return decode_literals_compressed(ctx, in, literals, block_type,
                                          size_format);
    }
}

/// Decodes literals blocks in raw or RLE form
static size_t decode_literals_simple(istream_t *const in, u8 **const literals,
                                     const int block_type,
                                     const int size_format) {
    size_t size;
    switch (size_format) {
    // These cases are in the form ?0
    // In this case, the ? bit is actually part of the size field
    case 0:
    case 2:
        // "Size_Format uses 1 bit. Regenerated_Size uses 5 bits (0-31)."
        IO_rewind_bits(in, 1);
        size = IO_read_bits(in, 5);
        break;
    case 1:
        // "Size_Format uses 2 bits. Regenerated_Size uses 12 bits (0-4095)."
        size = IO_read_bits(in, 12);
        break;
    case 3:
        // "Size_Format uses 2 bits. Regenerated_Size uses 20 bits (0-1048575)."
        size = IO_read_bits(in, 20);
        break;
    default:
        // Size format is in range 0-3
        IMPOSSIBLE();
    }

    if (size > MAX_LITERALS_SIZE) {
        CORRUPTION();
    }

    *literals = malloc(size);
    if (!*literals) {
        BAD_ALLOC();
    }

    switch (block_type) {
    case 0: {
        // "Raw_Literals_Block - Literals are stored uncompressed."
        const u8 *const read_ptr = IO_get_read_ptr(in, size);
        memcpy(*literals, read_ptr, size);
        break;
    }
    case 1: {
        // "RLE_Literals_Block - Literals consist of a single byte value repeated N times."
        const u8 *const read_ptr = IO_get_read_ptr(in, 1);
        memset(*literals, read_ptr[0], size);
        break;
    }
    default:
        IMPOSSIBLE();
    }

    return size;
}

/// Decodes Huffman compressed literals
static size_t decode_literals_compressed(frame_context_t *const ctx,
                                         istream_t *const in,
                                         u8 **const literals,
                                         const int block_type,
                                         const int size_format) {
    size_t regenerated_size, compressed_size;
    // Only size_format=0 has 1 stream, so default to 4
    int num_streams = 4;
    switch (size_format) {
    case 0:
        // "A single stream. Both Compressed_Size and Regenerated_Size use 10
        // bits (0-1023)."
        num_streams = 1;
    // Fall through as it has the same size format
    case 1:
        // "4 streams. Both Compressed_Size and Regenerated_Size use 10 bits
        // (0-1023)."
        regenerated_size = IO_read_bits(in, 10);
        compressed_size = IO_read_bits(in, 10);
        break;
    case 2:
        // "4 streams. Both Compressed_Size and Regenerated_Size use 14 bits
        // (0-16383)."
        regenerated_size = IO_read_bits(in, 14);
        compressed_size = IO_read_bits(in, 14);
        break;
    case 3:
        // "4 streams. Both Compressed_Size and Regenerated_Size use 18 bits
        // (0-262143)."
        regenerated_size = IO_read_bits(in, 18);
        compressed_size = IO_read_bits(in, 18);
        break;
    default:
        // Impossible
        IMPOSSIBLE();
    }
    if (regenerated_size > MAX_LITERALS_SIZE ||
        compressed_size >= regenerated_size) {
        CORRUPTION();
    }

    *literals = malloc(regenerated_size);
    if (!*literals) {
        BAD_ALLOC();
    }

    ostream_t lit_stream = IO_make_ostream(*literals, regenerated_size);
    istream_t huf_stream = IO_make_sub_istream(in, compressed_size);

    if (block_type == 2) {
        // Decode the provided Huffman table
        // "This section is only present when Literals_Block_Type type is
        // Compressed_Literals_Block (2)."

        HUF_free_dtable(&ctx->literals_dtable);
        decode_huf_table(&ctx->literals_dtable, &huf_stream);
    } else {
        // If the previous Huffman table is being repeated, ensure it exists
        if (!ctx->literals_dtable.symbols) {
            CORRUPTION();
        }
    }

    size_t symbols_decoded;
    if (num_streams == 1) {
        symbols_decoded = HUF_decompress_1stream(&ctx->literals_dtable, &lit_stream, &huf_stream);
    } else {
        symbols_decoded = HUF_decompress_4stream(&ctx->literals_dtable, &lit_stream, &huf_stream);
    }

    if (symbols_decoded != regenerated_size) {
        CORRUPTION();
    }

    return regenerated_size;
}

// Decode the Huffman table description
static void decode_huf_table(HUF_dtable *const dtable, istream_t *const in) {
    // "All literal values from zero (included) to last present one (excluded)
    // are represented by Weight with values from 0 to Max_Number_of_Bits."

    // "This is a single byte value (0-255), which describes how to decode the list of weights."
    const u8 header = IO_read_bits(in, 8);

    u8 weights[HUF_MAX_SYMBS];
    memset(weights, 0, sizeof(weights));

    int num_symbs;

    if (header >= 128) {
        // "This is a direct representation, where each Weight is written
        // directly as a 4 bits field (0-15). The full representation occupies
        // ((Number_of_Symbols+1)/2) bytes, meaning it uses a last full byte
        // even if Number_of_Symbols is odd. Number_of_Symbols = headerByte -
        // 127"
        num_symbs = header - 127;
        const size_t bytes = (num_symbs + 1) / 2;

        const u8 *const weight_src = IO_get_read_ptr(in, bytes);

        for (int i = 0; i < num_symbs; i++) {
            // "They are encoded forward, 2
            // weights to a byte with the first weight taking the top four bits
            // and the second taking the bottom four (e.g. the following
            // operations could be used to read the weights: Weight[0] =
            // (Byte[0] >> 4), Weight[1] = (Byte[0] & 0xf), etc.)."
            if (i % 2 == 0) {
                weights[i] = weight_src[i / 2] >> 4;
            } else {
                weights[i] = weight_src[i / 2] & 0xf;
            }
        }
    } else {
        // The weights are FSE encoded, decode them before we can construct the
        // table
        istream_t fse_stream = IO_make_sub_istream(in, header);
        ostream_t weight_stream = IO_make_ostream(weights, HUF_MAX_SYMBS);
        fse_decode_hufweights(&weight_stream, &fse_stream, &num_symbs);
    }

    // Construct the table using the decoded weights
    HUF_init_dtable_usingweights(dtable, weights, num_symbs);
}

static void fse_decode_hufweights(ostream_t *weights, istream_t *const in,
                                    int *const num_symbs) {
    const int MAX_ACCURACY_LOG = 7;

    FSE_dtable dtable;

    // "An FSE bitstream starts by a header, describing probabilities
    // distribution. It will create a Decoding Table. For a list of Huffman
    // weights, maximum accuracy is 7 bits."
    FSE_decode_header(&dtable, in, MAX_ACCURACY_LOG);

    // Decode the weights
    *num_symbs = FSE_decompress_interleaved2(&dtable, weights, in);

    FSE_free_dtable(&dtable);
}
/******* END LITERALS DECODING ************************************************/

/******* SEQUENCE DECODING ****************************************************/
/// The combination of FSE states needed to decode sequences
typedef struct {
    FSE_dtable ll_table;
    FSE_dtable of_table;
    FSE_dtable ml_table;

    u16 ll_state;
    u16 of_state;
    u16 ml_state;
} sequence_states_t;

/// Different modes to signal to decode_seq_tables what to do
typedef enum {
    seq_literal_length = 0,
    seq_offset = 1,
    seq_match_length = 2,
} seq_part_t;

typedef enum {
    seq_predefined = 0,
    seq_rle = 1,
    seq_fse = 2,
    seq_repeat = 3,
} seq_mode_t;

/// The predefined FSE distribution tables for `seq_predefined` mode
static const i16 SEQ_LITERAL_LENGTH_DEFAULT_DIST[36] = {
    4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1,  1,  2,  2,
    2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
static const i16 SEQ_OFFSET_DEFAULT_DIST[29] = {
    1, 1, 1, 1, 1, 1, 2, 2, 2, 1,  1,  1,  1,  1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};
static const i16 SEQ_MATCH_LENGTH_DEFAULT_DIST[53] = {
    1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1,  1,  1,  1,  1,  1,  1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};

/// The sequence decoding baseline and number of additional bits to read/add
/// https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md#the-codes-for-literals-lengths-match-lengths-and-offsets
static const u32 SEQ_LITERAL_LENGTH_BASELINES[36] = {
    0,  1,  2,   3,   4,   5,    6,    7,    8,    9,     10,    11,
    12, 13, 14,  15,  16,  18,   20,   22,   24,   28,    32,    40,
    48, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65538};
static const u8 SEQ_LITERAL_LENGTH_EXTRA_BITS[36] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  1,  1,
    1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

static const u32 SEQ_MATCH_LENGTH_BASELINES[53] = {
    3,  4,   5,   6,   7,    8,    9,    10,   11,    12,    13,   14, 15, 16,
    17, 18,  19,  20,  21,   22,   23,   24,   25,    26,    27,   28, 29, 30,
    31, 32,  33,  34,  35,   37,   39,   41,   43,    47,    51,   59, 67, 83,
    99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};
static const u8 SEQ_MATCH_LENGTH_EXTRA_BITS[53] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  1,  1,  1, 1,
    2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

/// Offset decoding is simpler so we just need a maximum code value
static const u8 SEQ_MAX_CODES[3] = {35, -1, 52};

static void decompress_sequences(frame_context_t *const ctx,
                                 istream_t *const in,
                                 sequence_command_t *const sequences,
                                 const size_t num_sequences);
static sequence_command_t decode_sequence(sequence_states_t *const state,
                                          const u8 *const src,
                                          i64 *const offset);
static void decode_seq_table(FSE_dtable *const table, istream_t *const in,
                               const seq_part_t type, const seq_mode_t mode);

static size_t decode_sequences(frame_context_t *const ctx, istream_t *in,
                               sequence_command_t **const sequences) {
    // "A compressed block is a succession of sequences . A sequence is a
    // literal copy command, followed by a match copy command. A literal copy
    // command specifies a length. It is the number of bytes to be copied (or
    // extracted) from the literal section. A match copy command specifies an
    // offset and a length. The offset gives the position to copy from, which
    // can be within a previous block."

    size_t num_sequences;

    // "Number_of_Sequences
    //
    // This is a variable size field using between 1 and 3 bytes. Let's call its
    // first byte byte0."
    u8 header = IO_read_bits(in, 8);
    if (header == 0) {
        // "There are no sequences. The sequence section stops there.
        // Regenerated content is defined entirely by literals section."
        *sequences = NULL;
        return 0;
    } else if (header < 128) {
        // "Number_of_Sequences = byte0 . Uses 1 byte."
        num_sequences = header;
    } else if (header < 255) {
        // "Number_of_Sequences = ((byte0-128) << 8) + byte1 . Uses 2 bytes."
        num_sequences = ((header - 128) << 8) + IO_read_bits(in, 8);
    } else {
        // "Number_of_Sequences = byte1 + (byte2<<8) + 0x7F00 . Uses 3 bytes."
        num_sequences = IO_read_bits(in, 16) + 0x7F00;
    }

    *sequences = malloc(num_sequences * sizeof(sequence_command_t));
    if (!*sequences) {
        BAD_ALLOC();
    }

    decompress_sequences(ctx, in, *sequences, num_sequences);
    return num_sequences;
}

/// Decompress the FSE encoded sequence commands
static void decompress_sequences(frame_context_t *const ctx, istream_t *in,
                                 sequence_command_t *const sequences,
                                 const size_t num_sequences) {
    // "The Sequences_Section regroup all symbols required to decode commands.
    // There are 3 symbol types : literals lengths, offsets and match lengths.
    // They are encoded together, interleaved, in a single bitstream."

    // "Symbol compression modes
    //
    // This is a single byte, defining the compression mode of each symbol
    // type."
    //
    // Bit number : Field name
    // 7-6        : Literals_Lengths_Mode
    // 5-4        : Offsets_Mode
    // 3-2        : Match_Lengths_Mode
    // 1-0        : Reserved
    u8 compression_modes = IO_read_bits(in, 8);

    if ((compression_modes & 3) != 0) {
        // Reserved bits set
        CORRUPTION();
    }

    // "Following the header, up to 3 distribution tables can be described. When
    // present, they are in this order :
    //
    // Literals lengths
    // Offsets
    // Match Lengths"
    // Update the tables we have stored in the context
    decode_seq_table(&ctx->ll_dtable, in, seq_literal_length,
                     (compression_modes >> 6) & 3);

    decode_seq_table(&ctx->of_dtable, in, seq_offset,
                     (compression_modes >> 4) & 3);

    decode_seq_table(&ctx->ml_dtable, in, seq_match_length,
                     (compression_modes >> 2) & 3);


    sequence_states_t states;

    // Initialize the decoding tables
    {
        states.ll_table = ctx->ll_dtable;
        states.of_table = ctx->of_dtable;
        states.ml_table = ctx->ml_dtable;
    }

    const size_t len = IO_istream_len(in);
    const u8 *const src = IO_get_read_ptr(in, len);

    // "After writing the last bit containing information, the compressor writes
    // a single 1-bit and then fills the byte with 0-7 0 bits of padding."
    const int padding = 8 - highest_set_bit(src[len - 1]);
    // The offset starts at the end because FSE streams are read backwards
    i64 bit_offset = len * 8 - padding;

    // "The bitstream starts with initial state values, each using the required
    // number of bits in their respective accuracy, decoded previously from
    // their normalized distribution.
    //
    // It starts by Literals_Length_State, followed by Offset_State, and finally
    // Match_Length_State."
    FSE_init_state(&states.ll_table, &states.ll_state, src, &bit_offset);
    FSE_init_state(&states.of_table, &states.of_state, src, &bit_offset);
    FSE_init_state(&states.ml_table, &states.ml_state, src, &bit_offset);

    for (size_t i = 0; i < num_sequences; i++) {
        // Decode sequences one by one
        sequences[i] = decode_sequence(&states, src, &bit_offset);
    }

    if (bit_offset != 0) {
        CORRUPTION();
    }
}

// Decode a single sequence and update the state
static sequence_command_t decode_sequence(sequence_states_t *const states,
                                          const u8 *const src,
                                          i64 *const offset) {
    // "Each symbol is a code in its own context, which specifies Baseline and
    // Number_of_Bits to add. Codes are FSE compressed, and interleaved with raw
    // additional bits in the same bitstream."

    // Decode symbols, but don't update states
    const u8 of_code = FSE_peek_symbol(&states->of_table, states->of_state);
    const u8 ll_code = FSE_peek_symbol(&states->ll_table, states->ll_state);
    const u8 ml_code = FSE_peek_symbol(&states->ml_table, states->ml_state);

    // Offset doesn't need a max value as it's not decoded using a table
    if (ll_code > SEQ_MAX_CODES[seq_literal_length] ||
        ml_code > SEQ_MAX_CODES[seq_match_length]) {
        CORRUPTION();
    }

    // Read the interleaved bits
    sequence_command_t seq;
    // "Decoding starts by reading the Number_of_Bits required to decode Offset.
    // It then does the same for Match_Length, and then for Literals_Length."
    seq.offset = ((u32)1 << of_code) + STREAM_read_bits(src, of_code, offset);

    seq.match_length =
        SEQ_MATCH_LENGTH_BASELINES[ml_code] +
        STREAM_read_bits(src, SEQ_MATCH_LENGTH_EXTRA_BITS[ml_code], offset);

    seq.literal_length =
        SEQ_LITERAL_LENGTH_BASELINES[ll_code] +
        STREAM_read_bits(src, SEQ_LITERAL_LENGTH_EXTRA_BITS[ll_code], offset);

    // "If it is not the last sequence in the block, the next operation is to
    // update states. Using the rules pre-calculated in the decoding tables,
    // Literals_Length_State is updated, followed by Match_Length_State, and
    // then Offset_State."
    // If the stream is complete don't read bits to update state
    if (*offset != 0) {
        FSE_update_state(&states->ll_table, &states->ll_state, src, offset);
        FSE_update_state(&states->ml_table, &states->ml_state, src, offset);
        FSE_update_state(&states->of_table, &states->of_state, src, offset);
    }

    return seq;
}

/// Given a sequence part and table mode, decode the FSE distribution
/// Errors if the mode is `seq_repeat` without a pre-existing table in `table`
static void decode_seq_table(FSE_dtable *const table, istream_t *const in,
                             const seq_part_t type, const seq_mode_t mode) {
    // Constant arrays indexed by seq_part_t
    const i16 *const default_distributions[] = {SEQ_LITERAL_LENGTH_DEFAULT_DIST,
                                                SEQ_OFFSET_DEFAULT_DIST,
                                                SEQ_MATCH_LENGTH_DEFAULT_DIST};
    const size_t default_distribution_lengths[] = {36, 29, 53};
    const size_t default_distribution_accuracies[] = {6, 5, 6};

    const size_t max_accuracies[] = {9, 8, 9};

    if (mode != seq_repeat) {
        // Free old one before overwriting
        FSE_free_dtable(table);
    }

    switch (mode) {
    case seq_predefined: {
        // "Predefined_Mode : uses a predefined distribution table."
        const i16 *distribution = default_distributions[type];
        const size_t symbs = default_distribution_lengths[type];
        const size_t accuracy_log = default_distribution_accuracies[type];

        FSE_init_dtable(table, distribution, symbs, accuracy_log);
        break;
    }
    case seq_rle: {
        // "RLE_Mode : it's a single code, repeated Number_of_Sequences times."
        const u8 symb = IO_get_read_ptr(in, 1)[0];
        FSE_init_dtable_rle(table, symb);
        break;
    }
    case seq_fse: {
        // "FSE_Compressed_Mode : standard FSE compression. A distribution table
        // will be present "
        FSE_decode_header(table, in, max_accuracies[type]);
        break;
    }
    case seq_repeat:
        // "Repeat_Mode : re-use distribution table from previous compressed
        // block."
        // Nothing to do here, table will be unchanged
        if (!table->symbols) {
            // This mode is invalid if we don't already have a table
            CORRUPTION();
        }
        break;
    default:
        // Impossible, as mode is from 0-3
        IMPOSSIBLE();
        break;
    }

}
/******* END SEQUENCE DECODING ************************************************/

/******* SEQUENCE EXECUTION ***************************************************/
static void execute_sequences(frame_context_t *const ctx, ostream_t *const out,
                              const u8 *const literals,
                              const size_t literals_len,
                              const sequence_command_t *const sequences,
                              const size_t num_sequences) {
    istream_t litstream = IO_make_istream(literals, literals_len);

    u64 *const offset_hist = ctx->previous_offsets;
    size_t total_output = ctx->current_total_output;

    for (size_t i = 0; i < num_sequences; i++) {
        const sequence_command_t seq = sequences[i];
        {
            const u32 literals_size = copy_literals(seq.literal_length, &litstream, out);
            total_output += literals_size;
        }

        size_t const offset = compute_offset(seq, offset_hist);

        size_t const match_length = seq.match_length;

        execute_match_copy(ctx, offset, match_length, total_output, out);

        total_output += match_length;
    }

    // Copy any leftover literals
    {
        size_t len = IO_istream_len(&litstream);
        copy_literals(len, &litstream, out);
        total_output += len;
    }

    ctx->current_total_output = total_output;
}

static u32 copy_literals(const size_t literal_length, istream_t *litstream,
                         ostream_t *const out) {
    // If the sequence asks for more literals than are left, the
    // sequence must be corrupted
    if (literal_length > IO_istream_len(litstream)) {
        CORRUPTION();
    }

    u8 *const write_ptr = IO_get_write_ptr(out, literal_length);
    const u8 *const read_ptr =
         IO_get_read_ptr(litstream, literal_length);
    // Copy literals to output
    memcpy(write_ptr, read_ptr, literal_length);

    return literal_length;
}

static size_t compute_offset(sequence_command_t seq, u64 *const offset_hist) {
    size_t offset;
    // Offsets are special, we need to handle the repeat offsets
    if (seq.offset <= 3) {
        // "The first 3 values define a repeated offset and we will call
        // them Repeated_Offset1, Repeated_Offset2, and Repeated_Offset3.
        // They are sorted in recency order, with Repeated_Offset1 meaning
        // 'most recent one'".

        // Use 0 indexing for the array
        u32 idx = seq.offset - 1;
        if (seq.literal_length == 0) {
            // "There is an exception though, when current sequence's
            // literals length is 0. In this case, repeated offsets are
            // shifted by one, so Repeated_Offset1 becomes Repeated_Offset2,
            // Repeated_Offset2 becomes Repeated_Offset3, and
            // Repeated_Offset3 becomes Repeated_Offset1 - 1_byte."
            idx++;
        }

        if (idx == 0) {
            offset = offset_hist[0];
        } else {
            // If idx == 3 then literal length was 0 and the offset was 3,
            // as per the exception listed above
            offset = idx < 3 ? offset_hist[idx] : offset_hist[0] - 1;

            // If idx == 1 we don't need to modify offset_hist[2], since
            // we're using the second-most recent code
            if (idx > 1) {
                offset_hist[2] = offset_hist[1];
            }
            offset_hist[1] = offset_hist[0];
            offset_hist[0] = offset;
        }
    } else {
        // When it's not a repeat offset:
        // "if (Offset_Value > 3) offset = Offset_Value - 3;"
        offset = seq.offset - 3;

        // Shift back history
        offset_hist[2] = offset_hist[1];
        offset_hist[1] = offset_hist[0];
        offset_hist[0] = offset;
    }
    return offset;
}

static void execute_match_copy(frame_context_t *const ctx, size_t offset,
                              size_t match_length, size_t total_output,
                              ostream_t *const out) {
    u8 *write_ptr = IO_get_write_ptr(out, match_length);
    if (total_output <= ctx->header.window_size) {
        // In this case offset might go back into the dictionary
        if (offset > total_output + ctx->dict_content_len) {
            // The offset goes beyond even the dictionary
            CORRUPTION();
        }

        if (offset > total_output) {
            // "The rest of the dictionary is its content. The content act
            // as a "past" in front of data to compress or decompress, so it
            // can be referenced in sequence commands."
            const size_t dict_copy =
                MIN(offset - total_output, match_length);
            const size_t dict_offset =
                ctx->dict_content_len - (offset - total_output);

            memcpy(write_ptr, ctx->dict_content + dict_offset, dict_copy);
            write_ptr += dict_copy;
            match_length -= dict_copy;
        }
    } else if (offset > ctx->header.window_size) {
        CORRUPTION();
    }

    // We must copy byte by byte because the match length might be larger
    // than the offset
    // ex: if the output so far was "abc", a command with offset=3 and
    // match_length=6 would produce "abcabcabc" as the new output
    for (size_t j = 0; j < match_length; j++) {
        *write_ptr = *(write_ptr - offset);
        write_ptr++;
    }
}
/******* END SEQUENCE EXECUTION ***********************************************/

/******* OUTPUT SIZE COUNTING *************************************************/
/// Get the decompressed size of an input stream so memory can be allocated in
/// advance.
/// This implementation assumes `src` points to a single ZSTD-compressed frame
size_t ZSTD_get_decompressed_size(const void *src, const size_t src_len) {
    istream_t in = IO_make_istream(src, src_len);

    // get decompressed size from ZSTD frame header
    {
        const u32 magic_number = IO_read_bits(&in, 32);

        if (magic_number == 0xFD2FB528U) {
            // ZSTD frame
            frame_header_t header;
            parse_frame_header(&header, &in);

            if (header.frame_content_size == 0 && !header.single_segment_flag) {
                // Content size not provided, we can't tell
                return -1;
            }

            return header.frame_content_size;
        } else {
            // not a real frame or skippable frame
            ERROR("ZSTD frame magic number did not match");
        }
    }
}
/******* END OUTPUT SIZE COUNTING *********************************************/

/******* DICTIONARY PARSING ***************************************************/
#define DICT_SIZE_ERROR() ERROR("Dictionary size cannot be less than 8 bytes")
#define NULL_SRC() ERROR("Tried to create dictionary with pointer to null src");

dictionary_t* create_dictionary() {
    dictionary_t* dict = calloc(1, sizeof(dictionary_t));
    if (!dict) {
        BAD_ALLOC();
    }
    return dict;
}

static void init_dictionary_content(dictionary_t *const dict,
                                    istream_t *const in);

void parse_dictionary(dictionary_t *const dict, const void *src,
                             size_t src_len) {
    const u8 *byte_src = (const u8 *)src;
    memset(dict, 0, sizeof(dictionary_t));
    if (src == NULL) { /* cannot initialize dictionary with null src */
        NULL_SRC();
    }
    if (src_len < 8) {
        DICT_SIZE_ERROR();
    }

    istream_t in = IO_make_istream(byte_src, src_len);

    const u32 magic_number = IO_read_bits(&in, 32);
    if (magic_number != 0xEC30A437) {
        // raw content dict
        IO_rewind_bits(&in, 32);
        init_dictionary_content(dict, &in);
        return;
    }

    dict->dictionary_id = IO_read_bits(&in, 32);

    // "Entropy_Tables : following the same format as the tables in compressed
    // blocks. They are stored in following order : Huffman tables for literals,
    // FSE table for offsets, FSE table for match lengths, and FSE table for
    // literals lengths. It's finally followed by 3 offset values, populating
    // recent offsets (instead of using {1,4,8}), stored in order, 4-bytes
    // little-endian each, for a total of 12 bytes. Each recent offset must have
    // a value < dictionary size."
    decode_huf_table(&dict->literals_dtable, &in);
    decode_seq_table(&dict->of_dtable, &in, seq_offset, seq_fse);
    decode_seq_table(&dict->ml_dtable, &in, seq_match_length, seq_fse);
    decode_seq_table(&dict->ll_dtable, &in, seq_literal_length, seq_fse);

    // Read in the previous offset history
    dict->previous_offsets[0] = IO_read_bits(&in, 32);
    dict->previous_offsets[1] = IO_read_bits(&in, 32);
    dict->previous_offsets[2] = IO_read_bits(&in, 32);

    // Ensure the provided offsets aren't too large
    // "Each recent offset must have a value < dictionary size."
    for (int i = 0; i < 3; i++) {
        if (dict->previous_offsets[i] > src_len) {
            ERROR("Dictionary corrupted");
        }
    }

    // "Content : The rest of the dictionary is its content. The content act as
    // a "past" in front of data to compress or decompress, so it can be
    // referenced in sequence commands."
    init_dictionary_content(dict, &in);
}

static void init_dictionary_content(dictionary_t *const dict,
                                    istream_t *const in) {
    // Copy in the content
    dict->content_size = IO_istream_len(in);
    dict->content = malloc(dict->content_size);
    if (!dict->content) {
        BAD_ALLOC();
    }

    const u8 *const content = IO_get_read_ptr(in, dict->content_size);

    memcpy(dict->content, content, dict->content_size);
}

/// Free an allocated dictionary
void free_dictionary(dictionary_t *const dict) {
    HUF_free_dtable(&dict->literals_dtable);
    FSE_free_dtable(&dict->ll_dtable);
    FSE_free_dtable(&dict->of_dtable);
    FSE_free_dtable(&dict->ml_dtable);

    free(dict->content);

    memset(dict, 0, sizeof(dictionary_t));

    free(dict);
}
/******* END DICTIONARY PARSING ***********************************************/

/******* IO STREAM OPERATIONS *************************************************/
#define UNALIGNED() ERROR("Attempting to operate on a non-byte aligned stream")
/// Reads `num` bits from a bitstream, and updates the internal offset
static inline u64 IO_read_bits(istream_t *const in, const int num_bits) {
    if (num_bits > 64 || num_bits <= 0) {
        ERROR("Attempt to read an invalid number of bits");
    }

    const size_t bytes = (num_bits + in->bit_offset + 7) / 8;
    const size_t full_bytes = (num_bits + in->bit_offset) / 8;
    if (bytes > in->len) {
        INP_SIZE();
    }

    const u64 result = read_bits_LE(in->ptr, num_bits, in->bit_offset);

    in->bit_offset = (num_bits + in->bit_offset) % 8;
    in->ptr += full_bytes;
    in->len -= full_bytes;

    return result;
}

/// If a non-zero number of bits have been read from the current byte, advance
/// the offset to the next byte
static inline void IO_rewind_bits(istream_t *const in, int num_bits) {
    if (num_bits < 0) {
        ERROR("Attempting to rewind stream by a negative number of bits");
    }

    // move the offset back by `num_bits` bits
    const int new_offset = in->bit_offset - num_bits;
    // determine the number of whole bytes we have to rewind, rounding up to an
    // integer number (e.g. if `new_offset == -5`, `bytes == 1`)
    const i64 bytes = -(new_offset - 7) / 8;

    in->ptr -= bytes;
    in->len += bytes;
    // make sure the resulting `bit_offset` is positive, as mod in C does not
    // convert numbers from negative to positive (e.g. -22 % 8 == -6)
    in->bit_offset = ((new_offset % 8) + 8) % 8;
}

/// If the remaining bits in a byte will be unused, advance to the end of the
/// byte
static inline void IO_align_stream(istream_t *const in) {
    if (in->bit_offset != 0) {
        if (in->len == 0) {
            INP_SIZE();
        }
        in->ptr++;
        in->len--;
        in->bit_offset = 0;
    }
}

/// Write the given byte into the output stream
static inline void IO_write_byte(ostream_t *const out, u8 symb) {
    if (out->len == 0) {
        OUT_SIZE();
    }

    out->ptr[0] = symb;
    out->ptr++;
    out->len--;
}

/// Returns the number of bytes left to be read in this stream.  The stream must
/// be byte aligned.
static inline size_t IO_istream_len(const istream_t *const in) {
    return in->len;
}

/// Returns a pointer where `len` bytes can be read, and advances the internal
/// state.  The stream must be byte aligned.
static inline const u8 *IO_get_read_ptr(istream_t *const in, size_t len) {
    if (len > in->len) {
        INP_SIZE();
    }
    if (in->bit_offset != 0) {
        UNALIGNED();
    }
    const u8 *const ptr = in->ptr;
    in->ptr += len;
    in->len -= len;

    return ptr;
}
/// Returns a pointer to write `len` bytes to, and advances the internal state
static inline u8 *IO_get_write_ptr(ostream_t *const out, size_t len) {
    if (len > out->len) {
        OUT_SIZE();
    }
    u8 *const ptr = out->ptr;
    out->ptr += len;
    out->len -= len;

    return ptr;
}

/// Advance the inner state by `len` bytes
static inline void IO_advance_input(istream_t *const in, size_t len) {
    if (len > in->len) {
         INP_SIZE();
    }
    if (in->bit_offset != 0) {
        UNALIGNED();
    }

    in->ptr += len;
    in->len -= len;
}

/// Returns an `ostream_t` constructed from the given pointer and length
static inline ostream_t IO_make_ostream(u8 *out, size_t len) {
    return (ostream_t) { out, len };
}

/// Returns an `istream_t` constructed from the given pointer and length
static inline istream_t IO_make_istream(const u8 *in, size_t len) {
    return (istream_t) { in, len, 0 };
}

/// Returns an `istream_t` with the same base as `in`, and length `len`
/// Then, advance `in` to account for the consumed bytes
/// `in` must be byte aligned
static inline istream_t IO_make_sub_istream(istream_t *const in, size_t len) {
    // Consume `len` bytes of the parent stream
    const u8 *const ptr = IO_get_read_ptr(in, len);

    // Make a substream using the pointer to those `len` bytes
    return IO_make_istream(ptr, len);
}
/******* END IO STREAM OPERATIONS *********************************************/

/******* BITSTREAM OPERATIONS *************************************************/
/// Read `num` bits (up to 64) from `src + offset`, where `offset` is in bits
static inline u64 read_bits_LE(const u8 *src, const int num_bits,
                               const size_t offset) {
    if (num_bits > 64) {
        ERROR("Attempt to read an invalid number of bits");
    }

    // Skip over bytes that aren't in range
    src += offset / 8;
    size_t bit_offset = offset % 8;
    u64 res = 0;

    int shift = 0;
    int left = num_bits;
    while (left > 0) {
        u64 mask = left >= 8 ? 0xff : (((u64)1 << left) - 1);
        // Read the next byte, shift it to account for the offset, and then mask
        // out the top part if we don't need all the bits
        res += (((u64)*src++ >> bit_offset) & mask) << shift;
        shift += 8 - bit_offset;
        left -= 8 - bit_offset;
        bit_offset = 0;
    }

    return res;
}

/// Read bits from the end of a HUF or FSE bitstream.  `offset` is in bits, so
/// it updates `offset` to `offset - bits`, and then reads `bits` bits from
/// `src + offset`.  If the offset becomes negative, the extra bits at the
/// bottom are filled in with `0` bits instead of reading from before `src`.
static inline u64 STREAM_read_bits(const u8 *const src, const int bits,
                                   i64 *const offset) {
    *offset = *offset - bits;
    size_t actual_off = *offset;
    size_t actual_bits = bits;
    // Don't actually read bits from before the start of src, so if `*offset <
    // 0` fix actual_off and actual_bits to reflect the quantity to read
    if (*offset < 0) {
        actual_bits += *offset;
        actual_off = 0;
    }
    u64 res = read_bits_LE(src, actual_bits, actual_off);

    if (*offset < 0) {
        // Fill in the bottom "overflowed" bits with 0's
        res = -*offset >= 64 ? 0 : (res << -*offset);
    }
    return res;
}
/******* END BITSTREAM OPERATIONS *********************************************/

/******* BIT COUNTING OPERATIONS **********************************************/
/// Returns `x`, where `2^x` is the largest power of 2 less than or equal to
/// `num`, or `-1` if `num == 0`.
static inline int highest_set_bit(const u64 num) {
    for (int i = 63; i >= 0; i--) {
        if (((u64)1 << i) <= num) {
            return i;
        }
    }
    return -1;
}
/******* END BIT COUNTING OPERATIONS ******************************************/

/******* HUFFMAN PRIMITIVES ***************************************************/
static inline u8 HUF_decode_symbol(const HUF_dtable *const dtable,
                                   u16 *const state, const u8 *const src,
                                   i64 *const offset) {
    // Look up the symbol and number of bits to read
    const u8 symb = dtable->symbols[*state];
    const u8 bits = dtable->num_bits[*state];
    const u16 rest = STREAM_read_bits(src, bits, offset);
    // Shift `bits` bits out of the state, keeping the low order bits that
    // weren't necessary to determine this symbol.  Then add in the new bits
    // read from the stream.
    *state = ((*state << bits) + rest) & (((u16)1 << dtable->max_bits) - 1);

    return symb;
}

static inline void HUF_init_state(const HUF_dtable *const dtable,
                                  u16 *const state, const u8 *const src,
                                  i64 *const offset) {
    // Read in a full `dtable->max_bits` bits to initialize the state
    const u8 bits = dtable->max_bits;
    *state = STREAM_read_bits(src, bits, offset);
}

static size_t HUF_decompress_1stream(const HUF_dtable *const dtable,
                                     ostream_t *const out,
                                     istream_t *const in) {
    const size_t len = IO_istream_len(in);
    if (len == 0) {
        INP_SIZE();
    }
    const u8 *const src = IO_get_read_ptr(in, len);

    // "Each bitstream must be read backward, that is starting from the end down
    // to the beginning. Therefore it's necessary to know the size of each
    // bitstream.
    //
    // It's also necessary to know exactly which bit is the latest. This is
    // detected by a final bit flag : the highest bit of latest byte is a
    // final-bit-flag. Consequently, a last byte of 0 is not possible. And the
    // final-bit-flag itself is not part of the useful bitstream. Hence, the
    // last byte contains between 0 and 7 useful bits."
    const int padding = 8 - highest_set_bit(src[len - 1]);

    // Offset starts at the end because HUF streams are read backwards
    i64 bit_offset = len * 8 - padding;
    u16 state;

    HUF_init_state(dtable, &state, src, &bit_offset);

    size_t symbols_written = 0;
    while (bit_offset > -dtable->max_bits) {
        // Iterate over the stream, decoding one symbol at a time
        IO_write_byte(out, HUF_decode_symbol(dtable, &state, src, &bit_offset));
        symbols_written++;
    }
    // "The process continues up to reading the required number of symbols per
    // stream. If a bitstream is not entirely and exactly consumed, hence
    // reaching exactly its beginning position with all bits consumed, the
    // decoding process is considered faulty."

    // When all symbols have been decoded, the final state value shouldn't have
    // any data from the stream, so it should have "read" dtable->max_bits from
    // before the start of `src`
    // Therefore `offset`, the edge to start reading new bits at, should be
    // dtable->max_bits before the start of the stream
    if (bit_offset != -dtable->max_bits) {
        CORRUPTION();
    }

    return symbols_written;
}

static size_t HUF_decompress_4stream(const HUF_dtable *const dtable,
                                     ostream_t *const out, istream_t *const in) {
    // "Compressed size is provided explicitly : in the 4-streams variant,
    // bitstreams are preceded by 3 unsigned little-endian 16-bits values. Each
    // value represents the compressed size of one stream, in order. The last
    // stream size is deducted from total compressed size and from previously
    // decoded stream sizes"
    const size_t csize1 = IO_read_bits(in, 16);
    const size_t csize2 = IO_read_bits(in, 16);
    const size_t csize3 = IO_read_bits(in, 16);

    istream_t in1 = IO_make_sub_istream(in, csize1);
    istream_t in2 = IO_make_sub_istream(in, csize2);
    istream_t in3 = IO_make_sub_istream(in, csize3);
    istream_t in4 = IO_make_sub_istream(in, IO_istream_len(in));

    size_t total_output = 0;
    // Decode each stream independently for simplicity
    // If we wanted to we could decode all 4 at the same time for speed,
    // utilizing more execution units
    total_output += HUF_decompress_1stream(dtable, out, &in1);
    total_output += HUF_decompress_1stream(dtable, out, &in2);
    total_output += HUF_decompress_1stream(dtable, out, &in3);
    total_output += HUF_decompress_1stream(dtable, out, &in4);

    return total_output;
}

/// Initializes a Huffman table using canonical Huffman codes
/// For more explanation on canonical Huffman codes see
/// http://www.cs.uofs.edu/~mccloske/courses/cmps340/huff_canonical_dec2015.html
/// Codes within a level are allocated in symbol order (i.e. smaller symbols get
/// earlier codes)
static void HUF_init_dtable(HUF_dtable *const table, const u8 *const bits,
                            const int num_symbs) {
    memset(table, 0, sizeof(HUF_dtable));
    if (num_symbs > HUF_MAX_SYMBS) {
        ERROR("Too many symbols for Huffman");
    }

    u8 max_bits = 0;
    u16 rank_count[HUF_MAX_BITS + 1];
    memset(rank_count, 0, sizeof(rank_count));

    // Count the number of symbols for each number of bits, and determine the
    // depth of the tree
    for (int i = 0; i < num_symbs; i++) {
        if (bits[i] > HUF_MAX_BITS) {
            ERROR("Huffman table depth too large");
        }
        max_bits = MAX(max_bits, bits[i]);
        rank_count[bits[i]]++;
    }

    const size_t table_size = 1 << max_bits;
    table->max_bits = max_bits;
    table->symbols = malloc(table_size);
    table->num_bits = malloc(table_size);

    if (!table->symbols || !table->num_bits) {
        free(table->symbols);
        free(table->num_bits);
        BAD_ALLOC();
    }

    // "Symbols are sorted by Weight. Within same Weight, symbols keep natural
    // order. Symbols with a Weight of zero are removed. Then, starting from
    // lowest weight, prefix codes are distributed in order."

    u32 rank_idx[HUF_MAX_BITS + 1];
    // Initialize the starting codes for each rank (number of bits)
    rank_idx[max_bits] = 0;
    for (int i = max_bits; i >= 1; i--) {
        rank_idx[i - 1] = rank_idx[i] + rank_count[i] * (1 << (max_bits - i));
        // The entire range takes the same number of bits so we can memset it
        memset(&table->num_bits[rank_idx[i]], i, rank_idx[i - 1] - rank_idx[i]);
    }

    if (rank_idx[0] != table_size) {
        CORRUPTION();
    }

    // Allocate codes and fill in the table
    for (int i = 0; i < num_symbs; i++) {
        if (bits[i] != 0) {
            // Allocate a code for this symbol and set its range in the table
            const u16 code = rank_idx[bits[i]];
            // Since the code doesn't care about the bottom `max_bits - bits[i]`
            // bits of state, it gets a range that spans all possible values of
            // the lower bits
            const u16 len = 1 << (max_bits - bits[i]);
            memset(&table->symbols[code], i, len);
            rank_idx[bits[i]] += len;
        }
    }
}

static void HUF_init_dtable_usingweights(HUF_dtable *const table,
                                         const u8 *const weights,
                                         const int num_symbs) {
    // +1 because the last weight is not transmitted in the header
    if (num_symbs + 1 > HUF_MAX_SYMBS) {
        ERROR("Too many symbols for Huffman");
    }

    u8 bits[HUF_MAX_SYMBS];

    u64 weight_sum = 0;
    for (int i = 0; i < num_symbs; i++) {
        // Weights are in the same range as bit count
        if (weights[i] > HUF_MAX_BITS) {
            CORRUPTION();
        }
        weight_sum += weights[i] > 0 ? (u64)1 << (weights[i] - 1) : 0;
    }

    // Find the first power of 2 larger than the sum
    const int max_bits = highest_set_bit(weight_sum) + 1;
    const u64 left_over = ((u64)1 << max_bits) - weight_sum;
    // If the left over isn't a power of 2, the weights are invalid
    if (left_over & (left_over - 1)) {
        CORRUPTION();
    }

    // left_over is used to find the last weight as it's not transmitted
    // by inverting 2^(weight - 1) we can determine the value of last_weight
    const int last_weight = highest_set_bit(left_over) + 1;

    for (int i = 0; i < num_symbs; i++) {
        // "Number_of_Bits = Number_of_Bits ? Max_Number_of_Bits + 1 - Weight : 0"
        bits[i] = weights[i] > 0 ? (max_bits + 1 - weights[i]) : 0;
    }
    bits[num_symbs] =
        max_bits + 1 - last_weight; // Last weight is always non-zero

    HUF_init_dtable(table, bits, num_symbs + 1);
}

static void HUF_free_dtable(HUF_dtable *const dtable) {
    free(dtable->symbols);
    free(dtable->num_bits);
    memset(dtable, 0, sizeof(HUF_dtable));
}

static void HUF_copy_dtable(HUF_dtable *const dst,
                            const HUF_dtable *const src) {
    if (src->max_bits == 0) {
        memset(dst, 0, sizeof(HUF_dtable));
        return;
    }

    const size_t size = (size_t)1 << src->max_bits;
    dst->max_bits = src->max_bits;

    dst->symbols = malloc(size);
    dst->num_bits = malloc(size);
    if (!dst->symbols || !dst->num_bits) {
        BAD_ALLOC();
    }

    memcpy(dst->symbols, src->symbols, size);
    memcpy(dst->num_bits, src->num_bits, size);
}
/******* END HUFFMAN PRIMITIVES ***********************************************/

/******* FSE PRIMITIVES *******************************************************/
/// For more description of FSE see
/// https://github.com/Cyan4973/FiniteStateEntropy/

/// Allow a symbol to be decoded without updating state
static inline u8 FSE_peek_symbol(const FSE_dtable *const dtable,
                                 const u16 state) {
    return dtable->symbols[state];
}

/// Consumes bits from the input and uses the current state to determine the
/// next state
static inline void FSE_update_state(const FSE_dtable *const dtable,
                                    u16 *const state, const u8 *const src,
                                    i64 *const offset) {
    const u8 bits = dtable->num_bits[*state];
    const u16 rest = STREAM_read_bits(src, bits, offset);
    *state = dtable->new_state_base[*state] + rest;
}

/// Decodes a single FSE symbol and updates the offset
static inline u8 FSE_decode_symbol(const FSE_dtable *const dtable,
                                   u16 *const state, const u8 *const src,
                                   i64 *const offset) {
    const u8 symb = FSE_peek_symbol(dtable, *state);
    FSE_update_state(dtable, state, src, offset);
    return symb;
}

static inline void FSE_init_state(const FSE_dtable *const dtable,
                                  u16 *const state, const u8 *const src,
                                  i64 *const offset) {
    // Read in a full `accuracy_log` bits to initialize the state
    const u8 bits = dtable->accuracy_log;
    *state = STREAM_read_bits(src, bits, offset);
}

static size_t FSE_decompress_interleaved2(const FSE_dtable *const dtable,
                                          ostream_t *const out,
                                          istream_t *const in) {
    const size_t len = IO_istream_len(in);
    if (len == 0) {
        INP_SIZE();
    }
    const u8 *const src = IO_get_read_ptr(in, len);

    // "Each bitstream must be read backward, that is starting from the end down
    // to the beginning. Therefore it's necessary to know the size of each
    // bitstream.
    //
    // It's also necessary to know exactly which bit is the latest. This is
    // detected by a final bit flag : the highest bit of latest byte is a
    // final-bit-flag. Consequently, a last byte of 0 is not possible. And the
    // final-bit-flag itself is not part of the useful bitstream. Hence, the
    // last byte contains between 0 and 7 useful bits."
    const int padding = 8 - highest_set_bit(src[len - 1]);
    i64 offset = len * 8 - padding;

    u16 state1, state2;
    // "The first state (State1) encodes the even indexed symbols, and the
    // second (State2) encodes the odd indexes. State1 is initialized first, and
    // then State2, and they take turns decoding a single symbol and updating
    // their state."
    FSE_init_state(dtable, &state1, src, &offset);
    FSE_init_state(dtable, &state2, src, &offset);

    // Decode until we overflow the stream
    // Since we decode in reverse order, overflowing the stream is offset going
    // negative
    size_t symbols_written = 0;
    while (1) {
        // "The number of symbols to decode is determined by tracking bitStream
        // overflow condition: If updating state after decoding a symbol would
        // require more bits than remain in the stream, it is assumed the extra
        // bits are 0. Then, the symbols for each of the final states are
        // decoded and the process is complete."
        IO_write_byte(out, FSE_decode_symbol(dtable, &state1, src, &offset));
        symbols_written++;
        if (offset < 0) {
            // There's still a symbol to decode in state2
            IO_write_byte(out, FSE_peek_symbol(dtable, state2));
            symbols_written++;
            break;
        }

        IO_write_byte(out, FSE_decode_symbol(dtable, &state2, src, &offset));
        symbols_written++;
        if (offset < 0) {
            // There's still a symbol to decode in state1
            IO_write_byte(out, FSE_peek_symbol(dtable, state1));
            symbols_written++;
            break;
        }
    }

    return symbols_written;
}

static void FSE_init_dtable(FSE_dtable *const dtable,
                            const i16 *const norm_freqs, const int num_symbs,
                            const int accuracy_log) {
    if (accuracy_log > FSE_MAX_ACCURACY_LOG) {
        ERROR("FSE accuracy too large");
    }
    if (num_symbs > FSE_MAX_SYMBS) {
        ERROR("Too many symbols for FSE");
    }

    dtable->accuracy_log = accuracy_log;

    const size_t size = (size_t)1 << accuracy_log;
    dtable->symbols = malloc(size * sizeof(u8));
    dtable->num_bits = malloc(size * sizeof(u8));
    dtable->new_state_base = malloc(size * sizeof(u16));

    if (!dtable->symbols || !dtable->num_bits || !dtable->new_state_base) {
        BAD_ALLOC();
    }

    // Used to determine how many bits need to be read for each state,
    // and where the destination range should start
    // Needs to be u16 because max value is 2 * max number of symbols,
    // which can be larger than a byte can store
    u16 state_desc[FSE_MAX_SYMBS];

    // "Symbols are scanned in their natural order for "less than 1"
    // probabilities. Symbols with this probability are being attributed a
    // single cell, starting from the end of the table. These symbols define a
    // full state reset, reading Accuracy_Log bits."
    int high_threshold = size;
    for (int s = 0; s < num_symbs; s++) {
        // Scan for low probability symbols to put at the top
        if (norm_freqs[s] == -1) {
            dtable->symbols[--high_threshold] = s;
            state_desc[s] = 1;
        }
    }

    // "All remaining symbols are sorted in their natural order. Starting from
    // symbol 0 and table position 0, each symbol gets attributed as many cells
    // as its probability. Cell allocation is spreaded, not linear."
    // Place the rest in the table
    const u16 step = (size >> 1) + (size >> 3) + 3;
    const u16 mask = size - 1;
    u16 pos = 0;
    for (int s = 0; s < num_symbs; s++) {
        if (norm_freqs[s] <= 0) {
            continue;
        }

        state_desc[s] = norm_freqs[s];

        for (int i = 0; i < norm_freqs[s]; i++) {
            // Give `norm_freqs[s]` states to symbol s
            dtable->symbols[pos] = s;
            // "A position is skipped if already occupied, typically by a "less
            // than 1" probability symbol."
            do {
                pos = (pos + step) & mask;
            } while (pos >=
                     high_threshold);
            // Note: no other collision checking is necessary as `step` is
            // coprime to `size`, so the cycle will visit each position exactly
            // once
        }
    }
    if (pos != 0) {
        CORRUPTION();
    }

    // Now we can fill baseline and num bits
    for (size_t i = 0; i < size; i++) {
        u8 symbol = dtable->symbols[i];
        u16 next_state_desc = state_desc[symbol]++;
        // Fills in the table appropriately, next_state_desc increases by symbol
        // over time, decreasing number of bits
        dtable->num_bits[i] = (u8)(accuracy_log - highest_set_bit(next_state_desc));
        // Baseline increases until the bit threshold is passed, at which point
        // it resets to 0
        dtable->new_state_base[i] =
            ((u16)next_state_desc << dtable->num_bits[i]) - size;
    }
}

/// Decode an FSE header as defined in the Zstandard format specification and
/// use the decoded frequencies to initialize a decoding table.
static void FSE_decode_header(FSE_dtable *const dtable, istream_t *const in,
                                const int max_accuracy_log) {
    // "An FSE distribution table describes the probabilities of all symbols
    // from 0 to the last present one (included) on a normalized scale of 1 <<
    // Accuracy_Log .
    //
    // It's a bitstream which is read forward, in little-endian fashion. It's
    // not necessary to know its exact size, since it will be discovered and
    // reported by the decoding process.
    if (max_accuracy_log > FSE_MAX_ACCURACY_LOG) {
        ERROR("FSE accuracy too large");
    }

    // The bitstream starts by reporting on which scale it operates.
    // Accuracy_Log = low4bits + 5. Note that maximum Accuracy_Log for literal
    // and match lengths is 9, and for offsets is 8. Higher values are
    // considered errors."
    const int accuracy_log = 5 + IO_read_bits(in, 4);
    if (accuracy_log > max_accuracy_log) {
        ERROR("FSE accuracy too large");
    }

    // "Then follows each symbol value, from 0 to last present one. The number
    // of bits used by each field is variable. It depends on :
    //
    // Remaining probabilities + 1 : example : Presuming an Accuracy_Log of 8,
    // and presuming 100 probabilities points have already been distributed, the
    // decoder may read any value from 0 to 255 - 100 + 1 == 156 (inclusive).
    // Therefore, it must read log2sup(156) == 8 bits.
    //
    // Value decoded : small values use 1 less bit : example : Presuming values
    // from 0 to 156 (inclusive) are possible, 255-156 = 99 values are remaining
    // in an 8-bits field. They are used this way : first 99 values (hence from
    // 0 to 98) use only 7 bits, values from 99 to 156 use 8 bits. "

    i32 remaining = 1 << accuracy_log;
    i16 frequencies[FSE_MAX_SYMBS];

    int symb = 0;
    while (remaining > 0 && symb < FSE_MAX_SYMBS) {
        // Log of the number of possible values we could read
        int bits = highest_set_bit(remaining + 1) + 1;

        u16 val = IO_read_bits(in, bits);

        // Try to mask out the lower bits to see if it qualifies for the "small
        // value" threshold
        const u16 lower_mask = ((u16)1 << (bits - 1)) - 1;
        const u16 threshold = ((u16)1 << bits) - 1 - (remaining + 1);

        if ((val & lower_mask) < threshold) {
            IO_rewind_bits(in, 1);
            val = val & lower_mask;
        } else if (val > lower_mask) {
            val = val - threshold;
        }

        // "Probability is obtained from Value decoded by following formula :
        // Proba = value - 1"
        const i16 proba = (i16)val - 1;

        // "It means value 0 becomes negative probability -1. -1 is a special
        // probability, which means "less than 1". Its effect on distribution
        // table is described in next paragraph. For the purpose of calculating
        // cumulated distribution, it counts as one."
        remaining -= proba < 0 ? -proba : proba;

        frequencies[symb] = proba;
        symb++;

        // "When a symbol has a probability of zero, it is followed by a 2-bits
        // repeat flag. This repeat flag tells how many probabilities of zeroes
        // follow the current one. It provides a number ranging from 0 to 3. If
        // it is a 3, another 2-bits repeat flag follows, and so on."
        if (proba == 0) {
            // Read the next two bits to see how many more 0s
            int repeat = IO_read_bits(in, 2);

            while (1) {
                for (int i = 0; i < repeat && symb < FSE_MAX_SYMBS; i++) {
                    frequencies[symb++] = 0;
                }
                if (repeat == 3) {
                    repeat = IO_read_bits(in, 2);
                } else {
                    break;
                }
            }
        }
    }
    IO_align_stream(in);

    // "When last symbol reaches cumulated total of 1 << Accuracy_Log, decoding
    // is complete. If the last symbol makes cumulated total go above 1 <<
    // Accuracy_Log, distribution is considered corrupted."
    if (remaining != 0 || symb >= FSE_MAX_SYMBS) {
        CORRUPTION();
    }

    // Initialize the decoding table using the determined weights
    FSE_init_dtable(dtable, frequencies, symb, accuracy_log);
}

static void FSE_init_dtable_rle(FSE_dtable *const dtable, const u8 symb) {
    dtable->symbols = malloc(sizeof(u8));
    dtable->num_bits = malloc(sizeof(u8));
    dtable->new_state_base = malloc(sizeof(u16));

    if (!dtable->symbols || !dtable->num_bits || !dtable->new_state_base) {
        BAD_ALLOC();
    }

    // This setup will always have a state of 0, always return symbol `symb`,
    // and never consume any bits
    dtable->symbols[0] = symb;
    dtable->num_bits[0] = 0;
    dtable->new_state_base[0] = 0;
    dtable->accuracy_log = 0;
}

static void FSE_free_dtable(FSE_dtable *const dtable) {
    free(dtable->symbols);
    free(dtable->num_bits);
    free(dtable->new_state_base);
    memset(dtable, 0, sizeof(FSE_dtable));
}

static void FSE_copy_dtable(FSE_dtable *const dst, const FSE_dtable *const src) {
    if (src->accuracy_log == 0) {
        memset(dst, 0, sizeof(FSE_dtable));
        return;
    }

    size_t size = (size_t)1 << src->accuracy_log;
    dst->accuracy_log = src->accuracy_log;

    dst->symbols = malloc(size);
    dst->num_bits = malloc(size);
    dst->new_state_base = malloc(size * sizeof(u16));
    if (!dst->symbols || !dst->num_bits || !dst->new_state_base) {
        BAD_ALLOC();
    }

    memcpy(dst->symbols, src->symbols, size);
    memcpy(dst->num_bits, src->num_bits, size);
    memcpy(dst->new_state_base, src->new_state_base, size * sizeof(u16));
}
/******* END FSE PRIMITIVES ***************************************************/
