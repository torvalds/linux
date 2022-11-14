#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Tests whether a suitable Rust toolchain is available.
#
# Pass `-v` for human output and more checks (as warnings).

set -e

min_tool_version=$(dirname $0)/min-tool-version.sh

# Convert the version string x.y.z to a canonical up-to-7-digits form.
#
# Note that this function uses one more digit (compared to other
# instances in other version scripts) to give a bit more space to
# `rustc` since it will reach 1.100.0 in late 2026.
get_canonical_version()
{
	IFS=.
	set -- $1
	echo $((100000 * $1 + 100 * $2 + $3))
}

# Check that the Rust compiler exists.
if ! command -v "$RUSTC" >/dev/null; then
	if [ "$1" = -v ]; then
		echo >&2 "***"
		echo >&2 "*** Rust compiler '$RUSTC' could not be found."
		echo >&2 "***"
	fi
	exit 1
fi

# Check that the Rust bindings generator exists.
if ! command -v "$BINDGEN" >/dev/null; then
	if [ "$1" = -v ]; then
		echo >&2 "***"
		echo >&2 "*** Rust bindings generator '$BINDGEN' could not be found."
		echo >&2 "***"
	fi
	exit 1
fi

# Check that the Rust compiler version is suitable.
#
# Non-stable and distributions' versions may have a version suffix, e.g. `-dev`.
rust_compiler_version=$( \
	LC_ALL=C "$RUSTC" --version 2>/dev/null \
		| head -n 1 \
		| grep -oE '[0-9]+\.[0-9]+\.[0-9]+' \
)
rust_compiler_min_version=$($min_tool_version rustc)
rust_compiler_cversion=$(get_canonical_version $rust_compiler_version)
rust_compiler_min_cversion=$(get_canonical_version $rust_compiler_min_version)
if [ "$rust_compiler_cversion" -lt "$rust_compiler_min_cversion" ]; then
	if [ "$1" = -v ]; then
		echo >&2 "***"
		echo >&2 "*** Rust compiler '$RUSTC' is too old."
		echo >&2 "***   Your version:    $rust_compiler_version"
		echo >&2 "***   Minimum version: $rust_compiler_min_version"
		echo >&2 "***"
	fi
	exit 1
fi
if [ "$1" = -v ] && [ "$rust_compiler_cversion" -gt "$rust_compiler_min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** Rust compiler '$RUSTC' is too new. This may or may not work."
	echo >&2 "***   Your version:     $rust_compiler_version"
	echo >&2 "***   Expected version: $rust_compiler_min_version"
	echo >&2 "***"
fi

# Check that the Rust bindings generator is suitable.
#
# Non-stable and distributions' versions may have a version suffix, e.g. `-dev`.
rust_bindings_generator_version=$( \
	LC_ALL=C "$BINDGEN" --version 2>/dev/null \
		| head -n 1 \
		| grep -oE '[0-9]+\.[0-9]+\.[0-9]+' \
)
rust_bindings_generator_min_version=$($min_tool_version bindgen)
rust_bindings_generator_cversion=$(get_canonical_version $rust_bindings_generator_version)
rust_bindings_generator_min_cversion=$(get_canonical_version $rust_bindings_generator_min_version)
if [ "$rust_bindings_generator_cversion" -lt "$rust_bindings_generator_min_cversion" ]; then
	if [ "$1" = -v ]; then
		echo >&2 "***"
		echo >&2 "*** Rust bindings generator '$BINDGEN' is too old."
		echo >&2 "***   Your version:    $rust_bindings_generator_version"
		echo >&2 "***   Minimum version: $rust_bindings_generator_min_version"
		echo >&2 "***"
	fi
	exit 1
fi
if [ "$1" = -v ] && [ "$rust_bindings_generator_cversion" -gt "$rust_bindings_generator_min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** Rust bindings generator '$BINDGEN' is too new. This may or may not work."
	echo >&2 "***   Your version:     $rust_bindings_generator_version"
	echo >&2 "***   Expected version: $rust_bindings_generator_min_version"
	echo >&2 "***"
fi

# Check that the `libclang` used by the Rust bindings generator is suitable.
bindgen_libclang_version=$( \
	LC_ALL=C "$BINDGEN" $(dirname $0)/rust_is_available_bindgen_libclang.h 2>&1 >/dev/null \
		| grep -F 'clang version ' \
		| grep -oE '[0-9]+\.[0-9]+\.[0-9]+' \
		| head -n 1 \
)
bindgen_libclang_min_version=$($min_tool_version llvm)
bindgen_libclang_cversion=$(get_canonical_version $bindgen_libclang_version)
bindgen_libclang_min_cversion=$(get_canonical_version $bindgen_libclang_min_version)
if [ "$bindgen_libclang_cversion" -lt "$bindgen_libclang_min_cversion" ]; then
	if [ "$1" = -v ]; then
		echo >&2 "***"
		echo >&2 "*** libclang (used by the Rust bindings generator '$BINDGEN') is too old."
		echo >&2 "***   Your version:    $bindgen_libclang_version"
		echo >&2 "***   Minimum version: $bindgen_libclang_min_version"
		echo >&2 "***"
	fi
	exit 1
fi

# If the C compiler is Clang, then we can also check whether its version
# matches the `libclang` version used by the Rust bindings generator.
#
# In the future, we might be able to perform a full version check, see
# https://github.com/rust-lang/rust-bindgen/issues/2138.
if [ "$1" = -v ]; then
	cc_name=$($(dirname $0)/cc-version.sh "$CC" | cut -f1 -d' ')
	if [ "$cc_name" = Clang ]; then
		clang_version=$( \
			LC_ALL=C "$CC" --version 2>/dev/null \
				| sed -nE '1s:.*version ([0-9]+\.[0-9]+\.[0-9]+).*:\1:p'
		)
		if [ "$clang_version" != "$bindgen_libclang_version" ]; then
			echo >&2 "***"
			echo >&2 "*** libclang (used by the Rust bindings generator '$BINDGEN')"
			echo >&2 "*** version does not match Clang's. This may be a problem."
			echo >&2 "***   libclang version: $bindgen_libclang_version"
			echo >&2 "***   Clang version:    $clang_version"
			echo >&2 "***"
		fi
	fi
fi

# Check that the source code for the `core` standard library exists.
#
# `$KRUSTFLAGS` is passed in case the user added `--sysroot`.
rustc_sysroot=$("$RUSTC" $KRUSTFLAGS --print sysroot)
rustc_src=${RUST_LIB_SRC:-"$rustc_sysroot/lib/rustlib/src/rust/library"}
rustc_src_core="$rustc_src/core/src/lib.rs"
if [ ! -e "$rustc_src_core" ]; then
	if [ "$1" = -v ]; then
		echo >&2 "***"
		echo >&2 "*** Source code for the 'core' standard library could not be found"
		echo >&2 "*** at '$rustc_src_core'."
		echo >&2 "***"
	fi
	exit 1
fi
