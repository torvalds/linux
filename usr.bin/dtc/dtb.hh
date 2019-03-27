/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 David Chisnall
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DTB_HH_
#define _DTB_HH_
#include <map>
#include <string>

#include <assert.h>

#include "input_buffer.hh"
#include "util.hh"

namespace dtc
{
/**
 * The dtb namespace contains code related to the generation of device tree
 * blobs, the binary representation of flattened device trees.  The abstract
 * tree representation calls into this code to generate the output.
 */
namespace dtb
{
/** The token types in the DTB, as defined by §7.4.1 of the ePAPR
 * specification.  All of these values are written in big-endian format in the
 * output.
 */
enum token_type
{
	/**
	 * Marker indicating the start of a node in the tree.  This is followed
	 * by the nul-terminated name.  If a unit address is specified, then
	 * the name also contains the address, with an @ symbol between the end
	 * of the name and the start of the address.
	 *
	 * The name is then padded such that the next token begins on a 4-byte
	 * boundary.  The node may contain properties, other nodes, both, or be
	 * empty.
	 */
	FDT_BEGIN_NODE = 0x00000001,
	/**
	 * Marker indicating the end of a node.  
	 */
	FDT_END_NODE   = 0x00000002,
	/**
	 * The start of a property.  This is followed by two 32-bit big-endian
	 * values.  The first indicates the length of the property value, the
	 * second its index in the strings table.  It is then followed by the
	 * property value, if the value is of non-zero length.
	 */
	FDT_PROP       = 0x00000003,
	/**
	 * Ignored token.  May be used for padding inside DTB nodes.
	 */
	FDT_NOP        = 0x00000004,
	/**
	 * Marker indicating the end of the tree.
	 */
	FDT_END        = 0x00000009
};

/**
 * Returns the token as a string.  This is used for debugging and for printing
 * human-friendly error messages about malformed DTB input.
 */
inline const char *token_type_name(token_type t)
{
	switch(t)
	{
		case FDT_BEGIN_NODE:
			return "FDT_BEGIN_NODE";
		case FDT_END_NODE:
			return "FDT_END_NODE";
		case FDT_PROP:
			return "FDT_PROP";
		case FDT_NOP:
			return "FDT_NOP";
		case FDT_END:
			return "FDT_END";
	}
	assert(0);
}

/**
 * Abstract class for writing a section of the output.  We create one
 * of these for each section that needs to be written.  It is intended to build
 * a temporary buffer of the output in memory and then write it to a file
 * stream.  The size can be returned after all of the data has been written
 * into the internal buffer, so the sizes of the three tables can be calculated
 * before storing them in the buffer.
 */
struct output_writer
{
	/**
	 * Writes a label into the output stream.  This is only applicable for
	 * assembly output, where the labels become symbols that can be
	 * resolved at link time.
	 */
	virtual void write_label(const std::string &name)   = 0;
	/**
	 * Writes a comment into the output stream.  Useful only when debugging
	 * the output.
	 */
	virtual void write_comment(const std::string &name) = 0;
	/**
	 * Writes a string.  A nul terminator is implicitly added.
	 */
	virtual void write_string(const std::string &name)  = 0;
	/**
	 * Writes a single 8-bit value.
	 */
	virtual void write_data(uint8_t)        = 0;
	/**
	 * Writes a single 32-bit value.  The value is written in big-endian
	 * format, but should be passed in the host's native endian.
	 */
	virtual void write_data(uint32_t)       = 0;
	/**
	 * Writes a single 64-bit value.  The value is written in big-endian
	 * format, but should be passed in the host's native endian.
	 */
	virtual void write_data(uint64_t)       = 0;
	/**
	 * Writes the collected output to the specified file descriptor.
	 */
	virtual void write_to_file(int fd)      = 0;
	/**
	 * Returns the number of bytes.
	 */
	virtual uint32_t size()                 = 0;
	/**
	 * Helper for writing tokens to the output stream.  This writes a
	 * comment above the token describing its value, for easier debugging
	 * of the output.
	 */
	inline void write_token(token_type t)
	{
		write_comment(token_type_name(t));
		write_data((uint32_t)t);
	}
	/**
	 * Helper function that writes a byte buffer to the output, one byte at
	 * a time.
	 */
	void write_data(byte_buffer b);
};

/**
 * Binary file writer.  This class is responsible for writing the DTB output
 * directly in blob format.
 */
class binary_writer : public output_writer
{
	/**
	 * The internal buffer used to store the blob while it is being
	 * constructed.
	 */
	byte_buffer buffer;
	public:
	/**
	 *  The binary format does not support labels, so this method
	 * does nothing.
	 */
	void write_label(const std::string &) override {}
	/**
	 * Comments are ignored by the binary writer.
	 */
	void write_comment(const std::string&)  override {}
	void write_string(const std::string &name) override;
	void write_data(uint8_t v) override;
	void write_data(uint32_t v) override;
	void write_data(uint64_t v) override;
	void write_to_file(int fd) override;
	uint32_t size() override;
};
/**
 * Assembly writer.  This class is responsible for writing the output in an
 * assembly format that is suitable for linking into a kernel, loader, and so
 * on.
 */
class asm_writer : public output_writer
{
	/**
	 * The internal buffer for temporary values.  Note that this actually
	 * contains ASCII text, but it is a byte buffer so that we can just
	 * copy strings across as-is.
	 */
	byte_buffer buffer;
	/**
	 * The number of bytes written to the current line.  This is used to
	 * allow line wrapping, where we aim to write four .byte directives to
	 * make the alignment clearer.
	 */
	int byte_count;
	/**
	 * The current number of bytes written.  This is the number in binary
	 * format, not the number of bytes in the buffer.
	 */
	uint32_t bytes_written;

