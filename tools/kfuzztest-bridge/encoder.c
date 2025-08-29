// SPDX-License-Identifier: GPL-2.0
/*
 * Encoder for KFuzzTest binary input format
 *
 * Copyright 2025 Google LLC
 */
#include <asm-generic/errno-base.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "byte_buffer.h"
#include "input_parser.h"
#include "rand_stream.h"

#define KFUZZTEST_MAGIC 0xBFACE
#define KFUZZTEST_PROTO_VERSION 0
/* 
 * The KFuzzTest binary input format requires at least 8 bytes of padding
 * at the head and tail of every region.
 */
#define KFUZZTEST_POISON_SIZE 8

#define BUFSIZE_SMALL 32
#define BUFSIZE_LARGE 128

struct region_info {
	const char *name;
	uint32_t offset;
	uint32_t size;
};

struct reloc_info {
	uint32_t src_reg;
	uint32_t offset;
	uint32_t dst_reg;
};

struct encoder_ctx {
	struct byte_buffer *payload;
	struct rand_stream *rand;

	struct region_info *regions;
	size_t num_regions;

	struct reloc_info *relocations;
	size_t num_relocations;

	size_t reg_offset;
	int curr_reg;
};

static void cleanup_ctx(struct encoder_ctx *ctx)
{
	if (ctx->regions)
		free(ctx->regions);
	if (ctx->relocations)
		free(ctx->relocations);
	if (ctx->payload)
		destroy_byte_buffer(ctx->payload);
}

static int pad_payload(struct encoder_ctx *ctx, size_t amount)
{
	int ret;

	if ((ret = pad(ctx->payload, amount)))
		return ret;
	ctx->reg_offset += amount;
	return 0;
}

static int align_payload(struct encoder_ctx *ctx, size_t alignment)
{
	size_t pad_amount = ROUND_UP_TO_MULTIPLE(ctx->payload->num_bytes, alignment) - ctx->payload->num_bytes;
	return pad_payload(ctx, pad_amount);
}

static int lookup_reg(struct encoder_ctx *ctx, const char *name)
{
	size_t i;

	for (i = 0; i < ctx->num_regions; i++) {
		if (strcmp(ctx->regions[i].name, name) == 0)
			return i;
	}
	return -ENOENT;
}

static int add_reloc(struct encoder_ctx *ctx, struct reloc_info reloc)
{
	void *new_ptr = realloc(ctx->relocations, (ctx->num_relocations + 1) * sizeof(struct reloc_info));
	if (!new_ptr)
		return -ENOMEM;

	ctx->relocations = new_ptr;
	ctx->relocations[ctx->num_relocations] = reloc;
	ctx->num_relocations++;
	return 0;
}

static int build_region_map(struct encoder_ctx *ctx, struct ast_node *top_level)
{
	struct ast_program *prog;
	struct ast_node *reg;
	size_t i;

	if (top_level->type != NODE_PROGRAM)
		return -EINVAL;

	prog = &top_level->data.program;
	ctx->regions = malloc(prog->num_members * sizeof(struct region_info));
	if (!ctx->regions)
		return -ENOMEM;

	ctx->num_regions = prog->num_members;
	for (i = 0; i < ctx->num_regions; i++) {
		reg = prog->members[i];
		/* Offset can only be determined after the second pass. */
		ctx->regions[i] = (struct region_info){
			.name = reg->data.region.name,
			.size = node_size(reg),
		};
	}
	return 0;
}
/**
 * Encodes a value node as little-endian. A value node is one that can be
 * directly written, i.e. a primitive, a pointer, or an array.
 */
static int encode_value_le(struct encoder_ctx *ctx, struct ast_node *node)
{
	size_t array_size;
	char rand_char;
	size_t length;
	size_t i;
	int reg;
	int ret;

	switch (node->type) {
	case NODE_ARRAY:
		array_size = node->data.array.num_elems * node->data.array.elem_size;
		for (i = 0; i < array_size; i++) {
			if ((ret = next_byte(ctx->rand, &rand_char)))
				return ret;
			if ((ret = append_byte(ctx->payload, rand_char)))
				return ret;
		}
		ctx->reg_offset += array_size;
		if (node->data.array.null_terminated) {
			if ((ret = pad_payload(ctx, 1)))
				return ret;
			ctx->reg_offset++;
		}
		break;
	case NODE_LENGTH:
		reg = lookup_reg(ctx, node->data.length.length_of);
		if (reg < 0)
			return reg;
		length = ctx->regions[reg].size;
		if ((ret = encode_le(ctx->payload, length, node->data.length.byte_width)))
			return ret;
		ctx->reg_offset += node->data.length.byte_width;
		break;
	case NODE_PRIMITIVE:
		for (i = 0; i < node->data.primitive.byte_width; i++) {
			if ((ret = next_byte(ctx->rand, &rand_char)))
				return ret;
			if ((ret = append_byte(ctx->payload, rand_char)))
				return ret;
		}
		ctx->reg_offset += node->data.primitive.byte_width;
		break;
	case NODE_POINTER:
		reg = lookup_reg(ctx, node->data.pointer.points_to);
		if (reg < 0)
			return reg;
		if ((ret = add_reloc(ctx, (struct reloc_info){ .src_reg = ctx->curr_reg,
							       .offset = ctx->reg_offset,
							       .dst_reg = reg })))
			return ret;
		/* Placeholder pointer value, as pointers are patched by KFuzzTest anyways. */
		if ((ret = encode_le(ctx->payload, UINTPTR_MAX, sizeof(uintptr_t))))
			return ret;
		ctx->reg_offset += sizeof(uintptr_t);
		break;
	case NODE_PROGRAM:
	case NODE_REGION:
	default:
		return -EINVAL;
	}
	return 0;
}

