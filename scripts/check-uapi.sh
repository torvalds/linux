#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Script to check commits for UAPI backwards compatibility

set -o errexit
set -o pipefail

print_usage() {
	name=$(basename "$0")
	cat << EOF
$name - check for UAPI header stability across Git commits

By default, the script will check to make sure the latest commit (or current
dirty changes) did not introduce ABI changes when compared to HEAD^1. You can
check against additional commit ranges with the -b and -p options.

The script will not check UAPI headers for architectures other than the one
defined in ARCH.

Usage: $name [-b BASE_REF] [-p PAST_REF] [-j N] [-l ERROR_LOG] [-q] [-v]

Options:
    -b BASE_REF    Base git reference to use for comparison. If unspecified or empty,
                   will use any dirty changes in tree to UAPI files. If there are no
                   dirty changes, HEAD will be used.
    -p PAST_REF    Compare BASE_REF to PAST_REF (e.g. -p v6.1). If unspecified or empty,
                   will use BASE_REF^1. Must be an ancestor of BASE_REF. Only headers
                   that exist on PAST_REF will be checked for compatibility.
    -j JOBS        Number of checks to run in parallel (default: number of CPU cores).
    -l ERROR_LOG   Write error log to file (default: no error log is generated).
    -q             Quiet operation (suppress stdout, still print stderr).
    -v             Verbose operation (print more information about each header being checked).

Environmental args:
    ABIDIFF  Custom path to abidiff binary
    CC       C compiler (default is "gcc")
    ARCH     Target architecture of C compiler (default is host arch)

Exit codes:
    $SUCCESS) Success
    $FAIL_ABI) ABI difference detected
    $FAIL_PREREQ) Prerequisite not met
    $FAIL_COMPILE) Compilation error
EOF
}

readonly SUCCESS=0
readonly FAIL_ABI=1
readonly FAIL_PREREQ=2
readonly FAIL_COMPILE=3

# Print to stderr
eprintf() {
	# shellcheck disable=SC2059
	printf "$@" >&2
}

# Check if git tree is dirty
tree_is_dirty() {
	if git diff --quiet; then
		return 1
	else
		return 0
	fi
}

# Get list of files installed in $ref
get_file_list() {
	local -r ref="$1"
	local -r tree="$(get_header_tree "$ref")"

	# Print all installed headers, filtering out ones that can't be compiled
	find "$tree" -type f -name '*.h' -printf '%P\n' | grep -v -f "$INCOMPAT_LIST"
}

# Add to the list of incompatible headers
add_to_incompat_list() {
	local -r ref="$1"

	# Start with the usr/include/Makefile to get a list of the headers
	# that don't compile using this method.
	if [ ! -f usr/include/Makefile ]; then
		eprintf "error - no usr/include/Makefile present at %s\n" "$ref"
		eprintf "Note: usr/include/Makefile was added in the v5.3 kernel release\n"
		exit "$FAIL_PREREQ"
	fi
	{
		# shellcheck disable=SC2016
		printf 'all: ; @echo $(no-header-test)\n'
		cat usr/include/Makefile
	} | SRCARCH="$ARCH" make --always-make -f - | tr " " "\n" \
	  | grep -v "asm-generic" >> "$INCOMPAT_LIST"

	# The makefile also skips all asm-generic files, but prints "asm-generic/%"
	# which won't work for our grep match. Instead, print something grep will match.
	printf "asm-generic/.*\.h\n" >> "$INCOMPAT_LIST"

	sort -u -o "$INCOMPAT_LIST" "$INCOMPAT_LIST"
}

# Compile the simple test app
do_compile() {
	local -r inc_dir="$1"
	local -r header="$2"
	local -r out="$3"
	printf "int main(void) { return 0; }\n" | \
		"$CC" -c \
		  -o "$out" \
		  -x c \
		  -O0 \
		  -std=c90 \
		  -fno-eliminate-unused-debug-types \
		  -g \
		  "-I${inc_dir}" \
		  -include "$header" \
		  -
}

# Run make headers_install
run_make_headers_install() {
	local -r install_dir="$1"
	make -j "$MAX_THREADS" ARCH="$ARCH" INSTALL_HDR_PATH="$install_dir" \
		headers_install > /dev/null
}

