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

#ifndef _INPUT_BUFFER_HH_
#define _INPUT_BUFFER_HH_
#include "util.hh"
#include <assert.h>
#include <stack>
#include <string>
#include <unordered_set>

namespace dtc
{

namespace {
struct expression;
typedef std::unique_ptr<expression> expression_ptr;
}

/**
 * Class encapsulating the input file.  Can be used as a const char*, but has
 * range checking.  Attempting to access anything out of range will return a 0
 * byte.  The input buffer can be cheaply copied, without copying the
 * underlying memory, however it is the user's responsibility to ensure that
 * such copies do not persist beyond the lifetime of the underlying memory.
 *
 * This also contains methods for reporting errors and for consuming the token
 * stream.
 */
class input_buffer
{
	friend class text_input_buffer;
	protected:
	/**
	 * The buffer.  This class doesn't own the buffer, but the
	 * mmap_input_buffer subclass does.
	 */
	const char* buffer;
	/**
	 * The size of the buffer.
	 */
	int size;
	private:
	/**
	 * The current place in the buffer where we are reading.  This class
	 * keeps a separate size, pointer, and cursor so that we can move
	 * forwards and backwards and still have checks that we haven't fallen
	 * off either end.
	 */
	int cursor;
	/**
	 * Private constructor.  This is used to create input buffers that
	 * refer to the same memory, but have different cursors.
	 */
	input_buffer(const char* b, int s, int c) : buffer(b), size(s),
		cursor(c) {}
	public:
	/**
	 * Returns the file name associated with this buffer.
	 */
	virtual const std::string &filename() const
	{
		static std::string s;
		return s;
	}
	static std::unique_ptr<input_buffer> buffer_for_file(const std::string &path,
	                                                     bool warn=true);
	/**
	 * Skips all characters in the input until the specified character is
	 * encountered.
	 */
	void skip_to(char);
	/**
	 * Parses up to a specified character and returns the intervening
	 * characters as a string.
	 */
	std::string parse_to(char);
	/**
	 * Return whether all input has been consumed.
	 */
	bool finished() { return cursor >= size; }
	/**
	 * Virtual destructor.  Does nothing, but exists so that subclasses
	 * that own the memory can run cleanup code for deallocating it.
	 */
	virtual ~input_buffer() {};
	/**
	 * Constructs an empty buffer.
	 */
	input_buffer() : buffer(0), size(0), cursor(0) {}
	/**
	 * Constructs a new buffer with a specified memory region and size.
	 */
	input_buffer(const char* b, int s) : buffer(b), size(s), cursor(0){}
	/**
	 * Returns a new input buffer referring into this input, clamped to the
	 * specified size.  If the requested buffer would fall outside the
	 * range of this one, then it returns an empty buffer.
	 *
	 * The returned buffer shares the same underlying storage as the
	 * original.  This is intended to be used for splitting up the various
	 * sections of a device tree blob.  Requesting a size of 0 will give a
	 * buffer that extends to the end of the available memory.
	 */
	input_buffer buffer_from_offset(int offset, int s=0);
	/**
	 * Dereferencing operator, allows the buffer to be treated as a char*
	 * and dereferenced to give a character.  This returns a null byte if
	 * the cursor is out of range.
	 */
	inline char operator*()
	{
		if (cursor >= size) { return '\0'; }
		if (cursor < 0) { return '\0'; }
		return buffer[cursor];
	}
	/**
	 * Array subscripting operator, returns a character at the specified
	 * index offset from the current cursor.  The offset may be negative,
	 * to reread characters that have already been read.  If the current
	 * cursor plus offset is outside of the range, this returns a nul
	 * byte.
	 */
	inline char operator[](int offset)
	{
		if (cursor + offset >= size) { return '\0'; }
		if (cursor + offset < 0) { return '\0'; }
		return buffer[cursor + offset];
	}
	/**
	 * Increments the cursor, iterating forward in the buffer.
	 */
	inline input_buffer &operator++()
	{
		cursor++; 
		return *this;
	}
	const char *begin()
	{
		return buffer;
	}
	const char *end()
	{
		return buffer + size;
	}
	/**
	 * Consumes a character.  Moves the cursor one character forward if the
	 * next character matches the argument, returning true.  If the current
	 * character does not match the argument, returns false.
	 */
	inline bool consume(char c)
	{
		if (*(*this) == c) 
		{
			++(*this);
			return true;
		}
		return false;
	}
	/**
	 * Consumes a string.  If the (null-terminated) string passed as the
	 * argument appears in the input, advances the cursor to the end and
	 * returns true.  Returns false if the string does not appear at the
	 * current point in the input.
	 */
	bool consume(const char *str);
	/**
	 * Reads an integer in base 8, 10, or 16.  Returns true and advances
	 * the cursor to the end of the integer if the cursor points to an
	 * integer, returns false and does not move the cursor otherwise.
	 *
	 * The parsed value is returned via the argument.
	 */
	bool consume_integer(unsigned long long &outInt);
	/**
	 * Reads an arithmetic expression (containing any of the normal C
	 * operators), evaluates it, and returns the result.
	 */
	bool consume_integer_expression(unsigned long long &outInt);
	/**
	 * Consumes two hex digits and return the resulting byte via the first
	 * argument.  If the next two characters are hex digits, returns true
	 * and advances the cursor.  If not, then returns false and leaves the
	 * cursor in place.
	 */
	bool consume_hex_byte(uint8_t &outByte);
	/**
	 * Template function that consumes a binary value in big-endian format
	 * from the input stream.  Returns true and advances the cursor if
	 * there is a value of the correct size.  This function assumes that
	 * all values must be natively aligned, and so advances the cursor to
	 * the correct alignment before reading.
	 */
	template<typename T>
	bool consume_binary(T &out)
	{
		int align = 0;
		int type_size = sizeof(T);
		if (cursor % type_size != 0)
		{
			align = type_size - (cursor % type_size);
		}
		if (size < cursor + align + type_size)
		{
			return false;
		}
		cursor += align;
		assert(cursor % type_size == 0);
		out = 0;
		for (int i=0 ; i<type_size ; ++i)
		{
			if (size < cursor)
			{
				return false;
			}
			out <<= 8;
			out |= (((T)buffer[cursor++]) & 0xff);
		}
		return true;
	}
#ifndef NDEBUG
	/**
	 * Dumps the current cursor value and the unconsumed values in the
	 * input buffer to the standard error.  This method is intended solely
	 * for debugging.
	 */
	void dump();
#endif
};
/**
 * Explicit specialisation for reading a single byte.
 */
template<>
inline bool input_buffer::consume_binary(uint8_t &out)
{
	if (size < cursor + 1)
	{
		return false;
	}
	out = buffer[cursor++];
	return true;
}

/**
 * An input buffer subclass used for parsing DTS files.  This manages a stack
 * of input buffers to handle /input/ operations.
 */
class text_input_buffer
{
	std::unordered_set<std::string> defines;
	/**
	 * The cursor is the input into the input stream where we are currently reading.
	 */
	int cursor = 0;
	/**
	 * The current stack of includes.  The current input is always from the top
	 * of the stack.
	 */
	std::stack<std::shared_ptr<input_buffer>> input_stack;
	/**
	 *
	 */
	const std::vector<std::string> include_paths;
	/**
	 * Reads forward past any spaces.  The DTS format is not whitespace
	 * sensitive and so we want to scan past whitespace when reading it.
	 */
	void skip_spaces();
	/**
	 * Returns the character immediately after the current one.
	 *
	 * This method does not look between files.
	 */
	char peek();
	/**
	 * If a /include/ token is encountered, then look up the corresponding
	 * input file, push it onto the input stack, and continue.
	 */
	void handle_include();
	/**
	 * The base directory for this file.
	 */
	const std::string dir;
	/**
	 * The file where dependencies should be output.
	 */
	FILE *depfile;
	public:
	/**
	 * Construct a new text input buffer with the specified buffer as the start
	 * of parsing and the specified set of input paths for handling new
	 * inclusions.
	 */
	text_input_buffer(std::unique_ptr<input_buffer> &&b,
	                  std::unordered_set<std::string> &&d,
	                  std::vector<std::string> &&i,
	                  const std::string directory,
	                  FILE *deps)
		: defines(d), include_paths(i), dir(directory), depfile(deps)
	{
		input_stack.push(std::move(b));
	}
	/**
	 * Skips all characters in the input until the specified character is
	 * encountered.
	 */
	void skip_to(char);
	/**
	 * Parse an expression.  If `stopAtParen` is set, then only parse a number
	 * or a parenthetical expression, otherwise assume that either is the
	 * left-hand side of a binary expression and try to parse the right-hand
	 * side.
	 */
	expression_ptr parse_expression(bool stopAtParen=false);
	/**
	 * Parse a binary expression, having already parsed the right-hand side.
	 */
	expression_ptr parse_binary_expression(expression_ptr lhs);
	/**
	 * Return whether all input has been consumed.
	 */
	bool finished()
	{
		return input_stack.empty() ||
			((input_stack.size() == 1) && input_stack.top()->finished());
	}
	/**
	 * Dereferencing operator.  Returns the current character in the top input buffer.
	 */
	inline char operator*()
	{
		if (input_stack.empty())
		{
			return 0;
		}
		return *(*input_stack.top());
	}
	/**
	 * Increments the cursor, iterating forward in the buffer.
	 */
	inline text_input_buffer &operator++()
	{
		if (input_stack.empty())
		{
			return *this;
		}
		cursor++;
		auto &top = *input_stack.top();
		++top;
		if (top.finished())
		{
			input_stack.pop();
		}
		return *this;
	}
	/**
	 * Consumes a character.  Moves the cursor one character forward if the
	 * next character matches the argument, returning true.  If the current
	 * character does not match the argument, returns false.
	 */
	inline bool consume(char c)
	{
		if (*(*this) == c)
		{
			++(*this);
			return true;
		}
		return false;
	}
	/**
	 * Consumes a string.  If the (null-terminated) string passed as the
	 * argument appears in the input, advances the cursor to the end and
	 * returns true.  Returns false if the string does not appear at the
	 * current point in the input.
	 *
	 * This method does not scan between files.
	 */
	bool consume(const char *str)
	{
		if (input_stack.empty())
		{
			return false;
		}
		return input_stack.top()->consume(str);
	}
	/**
	 * Reads an integer in base 8, 10, or 16.  Returns true and advances
	 * the cursor to the end of the integer if the cursor points to an
	 * integer, returns false and does not move the cursor otherwise.
	 *
	 * The parsed value is returned via the argument.
	 *
	 * This method does not scan between files.
	 */
	bool consume_integer(unsigned long long &outInt)
	{
		if (input_stack.empty())
		{
			return false;
		}
		return input_stack.top()->consume_integer(outInt);
	}
	/**
	 * Reads an arithmetic expression (containing any of the normal C
	 * operators), evaluates it, and returns the result.
	 */
	bool consume_integer_expression(unsigned long long &outInt);
	/**
	 * Consumes two hex digits and return the resulting byte via the first
	 * argument.  If the next two characters are hex digits, returns true
	 * and advances the cursor.  If not, then returns false and leaves the
	 * cursor in place.
	 *
	 * This method does not scan between files.
	 */
	bool consume_hex_byte(uint8_t &outByte)
	{
		if (input_stack.empty())
		{
			return false;
		}
		return input_stack.top()->consume_hex_byte(outByte);
	}
	/**
	 * Returns the longest string in the input buffer starting at the
	 * current cursor and composed entirely of characters that are valid in
	 * node names.
	*/
	std::string parse_node_name();
	/**
	 * Returns the longest string in the input buffer starting at the
	 * current cursor and composed entirely of characters that are valid in
	 * property names.
	 */
	std::string parse_property_name();
	/**
	 * Parses either a node or a property name.  If is_property is true on
	 * entry, then only property names are parsed.  If it is false, then it
	 * will be set, on return, to indicate whether the parsed name is only
	 * valid as a property.
	 */
	std::string parse_node_or_property_name(bool &is_property);
	/**
	 * Parses up to a specified character and returns the intervening
	 * characters as a string.
	 */
	std::string parse_to(char);
	/**
	 * Advances the cursor to the start of the next token, skipping
	 * comments and whitespace.  If the cursor already points to the start
	 * of a token, then this function does nothing.
	 */
	text_input_buffer &next_token();
	/**
	 * Location in the source file.  This should never be interpreted by
	 * anything other than error reporting functions of this class.  It will
	 * eventually become something more complex than an `int`.
	 */
	class source_location
	{
		friend class text_input_buffer;
		/**
		 * The text buffer object that included `b`.
		 */
		text_input_buffer &buffer;
		/**
		 * The underlying buffer that contains this location.
		 */
		std::shared_ptr<input_buffer> b;
		/**
		 * The offset within the current buffer of the source location.
		 */
		int cursor;
		source_location(text_input_buffer &buf)
			: buffer(buf),
			  b(buf.input_stack.empty() ? nullptr : buf.input_stack.top()),
			  cursor(b ? b->cursor : 0) {}
		public:
		/**
		 * Report an error at this location.
		 */
		void report_error(const char *msg)
		{
			if (b)
			{
				buffer.parse_error(msg, *b, cursor);
			}
			else
			{
				buffer.parse_error(msg);
			}
		}
	};
	/**
	 * Returns the current source location.
	 */
	source_location location()
	{
		return { *this };
	}
	/**
	 * Prints a message indicating the location of a parse error.
	 */
	void parse_error(const char *msg);
	/**
	 * Reads the contents of a binary file into `b`.  The file name is assumed
	 * to be relative to one of the include paths.
	 *
	 * Returns true if the file exists and can be read, false otherwise.
	 */
	bool read_binary_file(const std::string &filename, byte_buffer &b);
	private:
	/**
	 * Prints a message indicating the location of a parse error, given a
	 * specified location.  This is used when input has already moved beyond
	 * the location that caused the failure.
	 */
	void parse_error(const char *msg, input_buffer &b, int loc);
};

} // namespace dtc

#endif // !_INPUT_BUFFER_HH_
