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

#include "dtb.hh"
#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>

using std::string;

namespace dtc
{
namespace dtb
{

void output_writer::write_data(byte_buffer b)
{
	for (auto i : b)
	{
		write_data(i);
	}
}

void
binary_writer::write_string(const string &name)
{
	push_string(buffer, name);
	// Trailing nul
	buffer.push_back(0);
}

void
binary_writer::write_data(uint8_t v)
{
	buffer.push_back(v);
}

void
binary_writer::write_data(uint32_t v)
{
	while (buffer.size() % 4 != 0)
	{
		buffer.push_back(0);
	}
	push_big_endian(buffer, v);
}

void
binary_writer::write_data(uint64_t v)
{
	while (buffer.size() % 8 != 0)
	{
		buffer.push_back(0);
	}
	push_big_endian(buffer, v);
}

void
binary_writer::write_to_file(int fd)
{
	// FIXME: Check return
	write(fd, buffer.data(), buffer.size());
}

uint32_t
binary_writer::size()
{
	return buffer.size();
}

void
asm_writer::write_line(const char *c)
{
	if (byte_count != 0)
	{
		byte_count = 0;
		buffer.push_back('\n');
	}
	write_string(c);
}

void
asm_writer::write_byte(uint8_t b)
{
	char out[3] = {0};
	if (byte_count++ == 0)
	{
		buffer.push_back('\t');
	}
	write_string(".byte 0x");
	snprintf(out, 3, "%.2hhx", b);
	buffer.push_back(out[0]);
	buffer.push_back(out[1]);
	if (byte_count == 4)
	{
		buffer.push_back('\n');
		byte_count = 0;
	}
	else
	{
		buffer.push_back(';');
		buffer.push_back(' ');
	}
}

void
asm_writer::write_label(const string &name)
{
	write_line("\t.globl ");
	push_string(buffer, name);
	buffer.push_back('\n');
	push_string(buffer, name);
	buffer.push_back(':');
	buffer.push_back('\n');
	buffer.push_back('_');
	push_string(buffer, name);
	buffer.push_back(':');
	buffer.push_back('\n');
	
}

void
asm_writer::write_comment(const string &name)
{
	write_line("\t/* ");
	push_string(buffer, name);
	write_string(" */\n");
}

void
asm_writer::write_string(const char *c)
{
	while (*c)
	{
		buffer.push_back((uint8_t)*(c++));
	}
}


void
asm_writer::write_string(const string &name)
{
	write_line("\t.string \"");
	push_string(buffer, name);
	write_line("\"\n");
	bytes_written += name.size() + 1;
}

void
asm_writer::write_data(uint8_t v)
{
	write_byte(v);
	bytes_written++;
}

void
asm_writer::write_data(uint32_t v)
{
	if (bytes_written % 4 != 0)
	{
		write_line("\t.balign 4\n");
		bytes_written += (4 - (bytes_written % 4));
	}
	write_byte((v >> 24) & 0xff);
	write_byte((v >> 16) & 0xff);
	write_byte((v >> 8) & 0xff);
	write_byte((v >> 0) & 0xff);
	bytes_written += 4;
}

void
asm_writer::write_data(uint64_t v)
{
	if (bytes_written % 8 != 0)
	{
		write_line("\t.balign 8\n");
		bytes_written += (8 - (bytes_written % 8));
	}
	write_byte((v >> 56) & 0xff);
	write_byte((v >> 48) & 0xff);
	write_byte((v >> 40) & 0xff);
	write_byte((v >> 32) & 0xff);
	write_byte((v >> 24) & 0xff);
	write_byte((v >> 16) & 0xff);
	write_byte((v >> 8) & 0xff);
	write_byte((v >> 0) & 0xff);
	bytes_written += 8;
}

void
asm_writer::write_to_file(int fd)
{
	// FIXME: Check return
	write(fd, buffer.data(), buffer.size());
}

uint32_t
asm_writer::size()
{
	return bytes_written;
}

void
header::write(output_writer &out)
{
	out.write_label("dt_blob_start");
	out.write_label("dt_header");
	out.write_comment("magic");
	out.write_data(magic);
	out.write_comment("totalsize");
	out.write_data(totalsize);
	out.write_comment("off_dt_struct");
	out.write_data(off_dt_struct);
	out.write_comment("off_dt_strings");
	out.write_data(off_dt_strings);
	out.write_comment("off_mem_rsvmap");
	out.write_data(off_mem_rsvmap);
	out.write_comment("version");
	out.write_data(version);
	out.write_comment("last_comp_version");
	out.write_data(last_comp_version);
	out.write_comment("boot_cpuid_phys");
	out.write_data(boot_cpuid_phys);
	out.write_comment("size_dt_strings");
	out.write_data(size_dt_strings);
	out.write_comment("size_dt_struct");
	out.write_data(size_dt_struct);
}

bool
header::read_dtb(input_buffer &input)
{
	if (!input.consume_binary(magic))
	{
		fprintf(stderr, "Missing magic token in header.");
		return false;
	}
	if (magic != 0xd00dfeed)
	{
		fprintf(stderr, "Bad magic token in header.  Got %" PRIx32
		                " expected 0xd00dfeed\n", magic);
		return false;
	}
	return input.consume_binary(totalsize) &&
	       input.consume_binary(off_dt_struct) &&
	       input.consume_binary(off_dt_strings) &&
	       input.consume_binary(off_mem_rsvmap) &&
	       input.consume_binary(version) &&
	       input.consume_binary(last_comp_version) &&
	       input.consume_binary(boot_cpuid_phys) &&
	       input.consume_binary(size_dt_strings) &&
	       input.consume_binary(size_dt_struct);
}
uint32_t
string_table::add_string(const string &str)
{
	auto old = string_offsets.find(str);
	if (old == string_offsets.end())
	{
		uint32_t start = size;
		// Don't forget the trailing nul
		size += str.size() + 1;
		string_offsets.insert(std::make_pair(str, start));
		strings.push_back(str);
		return start;
	}
	else
	{
		return old->second;
	}
}

void
string_table::write(dtb::output_writer &writer)
{
	writer.write_comment("Strings table.");
	writer.write_label("dt_strings_start");
	for (auto &i : strings)
	{
		writer.write_string(i);
	}
	writer.write_label("dt_strings_end");
}

} // namespace dtb

} // namespace dtc

