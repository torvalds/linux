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

#include "input_buffer.hh"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#ifndef NDEBUG
#include <iostream>
#endif


#include <sys/stat.h>
#include <sys/mman.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef MAP_PREFAULT_READ
#define MAP_PREFAULT_READ 0
#endif

using std::string;

namespace
{
/**
 * Subclass of input_buffer that mmap()s a file and owns the resulting memory.
 * When this object is destroyed, the memory is unmapped.
 */
struct mmap_input_buffer : public dtc::input_buffer
{
	string fn;
	const string &filename() const override
	{
		return fn;
	}
	/**
	 * Constructs a new buffer from the file passed in as a file
	 * descriptor.
	 */
	mmap_input_buffer(int fd, string &&filename);
	/**
	 * Unmaps the buffer, if one exists.
	 */
	virtual ~mmap_input_buffer();
};
/**
 * Input buffer read from standard input.  This is used for reading device tree
 * blobs and source from standard input.  It reads the entire input into
 * malloc'd memory, so will be very slow for large inputs.  DTS and DTB files
 * are very rarely more than 10KB though, so this is probably not a problem.
 */
struct stream_input_buffer : public dtc::input_buffer
{
	const string &filename() const override
	{
		static string n = "<standard input>";
		return n;
	}
	/**
	 * The buffer that will store the data read from the standard input.
	 */
	std::vector<char> b;
	/**
	 * Constructs a new buffer from the standard input.
	 */
	stream_input_buffer();
};

mmap_input_buffer::mmap_input_buffer(int fd, string &&filename)
	: input_buffer(0, 0), fn(filename)
{
	struct stat sb;
	if (fstat(fd, &sb))
	{
		perror("Failed to stat file");
	}
	size = sb.st_size;
	buffer = (const char*)mmap(0, size, PROT_READ, MAP_PRIVATE |
			MAP_PREFAULT_READ, fd, 0);
	if (buffer == MAP_FAILED)
	{
		perror("Failed to mmap file");
		exit(EXIT_FAILURE);
	}
}

mmap_input_buffer::~mmap_input_buffer()
{
	if (buffer != 0)
	{
		munmap((void*)buffer, size);
	}
}

stream_input_buffer::stream_input_buffer() : input_buffer(0, 0)
{
	int c;
	while ((c = fgetc(stdin)) != EOF)
	{
		b.push_back(c);
	}
	buffer = b.data();
	size = b.size();
}

} // Anonymous namespace


