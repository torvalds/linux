// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#define BPF_ARENA_FORCE_ASM
#define arena_htab_llvm arena_htab_asm
#include "arena_htab.c"
