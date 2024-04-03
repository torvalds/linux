#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Tests whether a suitable Rust toolchain is available.

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

# Print a reference to the Quick Start guide in the documentation.
print_docs_reference()
{
	echo >&2 "***"
	echo >&2 "*** Please see Documentation/rust/quick-start.rst for details"
	echo >&2 "*** on how to set up the Rust support."
	echo >&2 "***"
}

# Print an explanation about the fact that the script is meant to be called from Kbuild.
print_kbuild_explanation()
{
	echo >&2 "***"
	echo >&2 "*** This script is intended to be called from Kbuild."
	echo >&2 "*** Please use the 'rustavailable' target to call it instead."
	echo >&2 "*** Otherwise, the results may not be meaningful."
	exit 1
}

# If the script fails for any reason, or if there was any warning, then
# print a reference to the documentation on exit.
warning=0
trap 'if [ $? -ne 0 ] || [ $warning -ne 0 ]; then print_docs_reference; fi' EXIT

# Check that the expected environment variables are set.
if [ -z "${RUSTC+x}" ]; then
	echo >&2 "***"
	echo >&2 "*** Environment variable 'RUSTC' is not set."
	print_kbuild_explanation
fi

if [ -z "${BINDGEN+x}" ]; then
	echo >&2 "***"
	echo >&2 "*** Environment variable 'BINDGEN' is not set."
	print_kbuild_explanation
fi

if [ -z "${CC+x}" ]; then
	echo >&2 "***"
	echo >&2 "*** Environment variable 'CC' is not set."
	print_kbuild_explanation
fi

# Check that the Rust compiler exists.
if ! command -v "$RUSTC" >/dev/null; then
	echo >&2 "***"
	echo >&2 "*** Rust compiler '$RUSTC' could not be found."
	echo >&2 "***"
	exit 1
fi

# Check that the Rust bindings generator exists.
if ! command -v "$BINDGEN" >/dev/null; then
	echo >&2 "***"
	echo >&2 "*** Rust bindings generator '$BINDGEN' could not be found."
	echo >&2 "***"
	exit 1
fi

# Check that the Rust compiler version is suitable.
#
# Non-stable and distributions' versions may have a version suffix, e.g. `-dev`.
rust_compiler_output=$( \
	LC_ALL=C "$RUSTC" --version 2>/dev/null
) || rust_compiler_code=$?
if [ -n "$rust_compiler_code" ]; then
	echo >&2 "***"
	echo >&2 "*** Running '$RUSTC' to check the Rust compiler version failed with"
	echo >&2 "*** code $rust_compiler_code. See output and docs below for details:"
	echo >&2 "***"
	echo >&2 "$rust_compiler_output"
	echo >&2 "***"
	exit 1
fi
rust_compiler_version=$( \
	echo "$rust_compiler_output" \
		| sed -nE '1s:.*rustc ([0-9]+\.[0-9]+\.[0-9]+).*:\1:p'
)
if [ -z "$rust_compiler_version" ]; then
	echo >&2 "***"
	echo >&2 "*** Running '$RUSTC' to check the Rust compiler version did not return"
	echo >&2 "*** an expected output. See output and docs below for details:"
	echo >&2 "***"
	echo >&2 "$rust_compiler_output"
	echo >&2 "***"
	exit 1
