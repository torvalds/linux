#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# is_rust_module.sh module.ko
#
# Returns `0` if `module.ko` is a Rust module, `1` otherwise.

set -e

# Using the `16_` prefix ensures other symbols with the same substring
# are not picked up (even if it would be unlikely). The last part is
# used just in case LLVM decides to use the `.` suffix.
#
# In the future, checking for the `.comment` section may be another
# option, see https://github.com/rust-lang/rust/pull/97550.
${NM} "$*" | grep -qE '^[0-9a-fA-F]+ [Rr] _R[^[:space:]]+16___IS_RUST_MODULE[^[:space:]]*$'
