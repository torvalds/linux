/*
 * parser.c -- compilation target for fallback (DNS) zone parser
 *
 * Copyright (c) 2023, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "zone.h"
#include "attributes.h"
#include "diagnostic.h"
#include "generic/endian.h"
#include "fallback/bits.h"
#include "generic/parser.h"
#include "fallback/scanner.h"
#include "generic/number.h"
#include "generic/ttl.h"
#include "generic/time.h"
#include "fallback/text.h"
#include "fallback/name.h"
#include "generic/ip4.h"
#include "generic/ip6.h"
#include "generic/base16.h"
#include "generic/base32.h"
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
#include "generic/type.h"
#include "generic/format.h"

diagnostic_push()
clang_diagnostic_ignored(missing-prototypes)

int32_t zone_fallback_parse(parser_t *parser)
{
  return parse(parser);
}

diagnostic_pop()
