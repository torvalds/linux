/*
 * bench.c -- SSE4.2 compilation target for benchmark function(s)
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "zone.h"
#include "attributes.h"
#include "diagnostic.h"
#include "westmere/simd.h"
#include "westmere/bits.h"
#include "generic/parser.h"
#include "generic/scanner.h"

diagnostic_push()
clang_diagnostic_ignored(missing-prototypes)

int32_t zone_bench_westmere_lex(zone_parser_t *parser, size_t *tokens)
{
  token_t token;

  (*tokens) = 0;
  take(parser, &token);
  while (token.code > 0) {
    (*tokens)++;
    take(parser, &token);
  }

  return token.code ? -1 : 0;
}

diagnostic_pop()
