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

#include <string>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctype.h>
#include <libgen.h>

#include "util.hh"

using std::string;

namespace dtc
{

void
push_string(byte_buffer &buffer, const string &s, bool escapes)
{
	size_t length = s.size();
	for (size_t i=0 ; i<length ; ++i)
	{
		uint8_t c = s[i];
		if (escapes && c == '\\' && i+1 < length)
		{
			c = s[++i];
			switch (c)
			{
				// For now, we just ignore invalid escape sequences.
				default:
				case '"':
				case '\'':
				case '\\':
					break;
				case 'a':
					c = '\a';
					break;
				case 'b':
					c = '\b';
					break;
				case 't':
					c = '\t';
					break;
				case 'n':
					c = '\n';
					break;
				case 'v':
					c = '\v';
					break;
				case 'f':
					c = '\f';
					break;
				case 'r':
					c = '\r';
					break;
				case '0'...'7':
				{
					int v = digittoint(c);
					if (i+1 < length && s[i+1] <= '7' && s[i+1] >= '0')
					{
						v <<= 3;
						v |= digittoint(s[i+1]);
						i++;
						if (i+1 < length && s[i+1] <= '7' && s[i+1] >= '0')
						{
							v <<= 3;
							v |= digittoint(s[i+1]);
						}
					}
					c = (uint8_t)v;
					break;
				}
				case 'x':
				{
					++i;
					if (i >= length)
					{
						break;
					}
					int v = digittoint(s[i]);
					if (i+1 < length && ishexdigit(s[i+1]))
					{
						v <<= 4;
						v |= digittoint(s[++i]);
					}
					c = (uint8_t)v;
					break;
				}
			}
		}
		buffer.push_back(c);
	}
}

namespace {
string
dirbasename(std::function<char*(char*)> fn, const string &s)
{
	if (s == string())
	{
		return string();
	}
	std::unique_ptr<char, decltype(free)*> str = {strdup(s.c_str()), free};
	string dn(fn(str.get()));
	return dn;
}
}

string dirname(const string &s)
{
	return dirbasename(::dirname, s);
}

string basename(const string &s)
{
	return dirbasename(::basename, s);
}
} // namespace dtc

