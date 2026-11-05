#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2026, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
#
# Sync tools/ header copies with the kernel sources, generate beauty
# string table diffs, verify perf still builds, and create git commits.

set -euo pipefail

YELLOW='\033[0;33m'
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PERF_DIR=$(cd "$(dirname "$0")/.." && pwd)
KERNEL_DIR=$(cd "$PERF_DIR/../.." && pwd)
DRY_RUN=0
BUILD=1
COMMIT=1
FORCE=0
OUTPUT_DIR="${OUTPUT_DIR:-${TMPDIR:-/tmp}/build/perf-tools-$(id -u)}"
SIGN_OFF_NAME="${SIGN_OFF_NAME:-}"
SIGN_OFF_EMAIL="${SIGN_OFF_EMAIL:-}"
HEADER_FILTER=""
VERBOSE=0

# Track build successes and failures for end-of-run summary
declare -a SYNCED_OK=()
declare -a BUILD_FAILED=()
declare -a COPY_FAILED=()
declare -a COMMIT_FAILED=()
declare -a TOOLS_AHEAD=()
declare -a HAND_SYNC=()
declare -a MANUAL=()
declare -a FORCED=()

info()  { echo -e "${GREEN}==>${NC} $*"; }
warn()  { echo -e "${YELLOW}Warning:${NC} $*"; }
err()   { echo -e "${RED}Error:${NC} $*" >&2; }
detail(){ [[ $VERBOSE -eq 1 ]] && echo -e "    $*"; return 0; }

usage() {
	local status="${1:-0}"

	cat <<EOF
Usage: $(basename "$0") [OPTIONS]

Sync tools/ header copies with the kernel sources and create commits.

Options:
  -n, --dry-run       Show what would be done without making changes
  -N, --no-build      Skip the perf build verification step
  -C, --no-commit     Copy files but do not create git commits
  -f, --filter PAT    Only process headers matching PAT (substring match
                      against the kernel-side path)
  -F, --force         Sync also when both copies have content the other
                      lacks, after manual investigation; tools/ additions
                      dropped are recorded in the commit
  -o, --output DIR    Build output directory (default: $OUTPUT_DIR)
  -v, --verbose       Show more details
  -h, --help          Show this help

Environment:
  SIGN_OFF_NAME       Override git user.name for Signed-off-by
  SIGN_OFF_EMAIL      Override git user.email for Signed-off-by
  OUTPUT_DIR          Build output directory

Through make:
  SYNC_HEADERS_ARGS   Options to pass to this script, e.g.:
                      make -C tools/perf sync-headers SYNC_HEADERS_ARGS='-n'

The script:
  1. Runs 'make -C tools/perf check-headers' to find stale copies.
  2. For each stale header, identifies the kernel commits that changed it,
     including the merges that brought content of their own.
  3. Copies the kernel header into the tools/ tree, skipping it when only
     the tools/ copy changed (it is ahead) or when it carries intentional
     edits, and stopping for manual investigation when both sides have
     content the other lacks.
  4. For beauty headers, runs the relevant beauty script(s) before and
     after, showing new/changed string table entries.
  5. Builds perf and the other consumers of the header to verify nothing
     breaks.
  6. Creates a git commit per header (or group) with the standard commit
     message format.

EOF
	exit "$status"
}

while [[ $# -gt 0 ]]; do
	case "$1" in
	-n|--dry-run)   DRY_RUN=1; shift ;;
	-N|--no-build)  BUILD=0; shift ;;
	-C|--no-commit) COMMIT=0; shift ;;
	-f|--filter)
		[[ $# -ge 2 ]] || { err "$1 needs an argument"; exit 1; }
		HEADER_FILTER="$2"; shift 2 ;;
	-F|--force)     FORCE=1; shift ;;
	-o|--output)
		[[ $# -ge 2 ]] || { err "$1 needs an argument"; exit 1; }
		OUTPUT_DIR="$2"; shift 2 ;;
	-v|--verbose)   VERBOSE=1; shift ;;
	-h|--help)      usage ;;
	*) err "Unknown option: $1"; usage 1 ;;
	esac
done