# Install headers for both git refs
install_headers() {
	local -r base_ref="$1"
	local -r past_ref="$2"

	for ref in "$base_ref" "$past_ref"; do
		printf "Installing user-facing UAPI headers from %s... " "${ref:-dirty tree}"
		if [ -n "$ref" ]; then
			git archive --format=tar --prefix="${ref}-archive/" "$ref" \
				| (cd "$TMP_DIR" && tar xf -)
			(
				cd "${TMP_DIR}/${ref}-archive"
				run_make_headers_install "${TMP_DIR}/${ref}/usr"
				add_to_incompat_list "$ref" "$INCOMPAT_LIST"
			)
		else
			run_make_headers_install "${TMP_DIR}/${ref}/usr"
			add_to_incompat_list "$ref" "$INCOMPAT_LIST"
		fi
		printf "OK\n"
	done
}

# Print the path to the headers_install tree for a given ref
get_header_tree() {
	local -r ref="$1"
	printf "%s" "${TMP_DIR}/${ref}/usr"
}

# Check file list for UAPI compatibility
check_uapi_files() {
	local -r base_ref="$1"
	local -r past_ref="$2"

	local passed=0;
	local failed=0;
	local -a threads=()
	set -o errexit

	printf "Checking changes to UAPI headers between %s and %s\n" "$past_ref" "${base_ref:-dirty tree}"
	# Loop over all UAPI headers that were installed by $past_ref (if they only exist on $base_ref,
	# there's no way they're broken and no way to compare anyway)
	while read -r file; do
		if [ "${#threads[@]}" -ge "$MAX_THREADS" ]; then
			if wait "${threads[0]}"; then
				passed=$((passed + 1))
			else
				failed=$((failed + 1))
			fi
			threads=("${threads[@]:1}")
		fi

		check_individual_file "$base_ref" "$past_ref" "$file" &
		threads+=("$!")
	done < <(get_file_list "$past_ref")

	for t in "${threads[@]}"; do
		if wait "$t"; then
			passed=$((passed + 1))
		else
			failed=$((failed + 1))
		fi
	done

	total="$((passed + failed))"
	if [ "$failed" -gt 0 ]; then
		eprintf "error - %d/%d UAPI headers compatible with %s appear _not_ to be backwards compatible\n" \
			"$failed" "$total" "$ARCH"
	else
		printf "All %d UAPI headers compatible with %s appear to be backwards compatible\n" "$total" "$ARCH"
	fi

	return "$failed"
}

# Check an individual file for UAPI compatibility
check_individual_file() {
	local -r base_ref="$1"
	local -r past_ref="$2"
	local -r file="$3"

	local -r base_header="$(get_header_tree "$base_ref")/${file}"
	local -r past_header="$(get_header_tree "$past_ref")/${file}"

	if [ ! -f "$base_header" ]; then
		printf "!!! UAPI header %s was incorrectly removed between %s and %s !!!\n" \
			"$file" "$past_ref" "${base_ref:-dirty tree}" \
				| tee "${base_header}.error" >&2
		return 1
	fi

	compare_abi "$file" "$base_header" "$past_header" "$base_ref" "$past_ref"
}