namespace dtc
{

void
input_buffer::skip_to(char c)
{
	while ((cursor < size) && (buffer[cursor] != c))
	{
		cursor++;
	}
}

void
text_input_buffer::skip_to(char c)
{
	while (!finished() && (*(*this) != c))
	{
		++(*this);
	}
}

void
text_input_buffer::skip_spaces()
{
	if (finished()) { return; }
	char c = *(*this);
	bool last_nl = false;
	while ((c == ' ') || (c == '\t') || (c == '\n') || (c == '\f')
	       || (c == '\v') || (c == '\r'))
	{
		last_nl = ((c == '\n') || (c == '\r'));
		++(*this);
		if (finished())
		{
			c = '\0';
		}
		else
		{
			c = *(*this);
		}
	}
	// Skip C preprocessor leftovers
	if ((c == '#') && ((cursor == 0) || last_nl))
	{
		skip_to('\n');
		skip_spaces();
	}
	if (consume("/include/"))
	{
		handle_include();
		skip_spaces();
	}
}

void
text_input_buffer::handle_include()
{
	bool reallyInclude = true;
	if (consume("if "))
	{
		next_token();
		string name = parse_property_name();
		if (defines.count(name) == 0)
		{
			reallyInclude = false;
		}
		consume('/');
	}
	next_token();
	if (!consume('"'))
	{
		parse_error("Expected quoted filename");
		return;
	}
	auto loc = location();
	string file = parse_to('"');
	consume('"');
	if (!reallyInclude)
	{
		return;
	}
	string include_file = dir + '/' + file;
	auto include_buffer = input_buffer::buffer_for_file(include_file, false);
	if (include_buffer == 0)
	{
		for (auto i : include_paths)
		{
			include_file = i + '/' + file;
			include_buffer = input_buffer::buffer_for_file(include_file, false);
			if (include_buffer != 0)
			{
				break;
			}
		}
	}
	if (depfile)
	{
		putc(' ', depfile);
		fputs(include_file.c_str(), depfile);
	}
	if (!include_buffer)
	{
		loc.report_error("Unable to locate input file");
		return;
	}
	input_stack.push(std::move(include_buffer));
}

bool text_input_buffer::read_binary_file(const std::string &filename, byte_buffer &b)
{
	bool try_include_paths = true;
	string include_file;
	if (filename[0] == '/')
	{
		include_file = filename;
		// Don't try include paths if we're given an absolute path.
		// Failing is better so that we don't accidentally do the wrong thing,
		// but make it seem like everything is alright.
		try_include_paths = false;
	}
	else
	{
		include_file = dir + '/' + filename;
	}
	auto include_buffer = input_buffer::buffer_for_file(include_file, false);
	if (include_buffer == 0 && try_include_paths)
	{
		for (auto i : include_paths)
		{
			include_file = i + '/' + filename;
			include_buffer = input_buffer::buffer_for_file(include_file, false);
			if (include_buffer != 0)
			{
				break;
			}
		}
	}
	if (!include_buffer)
	{
		return false;
	}
	if (depfile)
	{
		putc(' ', depfile);
		fputs(include_file.c_str(), depfile);
	}
	b.insert(b.begin(), include_buffer->begin(), include_buffer->end());
	return true;
}

input_buffer
input_buffer::buffer_from_offset(int offset, int s)
{
	if (offset < 0)
	{
		return input_buffer();
	}
	if (s == 0)
	{
		s = size - offset;
	}
	if (offset > size)
	{
		return input_buffer();
	}
	if (s > (size-offset))
	{
		return input_buffer();
	}
	return input_buffer(&buffer[offset], s);
}

bool
input_buffer::consume(const char *str)
{
	int len = strlen(str);
	if (len > size - cursor)
	{
		return false;
	}
	else
	{
		for (int i=0 ; i<len ; ++i)
		{
			if (str[i] != (*this)[i])
			{
				return false;
			}
		}
		cursor += len;
		return true;
	}
	return false;
}

bool
input_buffer::consume_integer(unsigned long long &outInt)
{
	// The first character must be a digit.  Hex and octal strings
	// are prefixed by 0 and 0x, respectively.
	if (!isdigit((*this)[0]))
	{
		return false;
	}
	char *end= const_cast<char*>(&buffer[size]);
	outInt = strtoull(&buffer[cursor], &end, 0);
	if (end == &buffer[cursor])
	{
		return false;
	}
	cursor = end - buffer;
	return true;
}

namespace {

/**
 * Convenience typedef for the type that we use for all values.
 */
typedef unsigned long long valty;

/**
 * Expression tree currently being parsed.
 */
struct expression
{
	typedef text_input_buffer::source_location source_location;
	/**
	 * The type that is returned when computing the result.  The boolean value
	 * indicates whether this is a valid expression.
	 *
	 * FIXME: Once we can use C++17, this should be `std::optional`.
	 */
	typedef std::pair<valty, bool> result;
	/**
	 * Evaluate this node, taking into account operator precedence.
	 */
	virtual result operator()() = 0;
	/**
	 * Returns the precedence of this node.  Lower values indicate higher
	 * precedence.
	 */
	virtual int precedence() = 0;
	/**
	 * Constructs an expression, storing the location where it was created.
	 */
	expression(source_location l) : loc(l) {}
	virtual ~expression() {}
#ifndef NDEBUG
	/**
	 * Dumps this expression to `std::cerr`, appending a newline if `nl` is
	 * `true`.
	 */
	void dump(bool nl=false)
	{
		void *ptr = this;
		if (ptr == nullptr)
		{
			std::cerr << "{nullptr}\n";
			return;
		}
		dump_impl();
		if (nl)
		{
			std::cerr << '\n';
		}
	}
	private:
	/**
	 * Method that sublcasses override to implement the behaviour of `dump()`.
	 */
	virtual void dump_impl() = 0;
#endif
	protected:
	source_location loc;
};

/**
 * Expression wrapping a single integer.  Leaf nodes in the expression tree.
 */
class terminal_expr : public expression
{
	/**
	 * The value that this wraps.
	 */
	valty val;
	/**
	 * Evaluate.  Trivially returns the value that this class wraps.
	 */
	result operator()() override
	{
		return {val, true};
	}
	int precedence() override
	{
		return 0;
	}
	public:
	/**
	 * Constructor.
	 */
	terminal_expr(source_location l, valty v) : expression(l), val(v) {}
#ifndef NDEBUG
	void dump_impl() override { std::cerr << val; }
#endif
};

/**
 * Parenthetical expression.  Exists to make the contents opaque.
 */
struct paren_expression : public expression
{
	/**
	 * The expression within the parentheses.
	 */
	expression_ptr subexpr;
	/**
	 * Constructor.  Takes the child expression as the only argument.
	 */
	paren_expression(source_location l, expression_ptr p) : expression(l),
	subexpr(std::move(p)) {}
	int precedence() override
	{
		return 0;
	}
	/**
	 * Evaluate - just forwards to the underlying expression.
	 */
	result operator()() override
	{
		return (*subexpr)();
	}
#ifndef NDEBUG
	void dump_impl() override
	{
		std::cerr << " (";
		subexpr->dump();
		std::cerr << ") ";
	}
#endif
};

/**
 * Template class for unary operators.  The `OpChar` template parameter is
 * solely for debugging and makes it easy to print the expression.  The `Op`
 * template parameter is a function object that implements the operator that
 * this class provides.  Most of these are provided by the `<functional>`
 * header.
 */
template<char OpChar, class Op>
class unary_operator : public expression
{
	/**
	 * The subexpression for this unary operator.
	 */
	expression_ptr subexpr;
	result operator()() override
	{
		Op op;
		result s = (*subexpr)();
		if (!s.second)
		{
			return s;
		}
		return {op(s.first), true};
	}
	/**
	 * All unary operators have the same precedence.  They are all evaluated
	 * before binary expressions, but after parentheses.
	 */
	int precedence() override
	{
		return 3;
	}
	public:
	unary_operator(source_location l, expression_ptr p) :
		expression(l), subexpr(std::move(p)) {}
#ifndef NDEBUG
	void dump_impl() override
	{
		std::cerr << OpChar;
		subexpr->dump();
	}
#endif
};

/**
 * Abstract base class for binary operators.  Allows the tree to be modified
 * without knowing what the operations actually are.
 */
struct binary_operator_base : public expression
{
	using expression::expression;
	/**
	 * The left side of the expression.
	 */
	expression_ptr lhs;
	/**
	 * The right side of the expression.
	 */
	expression_ptr rhs;
	/**
	 * Insert a node somewhere down the path of left children, until it would
	 * be preempting something that should execute first.
	 */
	void insert_left(binary_operator_base *new_left)
	{
		if (lhs->precedence() < new_left->precedence())
		{
			new_left->rhs = std::move(lhs);
			lhs.reset(new_left);
		}
		else
		{
			static_cast<binary_operator_base*>(lhs.get())->insert_left(new_left);
		}
	}
};

/**
 * Template class for binary operators.  The precedence and the operation are
 * provided as template parameters.
 */
template<int Precedence, class Op>
struct binary_operator : public binary_operator_base
{
	result operator()() override
	{
		Op op;
		result l = (*lhs)();
		result r = (*rhs)();
		if (!(l.second && r.second))
		{
			return {0, false};
		}
		return {op(l.first, r.first), true};
	}
	int precedence() override
	{
		return Precedence;
	}
#ifdef NDEBUG
	/**
	 * Constructor.  Takes the name of the operator as an argument, for
	 * debugging.  Only stores it in debug mode.
	 */
	binary_operator(source_location l, const char *) :
		binary_operator_base(l) {}
#else
	const char *opName;
	binary_operator(source_location l, const char *o) :
		binary_operator_base(l), opName(o) {}
	void dump_impl() override
	{
		lhs->dump();
		std::cerr << opName;
		rhs->dump();
	}
#endif
};

/**
 * Ternary conditional operators (`cond ? true : false`) are a special case -
 * there are no other ternary operators.
 */
class ternary_conditional_operator : public expression
{
	/**
	 * The condition for the clause.
	 */
	expression_ptr cond;
	/**
	 * The expression that this evaluates to if the condition is true.
	 */
	expression_ptr lhs;
	/**
	 * The expression that this evaluates to if the condition is false.
	 */
	expression_ptr rhs;
	result operator()() override
	{
		result c = (*cond)();
		result l = (*lhs)();
		result r = (*rhs)();
		if (!(l.second && r.second && c.second))
		{
			return {0, false};
		}
		return c.first ? l : r;
	}
	int precedence() override
	{
		// The actual precedence of a ternary conditional operator is 15, but
		// its associativity is the opposite way around to the other operators,
		// so we fudge it slightly.
		return 3;
	}
#ifndef NDEBUG
	void dump_impl() override
	{
		cond->dump();
		std::cerr << " ? ";
		lhs->dump();
		std::cerr << " : ";
		rhs->dump();
	}
#endif
	public:
	ternary_conditional_operator(source_location sl,
	                             expression_ptr c,
	                             expression_ptr l,
	                             expression_ptr r) :
		expression(sl), cond(std::move(c)), lhs(std::move(l)),
		rhs(std::move(r)) {}
};

template<typename T>
struct lshift
{
	constexpr T operator()(const T &lhs, const T &rhs) const
	{
		return lhs << rhs;
	}
};
template<typename T>
struct rshift
{
	constexpr T operator()(const T &lhs, const T &rhs) const
	{
		return lhs >> rhs;
	}
};
template<typename T>
struct unary_plus
{
	constexpr T operator()(const T &val) const
	{
		return +val;
	}
};
// TODO: Replace with std::bit_not once we can guarantee C++14 as a baseline.
template<typename T>
struct bit_not
{
	constexpr T operator()(const T &val) const
	{
		return ~val;
	}
};

template<typename T>
struct divmod : public binary_operator<5, T>
{
	using binary_operator<5, T>::binary_operator;
	using typename binary_operator_base::result;
	result operator()() override
	{
		result r = (*binary_operator_base::rhs)();
		if (r.second && (r.first == 0))
		{
			expression::loc.report_error("Division by zero");
			return {0, false};
		}
		return binary_operator<5, T>::operator()();
	}
};

} // anonymous namespace


expression_ptr text_input_buffer::parse_binary_expression(expression_ptr lhs)
{
	next_token();
	binary_operator_base *expr = nullptr;
	char op = *(*this);
	source_location l = location();
	switch (op)
	{
		default:
			return lhs;
		case '+':
			expr = new binary_operator<6, std::plus<valty>>(l, "+");
			break;
		case '-':
			expr = new binary_operator<6, std::minus<valty>>(l, "-");
			break;
		case '%':
			expr = new divmod<std::modulus<valty>>(l, "/");
			break;
		case '*':
			expr = new binary_operator<5, std::multiplies<valty>>(l, "*");
			break;
		case '/':
			expr = new divmod<std::divides<valty>>(l, "/");
			break;
		case '<':
			switch (peek())
			{
				default:
					parse_error("Invalid operator");
					return nullptr;
				case ' ':
				case '(':
				case '0'...'9':
					expr = new binary_operator<8, std::less<valty>>(l, "<");
					break;
				case '=':
					++(*this);
					expr = new binary_operator<8, std::less_equal<valty>>(l, "<=");
					break;
				case '<':
					++(*this);
					expr = new binary_operator<7, lshift<valty>>(l, "<<");
					break;
			}
			break;
		case '>':
			switch (peek())
			{
				default:
					parse_error("Invalid operator");
					return nullptr;
				case '(':
				case ' ':
				case '0'...'9':
					expr = new binary_operator<8, std::greater<valty>>(l, ">");
					break;
				case '=':
					++(*this);
					expr = new binary_operator<8, std::greater_equal<valty>>(l, ">=");
					break;
				case '>':
					++(*this);
					expr = new binary_operator<7, rshift<valty>>(l, ">>");
					break;
					return lhs;
			}
			break;
		case '=':
			if (peek() != '=')
			{
				parse_error("Invalid operator");
				return nullptr;
			}
			expr = new binary_operator<9, std::equal_to<valty>>(l, "==");
			break;
		case '!':
			if (peek() != '=')
			{
				parse_error("Invalid operator");
				return nullptr;
			}
			cursor++;
			expr = new binary_operator<9, std::not_equal_to<valty>>(l, "!=");
			break;
		case '&':
			if (peek() == '&')
			{
				expr = new binary_operator<13, std::logical_and<valty>>(l, "&&");
			}
			else
			{
				expr = new binary_operator<10, std::bit_and<valty>>(l, "&");
			}
			break;
		case '|':
			if (peek() == '|')
			{
				expr = new binary_operator<12, std::logical_or<valty>>(l, "||");
			}
			else
			{
				expr = new binary_operator<14, std::bit_or<valty>>(l, "|");
			}
			break;
		case '?':
		{
			consume('?');
			expression_ptr true_case = parse_expression();
			next_token();
			if (!true_case || !consume(':'))
			{
				parse_error("Expected : in ternary conditional operator");
				return nullptr;
			}
			expression_ptr false_case = parse_expression();
			if (!false_case)
			{
				parse_error("Expected false condition for ternary operator");
				return nullptr;
			}
			return expression_ptr(new ternary_conditional_operator(l, std::move(lhs),
						std::move(true_case), std::move(false_case)));
		}
	}
	++(*this);
	next_token();
	expression_ptr e(expr);
	expression_ptr rhs(parse_expression());
	if (!rhs)
	{
		return nullptr;
	}
	expr->lhs = std::move(lhs);
	if (rhs->precedence() < expr->precedence())
	{
		expr->rhs = std::move(rhs);
	}
	else
	{
		// If we're a normal left-to-right expression, then we need to insert
		// this as the far-left child node of the rhs expression
		binary_operator_base *rhs_op =
			static_cast<binary_operator_base*>(rhs.get());
		rhs_op->insert_left(expr);
		e.release();
		return rhs;
	}
	return e;
}

expression_ptr text_input_buffer::parse_expression(bool stopAtParen)
{
	next_token();
	unsigned long long leftVal;
	expression_ptr lhs;
	source_location l = location();
	switch (*(*this))
	{
		case '0'...'9':
			if (!consume_integer(leftVal))
			{
				return nullptr;
			}
			lhs.reset(new terminal_expr(l, leftVal));
			break;
		case '(':
		{
			consume('(');
			expression_ptr &&subexpr = parse_expression();
			if (!subexpr)
			{
				return nullptr;
			}
			lhs.reset(new paren_expression(l, std::move(subexpr)));
			if (!consume(')'))
			{
				return nullptr;
			}
			if (stopAtParen)
			{
				return lhs;
			}
			break;
		}
		case '+':
		{
			consume('+');
			expression_ptr &&subexpr = parse_expression();
			if (!subexpr)
			{
				return nullptr;
			}
			lhs.reset(new unary_operator<'+', unary_plus<valty>>(l, std::move(subexpr)));
			break;
		}
		case '-':
		{
			consume('-');
			expression_ptr &&subexpr = parse_expression();
			if (!subexpr)
			{
				return nullptr;
			}
			lhs.reset(new unary_operator<'-', std::negate<valty>>(l, std::move(subexpr)));
			break;
		}
		case '!':
		{
			consume('!');
			expression_ptr &&subexpr = parse_expression();
			if (!subexpr)
			{
				return nullptr;
			}
			lhs.reset(new unary_operator<'!', std::logical_not<valty>>(l, std::move(subexpr)));
			break;
		}
		case '~':
		{
			consume('~');
			expression_ptr &&subexpr = parse_expression();
			if (!subexpr)
			{
				return nullptr;
			}
			lhs.reset(new unary_operator<'~', bit_not<valty>>(l, std::move(subexpr)));
			break;
		}
	}
	if (!lhs)
	{
		return nullptr;
	}
	return parse_binary_expression(std::move(lhs));
}

bool
text_input_buffer::consume_integer_expression(unsigned long long &outInt)
{
	switch (*(*this))
	{
		case '(':
		{
			expression_ptr e(parse_expression(true));
			if (!e)
			{
				return false;
			}
			auto r = (*e)();
			if (r.second)
			{
				outInt = r.first;
				return true;
			}
			return false;
		}
		case '0'...'9':
			return consume_integer(outInt);
		default:
			return false;
	}
}

bool
input_buffer::consume_hex_byte(uint8_t &outByte)
{
	if (!ishexdigit((*this)[0]) && !ishexdigit((*this)[1]))
	{
		return false;
	}
	outByte = (digittoint((*this)[0]) << 4) | digittoint((*this)[1]);
	cursor += 2;
	return true;
}

text_input_buffer&
text_input_buffer::next_token()
{
	auto &self = *this;
	int start;
	do {
		start = cursor;
		skip_spaces();
		if (finished())
		{
			return self;
		}
		// Parse /* comments
		if (*self == '/' && peek() == '*')
		{
			// eat the start of the comment
			++self;
			++self;
			do {
				// Find the ending * of */
				while ((*self != '\0') && (*self != '*') && !finished())
				{
					++self;
				}
				// Eat the *
				++self;
			} while ((*self != '\0') && (*self != '/') && !finished());
			// Eat the /
			++self;
		}
		// Parse // comments
		if ((*self == '/' && peek() == '/'))
		{
			// eat the start of the comment
			++self;
			++self;
			// Find the ending of the line
			while (*self != '\n' && !finished())
			{
				++self;
			}
			// Eat the \n
			++self;
		}
	} while (start != cursor);
	return self;
}

void
text_input_buffer::parse_error(const char *msg)
{
	if (input_stack.empty())
	{
		fprintf(stderr, "Error: %s\n", msg);
		return;
	}
	input_buffer &b = *input_stack.top();
	parse_error(msg, b, b.cursor);
}
void
text_input_buffer::parse_error(const char *msg,
                               input_buffer &b,
                               int loc)
{
	int line_count = 1;
	int line_start = 0;
	int line_end = loc;
	if (loc < 0 || loc > b.size)
	{
		return;
	}
	for (int i=loc ; i>0 ; --i)
	{
		if (b.buffer[i] == '\n')
		{
			line_count++;
			if (line_start == 0)
			{
				line_start = i+1;
			}
		}
	}
	for (int i=loc+1 ; i<b.size ; ++i)
	{
		if (b.buffer[i] == '\n')
		{
			line_end = i;
			break;
		}
	}
	fprintf(stderr, "Error at %s:%d:%d: %s\n", b.filename().c_str(), line_count, loc - line_start, msg);
	fwrite(&b.buffer[line_start], line_end-line_start, 1, stderr);
	putc('\n', stderr);
	for (int i=0 ; i<(loc-line_start) ; ++i)
	{
		char c = (b.buffer[i+line_start] == '\t') ? '\t' : ' ';
		putc(c, stderr);
	}
	putc('^', stderr);
	putc('\n', stderr);
}
#ifndef NDEBUG
void
input_buffer::dump()
{
	fprintf(stderr, "Current cursor: %d\n", cursor);
	fwrite(&buffer[cursor], size-cursor, 1, stderr);
}
#endif


namespace
{
/**
 * The source files are ASCII, so we provide a non-locale-aware version of
 * isalpha.  This is a class so that it can be used with a template function
 * for parsing strings.
 */
struct is_alpha
{
	static inline bool check(const char c)
	{
		return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') &&
			(c <= 'Z'));
	}
};
/**
 * Check whether a character is in the set allowed for node names.  This is a
 * class so that it can be used with a template function for parsing strings.
 */
struct is_node_name_character
{
	static inline bool check(const char c)
	{
		switch(c)
		{
			default:
				return false;
			case 'a'...'z': case 'A'...'Z': case '0'...'9':
			case ',': case '.': case '+': case '-':
			case '_':
				return true;
		}
	}
};
/**
 * Check whether a character is in the set allowed for property names.  This is
 * a class so that it can be used with a template function for parsing strings.
 */
struct is_property_name_character
{
	static inline bool check(const char c)
	{
		switch(c)
		{
			default:
				return false;
			case 'a'...'z': case 'A'...'Z': case '0'...'9':
			case ',': case '.': case '+': case '-':
			case '_': case '#':
				return true;
		}
	}
};

template<class T>
string parse(text_input_buffer &s)
{
	std::vector<char> bytes;
	for (char c=*s ; T::check(c) ; c=*(++s))
	{
		bytes.push_back(c);
	}
	return string(bytes.begin(), bytes.end());
}

}