static int encode_region(struct encoder_ctx *ctx, struct ast_region *reg)
{
	struct ast_node *child;
	size_t i;
	int ret;

	ctx->reg_offset = 0;
	for (i = 0; i < reg->num_members; i++) {
		child = reg->members[i];
		align_payload(ctx, node_alignment(child));
		if ((ret = encode_value_le(ctx, child)))
			return ret;
	}
	return 0;
}

static int encode_payload(struct encoder_ctx *ctx, struct ast_node *top_level)
{
	struct ast_node *reg;
	size_t i;
	int ret;

	for (i = 0; i < ctx->num_regions; i++) {
		reg = top_level->data.program.members[i];
		align_payload(ctx, node_alignment(reg));

		ctx->curr_reg = i;
		ctx->regions[i].offset = ctx->payload->num_bytes;
		if ((ret = encode_region(ctx, &reg->data.region)))
			return ret;
		pad_payload(ctx, KFUZZTEST_POISON_SIZE);
	}
	return 0;
}

static int encode_region_array(struct encoder_ctx *ctx, struct byte_buffer **ret)
{
	struct byte_buffer *reg_array;
	struct region_info info;
	int retcode;
	size_t i;

	reg_array = new_byte_buffer(BUFSIZE_SMALL);
	if (!reg_array)
		return -ENOMEM;

	if ((retcode = encode_le(reg_array, ctx->num_regions, sizeof(uint32_t))))
		goto fail;

	for (i = 0; i < ctx->num_regions; i++) {
		info = ctx->regions[i];
		if ((retcode = encode_le(reg_array, info.offset, sizeof(uint32_t))))
			goto fail;
		if ((retcode = encode_le(reg_array, info.size, sizeof(uint32_t))))
			goto fail;
	}
	*ret = reg_array;
	return 0;

fail:
	destroy_byte_buffer(reg_array);
	return retcode;
}

static int encode_reloc_table(struct encoder_ctx *ctx, size_t padding_amount, struct byte_buffer **ret)
{
	struct byte_buffer *reloc_table;
	struct reloc_info info;
	int retcode;
	size_t i;

	reloc_table = new_byte_buffer(BUFSIZE_SMALL);
	if (!reloc_table)
		return -ENOMEM;

	if ((retcode = encode_le(reloc_table, ctx->num_relocations, sizeof(uint32_t))) ||
	    (retcode = encode_le(reloc_table, padding_amount, sizeof(uint32_t))))
		goto fail;

	for (i = 0; i < ctx->num_relocations; i++) {
		info = ctx->relocations[i];
		if ((retcode = encode_le(reloc_table, info.src_reg, sizeof(uint32_t))) ||
		    (retcode = encode_le(reloc_table, info.offset, sizeof(uint32_t))) ||
		    (retcode = encode_le(reloc_table, info.dst_reg, sizeof(uint32_t))))
			goto fail;
	}
	pad(reloc_table, padding_amount);
	*ret = reloc_table;
	return 0;

fail:
	destroy_byte_buffer(reloc_table);
	return retcode;
}

static size_t reloc_table_size(struct encoder_ctx *ctx)
{
	return 2 * sizeof(uint32_t) + 3 * ctx->num_relocations * sizeof(uint32_t);
}

int encode(struct ast_node *top_level, struct rand_stream *r, size_t *num_bytes, struct byte_buffer **ret)
{
	struct byte_buffer *region_array = NULL;
	struct byte_buffer *final_buffer = NULL;
	struct byte_buffer *reloc_table = NULL;
	size_t header_size;
	int alignment;
	int retcode;

	struct encoder_ctx ctx = { 0 };
	if ((retcode = build_region_map(&ctx, top_level)))
		goto fail;

	ctx.rand = r;
	ctx.payload = new_byte_buffer(32);
	if (!ctx.payload) {
		retcode = -ENOMEM;
		goto fail;
	}
	if ((retcode = encode_payload(&ctx, top_level)))
		goto fail;

	if ((retcode = encode_region_array(&ctx, &region_array)))
		goto fail;

	header_size = sizeof(uint64_t) + region_array->num_bytes + reloc_table_size(&ctx);
	alignment = node_alignment(top_level);
	if ((retcode = encode_reloc_table(
		     &ctx, ROUND_UP_TO_MULTIPLE(header_size + KFUZZTEST_POISON_SIZE, alignment) - header_size,
		     &reloc_table)))
		goto fail;

	final_buffer = new_byte_buffer(BUFSIZE_LARGE);
	if (!final_buffer) {
		retcode = -ENOMEM;
		goto fail;
	}

	if ((retcode = encode_le(final_buffer, KFUZZTEST_MAGIC, sizeof(uint32_t))) ||
	    (retcode = encode_le(final_buffer, KFUZZTEST_PROTO_VERSION, sizeof(uint32_t))) ||
	    (retcode = append_bytes(final_buffer, region_array->buffer, region_array->num_bytes)) ||
	    (retcode = append_bytes(final_buffer, reloc_table->buffer, reloc_table->num_bytes)) ||
	    (retcode = append_bytes(final_buffer, ctx.payload->buffer, ctx.payload->num_bytes))) {
		destroy_byte_buffer(final_buffer);
		goto fail;
	}

	*num_bytes = final_buffer->num_bytes;
	*ret = final_buffer;

fail:
	if (region_array)
		destroy_byte_buffer(region_array);
	if (reloc_table)
		destroy_byte_buffer(reloc_table);
	cleanup_ctx(&ctx);
	return retcode;
}
