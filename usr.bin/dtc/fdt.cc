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

#define __STDC_LIMIT_MACROS 1

#include "fdt.hh"
#include "dtb.hh"

#include <algorithm>
#include <sstream>

#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

using std::string;

namespace dtc
{

namespace fdt
{

uint32_t
property_value::get_as_uint32()
{
	if (byte_data.size() != 4)
	{
		return 0;
	}
	uint32_t v = 0;
	v &= byte_data[0] << 24;
	v &= byte_data[1] << 16;
	v &= byte_data[2] << 8;
	v &= byte_data[3] << 0;
	return v;
}

void
property_value::push_to_buffer(byte_buffer &buffer)
{
	if (!byte_data.empty())
	{
		buffer.insert(buffer.end(), byte_data.begin(), byte_data.end());
	}
	else
	{
		push_string(buffer, string_data, true);
		// Trailing nul
		buffer.push_back(0);
	}
}

void
property_value::write_dts(FILE *file)
{
	resolve_type();
	switch (type)
	{
		default:
			assert(0 && "Invalid type");
		case STRING:
		case STRING_LIST:
		case CROSS_REFERENCE:
			write_as_string(file);
			break;
		case PHANDLE:
			write_as_cells(file);
			break;
		case BINARY:
			if (byte_data.size() % 4 == 0)
			{
				write_as_cells(file);
				break;
			}
			write_as_bytes(file);
			break;
	}
}

void
property_value::resolve_type()
{
	if (type != UNKNOWN)
	{
		return;
	}
	if (byte_data.empty())
	{
		type = STRING;
		return;
	}
	if (byte_data.back() == 0)
	{
		bool is_all_printable = true;
		int nuls = 0;
		int bytes = 0;
		bool lastWasNull = false;
		for (auto i : byte_data)
		{
			bytes++;
			is_all_printable &= (i == '\0') || isprint(i);
			if (i == '\0')
			{
				// If there are two nulls in a row, then we're probably binary.
				if (lastWasNull)
				{
					type = BINARY;
					return;
				}
				nuls++;
				lastWasNull = true;
			}
			else
			{
				lastWasNull = false;
			}
			if (!is_all_printable)
			{
				break;
			}
		}
		if ((is_all_printable && (bytes > nuls)) || bytes == 0)
		{
			type = STRING;
			if (nuls > 1)
			{
				type = STRING_LIST;
			}
			return;
		}
	}
	type = BINARY;
}

size_t
property_value::size()
{
	if (!byte_data.empty())
	{
		return byte_data.size();
	}
	return string_data.size() + 1;
}

void
property_value::write_as_string(FILE *file)
{
	putc('"', file);
	if (byte_data.empty())
	{
		fputs(string_data.c_str(), file);
	}
	else
	{
		bool hasNull = (byte_data.back() == '\0');
		// Remove trailing null bytes from the string before printing as dts.
		if (hasNull)
		{
			byte_data.pop_back();
		}
		for (auto i : byte_data)
		{
			// FIXME Escape tabs, newlines, and so on.
			if (i == '\0')
			{
				fputs("\", \"", file);
				continue;
			}
			putc(i, file);
		}
		if (hasNull)
		{
			byte_data.push_back('\0');
		}
	}
	putc('"', file);
}

void
property_value::write_as_cells(FILE *file)
{
	putc('<', file);
	assert((byte_data.size() % 4) == 0);
	for (auto i=byte_data.begin(), e=byte_data.end(); i!=e ; ++i)
	{
		uint32_t v = 0;
		v = (v << 8) | *i;
		++i;
		v = (v << 8) | *i;
		++i;
		v = (v << 8) | *i;
		++i;
		v = (v << 8) | *i;
		fprintf(file, "0x%" PRIx32, v);
		if (i+1 != e)
		{
			putc(' ', file);
		}
	}
	putc('>', file);
}

void
property_value::write_as_bytes(FILE *file)
{
	putc('[', file);
	for (auto i=byte_data.begin(), e=byte_data.end(); i!=e ; i++)
	{
		fprintf(file, "%02hhx", *i);
		if (i+1 != e)
		{
			putc(' ', file);
		}
	}
	putc(']', file);
}

void
property::parse_string(text_input_buffer &input)
{
	property_value v;
	assert(*input == '"');
	++input;
	std::vector<char> bytes;
	bool isEscaped = false;
	while (char c = *input)
	{
		if (c == '"' && !isEscaped)
		{
			input.consume('"');
			break;
		}
		isEscaped = (c == '\\');
		bytes.push_back(c);
		++input;
	}
	v.string_data = string(bytes.begin(), bytes.end());
	values.push_back(v);
}

void
property::parse_cells(text_input_buffer &input, int cell_size)
{
	assert(*input == '<');
	++input;
	property_value v;
	input.next_token();
	while (!input.consume('>'))
	{
		input.next_token();
		// If this is a phandle then we need to get the name of the
		// referenced node
		if (input.consume('&'))
		{
			if (cell_size != 32)
			{
				input.parse_error("reference only permitted in 32-bit arrays");
				valid = false;
				return;
			}
			input.next_token();
			string referenced;
			if (!input.consume('{'))
			{
				referenced = input.parse_node_name();
			}
			else
			{
				referenced = input.parse_to('}');
				input.consume('}');
			}
			if (referenced.empty())
			{
				input.parse_error("Expected node name");
				valid = false;
				return;
			}
			input.next_token();
			// If we already have some bytes, make the phandle a
			// separate component.
			if (!v.byte_data.empty())
			{
				values.push_back(v);
				v = property_value();
			}
			v.string_data = referenced;
			v.type = property_value::PHANDLE;
			values.push_back(v);
			v = property_value();
		}
		else
		{
			//FIXME: We should support labels in the middle
			//of these, but we don't.
			unsigned long long val;
			if (!input.consume_integer_expression(val))
			{
				input.parse_error("Expected numbers in array of cells");
				valid = false;
				return;
			}
			switch (cell_size)
			{
				case 8:
					v.byte_data.push_back(val);
					break;
				case 16:
					push_big_endian(v.byte_data, (uint16_t)val);
					break;
				case 32:
					push_big_endian(v.byte_data, (uint32_t)val);
					break;
				case 64:
					push_big_endian(v.byte_data, (uint64_t)val);
					break;
				default:
					assert(0 && "Invalid cell size!");
			}
			input.next_token();
		}
	}
	// Don't store an empty string value here.
	if (v.byte_data.size() > 0)
	{
		values.push_back(v);
	}
}

void
property::parse_bytes(text_input_buffer &input)
{
	assert(*input == '[');
	++input;
	property_value v;
	input.next_token();
	while (!input.consume(']'))
	{
		{
			//FIXME: We should support
			//labels in the middle of
			//these, but we don't.
			uint8_t val;
			if (!input.consume_hex_byte(val))
			{
				input.parse_error("Expected hex bytes in array of bytes");
				valid = false;
				return;
			}
			v.byte_data.push_back(val);
			input.next_token();
		}
	}
	values.push_back(v);
}

void
property::parse_reference(text_input_buffer &input)
{
	assert(*input == '&');
	++input;
	input.next_token();
	property_value v;
	v.string_data = input.parse_node_name();
	if (v.string_data.empty())
	{
		input.parse_error("Expected node name");
		valid = false;
		return;
	}
	v.type = property_value::CROSS_REFERENCE;
	values.push_back(v);
}

property::property(input_buffer &structs, input_buffer &strings)
{
	uint32_t name_offset;
	uint32_t length;
	valid = structs.consume_binary(length) &&
		structs.consume_binary(name_offset);
	if (!valid)
	{
		fprintf(stderr, "Failed to read property\n");
		return;
	}
	// Find the name
	input_buffer name_buffer = strings.buffer_from_offset(name_offset);
	if (name_buffer.finished())
	{
		fprintf(stderr, "Property name offset %" PRIu32
			" is past the end of the strings table\n",
			name_offset);
		valid = false;
		return;
	}
	key = name_buffer.parse_to(0);

	// If we're empty, do not push anything as value.
	if (!length)
		return;

	// Read the value
	uint8_t byte;
	property_value v;
	for (uint32_t i=0 ; i<length ; i++)
	{
		if (!(valid = structs.consume_binary(byte)))
		{
			fprintf(stderr, "Failed to read property value\n");
			return;
		}
		v.byte_data.push_back(byte);
	}
	values.push_back(v);
}

void property::parse_define(text_input_buffer &input, define_map *defines)
{
	input.consume('$');
	if (!defines)
	{
		input.parse_error("No predefined properties to match name\n");
		valid = false;
		return;
	}
	string name = input.parse_property_name();
	define_map::iterator found;
	if ((name == string()) ||
	    ((found = defines->find(name)) == defines->end()))
	{
		input.parse_error("Undefined property name\n");
		valid = false;
		return;
	}
	values.push_back((*found).second->values[0]);
}

property::property(text_input_buffer &input,
                   string &&k,
                   string_set &&l,
                   bool semicolonTerminated,
                   define_map *defines) : key(k), labels(l), valid(true)
{
	do {
		input.next_token();
		switch (*input)
		{
			case '$':
			{
				parse_define(input, defines);
				if (valid)
				{
					break;
				}
			}
			default:
				input.parse_error("Invalid property value.");
				valid = false;
				return;
			case '/':
			{
				if (input.consume("/incbin/(\""))
				{
					auto loc = input.location();
					std::string filename = input.parse_to('"');
					if (!(valid = input.consume('"')))
					{
						loc.report_error("Syntax error, expected '\"' to terminate /incbin/(");
						return;
					}
					property_value v;
					if (!(valid = input.read_binary_file(filename, v.byte_data)))
					{
						input.parse_error("Cannot open binary include file");
						return;
					}
					if (!(valid &= input.consume(')')))
					{
						input.parse_error("Syntax error, expected ')' to terminate /incbin/(");
						return;
					}
					values.push_back(v);
					break;
				}
				unsigned long long bits = 0;
				valid = input.consume("/bits/");
				input.next_token();
				valid &= input.consume_integer(bits);
				if ((bits != 8) &&
				    (bits != 16) &&
				    (bits != 32) &&
				    (bits != 64)) {
					input.parse_error("Invalid size for elements");
					valid = false;
				}
				if (!valid) return;
				input.next_token();
				if (*input != '<')
				{
					input.parse_error("/bits/ directive is only valid on arrays");
					valid = false;
					return;
				}
				parse_cells(input, bits);
				break;
			}
			case '"':
				parse_string(input);
				break;
			case '<':
				parse_cells(input, 32);
				break;
			case '[':
				parse_bytes(input);
				break;
			case '&':
				parse_reference(input);
				break;
			case ';':
			{
				break;
			}
		}
		input.next_token();
	} while (input.consume(','));
	if (semicolonTerminated && !input.consume(';'))
	{
		input.parse_error("Expected ; at end of property");
		valid = false;
	}
}

property_ptr
property::parse_dtb(input_buffer &structs, input_buffer &strings)
{
	property_ptr p(new property(structs, strings));
	if (!p->valid)
	{
		p = nullptr;
	}
	return p;
}

property_ptr
property::parse(text_input_buffer &input, string &&key, string_set &&label,
                bool semicolonTerminated, define_map *defines)
{
	property_ptr p(new property(input,
	                            std::move(key),
	                            std::move(label),
	                            semicolonTerminated,
	                            defines));
	if (!p->valid)
	{
		p = nullptr;
	}
	return p;
}

void
property::write(dtb::output_writer &writer, dtb::string_table &strings)
{
	writer.write_token(dtb::FDT_PROP);
	byte_buffer value_buffer;
	for (value_iterator i=begin(), e=end() ; i!=e ; ++i)
	{
		i->push_to_buffer(value_buffer);
	}
	writer.write_data((uint32_t)value_buffer.size());
	writer.write_comment(key);
	writer.write_data(strings.add_string(key));
	writer.write_data(value_buffer);
}

bool
property_value::try_to_merge(property_value &other)
{
	resolve_type();
	switch (type)
	{
		case UNKNOWN:
			__builtin_unreachable();
			assert(0);
			return false;
		case EMPTY:
			*this = other;
		case STRING:
		case STRING_LIST:
		case CROSS_REFERENCE:
			return false;
		case PHANDLE:
		case BINARY:
			if (other.type == PHANDLE || other.type == BINARY)
			{
				type = BINARY;
				byte_data.insert(byte_data.end(), other.byte_data.begin(),
				                 other.byte_data.end());
				return true;
			}
	}
	return false;
}

void
property::write_dts(FILE *file, int indent)
{
	for (int i=0 ; i<indent ; i++)
	{
		putc('\t', file);
	}
#ifdef PRINT_LABELS
	for (auto &l : labels)
	{
		fputs(l.c_str(), file);
		fputs(": ", file);
	}
#endif
	if (key != string())
	{
		fputs(key.c_str(), file);
	}
	if (!values.empty())
	{
		std::vector<property_value> *vals = &values;
		std::vector<property_value> v;
		// If we've got multiple values then try to merge them all together.
		if (values.size() > 1)
		{
			vals = &v;
			v.push_back(values.front());
			for (auto i=(++begin()), e=end() ; i!=e ; ++i)
			{
				if (!v.back().try_to_merge(*i))
				{
					v.push_back(*i);
				}
			}
		}
		fputs(" = ", file);
		for (auto i=vals->begin(), e=vals->end() ; i!=e ; ++i)
		{
			i->write_dts(file);
			if (i+1 != e)
			{
				putc(',', file);
				putc(' ', file);
			}
		}
	}
	fputs(";\n", file);
}

size_t
property::offset_of_value(property_value &val)
{
	size_t off = 0;
	for (auto &v : values)
	{
		if (&v == &val)
		{
			return off;
		}
		off += v.size();
	}
	return -1;
}

string
node::parse_name(text_input_buffer &input, bool &is_property, const char *error)
{
	if (!valid)
	{
		return string();
	}
	input.next_token();
	if (is_property)
	{
		return input.parse_property_name();
	}
	string n = input.parse_node_or_property_name(is_property);
	if (n.empty())
	{
		if (n.empty())
		{
			input.parse_error(error);
			valid = false;
		}
	}
	return n;
}

node::visit_behavior
node::visit(std::function<visit_behavior(node&, node*)> fn, node *parent)
{
	visit_behavior behavior;
	behavior = fn(*this, parent);
	if (behavior == VISIT_BREAK)
	{
		return VISIT_BREAK;
	}
	else if (behavior != VISIT_CONTINUE)
	{
		for (auto &&c : children)
		{
			behavior = c->visit(fn, this);
			// Any status other than VISIT_RECURSE stops our execution and
			// bubbles up to our caller.  The caller may then either continue
			// visiting nodes that are siblings to this one or completely halt
			// visiting.
			if (behavior != VISIT_RECURSE)
			{
				return behavior;
			}
		}
	}
	// Continue recursion by default
	return VISIT_RECURSE;
}

node::node(input_buffer &structs, input_buffer &strings) : valid(true)
{
	std::vector<char> bytes;
	while (structs[0] != '\0' && structs[0] != '@')
	{
		bytes.push_back(structs[0]);
		++structs;
	}
	name = string(bytes.begin(), bytes.end());
	bytes.clear();
	if (structs[0] == '@')
	{
		++structs;
		while (structs[0] != '\0')
		{
			bytes.push_back(structs[0]);
			++structs;
		}
		unit_address = string(bytes.begin(), bytes.end());
	}
	++structs;
	uint32_t token;
	while (structs.consume_binary(token))
	{
		switch (token)
		{
			default:
				fprintf(stderr, "Unexpected token 0x%" PRIx32
					" while parsing node.\n", token);
				valid = false;
				return;
			// Child node, parse it.
			case dtb::FDT_BEGIN_NODE:
			{
				node_ptr child = node::parse_dtb(structs, strings);
				if (child == 0)
				{
					valid = false;
					return;
				}
				children.push_back(std::move(child));
				break;
			}
			// End of this node, no errors.
			case dtb::FDT_END_NODE:
				return;
			// Property, parse it.
			case dtb::FDT_PROP:
			{
				property_ptr prop = property::parse_dtb(structs, strings);
				if (prop == 0)
				{
					valid = false;
					return;
				}
				props.push_back(prop);
				break;
			}
				break;
			// End of structs table.  Should appear after
			// the end of the last node.
			case dtb::FDT_END:
				fprintf(stderr, "Unexpected FDT_END token while parsing node.\n");
				valid = false;
				return;
			// NOPs are padding.  Ignore them.
			case dtb::FDT_NOP:
				break;
		}
	}
	fprintf(stderr, "Failed to read token from structs table while parsing node.\n");
	valid = false;
	return;
}


node::node(const string &n,
           const std::vector<property_ptr> &p)
	: name(n)
{
	props.insert(props.begin(), p.begin(), p.end());
}

node_ptr node::create_special_node(const string &name,
                                   const std::vector<property_ptr> &props)
{
	node_ptr n(new node(name, props));
	return n;
}

node::node(text_input_buffer &input,
           string &&n,
           std::unordered_set<string> &&l,
           string &&a,
           define_map *defines)
	: labels(l), name(n), unit_address(a), valid(true)
{
	if (!input.consume('{'))
	{
		input.parse_error("Expected { to start new device tree node.\n");
	}
	input.next_token();
	while (valid && !input.consume('}'))
	{
		// flag set if we find any characters that are only in
		// the property name character set, not the node 
		bool is_property = false;
		string child_name, child_address;
		std::unordered_set<string> child_labels;
		auto parse_delete = [&](const char *expected, bool at)
		{
			if (child_name == string())
			{
				input.parse_error(expected);
				valid = false;
				return;
			}
			input.next_token();
			if (at && input.consume('@'))
			{
				child_name += '@';
				child_name += parse_name(input, is_property, "Expected unit address");
			}
			if (!input.consume(';'))
			{
				input.parse_error("Expected semicolon");
				valid = false;
				return;
			}
			input.next_token();
		};
		if (input.consume("/delete-node/"))
		{
			input.next_token();
			child_name = input.parse_node_name();
			parse_delete("Expected node name", true);
			if (valid)
			{
				deleted_children.insert(child_name);
			}
			continue;
		}
		if (input.consume("/delete-property/"))
		{
			input.next_token();
			child_name = input.parse_property_name();
			parse_delete("Expected property name", false);
			if (valid)
			{
				deleted_props.insert(child_name);
			}
			continue;
		}
		child_name = parse_name(input, is_property,
				"Expected property or node name");
		while (input.consume(':'))
		{
			// Node labels can contain any characters?  The
			// spec doesn't say, so we guess so...
			is_property = false;
			child_labels.insert(std::move(child_name));
			child_name = parse_name(input, is_property, "Expected property or node name");
		}
		if (input.consume('@'))
		{
			child_address = parse_name(input, is_property, "Expected unit address");
		}
		if (!valid)
		{
			return;
		}
		input.next_token();
		// If we're parsing a property, then we must actually do that.
		if (input.consume('='))
		{
			property_ptr p = property::parse(input, std::move(child_name),
					std::move(child_labels), true, defines);
			if (p == 0)
			{
				valid = false;
			}
			else
			{
				props.push_back(p);
			}
		}
		else if (!is_property && *input == ('{'))
		{
			node_ptr child = node::parse(input, std::move(child_name),
					std::move(child_labels), std::move(child_address), defines);
			if (child)
			{
				children.push_back(std::move(child));
			}
			else
			{
				valid = false;
			}
		}
		else if (input.consume(';'))
		{
			props.push_back(property_ptr(new property(std::move(child_name), std::move(child_labels))));
		}
		else
		{
			input.parse_error("Error parsing property.  Expected property value");
			valid = false;
		}
		input.next_token();
	}
	input.next_token();
	input.consume(';');
}

bool
node::cmp_properties(property_ptr &p1, property_ptr &p2)
{
	return p1->get_key() < p2->get_key();
}

bool
node::cmp_children(node_ptr &c1, node_ptr &c2)
{
	if (c1->name == c2->name)
	{
		return c1->unit_address < c2->unit_address;
	}
	return c1->name < c2->name;
}

void
node::sort()
{
	std::sort(property_begin(), property_end(), cmp_properties);
	std::sort(child_begin(), child_end(), cmp_children);
	for (auto &c : child_nodes())
	{
		c->sort();
	}
}

node_ptr
node::parse(text_input_buffer &input,
            string &&name,
            string_set &&label,
            string &&address,
            define_map *defines)
{
	node_ptr n(new node(input,
	                    std::move(name),
	                    std::move(label),
	                    std::move(address),
	                    defines));
	if (!n->valid)
	{
		n = 0;
	}
	return n;
}

node_ptr
node::parse_dtb(input_buffer &structs, input_buffer &strings)
{
	node_ptr n(new node(structs, strings));
	if (!n->valid)
	{
		n = 0;
	}
	return n;
}

property_ptr
node::get_property(const string &key)
{
	for (auto &i : props)
	{
		if (i->get_key() == key)
		{
			return i;
		}
	}
	return 0;
}

void
node::merge_node(node_ptr &other)
{
	for (auto &l : other->labels)
	{
		labels.insert(l);
	}
	// Note: this is an O(n*m) operation.  It might be sensible to
	// optimise this if we find that there are nodes with very
	// large numbers of properties, but for typical usage the
	// entire vector will fit (easily) into cache, so iterating
	// over it repeatedly isn't that expensive.
	for (auto &p : other->properties())
	{
		bool found = false;
		for (auto &mp : properties())
		{
			if (mp->get_key() == p->get_key())
			{
				mp = p;
				found = true;
				break;
			}
		}
		if (!found)
		{
			add_property(p);
		}
	}
	for (auto &c : other->children)
	{
		bool found = false;
		for (auto &i : children)
		{
			if (i->name == c->name && i->unit_address == c->unit_address)
			{
				i->merge_node(c);
				found = true;
				break;
			}
		}
		if (!found)
		{
			children.push_back(std::move(c));
		}
	}
	children.erase(std::remove_if(children.begin(), children.end(),
			[&](const node_ptr &p) {
				string full_name = p->name;
				if (p->unit_address != string())
				{
					full_name += '@';
					full_name += p->unit_address;
				}
				if (other->deleted_children.count(full_name) > 0)
				{
					other->deleted_children.erase(full_name);
					return true;
				}
				return false;
			}), children.end());
	props.erase(std::remove_if(props.begin(), props.end(),
			[&](const property_ptr &p) {
				if (other->deleted_props.count(p->get_key()) > 0)
				{
					other->deleted_props.erase(p->get_key());
					return true;
				}
				return false;
			}), props.end());
}

void
node::write(dtb::output_writer &writer, dtb::string_table &strings)
{
	writer.write_token(dtb::FDT_BEGIN_NODE);
	byte_buffer name_buffer;
	push_string(name_buffer, name);
	if (unit_address != string())
	{
		name_buffer.push_back('@');
		push_string(name_buffer, unit_address);
	}
	writer.write_comment(name);
	writer.write_data(name_buffer);
	writer.write_data((uint8_t)0);
	for (auto p : properties())
	{
		p->write(writer, strings);
	}
	for (auto &c : child_nodes())
	{
		c->write(writer, strings);
	}
	writer.write_token(dtb::FDT_END_NODE);
}

void
node::write_dts(FILE *file, int indent)
{
	for (int i=0 ; i<indent ; i++)
	{
		putc('\t', file);
	}
#ifdef PRINT_LABELS
	for (auto &label : labels)
	{
		fprintf(file, "%s: ", label.c_str());
	}
#endif
	if (name != string())
	{
		fputs(name.c_str(), file);
	}
	if (unit_address != string())
	{
		putc('@', file);
		fputs(unit_address.c_str(), file);
	}
	fputs(" {\n\n", file);
	for (auto p : properties())
	{
		p->write_dts(file, indent+1);
	}
	for (auto &c : child_nodes())
	{
		c->write_dts(file, indent+1);
	}
	for (int i=0 ; i<indent ; i++)
	{
		putc('\t', file);
	}
	fputs("};\n", file);
}

void
device_tree::collect_names_recursive(node_ptr &n, node_path &path)
{
	path.push_back(std::make_pair(n->name, n->unit_address));
	for (const string &name : n->labels)
	{
		if (name != string())
		{
			auto iter = node_names.find(name);
			if (iter == node_names.end())
			{
				node_names.insert(std::make_pair(name, n.get()));
				node_paths.insert(std::make_pair(name, path));
			}
			else
			{
				node_names.erase(iter);
				auto i = node_paths.find(name);
				if (i != node_paths.end())
				{
					node_paths.erase(name);
				}
				fprintf(stderr, "Label not unique: %s.  References to this label will not be resolved.\n", name.c_str());
			}
		}
	}
	for (auto &c : n->child_nodes())
	{
		collect_names_recursive(c, path);
	}
	// Now we collect the phandles and properties that reference
	// other nodes.
	for (auto &p : n->properties())
	{
		for (auto &v : *p)
		{
			if (v.is_phandle())
			{
				fixups.push_back({path, p, v});
			}
			if (v.is_cross_reference())
			{
				cross_references.push_back(&v);
			}
		}
		if ((p->get_key() == "phandle") ||
		    (p->get_key() == "linux,phandle"))
		{
			if (p->begin()->byte_data.size() != 4)
			{
				fprintf(stderr, "Invalid phandle value for node %s.  Should be a 4-byte value.\n", n->name.c_str());
				valid = false;
			}
			else
			{
				uint32_t phandle = p->begin()->get_as_uint32();
				used_phandles.insert(std::make_pair(phandle, n.get()));
			}
		}
	}
	path.pop_back();
}

void
device_tree::collect_names()
{
	node_path p;
	node_names.clear();
	node_paths.clear();
	cross_references.clear();
	fixups.clear();
	collect_names_recursive(root, p);
}

property_ptr
device_tree::assign_phandle(node *n, uint32_t &phandle)
{
	// If there is an existing phandle, use it
	property_ptr p = n->get_property("phandle");
	if (p == 0)
	{
		p = n->get_property("linux,phandle");
	}
	if (p == 0)
	{
		// Otherwise insert a new phandle node
		property_value v;
		while (used_phandles.find(phandle) != used_phandles.end())
		{
			// Note that we only don't need to
			// store this phandle in the set,
			// because we are monotonically
			// increasing the value of phandle and
			// so will only ever revisit this value
			// if we have used 2^32 phandles, at
			// which point our blob won't fit in
			// any 32-bit system and we've done
			// something badly wrong elsewhere
			// already.
			phandle++;
		}
		push_big_endian(v.byte_data, phandle++);
		if (phandle_node_name == BOTH || phandle_node_name == LINUX)
		{
			p.reset(new property("linux,phandle"));
			p->add_value(v);
			n->add_property(p);
		}
		if (phandle_node_name == BOTH || phandle_node_name == EPAPR)
		{
			p.reset(new property("phandle"));
			p->add_value(v);
			n->add_property(p);
		}
	}

	return (p);
}

void
device_tree::assign_phandles(node_ptr &n, uint32_t &next)
{
	if (!n->labels.empty())
	{
		assign_phandle(n.get(), next);
	}

	for (auto &c : n->child_nodes())
	{
		assign_phandles(c, next);
	}
}

void
device_tree::resolve_cross_references(uint32_t &phandle)
{
	for (auto *pv : cross_references)
	{
		node_path path = node_paths[pv->string_data];
		auto p = path.begin();
		auto pe = path.end();
		if (p != pe)
		{
			// Skip the first name in the path.  It's always "", and implicitly /
			for (++p ; p!=pe ; ++p)
			{
				pv->byte_data.push_back('/');
				push_string(pv->byte_data, p->first);
				if (!(p->second.empty()))
				{
					pv->byte_data.push_back('@');
					push_string(pv->byte_data, p->second);
				}
			}
			pv->byte_data.push_back(0);
		}
	}
	std::unordered_map<property_value*, fixup&> phandle_set;
	for (auto &i : fixups)
	{
		phandle_set.insert({&i.val, i});
	}
	std::vector<std::reference_wrapper<fixup>> sorted_phandles;
	root->visit([&](node &n, node *) {
		for (auto &p : n.properties())
		{
			for (auto &v : *p)
			{
				auto i = phandle_set.find(&v);
				if (i != phandle_set.end())
				{
					sorted_phandles.push_back(i->second);
				}
			}
		}
		// Allow recursion
		return node::VISIT_RECURSE;
	}, nullptr);
	assert(sorted_phandles.size() == fixups.size());

	for (auto &i : sorted_phandles)
	{
		string target_name = i.get().val.string_data;
		node *target = nullptr;
		string possible;
		// If the node name is a path, then look it up by following the path,
		// otherwise jump directly to the named node.
		if (target_name[0] == '/')
		{
			string path;
			target = root.get();
			std::istringstream ss(target_name);
			string path_element;
			// Read the leading /
			std::getline(ss, path_element, '/');
			// Iterate over path elements
			while (!ss.eof())
			{
				path += '/';
				std::getline(ss, path_element, '/');
				std::istringstream nss(path_element);
				string node_name, node_address;
				std::getline(nss, node_name, '@');
				std::getline(nss, node_address, '@');
				node *next = nullptr;
				for (auto &c : target->child_nodes())
				{
					if (c->name == node_name)
					{
						if (c->unit_address == node_address)
						{
							next = c.get();
							break;
						}
						else
						{
							possible = path + c->name;
							if (c->unit_address != string())
							{
								possible += '@';
								possible += c->unit_address;
							}
						}
					}
				}
				path += node_name;
				if (node_address != string())
				{
					path += '@';
					path += node_address;
				}
				target = next;
				if (target == nullptr)
				{
					break;
				}
			}
		}
		else
		{
			target = node_names[target_name];
		}
		if (target == nullptr)
		{
			if (is_plugin)
			{
				unresolved_fixups.push_back(i);
				continue;
			}
			else
			{
				fprintf(stderr, "Failed to find node with label: %s\n", target_name.c_str());
				if (possible != string())
				{
					fprintf(stderr, "Possible intended match: %s\n", possible.c_str());
				}
				valid = 0;
				return;
			}
		}
		// If there is an existing phandle, use it
		property_ptr p = assign_phandle(target, phandle);
		p->begin()->push_to_buffer(i.get().val.byte_data);
		assert(i.get().val.byte_data.size() == 4);
	}
}


void
device_tree::parse_file(text_input_buffer &input,
                        std::vector<node_ptr> &roots,
                        bool &read_header)
{
	input.next_token();
	// Read the header
	if (input.consume("/dts-v1/;"))
	{
		read_header = true;
	}
	input.next_token();
	if (input.consume("/plugin/;"))
	{
		is_plugin = true;
	}
	input.next_token();
	if (!read_header)
	{
		input.parse_error("Expected /dts-v1/; version string");
	}
	// Read any memory reservations
	while (input.consume("/memreserve/"))
	{
		unsigned long long start, len;
		input.next_token();
		// Read the start and length.
		if (!(input.consume_integer_expression(start) &&
		    (input.next_token(),
		    input.consume_integer_expression(len))))
		{
			input.parse_error("Expected size on /memreserve/ node.");
		}
		input.next_token();
		input.consume(';');
		reservations.push_back(reservation(start, len));
		input.next_token();
	}
	while (valid && !input.finished())
	{
		node_ptr n;
		if (input.consume('/'))
		{
			input.next_token();
			n = node::parse(input, string(), string_set(), string(), &defines);
		}
		else if (input.consume('&'))
		{
			input.next_token();
			string name;
			bool name_is_path_reference = false;
			// This is to deal with names intended as path references, e.g. &{/path}.
			// While it may make sense in a non-plugin context, we don't support such
			// usage at this time.
			if (input.consume('{') && is_plugin)
			{
				name = input.parse_to('}');
				input.consume('}');
				name_is_path_reference = true;
			}
			else
			{
				name = input.parse_node_name();
			}
			input.next_token();
			n = node::parse(input, std::move(name), string_set(), string(), &defines);
			n->name_is_path_reference = name_is_path_reference;
		}
		else
		{
			input.parse_error("Failed to find root node /.");
		}
		if (n)
		{
			roots.push_back(std::move(n));
		}
		else
		{
			valid = false;
		}
		input.next_token();
	}
}

template<class writer> void
device_tree::write(int fd)
{
	dtb::string_table st;
	dtb::header head;
	writer head_writer;
	writer reservation_writer;
	writer struct_writer;
	writer strings_writer;

	// Build the reservation table
	reservation_writer.write_comment(string("Memory reservations"));
	reservation_writer.write_label(string("dt_reserve_map"));
	for (auto &i : reservations)
	{
		reservation_writer.write_comment(string("Reservation start"));
		reservation_writer.write_data(i.first);
		reservation_writer.write_comment(string("Reservation length"));
		reservation_writer.write_data(i.first);
	}
	// Write n spare reserve map entries, plus the trailing 0.
	for (uint32_t i=0 ; i<=spare_reserve_map_entries ; i++)
	{
		reservation_writer.write_data((uint64_t)0);
		reservation_writer.write_data((uint64_t)0);
	}


	struct_writer.write_comment(string("Device tree"));
	struct_writer.write_label(string("dt_struct_start"));
	root->write(struct_writer, st);
	struct_writer.write_token(dtb::FDT_END);
	struct_writer.write_label(string("dt_struct_end"));

	st.write(strings_writer);
	// Find the strings size before we stick padding on the end.
	// Note: We should possibly use a new writer for the padding.
	head.size_dt_strings = strings_writer.size();

	// Stick the padding in the strings writer, but after the
	// marker indicating that it's the end.
	// Note: We probably should add a padding call to the writer so
	// that the asm back end can write padding directives instead
	// of a load of 0 bytes.
	for (uint32_t i=0 ; i<blob_padding ; i++)
	{
		strings_writer.write_data((uint8_t)0);
	}
	head.totalsize = sizeof(head) + strings_writer.size() +
		struct_writer.size() + reservation_writer.size();
	while (head.totalsize < minimum_blob_size)
	{
		head.totalsize++;
		strings_writer.write_data((uint8_t)0);
	}
	head.off_dt_struct = sizeof(head) + reservation_writer.size();;
	head.off_dt_strings = head.off_dt_struct + struct_writer.size();
	head.off_mem_rsvmap = sizeof(head);
	head.boot_cpuid_phys = boot_cpu;
	head.size_dt_struct = struct_writer.size();
	head.write(head_writer);

	head_writer.write_to_file(fd);
	reservation_writer.write_to_file(fd);
	struct_writer.write_to_file(fd);
	strings_writer.write_label(string("dt_blob_end"));
	strings_writer.write_to_file(fd);
}

node*
device_tree::referenced_node(property_value &v)
{
	if (v.is_phandle())
	{
		return node_names[v.string_data];
	}
	if (v.is_binary())
	{
		return used_phandles[v.get_as_uint32()];
	}
	return 0;
}

void
device_tree::write_binary(int fd)
{
	write<dtb::binary_writer>(fd);
}

void
device_tree::write_asm(int fd)
{
	write<dtb::asm_writer>(fd);
}

void
device_tree::write_dts(int fd)
{
	FILE *file = fdopen(fd, "w");
	fputs("/dts-v1/;\n\n", file);

	if (!reservations.empty())
	{
		const char msg[] = "/memreserve/";
		fwrite(msg, sizeof(msg), 1, file);
		for (auto &i : reservations)
		{
			fprintf(file, " %" PRIx64 " %" PRIx64, i.first, i.second);
		}
		fputs(";\n\n", file);
	}
	putc('/', file);
	putc(' ', file);
	root->write_dts(file, 0);
	fclose(file);
}

void
device_tree::parse_dtb(const string &fn, FILE *)
{
	auto in = input_buffer::buffer_for_file(fn);
	if (in == 0)
	{
		valid = false;
		return;
	}
	input_buffer &input = *in;
	dtb::header h;
	valid = h.read_dtb(input);
	boot_cpu = h.boot_cpuid_phys;
	if (h.last_comp_version > 17)
	{
		fprintf(stderr, "Don't know how to read this version of the device tree blob");
		valid = false;
	}
	if (!valid)
	{
		return;
	}
	input_buffer reservation_map =
		input.buffer_from_offset(h.off_mem_rsvmap, 0);
	uint64_t start, length;
	do
	{
		if (!(reservation_map.consume_binary(start) &&
		      reservation_map.consume_binary(length)))
		{
			fprintf(stderr, "Failed to read memory reservation table\n");
			valid = false;
			return;
		}
	} while (!((start == 0) && (length == 0)));
	input_buffer struct_table =
		input.buffer_from_offset(h.off_dt_struct, h.size_dt_struct);
	input_buffer strings_table =
		input.buffer_from_offset(h.off_dt_strings, h.size_dt_strings);
	uint32_t token;
	if (!(struct_table.consume_binary(token) &&
		(token == dtb::FDT_BEGIN_NODE)))
	{
		fprintf(stderr, "Expected FDT_BEGIN_NODE token.\n");
		valid = false;
		return;
	}
	root = node::parse_dtb(struct_table, strings_table);
	if (!(struct_table.consume_binary(token) && (token == dtb::FDT_END)))
	{
		fprintf(stderr, "Expected FDT_END token after parsing root node.\n");
		valid = false;
		return;
	}
	valid = (root != 0);
}

string
device_tree::node_path::to_string() const
{
	string path;
	auto p = begin();
	auto pe = end();
	if ((p == pe) || (p+1 == pe))
	{
		return string("/");
	}
	// Skip the first name in the path.  It's always "", and implicitly /
	for (++p ; p!=pe ; ++p)
	{
		path += '/';
		path += p->first;
		if (!(p->second.empty()))
		{
			path += '@';
			path += p->second;
		}
	}
	return path;
}

node_ptr
device_tree::create_fragment_wrapper(node_ptr &node, int &fragnum)
{
	// In a plugin, we can massage these non-/ root nodes into into a fragment
	std::string fragment_address = "fragment@" + std::to_string(fragnum);
	++fragnum;

	std::vector<property_ptr> symbols;

	// Intentionally left empty
	node_ptr newroot = node::create_special_node("", symbols);
	node_ptr wrapper = node::create_special_node("__overlay__", symbols);

	// Generate the fragment with $propname = <&name>
	property_value v;
	std::string propname;
	v.string_data = node->name;
	if (!node->name_is_path_reference)
	{
		propname = "target";
		v.type = property_value::PHANDLE;
	}
	else
	{
		propname = "target-path";
		v.type = property_value::STRING;
	}
	auto prop = std::make_shared<property>(std::string(propname));
	prop->add_value(v);
	symbols.push_back(prop);

	node_ptr fragment = node::create_special_node(fragment_address, symbols);

	wrapper->merge_node(node);
	fragment->add_child(std::move(wrapper));
	newroot->add_child(std::move(fragment));
	return newroot;
}

node_ptr
device_tree::generate_root(node_ptr &node, int &fragnum)
{

	string name = node->name;
	if (name == string())
	{
		return std::move(node);
	}
	else if (!is_plugin)
	{
		return nullptr;
	}

	return create_fragment_wrapper(node, fragnum);
}

void
device_tree::reassign_fragment_numbers(node_ptr &node, int &delta)
{

	for (auto &c : node->child_nodes())
	{
		if (c->name == std::string("fragment"))
		{
			int current_address = std::stoi(c->unit_address, nullptr, 16);
			std::ostringstream new_address;
			current_address += delta;
			// It's possible that we hopped more than one somewhere, so just reset
			// delta to the next in sequence.
			delta = current_address + 1;
			new_address << std::hex << current_address;
			c->unit_address = new_address.str();
		}
	}
}

void
device_tree::parse_dts(const string &fn, FILE *depfile)
{
	auto in = input_buffer::buffer_for_file(fn);
	if (!in)
	{
		valid = false;
		return;
	}
	std::vector<node_ptr> roots;
	std::unordered_set<string> defnames;
	for (auto &i : defines)
	{
		defnames.insert(i.first);
	}
	text_input_buffer input(std::move(in),
	                        std::move(defnames),
	                        std::vector<string>(include_paths),
	                        dirname(fn),
	                        depfile);
	bool read_header = false;
	int fragnum = 0;
	parse_file(input, roots, read_header);
	switch (roots.size())
	{
		case 0:
			valid = false;
			input.parse_error("Failed to find root node /.");
			return;
		case 1:
			root = generate_root(roots[0], fragnum);
			if (!root)
			{
				valid = false;
				input.parse_error("Failed to find root node /.");
				return;
			}
			break;
		default:
		{
			root = generate_root(roots[0], fragnum);
			if (!root)
			{
				valid = false;
				input.parse_error("Failed to find root node /.");
				return;
			}
			for (auto i=++(roots.begin()), e=roots.end() ; i!=e ; ++i)
			{
				auto &node = *i;
				string name = node->name;
				if (name == string())
				{
					if (is_plugin)
					{
						// Re-assign any fragment numbers based on a delta of
						// fragnum before we merge it
						reassign_fragment_numbers(node, fragnum);
					}
					root->merge_node(node);
				}
				else
				{
					auto existing = node_names.find(name);
					if (existing == node_names.end())
					{
						collect_names();
						existing = node_names.find(name);
					}
					if (existing == node_names.end())
					{
						if (is_plugin)
						{
							auto fragment = create_fragment_wrapper(node, fragnum);
							root->merge_node(fragment);
						}
						else
						{
							fprintf(stderr, "Unable to merge node: %s\n", name.c_str());
						}
					}
					else
					{
						existing->second->merge_node(node);
					}
				}
			}
		}
	}
	collect_names();
	uint32_t phandle = 1;
	// If we're writing symbols, go ahead and assign phandles to the entire
	// tree. We'll do this before we resolve cross references, just to keep
	// order semi-predictable and stable.
	if (write_symbols)
	{
		assign_phandles(root, phandle);
	}
	resolve_cross_references(phandle);
	if (write_symbols)
	{
		std::vector<property_ptr> symbols;
		// Create a symbol table.  Each label  in this device tree may be
		// referenced by other plugins, so we create a __symbols__ node inside
		// the root that contains mappings (properties) from label names to
		// paths.
		for (auto &s : node_paths)
		{
			property_value v;
			v.string_data = s.second.to_string();
			v.type = property_value::STRING;
			string name = s.first;
			auto prop = std::make_shared<property>(std::move(name));
			prop->add_value(v);
			symbols.push_back(prop);
		}
		root->add_child(node::create_special_node("__symbols__", symbols));
	}
	// If this is a plugin, then we also need to create two extra nodes.
	// Internal phandles will need to be renumbered to avoid conflicts with
	// already-loaded nodes and external references will need to be
	// resolved.
	if (is_plugin)
	{
		std::vector<property_ptr> symbols;
		// Create the fixups entry.  This is of the form:
		// {target} = {path}:{property name}:{offset}
		auto create_fixup_entry = [&](fixup &i, string target)
			{
				string value = i.path.to_string();
				value += ':';
				value += i.prop->get_key();
				value += ':';
				value += std::to_string(i.prop->offset_of_value(i.val));
				property_value v;
				v.string_data = value;
				v.type = property_value::STRING;
				auto prop = std::make_shared<property>(std::move(target));
				prop->add_value(v);
				return prop;
			};
		// If we have any unresolved phandle references in this plugin,
		// then we must update them to 0xdeadbeef and leave a property in
		// the /__fixups__ node whose key is the label and whose value is
		// as described above.
		if (!unresolved_fixups.empty())
		{
			for (auto &i : unresolved_fixups)
			{
				auto &val = i.get().val;
				symbols.push_back(create_fixup_entry(i, val.string_data));
				val.byte_data.push_back(0xde);
				val.byte_data.push_back(0xad);
				val.byte_data.push_back(0xbe);
				val.byte_data.push_back(0xef);
				val.type = property_value::BINARY;
			}
			root->add_child(node::create_special_node("__fixups__", symbols));
		}
		symbols.clear();
		// If we have any resolved phandle references in this plugin, then
		// we must create a child in the __local_fixups__ node whose path
		// matches the node path from the root and whose value contains the
		// location of the reference within a property.
		
		// Create a local_fixups node that is initially empty.
		node_ptr local_fixups = node::create_special_node("__local_fixups__", symbols);
		for (auto &i : fixups)
		{
			if (!i.val.is_phandle())
			{
				continue;
			}
			node *n = local_fixups.get();
			for (auto &p : i.path)
			{
				// Skip the implicit root
				if (p.first.empty())
				{
					continue;
				}
				bool found = false;
				for (auto &c : n->child_nodes())
				{
					if (c->name == p.first)
					{
						string path = p.first;
						if (!(p.second.empty()))
						{
							path += '@';
							path += p.second;
						}
						n->add_child(node::create_special_node(path, symbols));
						n = (--n->child_end())->get();
					}
				}
				if (!found)
				{
					n->add_child(node::create_special_node(p.first, symbols));
					n = (--n->child_end())->get();
				}
			}
			assert(n);
			property_value pv;
			push_big_endian(pv.byte_data, static_cast<uint32_t>(i.prop->offset_of_value(i.val)));
			pv.type = property_value::BINARY;
			auto key = i.prop->get_key();
			property_ptr prop = n->get_property(key);
			// If we don't have an existing property then create one and
			// use this property value
			if (!prop)
			{
				prop = std::make_shared<property>(std::move(key));
				n->add_property(prop);
				prop->add_value(pv);
			}
			else
			{
				// If we do have an existing property value, try to append
				// this value.
				property_value &old_val = *(--prop->end());
				if (!old_val.try_to_merge(pv))
				{
					prop->add_value(pv);
				}
			}
		}
		// We've iterated over all fixups, but only emit the
		// __local_fixups__ if we found some that were resolved internally.
		if (local_fixups->child_begin() != local_fixups->child_end())
		{
			root->add_child(std::move(local_fixups));
		}
	}
}

bool device_tree::parse_define(const char *def)
{
	const char *val = strchr(def, '=');
	if (!val)
	{
		if (strlen(def) != 0)
		{
			string name(def);
			defines[name];
			return true;
		}
		return false;
	}
	string name(def, val-def);
	string name_copy = name;
	val++;
	std::unique_ptr<input_buffer> raw(new input_buffer(val, strlen(val)));
	text_input_buffer in(std::move(raw),
	                     std::unordered_set<string>(),
	                     std::vector<string>(),
	                     string(),
	                     nullptr);
	property_ptr p = property::parse(in, std::move(name_copy), string_set(), false);
	if (p)
		defines[name] = p;
	return (bool)p;
}

} // namespace fdt

} // namespace dtc

