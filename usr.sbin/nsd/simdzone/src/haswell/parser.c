/*
 * parser.c -- AVX2 specific compilation target for (DNS) zone file parser
 *
 * Copyright (c) 2022, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause.
 *
 */
#include "zone.h"
#include "attributes.h"
#include "diagnostic.h"
#include "haswell/simd.h"
#include "generic/endian.h"
#include "haswell/bits.h"
#include "generic/parser.h"
#include "generic/scanner.h"
#include "generic/number.h"
#include "generic/ttl.h"
#include "westmere/time.h"
#include "westmere/ip4.h"
#include "generic/ip6.h"
#include "generic/text.h"
#include "generic/name.h"
#include "generic/base16.h"
#include "haswell/base32.h"
#include "generic/base64.h"
#include "generic/nsec.h"
#include "generic/nxt.h"
#include "generic/caa.h"
#include "generic/ilnp64.h"
#include "generic/eui.h"
#include "generic/nsap.h"
#include "generic/wks.h"
#include "generic/loc.h"
#include "generic/gpos.h"
#include "generic/apl.h"
#include "generic/svcb.h"
#include "generic/cert.h"
#include "generic/atma.h"
#include "generic/algorithm.h"
#include "generic/types.h"
#include "westmere/type.h"
#include "generic/format.h"

diagnostic_push()
clang_diagnostic_ignored(missing-prototypes)

int32_t zone_haswell_parse(parser_t *parser)
{
  return parse(parser);
}

diagnostic_pop()