fi
rust_compiler_min_version=$($min_tool_version rustc)
rust_compiler_cversion=$(get_canonical_version $rust_compiler_version)
rust_compiler_min_cversion=$(get_canonical_version $rust_compiler_min_version)
if [ "$rust_compiler_cversion" -lt "$rust_compiler_min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** Rust compiler '$RUSTC' is too old."
	echo >&2 "***   Your version:    $rust_compiler_version"
	echo >&2 "***   Minimum version: $rust_compiler_min_version"
	echo >&2 "***"
	exit 1
fi
if [ "$rust_compiler_cversion" -gt "$rust_compiler_min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** Rust compiler '$RUSTC' is too new. This may or may not work."
	echo >&2 "***   Your version:     $rust_compiler_version"
	echo >&2 "***   Expected version: $rust_compiler_min_version"
	echo >&2 "***"
	warning=1
fi

# Check that the Rust bindings generator is suitable.
#
# Non-stable and distributions' versions may have a version suffix, e.g. `-dev`.
rust_bindings_generator_output=$( \
	LC_ALL=C "$BINDGEN" --version 2>/dev/null
) || rust_bindings_generator_code=$?
if [ -n "$rust_bindings_generator_code" ]; then
	echo >&2 "***"
	echo >&2 "*** Running '$BINDGEN' to check the Rust bindings generator version failed with"
	echo >&2 "*** code $rust_bindings_generator_code. See output and docs below for details:"
	echo >&2 "***"
	echo >&2 "$rust_bindings_generator_output"
	echo >&2 "***"
	exit 1
fi
rust_bindings_generator_version=$( \
	echo "$rust_bindings_generator_output" \
		| sed -nE '1s:.*bindgen ([0-9]+\.[0-9]+\.[0-9]+).*:\1:p'
)
if [ -z "$rust_bindings_generator_version" ]; then
	echo >&2 "***"
	echo >&2 "*** Running '$BINDGEN' to check the bindings generator version did not return"
	echo >&2 "*** an expected output. See output and docs below for details:"
	echo >&2 "***"
	echo >&2 "$rust_bindings_generator_output"
	echo >&2 "***"
	exit 1
fi
rust_bindings_generator_min_version=$($min_tool_version bindgen)
rust_bindings_generator_cversion=$(get_canonical_version $rust_bindings_generator_version)
rust_bindings_generator_min_cversion=$(get_canonical_version $rust_bindings_generator_min_version)
if [ "$rust_bindings_generator_cversion" -lt "$rust_bindings_generator_min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** Rust bindings generator '$BINDGEN' is too old."
	echo >&2 "***   Your version:    $rust_bindings_generator_version"
	echo >&2 "***   Minimum version: $rust_bindings_generator_min_version"
	echo >&2 "***"
	exit 1
fi
if [ "$rust_bindings_generator_cversion" -gt "$rust_bindings_generator_min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** Rust bindings generator '$BINDGEN' is too new. This may or may not work."
	echo >&2 "***   Your version:     $rust_bindings_generator_version"
	echo >&2 "***   Expected version: $rust_bindings_generator_min_version"
	echo >&2 "***"
	warning=1
fi

# Check that the `libclang` used by the Rust bindings generator is suitable.
#
# In order to do that, first invoke `bindgen` to get the `libclang` version
# found by `bindgen`. This step may already fail if, for instance, `libclang`
# is not found, thus inform the user in such a case.
bindgen_libclang_output=$( \
	LC_ALL=C "$BINDGEN" $(dirname $0)/rust_is_available_bindgen_libclang.h 2>&1 >/dev/null
) || bindgen_libclang_code=$?
if [ -n "$bindgen_libclang_code" ]; then
	echo >&2 "***"
	echo >&2 "*** Running '$BINDGEN' to check the libclang version (used by the Rust"
	echo >&2 "*** bindings generator) failed with code $bindgen_libclang_code. This may be caused by"
	echo >&2 "*** a failure to locate libclang. See output and docs below for details:"
	echo >&2 "***"
	echo >&2 "$bindgen_libclang_output"
	echo >&2 "***"
	exit 1
fi

# `bindgen` returned successfully, thus use the output to check that the version
# of the `libclang` found by the Rust bindings generator is suitable.
#
# Unlike other version checks, note that this one does not necessarily appear
# in the first line of the output, thus no `sed` address is provided.
bindgen_libclang_version=$( \
	echo "$bindgen_libclang_output" \
		| sed -nE 's:.*clang version ([0-9]+\.[0-9]+\.[0-9]+).*:\1:p'
)
if [ -z "$bindgen_libclang_version" ]; then
	echo >&2 "***"
	echo >&2 "*** Running '$BINDGEN' to check the libclang version (used by the Rust"
	echo >&2 "*** bindings generator) did not return an expected output. See output"
	echo >&2 "*** and docs below for details:"
	echo >&2 "***"
	echo >&2 "$bindgen_libclang_output"
	echo >&2 "***"
	exit 1
fi
bindgen_libclang_min_version=$($min_tool_version llvm)
bindgen_libclang_cversion=$(get_canonical_version $bindgen_libclang_version)
bindgen_libclang_min_cversion=$(get_canonical_version $bindgen_libclang_min_version)
if [ "$bindgen_libclang_cversion" -lt "$bindgen_libclang_min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** libclang (used by the Rust bindings generator '$BINDGEN') is too old."
	echo >&2 "***   Your version:    $bindgen_libclang_version"
	echo >&2 "***   Minimum version: $bindgen_libclang_min_version"
	echo >&2 "***"
	exit 1
fi

# If the C compiler is Clang, then we can also check whether its version
# matches the `libclang` version used by the Rust bindings generator.
#
# In the future, we might be able to perform a full version check, see
# https://github.com/rust-lang/rust-bindgen/issues/2138.
cc_name=$($(dirname $0)/cc-version.sh $CC | cut -f1 -d' ')
if [ "$cc_name" = Clang ]; then
	clang_version=$( \
		LC_ALL=C $CC --version 2>/dev/null \
			| sed -nE '1s:.*version ([0-9]+\.[0-9]+\.[0-9]+).*:\1:p'
	)
	if [ "$clang_version" != "$bindgen_libclang_version" ]; then
		echo >&2 "***"
		echo >&2 "*** libclang (used by the Rust bindings generator '$BINDGEN')"
		echo >&2 "*** version does not match Clang's. This may be a problem."
		echo >&2 "***   libclang version: $bindgen_libclang_version"
		echo >&2 "***   Clang version:    $clang_version"
		echo >&2 "***"
		warning=1
	fi
fi

# Check that the source code for the `core` standard library exists.
#
# `$KRUSTFLAGS` is passed in case the user added `--sysroot`.
rustc_sysroot=$("$RUSTC" $KRUSTFLAGS --print sysroot)
rustc_src=${RUST_LIB_SRC:-"$rustc_sysroot/lib/rustlib/src/rust/library"}
rustc_src_core="$rustc_src/core/src/lib.rs"
if [ ! -e "$rustc_src_core" ]; then
	echo >&2 "***"
	echo >&2 "*** Source code for the 'core' standard library could not be found"
	echo >&2 "*** at '$rustc_src_core'."
	echo >&2 "***"
	exit 1
fi