# make -C resolves a relative O= from the directory it changes to, so pin
# the build directory down before main() moves into the kernel tree.
case "$OUTPUT_DIR" in
/*) ;;
*) OUTPUT_DIR="$PWD/$OUTPUT_DIR" ;;
esac

sign_off() {
	local name="${SIGN_OFF_NAME:-$(git config user.name)}"
	local email="${SIGN_OFF_EMAIL:-$(git config user.email)}"
	echo "Signed-off-by: $name <$email>"
}

# --- Detect stale headers ------------------------------------------------

detect_stale_headers() {
	local tmpfile
	tmpfile=$(mktemp)

	# check-headers.sh must be run from tools/perf/ (it does cd ../.. internally)
	(cd "$PERF_DIR" && bash check-headers.sh 2>/dev/null) | \
		grep '^ *diff -u ' | \
		sed 's/^ *diff -u //' > "$tmpfile"

	if [[ ! -s "$tmpfile" ]]; then
		rm -f "$tmpfile"
		return 1
	fi

	cat "$tmpfile"
	rm -f "$tmpfile"
	return 0
}

# --- Classify a header ---------------------------------------------------
#
# Returns via globals:
#   HDR_CLASS: "files" | "beauty"
classify_header() {
	local tools_path="$1"

	if [[ "$tools_path" == tools/perf/trace/beauty/* ]]; then
		HDR_CLASS="beauty"
	else
		HDR_CLASS="files"
	fi
}

# --- Find kernel commits that changed a header since our copy ------------

# Copies checked with -I or hunk-ignore rules in check-headers.sh carry
# intentional edits, so a sync commit never leaves the two blobs equal.
has_ignore_rules() {
	local kernel_path="$1"
	local check_headers="$PERF_DIR/check-headers.sh"

	grep -Eq "^[[:space:]]*(check[[:space:]]+${kernel_path}[[:space:]]+.*-I|check_ignore_some_hunks[[:space:]]+${kernel_path}([[:space:]]|$))" \
		"$check_headers" 2>/dev/null && return 0
	sed -n '/^declare -a SYNC_CHECK_FILES=/,/)/p' "$check_headers" 2>/dev/null | \
		grep -Fq "\"$kernel_path\""
}

# The last commit that left the two copies equal: an in-tree kernel commit
# can update both files while an earlier kernel change is still missing.
find_sync_point() {
	local kernel_path="$1"
	local tools_path="$2"
	local -a candidates blobs
	local i

	# Files with ignore rules carry intentional edits and will never have
	# identical blobs; fall back to the last commit touching tools/.
	if ! has_ignore_rules "$kernel_path"; then
		mapfile -t candidates < <(git log --no-merges --format='%H' -- "$kernel_path" "$tools_path" 2>/dev/null)
		mapfile -t blobs < <(
			printf '%s\n' "${candidates[@]}" | \
			awk -v k="$kernel_path" -v t="$tools_path" '{ print $1 ":" k ORS $1 ":" t }' | \
			git cat-file --batch-check)

		for ((i = 0; i < ${#candidates[@]}; i++)); do
			# "<spec> missing" keeps the ':' of the spec as its first field,
			# which no object id has.
			if [[ "${blobs[2*i]%% *}" == "${blobs[2*i+1]%% *}" && \
			      "${blobs[2*i]%% *}" != *:* ]]; then
				echo "${candidates[i]}"
				return
			fi
		done
	fi

	git log --no-merges --format='%H' -1 -- "$tools_path" 2>/dev/null
}

# A kernel commit that changed both the header and the tools copy already
# carried its change into tools/, so it has nothing left to pick up here.
find_kernel_commits() {
	local kernel_path="$1"
	local tools_path="$2"
	local sync_point="$3"
	local copied sha

	copied=$(git log --no-merges --format='%H' \
		"${sync_point}..HEAD" -- "$tools_path" 2>/dev/null)
	[[ -n "$copied" ]] && copied+=$'\n'

	while IFS= read -r sha; do
		[[ "$copied" == *"$sha"$'\n'* ]] && continue
		echo "$sha"
	done < <(git log --no-merges --format='%H' \
		"${sync_point}..HEAD" -- "$kernel_path" 2>/dev/null)
}

# The lines the kernel copy has and the tools/ copy lacks, as "start,end"
# ranges on the kernel side.
added_line_ranges() {
	local kernel_path="$1"
	local tools_path="$2"
	local out

	out=$(diff -U0 "$tools_path" "$kernel_path" 2>/dev/null) || true
	printf '%s\n' "$out" | awk '
		/^@@/ {
			split($3, a, ",");
			start = substr(a[1], 2);
			count = (a[2] == "" ? 1 : a[2]);
			if (count > 0)
				print start "," start + count - 1;
		}'
}

# A merge that resolves a conflict brings content of its own, e.g. a
# capability renumbered to avoid clashing with another one; git log
# --no-merges does not see those, so credit the merge with the lines it
# introduced.
merge_pickup_commits() {
	local kernel_path="$1"
	local tools_path="$2"
	local sync_point="$3"
	local range sha
	local -a blame_args=()
	local -a shas=()

	[[ -n "$sync_point" ]] || return 0

	while read -r range; do
		[[ -z "$range" ]] && continue
		blame_args+=(-L "$range")
	done < <(added_line_ranges "$kernel_path" "$tools_path")

	if [[ ${#blame_args[@]} -eq 0 ]]; then
		return 0
	fi

	# Blame only what changed since the last sync, the lines older than
	# it come out as the boundary commit and are not to be picked up.
	while read -r sha; do
		[[ -z "$sha" ]] && continue
		shas+=("$sha")
	done < <(git blame --porcelain "${sync_point}..HEAD" \
		"${blame_args[@]}" -- "$kernel_path" 2>/dev/null |
		awk '
			/^[0-9a-f]{40} / {
				sha = $1;
				if (sha !~ /^0+$/)
					order[++n] = sha;
				next;
			}
			/^boundary$/ {
				skip[sha] = 1;
				next;
			}
			END {
				for (i = 1; i <= n; i++)
					if (!(order[i] in skip))
						print order[i];
			}')

	if [[ ${#shas[@]} -eq 0 ]]; then
		return 0
	fi

	printf '%s\n' "${shas[@]}" | awk 'NF && !seen[$0]++' |
		git rev-list --no-walk=unsorted --stdin --min-parents=2 2>/dev/null
}

# Render commit ids with FORMAT at the usual 12 chars, keeping their order.
format_commits() {
	local format="$1"
	local ids="$2"

	[[ -n "$ids" ]] || return 0
	printf '%s\n' "$ids" | git log --no-walk=unsorted --abbrev=12 \
		--format="$format" --stdin 2>/dev/null
}

# "add" when only the kernel changed since the sync point, "delete" when
# only the tools/ copy did, "both" when each side did; the sync point is
# where the blobs were equal, so comparing them is exact.
sync_direction() {
	local kernel_path="$1"
	local tools_path="$2"
	local sync_point="$3"
	local tools_base kernel_base tools_now kernel_now

	[[ -n "$sync_point" ]] || { echo add; return; }
	tools_base=$(git -C "$KERNEL_DIR" rev-parse -q --verify "$sync_point:$tools_path") || {
		echo add
		return
	}
	kernel_base=$(git -C "$KERNEL_DIR" rev-parse -q --verify "$sync_point:$kernel_path") || {
		echo add
		return
	}
	tools_now=$(git -C "$KERNEL_DIR" hash-object --path="$tools_path" "$tools_path") || {
		echo add
		return
	}
	kernel_now=$(git -C "$KERNEL_DIR" hash-object --path="$kernel_path" "$kernel_path") || {
		echo add
		return
	}

	if [[ "$tools_now" == "$tools_base" ]]; then
		echo add		# only the kernel moved
	elif [[ "$kernel_now" == "$kernel_base" ]]; then
		echo delete		# only tools/ moved, it is ahead
	else
		echo both
	fi
}

# A clean merge that matches the kernel means the tools/ side changed only
# what the kernel already has, so a copy drops nothing unique.
merge_matches_kernel() {
	local kernel_path="$1"
	local tools_path="$2"
	local sync_point="$3"
	local base merged status

	[[ -n "$sync_point" ]] || return 1
	base=$(mktemp) || return 1
	merged=$(mktemp) || { rm -f "$base"; return 1; }
	if ! git -C "$KERNEL_DIR" show "$sync_point:$tools_path" > "$base" 2>/dev/null; then
		rm -f "$base" "$merged"
		return 1
	fi
	if ! git merge-file -p "$KERNEL_DIR/$tools_path" "$base" \
		"$KERNEL_DIR/$kernel_path" > "$merged" 2>/dev/null; then
		rm -f "$base" "$merged"
		return 1
	fi
	cmp -s "$merged" "$KERNEL_DIR/$kernel_path"
	status=$?
	rm -f "$base" "$merged"
	return $status
}

# The additions only the tools/ copy added since the sync point, what a
# forced sync drops; diffing against the sync-point blob gives it exactly.
tools_only_lines() {
	local tools_path="$1"
	local sync_point="$2"
	local line

	[[ -n "$sync_point" ]] || return 0
	git -C "$KERNEL_DIR" rev-parse -q --verify "$sync_point:$tools_path" \
		>/dev/null || return 0

	while IFS= read -r line; do
		case "$line" in
		---*|+++*) continue ;;
		-*) echo "  $line" ;;
		esac
	done < <(git -C "$KERNEL_DIR" cat-file blob "$sync_point:$tools_path" | \
		diff -u "$KERNEL_DIR/$tools_path" - 2>/dev/null)
}

# --- Map a kernel header path to its beauty script(s) --------------------
#
# The mapping comes from the beauty Build files: a script reads the
# headers listed as its prerequisites and the directories passed as its
# arguments.  The header name still has to appear in the script so that
# one reading a whole tree, like syscalltbl.sh, doesn't match everything.

declare -A BEAUTY_MAKE_DIRS=()
declare -A BEAUTY_MAKE_SCRIPTS=()
declare -A BEAUTY_SCRIPT_ARGS=()
declare -A BEAUTY_SCRIPT_PATH=()
declare -A BEAUTY_SCRIPT_ROOTS=()
declare -A BEAUTY_SCRIPT_FILES=()
declare -a BEAUTY_SCRIPTS=()
BEAUTY_ARCH=""

beauty_add_script() {
	local script="$1"

	[[ -n "$script" ]] || return 0
	[[ -n "${BEAUTY_SCRIPT_FILES[$script]+x}" ]] && return 0
	BEAUTY_SCRIPT_FILES["$script"]=""
	BEAUTY_SCRIPT_ROOTS["$script"]=""
	BEAUTY_SCRIPTS+=("$script")
}

find_beauty_scripts() {
	local kernel_path="$1"
	local tools_path="$2"
	local header_basename script script_path root matched

	header_basename=$(basename "$kernel_path")

	for script in "${BEAUTY_SCRIPTS[@]+"${BEAUTY_SCRIPTS[@]}"}"; do
		script_path="${BEAUTY_SCRIPT_PATH[$script]:-$script}"
		matched=0
		case $'\n'"${BEAUTY_SCRIPT_FILES[$script]:-}"$'\n' in
		*$'\n'"$tools_path"$'\n'*) matched=1 ;;
		esac
		if [[ $matched -eq 0 ]]; then
			while IFS= read -r root; do
				[[ -n "$root" ]] || continue
				[[ "$tools_path" == "$root" || \
				   "$tools_path" == "$root"/* ]] || continue
				if grep -F -q -e "/$header_basename" \
					 -e "\"$header_basename\"" \
					 "$KERNEL_DIR/$script_path" 2>/dev/null; then
					matched=1
					break
				fi
			done <<< "${BEAUTY_SCRIPT_ROOTS[$script]:-}"
		fi
		[[ $matched -eq 1 ]] && echo "$script_path"
	done
}

# --- Find buildable targets that consume a header -------------------------
#
# Given a tools/ header path, grep tools/ source files to find which
# buildable directories include it, then return them as build commands.
# This is discovered dynamically so new consumers are picked up automatically.

# C sources include tools/ headers by their include-relative path, e.g.
# <linux/socket.h> for tools/include/uapi/linux/socket.h.
include_relative_path() {
	local tools_path="$1"

	case "$tools_path" in
	tools/include/uapi/*)
		echo "${tools_path#tools/include/uapi/}"
		;;
	tools/include/*)
		echo "${tools_path#tools/include/}"
		;;
	tools/arch/*/include/uapi/*)
		echo "${tools_path#tools/arch/*/include/uapi/}"
		;;
	tools/arch/*/include/*)
		echo "${tools_path#tools/arch/*/include/}"
		;;
	*)
		basename "$tools_path"
		;;
	esac
}

