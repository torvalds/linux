// SPDX-License-Identifier: GPL-2.0
/*
 * Parser for KFuzzTest textual input format
 *
 * Copyright 2025 Google LLC
 */
#ifndef KFUZZTEST_BRIDGE_INPUT_PARSER_H
#define KFUZZTEST_BRIDGE_INPUT_PARSER_H

#include <stdlib.h>

/* Rounds x up to the nearest multiple of n. */
#define ROUND_UP_TO_MULTIPLE(x, n) ((n) == 0) ? (0) : (((x) + (n) - 1) / (n)) * (n)

enum ast_node_type {
	NODE_PROGRAM,
	NODE_REGION,
	NODE_ARRAY,
	NODE_LENGTH,
	NODE_PRIMITIVE,
	NODE_POINTER,
};

struct ast_node; /* Forward declaration. */

struct ast_program {
	struct ast_node **members;
	size_t num_members;
};

struct ast_region {
	const char *name;
	struct ast_node **members;
	size_t num_members;
};

struct ast_array {
	int elem_size;
	int null_terminated; /* True iff the array should always end with 0. */
	size_t num_elems;
};

struct ast_length {
	size_t byte_width;
	const char *length_of;
};

struct ast_primitive {
	size_t byte_width;
};

struct ast_pointer {
	const char *points_to;
};

struct ast_node {
	enum ast_node_type type;
	union {
		struct ast_program program;
		struct ast_region region;
		struct ast_array array;
		struct ast_length length;
		struct ast_primitive primitive;
		struct ast_pointer pointer;
	} data;
};

struct parser {
	struct token **tokens;
	size_t token_count;
	size_t curr_token;
};

int parse(struct token **tokens, size_t token_count, struct ast_node **node_ret);

size_t node_size(struct ast_node *node);
size_t node_alignment(struct ast_node *node);

#endif /* KFUZZTEST_BRIDGE_INPUT_PARSER_H */
