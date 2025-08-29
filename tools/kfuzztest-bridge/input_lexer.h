// SPDX-License-Identifier: GPL-2.0
/*
 * Lexer for KFuzzTest textual input format
 *
 * Copyright 2025 Google LLC
 */
#ifndef KFUZZTEST_BRIDGE_INPUT_LEXER_H
#define KFUZZTEST_BRIDGE_INPUT_LEXER_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

enum token_type {
	TOKEN_LBRACE,
	TOKEN_RBRACE,
	TOKEN_LBRACKET,
	TOKEN_RBRACKET,
	TOKEN_COMMA,
	TOKEN_SEMICOLON,

	TOKEN_KEYWORD_PTR,
	TOKEN_KEYWORD_ARR,
	TOKEN_KEYWORD_LEN,
	TOKEN_KEYWORD_STR,
	TOKEN_KEYWORD_U8,
	TOKEN_KEYWORD_U16,
	TOKEN_KEYWORD_U32,
	TOKEN_KEYWORD_U64,

	TOKEN_IDENTIFIER,
	TOKEN_INTEGER,

	TOKEN_EOF,
	TOKEN_ERROR,
};

struct token {
	enum token_type type;
	union {
		uint64_t integer;
		struct {
			const char *start;
			size_t length;
		} identifier;
	} data;
	int position;
};

int tokenize(const char *input, struct token ***tokens, size_t *num_tokens);

bool is_primitive(struct token *tok);
int primitive_byte_width(enum token_type type);

#endif /* KFUZZTEST_BRIDGE_INPUT_LEXER_H */