# Map from a source file path to its buildable target directory and make command.
# Returns empty if the file doesn't belong to a known buildable target.
buildable_target() {
	local src="$1"
	case "$src" in
	tools/perf/*)
		echo "perf:$PERF_DIR"
		;;
	tools/lib/bpf/*)
		echo "libbpf:$KERNEL_DIR/tools/lib/bpf"
		;;
	tools/bpf/bpftool/*)
		echo "bpftool:$KERNEL_DIR/tools/bpf/bpftool"
		;;
	tools/objtool/*)
		echo "objtool:$KERNEL_DIR/tools/objtool"
		;;
	tools/lib/subcmd/*)
		echo "libsubcmd:$KERNEL_DIR/tools/lib/subcmd"
		;;
	tools/lib/api/*)
		echo "libapi:$KERNEL_DIR/tools/lib/api"
		;;
	tools/lib/perf/*)
		echo "libperf:$KERNEL_DIR/tools/lib/perf"
		;;
	tools/lib/symbol/*)
		echo "libsymbol:$KERNEL_DIR/tools/lib/symbol"
		;;
	tools/tracing/*)
		echo "tracing:$KERNEL_DIR/tools/tracing"
		;;
	esac
}

find_build_targets() {
	local tools_path="$1"
	local header_class="$2"
	local include_path
	local targets=()
	local seen=()

	# Beauty copies are only on the include path of the beauty scripts.
	if [[ "$header_class" == "beauty" ]]; then
		echo "perf:$PERF_DIR"
		return
	fi

	include_path=$(include_relative_path "$tools_path")

	while IFS= read -r src; do
		[[ -z "$src" ]] && continue
		local target
		target=$(buildable_target "$src")
		[[ -z "$target" ]] && continue

		# Deduplicate
		local already=0
		for s in "${seen[@]+"${seen[@]}"}"; do
			if [[ "$s" == "$target" ]]; then
				already=1
				break
			fi
		done
		if [[ $already -eq 0 ]]; then
			seen+=("$target")
			targets+=("$target")
		fi
	done < <(grep -rlF -e "<$include_path>" -e "\"$include_path\"" -e "/$include_path" \
		--include='*.c' --include='*.h' --include='*.S' \
		"$KERNEL_DIR/tools/" 2>/dev/null | \
		sed "s|^$KERNEL_DIR/||" | grep -v '^tools/include/')

	# Always include perf
	local has_perf=0
	for t in "${targets[@]+"${targets[@]}"}"; do
		[[ "$t" == perf:* ]] && has_perf=1
	done
	if [[ $has_perf -eq 0 ]]; then
		targets+=("perf:$PERF_DIR")
	fi

	printf '%s\n' "${targets[@]}"
}

# Print the consumers that will be built for a header or group.
show_build_targets() {
	local what="$1"
	local target_list="$2"

	info "Build-testing consumers of $what:"
	while IFS=: read -r name dir; do
		[[ -z "$name" ]] && continue
		echo "    $name ($dir)"
	done <<< "$target_list"
}

# Keep build output out of the source tree; perf, tracing and objtool take
# O=, the libraries and bpftool take OUTPUT=.
build_target() {
	local name="$1"
	local dir="$2"

	case "$name" in
	perf)
		make -C "$dir" O="$OUTPUT_DIR" -j"$(nproc)"
		;;
	tracing|objtool)
		(umask 077 && mkdir -p "$OUTPUT_DIR/$name")
		make -C "$dir" O="$OUTPUT_DIR/$name" -j"$(nproc)"
		;;
	*)
		(umask 077 && mkdir -p "$OUTPUT_DIR/$name")
		make -C "$dir" OUTPUT="$OUTPUT_DIR/$name/" -j"$(nproc)"
		;;
	esac
}

# Run build tests for all targets that consume a header.
# Returns 0 if all builds pass, 1 if any fail.
build_test_targets() {
	local tools_path="$1"
	local header_class="$2"
	local header_basename log_name
	local target_list failed=0

	header_basename=$(basename "$tools_path")
	# Key the log by tools path, same-basename headers in different
	# directories would otherwise share a log.
	log_name="${tools_path//\//-}"
	target_list=$(find_build_targets "$tools_path" "$header_class")
	show_build_targets "$header_basename" "$target_list"

	while IFS=: read -r name dir; do
		[[ -z "$name" ]] && continue
		local log="$OUTPUT_DIR/$name.$log_name.log"
		if ! build_target "$name" "$dir" > "$log" 2>&1; then
			err "  $name build failed, see $log"
			failed=1
		fi
	done <<< "$target_list"

	return $failed
}

# --- Run a beauty script capturing output --------------------------------
#
# The arguments come from the same Build files the build uses, so a new
# script or a changed directory needs no edit here; run bare,
# arch_errno_names.sh misses the header copies.

# Resolve a Build variable token, $(srctree)/path or $(dir)/suffix, to a
# tree-relative path.
beauty_resolve_token() {
	local token="$1"
	local name suffix

	case "$token" in
	'$(srctree)'*)
		suffix="${token//\$(srctree)/}"
		echo "${suffix#/}"
		;;
	'$('*')'*)
		name="${token#\$(}"
		name="${name%%)*}"
		suffix="${token#*\)}"
		[[ -n "${BEAUTY_MAKE_DIRS[$name]:-}" ]] || return 1
		printf '%s\n' "${BEAUTY_MAKE_DIRS[$name]}$suffix" | \
			sed "s/\$(SRCARCH)/$BEAUTY_ARCH/g"
		;;
	*)
		return 1
		;;
	esac
}

# Collect, from one Build file, the script each rule runs with its
# arguments and the headers it takes as prerequisites.
parse_beauty_build() {
	local build="$1"
	local -a rule_lines=()
	local line last_rule="" var script args token value roots files needle
	local name path re_dir re_tbl

	[[ -r "$build" ]] || return 0

	# _dir :=, _tbl := assignments, and the $(SHELL) rules using them.
	re_dir='^([A-Za-z0-9_]+_(dir|dirs))[[:space:]]*(:=|\?=)[[:space:]]*'
	re_dir+='\$\(srctree\)/(.*)$'
	re_tbl='^([A-Za-z0-9_]+_(tbl|tbls))[[:space:]]*(:=|\?=)[[:space:]]*'
	re_tbl+='\$\(srctree\)/((.*\.sh))[[:space:]]*$'

	while IFS= read -r line; do
		if [[ "$line" =~ $re_dir ]]; then
			BEAUTY_MAKE_DIRS["${BASH_REMATCH[1]}"]=$(printf '%s' \
				"${BASH_REMATCH[4]%/}" | \
				sed "s/\$(SRCARCH)/$BEAUTY_ARCH/g")
		elif [[ "$line" =~ $re_tbl ]]; then
			BEAUTY_MAKE_SCRIPTS["${BASH_REMATCH[1]}"]="${BASH_REMATCH[4]}"
		elif [[ "$line" == *'$(SHELL)'* && -n "$last_rule" ]]; then
			rule_lines+=("$last_rule"$'\n'"$line")
		elif [[ "$line" != '#'* && "$line" != $'\t'* && \
			"$line" == *:* && "$line" != *':='* && \
			"$line" != *'?='* && "$line" != *'+='* ]]; then
			last_rule="$line"
		fi
	done < "$build"

	local rule prereqs command
	for rule in "${rule_lines[@]+"${rule_lines[@]}"}"; do
		prereqs="${rule%%$'\n'*}"
		command="${rule#*$'\n'}"

		script=""
		for var in "${!BEAUTY_MAKE_SCRIPTS[@]}"; do
			needle="'\$($var)'"
			if [[ "$command" == *"$needle"* ]]; then
				script="${BEAUTY_MAKE_SCRIPTS[$var]##*/}"
				BEAUTY_SCRIPT_PATH["$script"]="${BEAUTY_MAKE_SCRIPTS[$var]}"
				args="${command#*"$needle"}"
				break
			fi
		done
		[[ -n "$script" ]] || continue
		beauty_add_script "$script"

		args="${args//\'}"
		args=$(printf '%s\n' "$args" | \
			sed 's/\$(patsubst -%,,\$(CC))/gcc/g')
		args="${args%' > $@'}"
		value=""
		roots=""
		for token in $args; do
			case "$token" in
			'$@')
				token='@OUTFILE@'
				;;
			'$(srctree)'*|'$('*')'*)
				path=$(beauty_resolve_token "$token") || {
					value=""
					break
				}
				case "$token" in
				'$(srctree)'*)
					roots+="${path}"$'\n'
					;;
				*)
					name="${token#\$(}"
					name="${name%%)*}"
					roots+="${BEAUTY_MAKE_DIRS[$name]:-}"$'\n'
					;;
				esac
				token="$KERNEL_DIR/$path"
				;;
			*) ;;
			esac
			token=$(printf '%s' "$token" | sed "s/\$(SRCARCH)/$BEAUTY_ARCH/g")
			if [[ "$token" == *'$('* ]]; then
				value=""
				break
			fi
			value+="${value:+ }$token"
		done
		BEAUTY_SCRIPT_ROOTS["$script"]+="$roots"
		[[ -n "$value" ]] && BEAUTY_SCRIPT_ARGS["$script"]="$value"

		prereqs="${prereqs#*:}"
		files=""
		for token in $prereqs; do
			path=$(beauty_resolve_token "$token") || continue
			[[ "$path" == *'*'* ]] && continue
			[[ -n "$path" ]] && files+="$path"$'\n'
		done
		BEAUTY_SCRIPT_FILES["$script"]+="$files"
	done
}