	/**
	 * Writes a string directly to the output as-is.  This is the function that
	 * performs the real output.
	 */
	void write_string(const char *c);
	/**
	 * Write a string to the output.
	 */
	void write_string(const std::string &c) override;
	/**
	 * Writes the string, starting on a new line.  
	 */
	void write_line(const char *c);
	/**
	 * Writes a byte in binary format.  This will emit a single .byte
	 * directive, with up to four per line.
	 */
	void write_byte(uint8_t b);
	public:
	asm_writer() : byte_count(0), bytes_written(0) {}
	void write_label(const std::string &name) override;
	void write_comment(const std::string &name) override;
	void write_data(uint8_t v) override;
	void write_data(uint32_t v) override;
	void write_data(uint64_t v) override;
	void write_to_file(int fd) override;
	uint32_t size() override;
};

/**
 * Class encapsulating the device tree blob header.  This class stores all of
 * the values found in the header and is responsible for writing them to the
 * output.
 */
struct header
{
	/**
	 * Magic value, used to validate that this really is a device tree
	 * blob.  Should always be set to 0xd00dfeed.
	 */
	uint32_t magic;
	/**
	 * The total size of the blob, including header, reservations, strings
	 * table, and padding.
	 */
	uint32_t totalsize;
	/**
	 * The offset from the start of the blob of the struct table (i.e. the
	 * part of the blob containing the entire device tree).
	 */
	uint32_t off_dt_struct;
	/**
	 * The offset from the start of the blob of the strings table.  
	 */
	uint32_t off_dt_strings;
	/**
	 * The offset of the reservation map from the start of the blob.
	 */
	uint32_t off_mem_rsvmap;
	/**
	 * The version of the blob.  This should always be 17.
	 */
	uint32_t version;
	/**
	 * The earliest version of the DTB specification with which this blob
	 * is backwards compatible.  This should always be 16.
	 */
	uint32_t last_comp_version;
	/**
	 * The ID of the CPU where this boots.
	 */
	uint32_t boot_cpuid_phys;
	/**
	 * The size of the strings table.
	 */
	uint32_t size_dt_strings;
	/**
	 * The size of the struct table.
	 */
	uint32_t size_dt_struct;
	/**
	 * Writes the entire header to the specified output buffer.  
	 */
	void write(output_writer &out);
	/**
	 * Reads the header from bits binary representation in a blob.
	 */
	bool read_dtb(input_buffer &input);
	/**
	 * Default constructor.  Initialises the values that have sensible
	 * defaults, leaves the others blank.
	 */
	header() : magic(0xd00dfeed), version(17), last_comp_version(16),
		boot_cpuid_phys(0) {}
};

/**
 * Class encapsulating the string table.  FDT strings are stored in a string
 * section.  This maintains a map from strings to their offsets in the strings
 * section.
 *
 * Note: We don't currently do suffix matching, which may save a small amount
 * of space.
 */
class string_table {
	/**
	 * Map from strings to their offset. 
	 */
	std::map<std::string, uint32_t> string_offsets;
	/**
	 * The strings, in the order in which they should be written to the
	 * output.  The order must be stable - adding another string must not
	 * change the offset of any that we have already referenced - and so we
	 * simply write the strings in the order that they are passed.
	 */
	std::vector<std::string> strings;
	/**
	 * The current size of the strings section.
	 */
	uint32_t size;
	public:
	/**
	 * Default constructor, creates an empty strings table.
	 */
	string_table() : size(0) {}
	/**
	 * Adds a string to the table, returning the offset from the start
	 * where it will be written.  If the string is already present, this
	 * will return its existing offset, otherwise it will return a new
	 * offset.
	 */
	uint32_t add_string(const std::string &str);
	/**
	 * Writes the strings table to the specified output.
	 */
	void write(dtb::output_writer &writer);
};

} // namespace dtb

} // namespace dtc

#endif // !_DTB_HH_
