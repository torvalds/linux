// SPDX-License-Identifier: GPL-2.0
/*
 * Parser for KFuzzTest textual input format
 *
 * Copyright 2025 Google LLC
 */
#include <asm-generic/errno-base.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input_lexer.h"

struct keyword_map {
	const char *keyword;
	enum token_type type;
};

static struct keyword_map keywords[] = {
	{ "ptr", TOKEN_KEYWORD_PTR }, { "arr", TOKEN_KEYWORD_ARR }, { "len", TOKEN_KEYWORD_LEN },
	{ "str", TOKEN_KEYWORD_STR }, { "u8", TOKEN_KEYWORD_U8 },   { "u16", TOKEN_KEYWORD_U16 },
	{ "u32", TOKEN_KEYWORD_U32 }, { "u64", TOKEN_KEYWORD_U64 },
};

static struct token *make_token(enum token_type type)
{
	struct token *ret = calloc(1, sizeof(*ret));
	ret->type = type;
	return ret;
}

struct lexer {
	const char *start;
	const char *current;
};

static char advance(struct lexer *l)
{
	l->current++;
	return l->current[-1];
}

static void retreat(struct lexer *l)
{
	l->current--;
}

static char peek(struct lexer *l)
{
	return *l->current;
}

static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static bool is_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_whitespace(char c)
{
	switch (c) {
	case ' ':
	case '\r':
	case '\t':
	case '\n':
		return true;
	default:
		return false;
	}
}

static void skip_whitespace(struct lexer *l)
{
	for (;;) {
		if (is_whitespace(peek(l))) {
			advance(l);
		} else {
			return;
		}
	}
}

static struct token *number(struct lexer *l)
{
	struct token *tok;
	uint64_t value;
	while (is_digit(peek(l)))
		advance(l);
	value = strtoull(l->start, NULL, 10);
	tok = make_token(TOKEN_INTEGER);
	tok->data.integer = value;
	return tok;
}

static enum token_type check_keyword(struct lexer *l, const char *keyword, enum token_type type)
{
	size_t len = strlen(keyword);

	if (((size_t)(l->current - l->start) == len) && strncmp(l->start, keyword, len) == 0)
		return type;
	return TOKEN_IDENTIFIER;
}

static struct token *identifier(struct lexer *l)
{
	enum token_type type = TOKEN_IDENTIFIER;
	struct token *tok;
	size_t i;

	while (is_digit(peek(l)) || is_alpha(peek(l)) || peek(l) == '_')
		advance(l);

	for (i = 0; i < ARRAY_SIZE(keywords); i++) {
		if (check_keyword(l, keywords[i].keyword, keywords[i].type) != TOKEN_IDENTIFIER) {
			type = keywords[i].type;
			break;
		}
	}

	tok = make_token(type);
	if (!tok)
		return NULL;
	if (type == TOKEN_IDENTIFIER) {
		tok->data.identifier.start = l->start;
		tok->data.identifier.length = l->current - l->start;
	}
	return tok;
}

static struct token *scan_token(struct lexer *l)
{
	char c;
	skip_whitespace(l);

	l->start = l->current;
	c = peek(l);

	if (c == '\0')
		return make_token(TOKEN_EOF);

	advance(l);
	switch (c) {
	case '{':
		return make_token(TOKEN_LBRACE);
	case '}':
		return make_token(TOKEN_RBRACE);
	case '[':
		return make_token(TOKEN_LBRACKET);
	case ']':
		return make_token(TOKEN_RBRACKET);
	case ',':
		return make_token(TOKEN_COMMA);
	case ';':
		return make_token(TOKEN_SEMICOLON);
	default:
		retreat(l);
		if (is_digit(c))
			return number(l);
		if (is_alpha(c) || c == '_')
			return identifier(l);
		return make_token(TOKEN_ERROR);
	}
}

int primitive_byte_width(enum token_type type)
{
	switch (type) {
	case TOKEN_KEYWORD_U8:
		return 1;
	case TOKEN_KEYWORD_U16:
		return 2;
	case TOKEN_KEYWORD_U32:
		return 4;
	case TOKEN_KEYWORD_U64:
		return 8;
	default:
		return 0;
	}
}

int tokenize(const char *input, struct token ***tokens, size_t *num_tokens)
{
	struct lexer l = { .start = input, .current = input };
	struct token **ret_tokens;
	size_t token_arr_size;
	size_t token_count;
	struct token *tok;
	void *tmp;
	size_t i;
	int err;

	token_arr_size = 128;
	ret_tokens = calloc(token_arr_size, sizeof(struct token *));
	if (!ret_tokens)
		return -ENOMEM;

	token_count = 0;
	do {
		tok = scan_token(&l);
		if (!tok) {
			err = -ENOMEM;
			goto failure;
		}

		if (token_count == token_arr_size) {
			token_arr_size *= 2;
			tmp = realloc(ret_tokens, token_arr_size);
			if (!tmp) {
				err = -ENOMEM;
				goto failure;
			}
			ret_tokens = tmp;
		}

		ret_tokens[token_count] = tok;
		if (tok->type == TOKEN_ERROR) {
			err = -EINVAL;
			goto failure;
		}
		token_count++;
	} while (tok->type != TOKEN_EOF);

	*tokens = ret_tokens;
	*num_tokens = token_count;
	return 0;

failure:
	for (i = 0; i < token_count; i++)
		free(ret_tokens[i]);
	free(ret_tokens);
	return err;
}

bool is_primitive(struct token *tok)
{
	return tok->type >= TOKEN_KEYWORD_U8 && tok->type <= TOKEN_KEYWORD_U64;
}