setup_beauty_args() {
	local build found=0

	BEAUTY_ARCH=$(uname -m)
	case "$BEAUTY_ARCH" in
	x86_64|i[3-6]86) BEAUTY_ARCH=x86 ;;
	aarch64|arm64)   BEAUTY_ARCH=arm64 ;;
	s390*)           BEAUTY_ARCH=s390 ;;
	riscv*)          BEAUTY_ARCH=riscv ;;
	loongarch*)      BEAUTY_ARCH=loongarch ;;
	ppc*)            BEAUTY_ARCH=powerpc ;;
	sparc*)          BEAUTY_ARCH=sparc ;;
	mips*)           BEAUTY_ARCH=mips ;;
	esac

	for build in "$PERF_DIR/trace/beauty/Build" \
		     "$PERF_DIR/trace/beauty/tracepoints/Build"; do
		[[ -r "$build" ]] || continue
		parse_beauty_build "$build"
		found=1
	done

	[[ $found -eq 1 ]] || \
		detail "No beauty Build file, running the scripts without arguments"
}

run_beauty_script() {
	local script="$1"
	local -a cmd=()
	local outfile="" i

	read -ra cmd <<< "${BEAUTY_SCRIPT_ARGS["${script##*/}"]:-}"
	for ((i = 0; i < ${#cmd[@]}; i++)); do
		if [[ "${cmd[$i]}" == '@OUTFILE@' ]]; then
			outfile=$(mktemp) || continue
			cmd[$i]="$outfile"
		fi
	done
	bash "$script" "${cmd[@]+"${cmd[@]}"}" 2>/dev/null || true
	if [[ -n "$outfile" ]]; then
		cat "$outfile" 2>/dev/null || true
		rm -f "$outfile"
	fi
}

# Run one beauty script against the kernel content swapped into the tools
# copy, capturing its output before and after; the saved copy restores the
# tools copy, also when the script is interrupted.
run_beauty_pass() {
	local script="$1"
	local kernel_path="$2"
	local tools_path="$3"
	local tmpdir="$4"
	local idx="$5"
	local before="$tmpdir/beauty.$idx.before"
	local after="$tmpdir/beauty.$idx.after"
	local saved="$tmpdir/beauty.$idx.saved"
	local restore

	if ! cp -p "$KERNEL_DIR/$tools_path" "$saved"; then
		err "Failed to save $tools_path"
		return 1
	fi
	restore=$(printf 'cp -p %q %q' "$saved" "$KERNEL_DIR/$tools_path")
	# shellcheck disable=SC2064 # expand now, locals may be gone
	trap "$restore" EXIT
	# shellcheck disable=SC2064 # expand now, locals may be gone
	trap "$restore; trap - EXIT; exit 130" INT
	# shellcheck disable=SC2064 # expand now, locals may be gone
	trap "$restore; trap - EXIT; exit 143" TERM
	(cd "$KERNEL_DIR" && run_beauty_script "$script") > "$before"
	if ! cp "$KERNEL_DIR/$kernel_path" "$KERNEL_DIR/$tools_path"; then
		err "Failed to copy $kernel_path -> $tools_path"
		cp -p "$saved" "$KERNEL_DIR/$tools_path" || \
			err "Failed to restore $tools_path"
		trap - INT TERM EXIT
		return 1
	fi
	(cd "$KERNEL_DIR" && run_beauty_script "$script") > "$after"
	if ! cp -p "$saved" "$KERNEL_DIR/$tools_path"; then
		err "Failed to restore $tools_path"
		trap - INT TERM EXIT
		return 1
	fi
	trap - INT TERM EXIT
}

# The tools/perf/trace/beauty/ relative path of a beauty script, as shown
# in the commit messages.
beauty_script_relpath() {
	echo "tools/perf/trace/beauty/${1##*/trace/beauty/}"
}

# The commit message fragment for a beauty script whose table changed:
# the recipe the pre-pass ran and the difference between its two runs.
beauty_body_fragment() {
	local script="$1"
	local kernel_path="$2"
	local tools_path="$3"
	local before="$4"
	local after="$5"
	local rel_script args

	rel_script=$(beauty_script_relpath "$script")
	# The runs are from the tree root, show the dirs relative to it.
	args="${BEAUTY_SCRIPT_ARGS["${script##*/}"]:-}"
	args="${args//"$KERNEL_DIR/"/}"
	local before_args="$args" after_args="$args"
	local before_tail=" > before.txt" after_tail=" > after.txt"

	# Scripts taking the output file as an argument write it themselves.
	if [[ "$args" == *@OUTFILE@* ]]; then
		before_args="${args//@OUTFILE@/out.before}"
		after_args="${args//@OUTFILE@/out.after}"
		before_tail=""
		after_tail=""
	fi

	printf '\n'
	printf '  $ %s%s%s\n' "$rel_script" "${before_args:+ $before_args}" "$before_tail"
	printf '  $ cp %s %s\n' "$kernel_path" "$tools_path"
	printf '  $ %s%s%s\n' "$rel_script" "${after_args:+ $after_args}" "$after_tail"
	if [[ "$args" == *@OUTFILE@* ]]; then
		printf '  $ diff -u out.before out.after\n'
	else
		printf '  $ diff -u before.txt after.txt\n'
	fi
	printf '\n'
	diff -u "$before" "$after" | tail -n +3 | sed 's/^/  /'
	printf '\n'
}

cleanup_tmpdir() {
	[[ -n "$1" ]] || return 0
	rm -rf "$1"
}

# --- Copy header and create commit ---------------------------------------

process_header() {
	local tools_path="$1"
	local kernel_path="$2"

	classify_header "$tools_path"

	if [[ -n "$HEADER_FILTER" ]] && [[ "$kernel_path" != *"$HEADER_FILTER"* ]]; then
		detail "Skipping $kernel_path (does not match filter)"
		return
	fi

	info "Processing: $kernel_path"
	detail "  Class: $HDR_CLASS"
	detail "  Tools: $tools_path"
	detail "  Kernel: $kernel_path"

	# Find kernel commits that changed this file
	local sync_point pickup_commits=""
	sync_point=$(find_sync_point "$kernel_path" "$tools_path")
	if [[ -n "$sync_point" ]]; then
		pickup_commits=$(find_kernel_commits "$kernel_path" \
			"$tools_path" "$sync_point")
	else
		warn "No previous sync commit found for $tools_path"
	fi

	local kernel_commits=""
	if [[ -n "$pickup_commits" ]]; then
		kernel_commits=$(format_commits '  %h ("%s")' "$pickup_commits")
	fi

	# A merge can bring content of its own; list it too, it is not the
	# author of a change, so it gets no Cc.
	local merge_commits=""
	merge_commits=$(merge_pickup_commits "$kernel_path" "$tools_path" \
		"$sync_point")
	if [[ -n "$merge_commits" ]]; then
		if [[ -n "$kernel_commits" ]]; then
			kernel_commits+=$'\n'
		fi
		kernel_commits+=$(format_commits '  %h ("%s")' "$merge_commits")
	fi

	if [[ -n "$kernel_commits" ]]; then
		echo ""
		echo "  Kernel commits since last sync:"
		echo "$kernel_commits"
	fi

	# Copies with ignore rules carry deliberate edits that a plain copy
	# would drop; leave them for a hand sync, printing the Cc list for them.
	if has_ignore_rules "$kernel_path"; then
		local cc_lines=""
		if [[ -n "$pickup_commits" ]]; then
			cc_lines=$(format_commits 'Cc: %an <%ae>' "$pickup_commits" | sort -u)
			if [[ -n "$cc_lines" ]]; then
				echo ""
				echo "$cc_lines" | sed 's/^/  /'
			fi
			echo ""
		fi
		warn "$tools_path carries intentional edits, sync by hand"
		HAND_SYNC+=("$tools_path")
		return
	fi

	# The kernel is the reference, but only copy when that doesn't drop
	# content only the tools/ copy has.
	local direction dropped="" forced=0
	direction=$(sync_direction "$kernel_path" "$tools_path" "$sync_point")
	case "$direction" in
	delete)
		warn "$tools_path is ahead of the kernel sources, leaving it alone"
		TOOLS_AHEAD+=("$tools_path")
		return
		;;
	both)
		if merge_matches_kernel "$kernel_path" "$tools_path" "$sync_point"; then
			detail "Tools/ changes are already in the kernel"
		elif [[ $FORCE -eq 0 ]]; then
			err "$tools_path and $kernel_path both have content the other lacks, needs manual investigation"
			MANUAL+=("$tools_path")
			return
		else
			warn "Both copies have unique content, dropping the tools/ one (--force)"
			dropped=$(tools_only_lines "$tools_path" "$sync_point")
			forced=1
		fi
		;;
	esac

	# For beauty files, capture the string table changes and keep the
	# before/after files for the commit message.
	local beauty_changed=0
	local beauty_tmpdir=""
	local beauty_scripts
	local -a beauty_script_list=()
	beauty_scripts=$(find_beauty_scripts "$kernel_path" "$tools_path")
	while IFS= read -r script; do
		[[ -z "$script" ]] && continue
		beauty_script_list+=("$script")
	done <<< "$beauty_scripts"
	if [[ ${#beauty_script_list[@]} -gt 0 ]]; then
		beauty_tmpdir=$(mktemp -d)

		local idx=0
		for script in "${beauty_script_list[@]}"; do
			local script_name
			script_name=$(basename "$script")
			local before="$beauty_tmpdir/beauty.$idx.before"
			local after="$beauty_tmpdir/beauty.$idx.after"

			# The script reads the tools/ copy, so swap in the kernel one
			# and restore the saved copy, also in a dry run.
			if ! run_beauty_pass "$script" "$kernel_path" "$tools_path" \
				"$beauty_tmpdir" "$idx"; then
				COPY_FAILED+=("$tools_path")
				cleanup_tmpdir "$beauty_tmpdir"
				return 1
			fi

			if ! diff -q "$before" "$after" > /dev/null 2>&1; then
				echo ""
				info "Beauty string changes from $script_name:"
				diff -u "$before" "$after" | head -60 || true
				beauty_changed=1
			fi
			idx=$((idx + 1))
		done
	fi

	if [[ $DRY_RUN -eq 1 ]]; then
		[[ $forced -eq 1 ]] && FORCED+=("$tools_path")
		cleanup_tmpdir "$beauty_tmpdir"
		info "(dry run) Would copy $kernel_path -> $tools_path"
		return
	fi

	# Do the actual copy
	if ! cp "$KERNEL_DIR/$kernel_path" "$KERNEL_DIR/$tools_path"; then
		err "Failed to copy $kernel_path -> $tools_path"
		COPY_FAILED+=("$tools_path")
		cleanup_tmpdir "$beauty_tmpdir"
		return 1
	fi
	info "Copied $kernel_path -> $tools_path"

	# Verify the consumers still build
	if [[ $BUILD -eq 1 ]]; then
		if ! build_test_targets "$tools_path" "$HDR_CLASS"; then
			err "Build failed after copying $kernel_path — reverting."
			git -C "$KERNEL_DIR" checkout -- "$tools_path" || \
				err "Failed to revert $tools_path"
			BUILD_FAILED+=("$tools_path")
			cleanup_tmpdir "$beauty_tmpdir"
			return 1
		fi
		info "Build OK."
	fi

	[[ $forced -eq 1 ]] && FORCED+=("$tools_path")

	if [[ $COMMIT -eq 0 ]]; then
		SYNCED_OK+=("$tools_path")
		cleanup_tmpdir "$beauty_tmpdir"
		return
	fi

	# Compose commit message
	local header_short
	header_short=$(basename "$kernel_path")

	# The subject follows the path of the copy, one wording per family.
	local subject arch uapi_rel
	case "$kernel_path" in
	include/uapi/*)
		uapi_rel="${kernel_path#include/uapi/}"
		if [[ "$HDR_CLASS" == "beauty" ]]; then
			subject="tools headers: Sync UAPI $uapi_rel with the kernel sources"
		else
			subject="tools headers UAPI: Sync $uapi_rel with the kernel sources"
		fi
		;;
	include/*)
		subject="tools headers: Sync ${kernel_path#include/} with the kernel sources"
		;;
	arch/*/include/uapi/*)
		arch="${kernel_path#arch/}"
		arch="${arch%%/*}"
		subject="tools headers UAPI: Sync $arch/asm/${header_short} with the kernel sources"
		;;
	arch/*)
		arch="${kernel_path#arch/}"
		arch="${arch%%/*}"
		subject="tools arch $arch: Sync the $header_short copy with the kernel sources"
		;;
	*)
		subject="tools headers: Sync $kernel_path with the kernel sources"
		;;
	esac

	# Build commit body
	local body=""

	if [[ -n "$kernel_commits" ]]; then
		body+="To pick up the changes in:"
		body+=$'\n'
		body+=$'\n'
		body+="$kernel_commits"
		body+=$'\n'
	fi

	if [[ $beauty_changed -eq 1 ]]; then
		# Show the beauty diff in the commit message.
		body+=$'\n'
		body+="That introduce these new entries in the string tables:"
		body+=$'\n'

		local idx=0
		for script in "${beauty_script_list[@]}"; do
			local before_msg="$beauty_tmpdir/beauty.$idx.before"
			local after_msg="$beauty_tmpdir/beauty.$idx.after"

			if ! diff -q "$before_msg" "$after_msg" > /dev/null 2>&1; then
				body+=$(beauty_body_fragment "$script" "$kernel_path" \
					"$tools_path" "$before_msg" "$after_msg")
				body+=$'\n'
			fi
			idx=$((idx + 1))
		done
	elif [[ ${#beauty_script_list[@]} -gt 0 ]]; then
		body+=$'\n'
		body+="That don't result in changes to the string tables generated"
		body+=" from this header."
	else
		body+=$'\n'
		body+="That just rebuilds perf, silencing this build warning."
	fi

	if [[ -n "$dropped" ]]; then
		body+=$'\n'
		body+=$'\n'
		body+="The tools/ copy had content not present in the kernel sources,"
		body+=$'\n'
		body+="dropped here after manual review:"
		body+=$'\n'
		body+=$'\n'
		body+="$dropped"
	fi

	body+=$'\n'
	body+=$'\n'
	body+="This addresses this perf build warning:"
	body+=$'\n'
	body+=$'\n'
	body+="  Warning: Kernel ABI header differences:"
	body+=$'\n'
	body+="    diff -u $tools_path $kernel_path"
	body+=$'\n'
	body+=$'\n'
	body+="Please see tools/include/uapi/README for further details."

	# Add Cc for kernel commit authors
	local cc_lines=""
	if [[ -n "$pickup_commits" ]]; then
		cc_lines=$(format_commits 'Cc: %an <%ae>' "$pickup_commits" | sort -u)
	fi

	body+=$'\n'
	body+=$'\n'
	if [[ -n "$cc_lines" ]]; then
		body+="$cc_lines"$'\n'
	fi
	body+="$(sign_off)"

	cleanup_tmpdir "$beauty_tmpdir"

	if ! git -C "$KERNEL_DIR" add "$tools_path"; then
		err "Failed to stage $tools_path"
		COMMIT_FAILED+=("$tools_path")
		return 1
	fi
	if ! git -C "$KERNEL_DIR" commit "$tools_path" \
		-m "$(printf '%s\n\n%s\n' "$subject" "$body")"; then
		err "Commit failed for $tools_path"
		COMMIT_FAILED+=("$tools_path")
		return 1
	fi

	SYNCED_OK+=("$tools_path")
	info "Committed: $subject"
	echo ""
}

# --- Group related headers -----------------------------------------------
#
# Some headers should be committed together (e.g. all KVM arch headers,
# all syscall tables). This function processes groups.

process_group() {
	local group_name="$1"
	shift
	local pairs=("$@")

	if [[ ${#pairs[@]} -eq 0 ]]; then
		return
	fi

	local file_count
	file_count=$((${#pairs[@]} / 2))
	info "Processing group: $group_name ($file_count files)"

	local group_slug="${group_name// /-}"

	local all_kernel_commits=""
	local all_cc_lines=""
	local all_warnings=""
	local group_beauty_tmpdir=""
	local group_beauty_body=""
	local group_beauty_scripts=0
	local group_dropped=""
	local -a group_forced=()
	local files_to_add=()

	# Collect info for all files in the group
	local i=0
	while [[ $i -lt ${#pairs[@]} ]]; do
		local tools_path="${pairs[$i]}"
		local kernel_path="${pairs[$((i+1))]}"
		local fidx=$i
		i=$((i + 2))

		if [[ -n "$HEADER_FILTER" ]] && [[ "$kernel_path" != *"$HEADER_FILTER"* ]]; then
			continue
		fi

		if has_ignore_rules "$kernel_path"; then
			local sync_point pickup_commits="" merge_commits=""
			sync_point=$(find_sync_point "$kernel_path" "$tools_path")
			if [[ -n "$sync_point" ]]; then
				pickup_commits=$(find_kernel_commits "$kernel_path" \
					"$tools_path" "$sync_point")
				merge_commits=$(merge_pickup_commits "$kernel_path" \
					"$tools_path" "$sync_point")
			fi
			if [[ -n "$pickup_commits" || -n "$merge_commits" ]]; then
				echo ""
				echo "  Kernel commits since last sync ($tools_path):"
				if [[ -n "$pickup_commits" ]]; then
					format_commits '  %h ("%s")' "$pickup_commits"
				fi
				if [[ -n "$merge_commits" ]]; then
					format_commits '  %h ("%s")' "$merge_commits"
				fi
				local ccs
				ccs=$(format_commits 'Cc: %an <%ae>' "$pickup_commits" | sort -u)
				if [[ -n "$ccs" ]]; then
					echo ""
					echo "$ccs" | sed 's/^/  /'
				fi
				echo ""
			fi
			warn "$tools_path carries intentional edits, sync by hand"
			HAND_SYNC+=("$tools_path")
			continue
		fi

		local sync_point direction
		sync_point=$(find_sync_point "$kernel_path" "$tools_path")
		direction=$(sync_direction "$kernel_path" "$tools_path" \
			"$sync_point")
		case "$direction" in
		delete)
			warn "$tools_path is ahead of the kernel sources, leaving it alone"
			TOOLS_AHEAD+=("$tools_path")
			continue
			;;
		both)
			if merge_matches_kernel "$kernel_path" "$tools_path" "$sync_point"; then
				detail "Tools/ changes are already in the kernel"
			elif [[ $FORCE -eq 0 ]]; then
				err "$tools_path and $kernel_path both have content the other lacks, needs manual investigation"
				MANUAL+=("$tools_path")
				continue
			else
				warn "Both copies have unique content, dropping the tools/ one (--force)"
				local file_dropped
				file_dropped=$(tools_only_lines "$tools_path" "$sync_point")
				if [[ -n "$file_dropped" ]]; then
					group_dropped+="$file_dropped"$'\n'
				fi
				group_forced+=("$tools_path")
			fi
			;;
		esac

		local pickup_commits=""
		if [[ -n "$sync_point" ]]; then
			pickup_commits=$(find_kernel_commits "$kernel_path" \
				"$tools_path" "$sync_point")
		else
			warn "No previous sync commit found for $tools_path"
		fi
		if [[ -n "$pickup_commits" ]]; then
			all_kernel_commits+=$(format_commits '  %h ("%s")' "$pickup_commits")
			all_kernel_commits+=$'\n'

			local ccs
			ccs=$(format_commits 'Cc: %an <%ae>' "$pickup_commits")
			if [[ -n "$ccs" ]]; then
				all_cc_lines+="$ccs"$'\n'
			fi
		fi

		# A merge can bring content of its own; list it too, it is not the
		# author of a change, so it gets no Cc.
		local merge_commits=""
		merge_commits=$(merge_pickup_commits "$kernel_path" "$tools_path" \
			"$sync_point")
		if [[ -n "$merge_commits" ]]; then
			all_kernel_commits+=$(format_commits '  %h ("%s")' "$merge_commits")
			all_kernel_commits+=$'\n'
		fi

		all_warnings+="    diff -u $tools_path $kernel_path"$'\n'

		# Beauty string tables; the script reads the tools/ copy, so this
		# runs before the copy too, also in a dry run.
		local -a group_script_list=()
		while IFS= read -r script; do
			[[ -z "$script" ]] && continue
			group_script_list+=("$script")
		done < <(find_beauty_scripts "$kernel_path" "$tools_path")
		if [[ ${#group_script_list[@]} -gt 0 ]]; then
			group_beauty_scripts=1
			[[ -z "$group_beauty_tmpdir" ]] && group_beauty_tmpdir=$(mktemp -d)
			local sidx=0
			for script in "${group_script_list[@]}"; do
				local script_name
				script_name=$(basename "$script")
				local before="$group_beauty_tmpdir/beauty.$fidx.$sidx.before"
				local after="$group_beauty_tmpdir/beauty.$fidx.$sidx.after"

				if ! run_beauty_pass "$script" "$kernel_path" "$tools_path" \
					"$group_beauty_tmpdir" "$fidx.$sidx"; then
					COPY_FAILED+=("$tools_path")
					for f in "${files_to_add[@]}"; do
						git -C "$KERNEL_DIR" checkout -- "$f" || \
							err "Failed to revert $f"
					done
					cleanup_tmpdir "$group_beauty_tmpdir"
					return 1
				fi

				if ! diff -q "$before" "$after" > /dev/null 2>&1; then
					echo ""
					info "Beauty string changes from $script_name:"
					diff -u "$before" "$after" | head -60 || true
					group_beauty_body+=$(beauty_body_fragment "$script" \
						"$kernel_path" "$tools_path" "$before" "$after")
					group_beauty_body+=$'\n'
				fi
				sidx=$((sidx + 1))
			done
		fi

		if [[ $DRY_RUN -eq 0 ]]; then
			if ! cp "$KERNEL_DIR/$kernel_path" "$KERNEL_DIR/$tools_path"; then
				err "Failed to copy $kernel_path -> $tools_path"
				COPY_FAILED+=("$tools_path")
				for f in "${files_to_add[@]}"; do
					git -C "$KERNEL_DIR" checkout -- "$f" || \
						err "Failed to revert $f"
				done
				cleanup_tmpdir "$group_beauty_tmpdir"
				return 1
			fi
			files_to_add+=("$tools_path")
			info "Copied $kernel_path -> $tools_path"
		else
			info "(dry run) Would copy $kernel_path -> $tools_path"
		fi
	done

	# The beauty before/after files are captured into the fragments above.
	cleanup_tmpdir "$group_beauty_tmpdir"

	if [[ ${#files_to_add[@]} -eq 0 ]]; then
		FORCED+=("${group_forced[@]+"${group_forced[@]}"}")
		return
	fi

	if [[ $BUILD -eq 1 ]]; then
		# Collect all unique build targets for the group
		local group_targets=""
		for f in "${files_to_add[@]}"; do
			classify_header "$f"
			group_targets+=$(find_build_targets "$f" "$HDR_CLASS")
			group_targets+=$'\n'
		done
		group_targets=$(echo "$group_targets" | sort -u | grep -v '^$')

		show_build_targets "group $group_name" "$group_targets"
		local build_failed=0
		while IFS=: read -r name dir; do
			[[ -z "$name" ]] && continue
			detail "  Build-testing $name ($dir)..."
			local log="$OUTPUT_DIR/$name.$group_slug.log"
			if ! build_target "$name" "$dir" > "$log" 2>&1; then
				err "  $name build failed, see $log"
				build_failed=1
			fi
		done <<< "$group_targets"

		if [[ $build_failed -eq 1 ]]; then
			err "Build failed after copying group $group_name — reverting."
			for f in "${files_to_add[@]}"; do
				git -C "$KERNEL_DIR" checkout -- "$f" || \
					err "Failed to revert $f"
				BUILD_FAILED+=("$f (group: $group_name)")
			done
			return 1
		fi
		info "Build OK."
	fi

	FORCED+=("${group_forced[@]+"${group_forced[@]}"}")

	if [[ $DRY_RUN -eq 1 ]]; then
		return
	fi

	if [[ $COMMIT -eq 0 ]]; then
		for f in "${files_to_add[@]}"; do
			SYNCED_OK+=("$f")
		done
		return
	fi

	# Deduplicate kernel commits and Cc lines; the commits keep the git log
	# order they came in, the Cc lines stay sorted.
	all_kernel_commits=$(printf '%s\n' "$all_kernel_commits" | \
		awk 'NF && !seen[$0]++')
	all_cc_lines=$(echo "$all_cc_lines" | sort -u | grep -v '^$' || true)

	local subject="tools headers: Sync $group_name with the kernel sources"
	local body=""

	if [[ -n "$all_kernel_commits" ]]; then
		body+="To pick up the changes in:"
		body+=$'\n'
		body+=$'\n'
		body+="$all_kernel_commits"
		body+=$'\n'
	fi

	if [[ -n "$group_beauty_body" ]]; then
		body+=$'\n'
		body+="That introduce these new entries in the string tables:"
		body+=$'\n'
		body+="$group_beauty_body"
	elif [[ $group_beauty_scripts -eq 1 ]]; then
		body+=$'\n'
		body+="That don't result in changes to the string tables generated"
		body+=" from these headers."
	fi

	if [[ -n "$group_dropped" ]]; then
		body+=$'\n'
		body+=$'\n'
		body+="The tools/ copies had content not present in the kernel"
		body+=" sources, dropped here after manual review:"
		body+=$'\n'
		body+=$'\n'
		body+="${group_dropped%$'\n'}"
	fi

	body+=$'\n'
	body+=$'\n'
	body+="This addresses these perf build warnings:"
	body+=$'\n'
	body+=$'\n'
	body+="  Warning: Kernel ABI header differences:"
	body+=$'\n'
	body+="$all_warnings"
	body+=$'\n'
	body+="Please see tools/include/uapi/README for further details."
	body+=$'\n'
	body+=$'\n'
	if [[ -n "$all_cc_lines" ]]; then
		body+="$all_cc_lines"$'\n'
	fi
	body+="$(sign_off)"

	for f in "${files_to_add[@]}"; do
		if ! git -C "$KERNEL_DIR" add "$f"; then
			err "Failed to stage $f"
			for g in "${files_to_add[@]}"; do
				COMMIT_FAILED+=("$g")
			done
			return 1
		fi
	done

	if ! git -C "$KERNEL_DIR" commit "${files_to_add[@]}" \
		-m "$(printf '%s\n\n%s\n' "$subject" "$body")"; then
		err "Commit failed for group $group_name"
		for f in "${files_to_add[@]}"; do
			COMMIT_FAILED+=("$f")
		done
		return 1
	fi

	for f in "${files_to_add[@]}"; do
		SYNCED_OK+=("$f")
	done

	info "Committed: $subject"
	echo ""
}

# --- Main -----------------------------------------------------------------

main() {
	cd "$KERNEL_DIR"

	if [[ ! -d include/uapi ]]; then
		err "Must be run from a kernel tree root (or tools/perf/)"
		exit 1
	fi

	if [[ ! -d tools/perf ]]; then
		err "Cannot find tools/perf/ in kernel tree"
		exit 1
	fi

	setup_beauty_args || true

	if [[ $BUILD -eq 1 ]]; then
		# umask covers every level created, -m with -p only the last.
		(umask 077 && mkdir -p "$OUTPUT_DIR")
	fi

	# Check for uncommitted changes in tools/
	if ! git diff --quiet -- tools/include/ tools/perf/trace/beauty/ tools/perf/util/ tools/perf/arch/*/entry/syscalls/ tools/scripts/ tools/lib/ tools/arch/; then
		warn "You have uncommitted changes in tools/. Commit or stash them first."
		if [[ $DRY_RUN -eq 0 ]]; then
			exit 1
		fi
	fi

	info "Running check-headers to detect stale copies..."
	echo ""

	local stale
	if ! stale=$(detect_stale_headers); then
		info "All headers are up to date."
		exit 0
	fi

	echo "Stale headers found:"
	echo "$stale"
	echo ""

	# Parse the stale header pairs and group them
	# Each line is: tools_path kernel_path
	local kvm_group=()
	local syscall_group=()
	local singles=()

	while IFS=' ' read -r tools_path kernel_path; do
		case "$kernel_path" in
		*kvm.h|*svm.h|*vmx.h|*sie.h)
			kvm_group+=("$tools_path" "$kernel_path")
			;;
		*syscall*.tbl|*unistd.h)
			syscall_group+=("$tools_path" "$kernel_path")
			;;
		*)
			singles+=("$tools_path" "$kernel_path")
			;;
		esac
	done <<< "$stale"

	# Process individual headers first (beauty and simple copies).
	# || true: continue on failure; failures are tracked in BUILD_FAILED[],
	# COPY_FAILED[] and COMMIT_FAILED[] and reported in the summary.
	local i=0
	while [[ $i -lt ${#singles[@]} ]]; do
		process_header "${singles[$i]}" "${singles[$((i+1))]}" || true
		i=$((i + 2))
	done

	# Process grouped headers
	if [[ ${#kvm_group[@]} -gt 0 ]]; then
		process_group "KVM headers" "${kvm_group[@]}" || true
	fi

	if [[ ${#syscall_group[@]} -gt 0 ]]; then
		process_group "syscall tables and unistd.h" "${syscall_group[@]}" || true
	fi

	# Summary
	echo ""
	echo "========================================="
	echo " Summary"
	echo "========================================="

	if [[ ${#SYNCED_OK[@]} -gt 0 ]]; then
		info "Synced successfully (${#SYNCED_OK[@]}):"
		for f in "${SYNCED_OK[@]}"; do
			echo "    $f"
		done
	fi

	if [[ ${#BUILD_FAILED[@]} -gt 0 ]]; then
		echo ""
		err "Build failures (${#BUILD_FAILED[@]}) — reverted, need manual attention:"
		for f in "${BUILD_FAILED[@]}"; do
			echo "    $f"
		done
	fi

	if [[ ${#COPY_FAILED[@]} -gt 0 ]]; then
		echo ""
		err "Copy failures (${#COPY_FAILED[@]}) — need manual attention:"
		for f in "${COPY_FAILED[@]}"; do
			echo "    $f"
		done
	fi

	if [[ ${#COMMIT_FAILED[@]} -gt 0 ]]; then
		echo ""
		err "Commit failures (${#COMMIT_FAILED[@]}) — need manual attention:"
		for f in "${COMMIT_FAILED[@]}"; do
			echo "    $f"
		done
	fi

	if [[ ${#TOOLS_AHEAD[@]} -gt 0 ]]; then
		echo ""
		warn "tools/ copies ahead of the kernel, left alone (${#TOOLS_AHEAD[@]}):"
		for f in "${TOOLS_AHEAD[@]}"; do
			echo "    $f"
		done
	fi

	if [[ ${#HAND_SYNC[@]} -gt 0 ]]; then
		echo ""
		warn "Carry intentional edits, need manual sync (${#HAND_SYNC[@]}):"
		for f in "${HAND_SYNC[@]}"; do
			echo "    $f"
		done
	fi

	if [[ ${#MANUAL[@]} -gt 0 ]]; then
		echo ""
		err "Left for manual investigation (${#MANUAL[@]}):"
		for f in "${MANUAL[@]}"; do
			echo "    $f"
		done
	fi

	if [[ ${#FORCED[@]} -gt 0 ]]; then
		echo ""
		info "Synced with --force, tools/ additions dropped (${#FORCED[@]}):"
		for f in "${FORCED[@]}"; do
			echo "    $f"
		done
	fi

	# Final check-headers run to see what's still out of sync
	echo ""
	local remaining
	if remaining=$(detect_stale_headers); then
		warn "Headers still out of sync:"
		echo "$remaining"
	else
		info "All headers are now in sync."
	fi

	if [[ ${#BUILD_FAILED[@]} -gt 0 || ${#COPY_FAILED[@]} -gt 0 || \
		${#COMMIT_FAILED[@]} -gt 0 || ${#MANUAL[@]} -gt 0 ]]; then
		exit 1
	fi
}

main "$@"