# Perform the A/B compilation and compare output ABI
compare_abi() {
	local -r file="$1"
	local -r base_header="$2"
	local -r past_header="$3"
	local -r base_ref="$4"
	local -r past_ref="$5"
	local -r log="${TMP_DIR}/log/${file}.log"

	mkdir -p "$(dirname "$log")"

	if ! do_compile "$(get_header_tree "$base_ref")/include" "$base_header" "${base_header}.bin" 2> "$log"; then
		eprintf "error - couldn't compile version of UAPI header %s at %s\n" "$file" "$base_ref"
		cat "$log" >&2
		exit "$FAIL_COMPILE"
	fi

	if ! do_compile "$(get_header_tree "$past_ref")/include" "$past_header" "${past_header}.bin" 2> "$log"; then
		eprintf "error - couldn't compile version of UAPI header %s at %s\n" "$file" "$past_ref"
		cat "$log" >&2
		exit "$FAIL_COMPILE"
	fi

	local ret=0
	"$ABIDIFF" --non-reachable-types "${past_header}.bin" "${base_header}.bin" \
		> "$log" || ret="$?"
	if [ "$ret" -eq 0 ]; then
		if [ "$VERBOSE" = "true" ]; then
			printf "No ABI differences detected in %s from %s -> %s\n" "$file" "$past_ref" "${base_ref:-dirty tree}"
		fi
	else
		# Bits in abidiff's return code can be used to determine the type of error
		if [ $((ret & 0x1)) -gt 0 ]; then
			eprintf "error - abidiff did not run properly\n"
			exit 1
		fi

		# If the only changes were additions (not modifications to existing APIs), then
		# there's no problem. Ignore these diffs.
		if grep "Unreachable types summary" "$log" | grep -q "0 removed" &&
		   grep "Unreachable types summary" "$log" | grep -q "0 changed"; then
			return 0
		fi


		{
			printf "!!! ABI differences detected in %s from %s -> %s !!!\n\n" "$file" "$past_ref" "${base_ref:-dirty tree}"
			sed  -e '/summary:/d' -e '/changed type/d' -e '/^$/d' -e 's/^/  /g' "$log"

			if ! cmp "$past_header" "$base_header" > /dev/null 2>&1; then
				printf "\nHeader file diff (after headers_install):\n"
				diff -Naur "$past_header" "$base_header" \
					| sed -e "s|${past_header}|${past_ref}/${file}|g" \
					      -e "s|${base_header}|${base_ref:-dirty}/${file}|g"
				printf "\n"
			else
				printf "\n%s did not change between %s and %s...\n" "$file" "$past_ref" "${base_ref:-dirty tree}"
				printf "It's possible a change to one of the headers it includes caused this error:\n"
				grep '^#include' "$base_header"
				printf "\n"
			fi
		} | tee "${base_header}.error" >&2

		return 1
	fi
}

min_version_is_satisfied() {
	local -r min_version="$1"
	local -r version_installed="$2"

	printf "%s\n%s\n" "$min_version" "$version_installed" | sort -Vc > /dev/null 2>&1
}

# Make sure we have the tools we need and the arguments make sense
check_deps() {
	ABIDIFF="${ABIDIFF:-abidiff}"
	CC="${CC:-gcc}"
	ARCH="${ARCH:-$(uname -m)}"
	if [ "$ARCH" = "x86_64" ]; then
		ARCH="x86"
	fi

	local -r abidiff_min_version="1.7"
	local -r libdw_min_version_if_clang="0.171"

	if ! command -v "$ABIDIFF" > /dev/null 2>&1; then
		eprintf "error - abidiff not found!\n"
		eprintf "Please install abigail-tools version %s or greater\n" "$abidiff_min_version"
		eprintf "See: https://sourceware.org/libabigail/manual/libabigail-overview.html\n"
		return 1
	fi

	local -r abidiff_version="$("$ABIDIFF" --version | cut -d ' ' -f 2)"
	if ! min_version_is_satisfied "$abidiff_min_version" "$abidiff_version"; then
		eprintf "error - abidiff version too old: %s\n" "$abidiff_version"
		eprintf "Please install abigail-tools version %s or greater\n" "$abidiff_min_version"
		eprintf "See: https://sourceware.org/libabigail/manual/libabigail-overview.html\n"
		return 1
	fi

	if ! command -v "$CC" > /dev/null 2>&1; then
		eprintf 'error - %s not found\n' "$CC"
		return 1
	fi

	if "$CC" --version | grep -q clang; then
		local -r libdw_version="$(ldconfig -v 2>/dev/null | grep -v SKIPPED | grep -m 1 -o 'libdw-[0-9]\+.[0-9]\+' | cut -c 7-)"
		if ! min_version_is_satisfied "$libdw_min_version_if_clang" "$libdw_version"; then
			eprintf "error - libdw version too old for use with clang: %s\n" "$libdw_version"
			eprintf "Please install libdw from elfutils version %s or greater\n" "$libdw_min_version_if_clang"
			eprintf "See: https://sourceware.org/elfutils/\n"
			return 1
		fi
	fi

	if [ ! -d "arch/${ARCH}" ]; then
		eprintf 'error - ARCH "%s" is not a subdirectory under arch/\n' "$ARCH"
		eprintf "Please set ARCH to one of:\n%s\n" "$(find arch -maxdepth 1 -mindepth 1 -type d -printf '%f ' | fmt)"
		return 1
	fi

	if ! git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
		eprintf "error - this script requires the kernel tree to be initialized with Git\n"
		return 1
	fi

	if ! git rev-parse --verify "$past_ref" > /dev/null 2>&1; then
		printf 'error - invalid git reference "%s"\n' "$past_ref"
		return 1
	fi

	if [ -n "$base_ref" ]; then
		if ! git merge-base --is-ancestor "$past_ref" "$base_ref" > /dev/null 2>&1; then
			printf 'error - "%s" is not an ancestor of base ref "%s"\n' "$past_ref" "$base_ref"
			return 1
		fi
		if [ "$(git rev-parse "$base_ref")" = "$(git rev-parse "$past_ref")" ]; then
			printf 'error - "%s" and "%s" are the same reference\n' "$past_ref" "$base_ref"
			return 1
		fi
	fi
}

