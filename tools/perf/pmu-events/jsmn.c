/*
 * Copyright (c) 2010 Serge A. Zaitsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Slightly modified by AK to not assume 0 terminated input.
 */

#include <stdlib.h>
#include "jsmn.h"

/*
 * Allocates a fresh unused token from the token pool.
 */
static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser,
				   jsmntok_t *tokens, size_t num_tokens)
{
	jsmntok_t *tok;

	if ((unsigned)parser->toknext >= num_tokens)
		return NULL;
	tok = &tokens[parser->toknext++];
	tok->start = tok->end = -1;
	tok->size = 0;
	return tok;
}

/*
 * Fills token type and boundaries.
 */
static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type,
			    int start, int end)
{
	token->type = type;
	token->start = start;
	token->end = end;
	token->size = 0;
}

/*
 * Fills next available token with JSON primitive.
 */
static jsmnerr_t jsmn_parse_primitive(jsmn_parser *parser, const char *js,
				      size_t len,
				      jsmntok_t *tokens, size_t num_tokens)
{
	jsmntok_t *token;
	int start;

	start = parser->pos;

	for (; parser->pos < len; parser->pos++) {
		switch (js[parser->pos]) {
#ifndef JSMN_STRICT
		/*
		 * In strict mode primitive must be followed by ","
		 * or "}" or "]"
		 */
		case ':':
#endif
		case '\t':
		case '\r':
		case '\n':
		case ' ':
		case ',':
		case ']':
		case '}':
			goto found;
		default:
			break;
		}
		if (js[parser->pos] < 32 || js[parser->pos] >= 127) {
			parser->pos = start;
			return JSMN_ERROR_INVAL;
		}
	}
#ifdef JSMN_STRICT
	/*
	 * In strict mode primitive must be followed by a
	 * comma/object/array.
	 */
	parser->pos = start;
	return JSMN_ERROR_PART;
#endif

found:
	token = jsmn_alloc_token(parser, tokens, num_tokens);
	if (token == NULL) {
		parser->pos = start;
		return JSMN_ERROR_NOMEM;
	}
	jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
	parser->pos--; /* parent sees closing brackets */
	return JSMN_SUCCESS;
}

/*
 * Fills next token with JSON string.
 */
static jsmnerr_t jsmn_parse_string(jsmn_parser *parser, const char *js,
				   size_t len,
				   jsmntok_t *tokens, size_t num_tokens)
{
	jsmntok_t *token;
	int start = parser->pos;

	/* Skip starting quote */
	parser->pos++;

	for (; parser->pos < len; parser->pos++) {
		char c = js[parser->pos];

		/* Quote: end of string */
		if (c == '\"') {
			token = jsmn_alloc_token(parser, tokens, num_tokens);
			if (token == NULL) {
				parser->pos = start;
				return JSMN_ERROR_NOMEM;
			}
			jsmn_fill_token(token, JSMN_STRING, start+1,
					parser->pos);
			return JSMN_SUCCESS;
		}

		/* Backslash: Quoted symbol expected */
		if (c == '\\') {
			parser->pos++;
			switch (js[parser->pos]) {
				/* Allowed escaped symbols */
			case '\"':
			case '/':
			case '\\':
			case 'b':
			case 'f':
			case 'r':
			case 'n':
			case 't':
				break;
				/* Allows escaped symbol \uXXXX */
			case 'u':
				/* TODO */
				break;
				/* Unexpected symbol */
			default:
				parser->pos = start;
				return JSMN_ERROR_INVAL;
			}
		}
	}
	parser->pos = start;
	return JSMN_ERROR_PART;
}

/*
 * Parse JSON string and fill tokens.
 */
jsmnerr_t jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
		     jsmntok_t *tokens, unsigned int num_tokens)
{
	jsmnerr_t r;
	int i;
	jsmntok_t *token;

	for (; parser->pos < len; parser->pos++) {
		char c;
		jsmntype_t type;

		c = js[parser->pos];
		switch (c) {
		case '{':
		case '[':
			token = jsmn_alloc_token(parser, tokens, num_tokens);
			if (token == NULL)
				return JSMN_ERROR_NOMEM;
			if (parser->toksuper != -1)
				tokens[parser->toksuper].size++;
			token->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
			token->start = parser->pos;
			parser->toksuper = parser->toknext - 1;
			break;
		case '}':
		case ']':
			type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
			for (i = parser->toknext - 1; i >= 0; i--) {
				token = &tokens[i];
				if (token->start != -1 && token->end == -1) {
					if (token->type != type)
						return JSMN_ERROR_INVAL;
					parser->toksuper = -1;
					token->end = parser->pos + 1;
					break;
				}
			}
			/* Error if unmatched closing bracket */
			if (i == -1)
				return JSMN_ERROR_INVAL;
			for (; i >= 0; i--) {
				token = &tokens[i];
				if (token->start != -1 && token->end == -1) {
					parser->toksuper = i;
					break;
				}
			}
			break;
		case '\"':
			r = jsmn_parse_string(parser, js, len, tokens,
					      num_tokens);
			if (r < 0)
				return r;
			if (parser->toksuper != -1)
				tokens[parser->toksuper].size++;
			break;
		case '\t':
		case '\r':
		case '\n':
		case ':':
		case ',':
		case ' ':
			break;
#ifdef JSMN_STRICT
			/*
			 * In strict mode primitives are:
			 * numbers and booleans.
			 */
		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case 't':
		case 'f':
		case 'n':
#else
			/*
			 * In non-strict mode every unquoted value
			 * is a primitive.
			 */
			/*FALL THROUGH */
		default:
#endif
			r = jsmn_parse_primitive(parser, js, len, tokens,
						 num_tokens);
			if (r < 0)
				return r;
			if (parser->toksuper != -1)
				tokens[parser->toksuper].size++;
			break;

#ifdef JSMN_STRICT
			/* Unexpected char in strict mode */
		default:
			return JSMN_ERROR_INVAL;
#endif
		}
	}

	for (i = parser->toknext - 1; i >= 0; i--) {
		/* Unmatched opened object or array */
		if (tokens[i].start != -1 && tokens[i].end == -1)
			return JSMN_ERROR_PART;
	}

	return JSMN_SUCCESS;
}

/*
 * Creates a new parser based over a given  buffer with an array of tokens
 * available.
 */
void jsmn_init(jsmn_parser *parser)
{
	parser->pos = 0;
	parser->toknext = 0;
	parser->toksuper = -1;
}

const char *jsmn_strerror(jsmnerr_t err)
{
	switch (err) {
	case JSMN_ERROR_NOMEM:
		return "No enough tokens";
	case JSMN_ERROR_INVAL:
		return "Invalid character inside JSON string";
	case JSMN_ERROR_PART:
		return "The string is not a full JSON packet, more bytes expected";
	case JSMN_SUCCESS:
		return "Success";
	default:
		return "Unknown json error";
	}
}
