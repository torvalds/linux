// SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#pragma once

#ifdef __BPF__

/* Selftests use these tags for compatibility with test_progs. */
#define __test_tag(tag)		__attribute__((btf_decl_tag("comment:" XSTR(__COUNTER__) ":" tag)))
#define __stderr(msg)		__test_tag("test_expect_stderr=" msg)
#define __stderr_unpriv(msg)	__test_tag("test_expect_stderr_unpriv=" msg)

#define XSTR(s) STR(s)
#define STR(s) #s

#endif