run() {
	local base_ref="$1"
	local past_ref="$2"
	local abi_error_log="$3"
	shift 3

	if [ -z "$KERNEL_SRC" ]; then
		KERNEL_SRC="$(realpath "$(dirname "$0")"/..)"
	fi

	cd "$KERNEL_SRC"

	if [ -z "$base_ref" ] && ! tree_is_dirty; then
		base_ref=HEAD
	fi

	if [ -z "$past_ref" ]; then
		if [ -n "$base_ref" ]; then
			past_ref="${base_ref}^1"
		else
			past_ref=HEAD
		fi
	fi

	if ! check_deps; then
		exit "$FAIL_PREREQ"
	fi

	TMP_DIR=$(mktemp -d)
	readonly TMP_DIR
	trap 'rm -rf "$TMP_DIR"' EXIT

	readonly INCOMPAT_LIST="${TMP_DIR}/incompat_list.txt"
	touch "$INCOMPAT_LIST"

	# Run make install_headers for both refs
	install_headers "$base_ref" "$past_ref"

	# Check for any differences in the installed header trees
	if diff -r -q "$(get_header_tree "$base_ref")" "$(get_header_tree "$past_ref")" > /dev/null 2>&1; then
		printf "No changes to UAPI headers were applied between %s and %s\n" "$past_ref" "${base_ref:-dirty tree}"
		exit "$SUCCESS"
	fi

	if ! check_uapi_files "$base_ref" "$past_ref"; then
		eprintf "error - UAPI header ABI check failed\n"
		if [ -n "$abi_error_log" ]; then
			{
				printf 'Generated by "%s %s" from git ref %s\n\n' "$0" "$*" "$(git rev-parse HEAD)"
				find "$TMP_DIR" -type f -name '*.error' -exec cat {} +
			} > "$abi_error_log"
			eprintf "Failure summary saved to %s\n" "$abi_error_log"
		fi
		exit "$FAIL_ABI"
	fi
}

main() {
	MAX_THREADS=$(nproc)
	VERBOSE="false"
	quiet="false"
	local base_ref=""
	while getopts "hb:p:mj:l:qv" opt; do
		case $opt in
		h)
			print_usage
			exit "$SUCCESS"
			;;
		b)
			base_ref="$OPTARG"
			;;
		p)
			past_ref="$OPTARG"
			;;
		j)
			MAX_THREADS="$OPTARG"
			;;
		l)
			abi_error_log="$OPTARG"
			;;
		q)
			quiet="true"
			VERBOSE="false"
			;;
		v)
			VERBOSE="true"
			quiet="false"
			;;
		*)
			exit "$FAIL_PREREQ"
		esac
	done


	if [ "$quiet" = "true" ]; then
		exec > /dev/null 2>&1
	fi

	run "$base_ref" "$past_ref" "$abi_error_log" "$@"
}

main "$@"
