// SPDX-License-Identifier: GPL-2.0
/*
 * A hack to export Rust symbols for loadable modules without having to redo
 * the entire `include/linux/export.h` logic in Rust.
 *
 * This requires the Rust's new/future `v0` mangling scheme because the default
 * one ("legacy") uses invalid characters for C identifiers (thus we cannot use
 * the `EXPORT_SYMBOL_*` macros).
 *
 * All symbols are exported as GPL-only to guarantee no GPL-only feature is
 * accidentally exposed.
 */

#include <linux/module.h>

#define EXPORT_SYMBOL_RUST_GPL(sym) extern int sym; EXPORT_SYMBOL_GPL(sym)

#include "exports_core_generated.h"
#include "exports_alloc_generated.h"
#include "exports_bindings_generated.h"
#include "exports_kernel_generated.h"
