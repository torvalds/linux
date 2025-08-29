// SPDX-License-Identifier: GPL-2.0
/*
 * Encoder for KFuzzTest binary input format
 *
 * Copyright 2025 Google LLC
 */
#ifndef KFUZZTEST_BRIDGE_ENCODER_H
#define KFUZZTEST_BRIDGE_ENCODER_H

#include "input_parser.h"
#include "rand_stream.h"
#include "byte_buffer.h"

int encode(struct ast_node *top_level, struct rand_stream *r, size_t *num_bytes, struct byte_buffer **ret);

#endif /* KFUZZTEST_BRIDGE_ENCODER_H */