string
text_input_buffer::parse_node_name()
{
	return parse<is_node_name_character>(*this);
}

string
text_input_buffer::parse_property_name()
{
	return parse<is_property_name_character>(*this);
}

string
text_input_buffer::parse_node_or_property_name(bool &is_property)
{
	if (is_property)
	{
		return parse_property_name();
	}
	std::vector<char> bytes;
	for (char c=*(*this) ; is_node_name_character::check(c) ; c=*(++(*this)))
	{
		bytes.push_back(c);
	}
	for (char c=*(*this) ; is_property_name_character::check(c) ; c=*(++(*this)))
	{
		bytes.push_back(c);
		is_property = true;
	}
	return string(bytes.begin(), bytes.end());
}

string
input_buffer::parse_to(char stop)
{
	std::vector<char> bytes;
	for (char c=*(*this) ; c != stop ; c=*(++(*this)))
	{
		bytes.push_back(c);
	}
	return string(bytes.begin(), bytes.end());
}

string
text_input_buffer::parse_to(char stop)
{
	std::vector<char> bytes;
	for (char c=*(*this) ; c != stop ; c=*(++(*this)))
	{
		if (finished())
		{
			break;
		}
		bytes.push_back(c);
	}
	return string(bytes.begin(), bytes.end());
}

char
text_input_buffer::peek()
{
	return (*input_stack.top())[1];
}

std::unique_ptr<input_buffer>
input_buffer::buffer_for_file(const string &path, bool warn)
{
	if (path == "-")
	{
		std::unique_ptr<input_buffer> b(new stream_input_buffer());
		return b;
	}
	int source = open(path.c_str(), O_RDONLY);
	if (source == -1)
	{
		if (warn)
		{
			fprintf(stderr, "Unable to open file '%s'.  %s\n", path.c_str(), strerror(errno));
		}
		return 0;
	}
	struct stat st;
	if (fstat(source, &st) == 0 && S_ISDIR(st.st_mode))
	{
		if (warn)
		{
			fprintf(stderr, "File %s is a directory\n", path.c_str());
		}
		close(source);
		return 0;
	}
	std::unique_ptr<input_buffer> b(new mmap_input_buffer(source, string(path)));
	close(source);
	return b;
}

} // namespace dtc

