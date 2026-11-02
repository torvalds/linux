# SPDX-License-Identifier: GPL-2.0
#
# Helpers for the objtool klp tests.  A test builds a fixture twice, as the
# original and (with -DPATCHED) the patched object, runs both through
# "klp checksum" and diffs them, then asserts on the result.
#
# Assertions check properties rather than compare against recorded output:
# codegen varies between compilers and golden files would report churn instead
# of regressions.

set -u

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Tests live in generic/ or in an architecture directory beside it, and each
# carries its own fixtures.
FIXTURES_DIR="$(cd "$(dirname "$0")/fixtures" 2>/dev/null && pwd)"

# The kernel's convention: CROSS_COMPILE is the one knob, with per-tool
# overrides for what it does not cover.  objtool itself is always a host binary
# -- it is built with HOSTCC and only reads ELF -- so an arm64 machine can run
# the x86 tests against x86 objects given a compiler that emits them.
#
# readelf reads any target, so it rarely needs overriding, and either GNU
# readelf or llvm-readelf will do: the assertions match on fields rather than
# on columns, and where the two spell something differently -- "OS [0xff20]"
# against "OS[0xff20]" for SHN_LIVEPATCH -- they accept both.  BFD's objcopy is
# usually built for the host's target alone, and llvm-objcopy is the
# target-agnostic replacement.
CROSS_COMPILE="${CROSS_COMPILE:-}"
CC="${CC:-${CROSS_COMPILE}gcc}"
LD="${LD:-${CROSS_COMPILE}ld}"
READELF="${READELF:-${CROSS_COMPILE}readelf}"
OBJCOPY="${OBJCOPY:-${CROSS_COMPILE}objcopy}"

OBJTOOL="${OBJTOOL:-$TESTS_DIR/../objtool}"

# klp_preflight
#
# Check the environment once, before any test runs, and report what was found.
#
klp_preflight()
{
	local tmp tool cc_version arch host cc_arch

	bail() { echo "Bail out! $*" >&2; exit 1; }

	# A relative $OBJTOOL is relative to the objtool directory, not tests/.
	[ -x "$OBJTOOL" ] || [ ! -x "$TESTS_DIR/../$OBJTOOL" ] ||
		OBJTOOL="$TESTS_DIR/../$OBJTOOL"

	[ -x "$OBJTOOL" ] ||
		bail "objtool not found at '$OBJTOOL' -- build it first"

	# run_diff() runs objtool from inside the test's working directory, so
	# a relative path would resolve against that instead.
	OBJTOOL="$(realpath "$OBJTOOL")"

	"$OBJTOOL" klp 2>&1 | grep -q checksum ||
		bail "objtool was built without klp support; install libxxhash (>= 0.8) and rebuild"

	command -v "${CC%% *}" >/dev/null || bail "compiler not found: $CC"

	for tool in "$READELF" "$OBJCOPY" "$LD"; do
		command -v "${tool%% *}" >/dev/null || bail "$tool not found"
	done

	tmp="$(mktemp -d)" || bail "mktemp failed"
	echo 'int probe(void) { return 0; }' > "$tmp/probe.c"
	$CC -c -o "$tmp/probe.o" "$tmp/probe.c" 2>/dev/null ||
		{ rm -rf "$tmp"; bail "$CC cannot compile a trivial object"; }

	# $CC, $ARCH and objtool have to agree about the target, and cross runs
	# are where they stop agreeing: plain "CC=clang ARCH=x86_64" on an arm64
	# box selects the x86 tests and then builds arm64 objects, because clang
	# needs --target= to emit anything but the host's.
	#
	# Ask objtool rather than comparing machine names.  It rejects an object
	# it was not built for -- "unexpected ELF machine type" -- so one check
	# covers every way the three can disagree, and says so once instead of
	# failing every test for the same reason.
	"$OBJTOOL" klp checksum "$tmp/probe.o" >/dev/null 2>&1 ||
		{ rm -rf "$tmp"
		  bail "objtool rejects an object built by '$CC'; they target" \
		       "different architectures (set CROSS_COMPILE, or" \
		       "--target= for clang)"; }

	# BFD objcopy is usually built for the host's target alone, and
	# checksum_of() needs it to read the object under test.
	$OBJCOPY -O binary --only-section=.text "$tmp/probe.o" "$tmp/probe.bin" 2>/dev/null ||
		{ rm -rf "$tmp"
		  bail "$OBJCOPY cannot read objects built by '$CC'; install" \
		       "binutils-multiarch or set OBJCOPY=llvm-objcopy"; }
	# $ARCH only chooses which directory of tests runs, so it can disagree
	# with what $CC builds without objtool noticing -- and the result is the
	# wrong set of tests, quietly.
	case "$($READELF -hW "$tmp/probe.o" | sed -n 's/.*Machine: *//p')" in
	*X86-64*|*Intel*80386*)	cc_arch=x86 ;;
	*AArch64*)		cc_arch=arm64 ;;
	*)			cc_arch= ;;
	esac
	rm -rf "$tmp"

	# Normalize to the kernel's SRCARCH.
	case "${ARCH:-$(uname -m)}" in
	x86_64|i?86)	arch=x86 ;;
	aarch64*)	arch=arm64 ;;
	*)		arch="${ARCH:-$(uname -m)}" ;;
	esac

	case "$(uname -m)" in
	x86_64|i?86)	host=x86 ;;
	aarch64*)	host=arm64 ;;
	*)		host="$(uname -m)" ;;
	esac

	[ -z "$cc_arch" ] || [ "$cc_arch" = "$arch" ] ||
		bail "ARCH says $arch but '$CC' builds $cc_arch objects;" \
		     "the $arch tests would run against the wrong architecture"

	KLP_TEST_ARCH="$arch"
	KLP_TEST_PREFLIGHT=done
	export OBJTOOL CC KLP_TEST_ARCH KLP_TEST_PREFLIGHT

	cc_version="$($CC --version 2>/dev/null | head -1)"
	cat <<EOF
# preflight
#   objtool   $OBJTOOL (klp: yes)
#   compiler  $cc_version
#   arch      $KLP_TEST_ARCH$([ "$arch" = "$host" ] || echo "  (host $host, cross)")
#   tmpdir    ${TMPDIR:-/tmp}  (each test builds in a fresh directory here)
EOF
}

[ -n "${KLP_TEST_PREFLIGHT:-}" ] || klp_preflight

# What every fixture is built with.  These describe the kernel a fixture stands
# in for; -c is build_one's contract rather than a property of that kernel, so
# it lives at the compile where an override cannot drop it.
#
#   -O2					the kernel's default
#   -ffunction-sections -fdata-sections	klp-build passes these itself, through
#					KCFLAGS, whatever the configuration
#   -fno-asynchronous-unwind-tables	arch/x86/Makefile sets this always, so
#					kernel objects carry no .eh_frame
#   -fno-common				the kernel's Makefile sets it, so an
#					uninitialised global there lands in
#					.bss rather than being SHN_COMMON,
#					which has no section and so no
#					checksum
#
# A test overrides it; see tools/objtool/Documentation/klp-write-tests.txt.
FIXTURE_CFLAGS="-O2 -ffunction-sections -fdata-sections -fno-common \
		-fno-asynchronous-unwind-tables"

test_name="$(basename "$0" .sh)"
workdir=

# The pair run_diff() and the checksum helpers work on.  build_pair() names
# them again and make_vmlinux_pair() repoints orig_obj at the image it links,
# but a test which builds its objects itself with build_one() sets neither, so
# the default belongs here.
orig_obj=orig.o
patched_obj=patched.o

pass() { KLP_TEST_REPORTED=1; echo "ok - $test_name${1:+: $1}"; exit 0; }
fail() { KLP_TEST_FAILED=1; echo "not ok - $test_name: $*"; exit 1; }

# Two kinds of skip, and the runner tells them apart.
#
#   declared_skip  the test said in advance it does not apply here, e.g.
#                  gcc_only on a clang run.  Expected indefinitely.
#   probe_skip     the construct did not turn up in the built object this
#                  time.  Weaker: it may appear on another compiler version,
#                  and one which becomes permanent is a fixture that quietly
#                  stopped testing anything.
#
# A bare skip() is neither, and the runner counts it as a failure: a test which
# gives up for a reason it never declared is a hole, not an outcome.
declared_skip()
{
	KLP_TEST_REPORTED=1
	echo "ok - $test_name # SKIP (declared) $*"
	exit 0
}

probe_skip()
{
	KLP_TEST_REPORTED=1
	echo "ok - $test_name # SKIP (probe) $*"
	exit 0
}
skip()          { echo "ok - $test_name # SKIP $*"; exit 0; }

# TAP directives.  A test which is known to fail reports it rather than being
# commented out and forgotten, and one which starts passing again says so
# instead of quietly going green: the expectation has to be removed by hand,
# which is the point.
xfail()
{
	KLP_TEST_REPORTED=1
	echo "not ok - $test_name${1:+: $1} # TODO known failure"
	exit 0
}

xpass()
{
	KLP_TEST_FAILED=1
	echo "ok - $test_name${1:+: $1} # TODO expected failure, but passed"
	exit 1
}

# cleanup [exit]
#
# Called with "exit" from the trap, when the test is over and what it built may
# be worth keeping.  Called bare by a test which has finished with one segment
# and is about to setup() another: that one is done with, whatever the outcome
# of the segments still to come, so it goes.
cleanup()
{
	[ -n "$workdir" ] || return 0

	# run-tests.sh exports KLP_TEST_KEEP, having validated it; a test run on
	# its own reads KEEP itself, so the same setting means the same thing
	# either way.
	case "${KLP_TEST_KEEP:-${KEEP:-failed}}" in
	all)	return 0 ;;
	none)	rm -rf "$workdir" ;;
	failed|*)
		[ "${1:-}" = exit ] || {
			rm -rf "$workdir"
			return 0
		}
		# Keep what the runner is going to point at.  It counts as a
		# failure anything which did not report an expected outcome --
		# including a test which died before printing one, and an
		# undeclared skip -- and none of those set KLP_TEST_FAILED, so
		# the question to ask is whether a result was reported at all.
		# An exit status cannot answer it: a test killed by a signal
		# runs this trap with the status of whatever ran last.
		[ -n "${KLP_TEST_REPORTED:-}" ] && [ -z "${KLP_TEST_FAILED:-}" ] && {
			rm -rf "$workdir"
			return 0
		}
		# run on its own there is no runner to say where it was kept
		[ -n "${KLP_TEST_WORKDIR:-}" ] ||
			echo "# kept $workdir"
		;;
	esac
}

# setup [exported symbol...]
setup()
{
	if [ -n "${KLP_TEST_WORKDIR:-}" ]; then
		workdir="$KLP_TEST_WORKDIR"
		mkdir -p "$workdir" || fail "cannot create $workdir"
	else
		workdir="$(mktemp -d)" || fail "mktemp failed"
	fi
	trap 'cleanup exit' EXIT

	export_syms "$@"
}

# export_syms [symbol...]
#
# Rewrite Module.symvers so exactly these symbols are exported by vmlinux.
# Whether a symbol is listed decides between an ordinary relocation and a klp
# relocation, so tests flip it to cover both.
export_syms()
{
	: > "$workdir/Module.symvers"
	add_exports vmlinux "$@"
}

# add_exports <object> [symbol...]
#
# Append exports owned by one object, without clearing what is already there,
# so a test can describe a kernel where several objects export things.
#
# Which object owns a symbol is not cosmetic: a reference to a vmlinux symbol
# is applied when the patch module loads, and a reference to a module's symbol
# when that patched module loads, so klp diff files them in different sections.
add_exports()
{
	local owner="$1"; shift

	add_exports_ns "$owner" "" "$@"
}

# add_exports_ns <object> <namespace> [symbol...]
#
# Exports in a symbol namespace, the last field of a Module.symvers line.
#
# A "module:<names>" namespace is EXPORT_SYMBOL_FOR_MODULES(), where the module
# loader grants access by matching the importing module's name against the
# list.  A livepatch module is never on that list, so such a symbol has to be
# referenced the way an unexported one is.  Ordinary namespaces are not
# special here.
add_exports_ns()
{
	local owner="$1" ns="$2"; shift 2

	local sym

	for sym in "$@"; do
		printf '0x00000000\t%s\t%s\tEXPORT_SYMBOL\t%s\n' \
			"$sym" "$owner" "$ns" >> "$workdir/Module.symvers"
	done
}

# gcc_only / clang_only <reason>
gcc_only()
{
	case "$($CC --version 2>/dev/null | head -1)" in
	*[Gg][Cc][Cc]*)	return 0 ;;
	esac
	declared_skip "gcc only${1:+: $1}"
}

clang_only()
{
	case "$($CC --version 2>/dev/null | head -1)" in
	*clang*)	return 0 ;;
	esac
	declared_skip "clang only${1:+: $1}"
}

# build_one <fixture.c> <output object> [cflags...]
build_one()
{
	local fixture out
	fixture="$FIXTURES_DIR/$1"
	out="$workdir/$2"
	shift 2

	[ -f "$fixture" ] || fail "missing fixture $fixture"

	# run_checksum only runs once per workdir.  A fresh object has no
	# checksums in it, so anything built now needs that to happen again.
	rm -f "$workdir/.checksummed"

	$CC -c $FIXTURE_CFLAGS "$@" -o "$out" "$fixture" 2>"$workdir/cc.log" ||
		fail "$(basename "$fixture") does not build: $(tail -1 "$workdir/cc.log")"
}

# build_pair <fixture.c> [cflags...]
build_pair()
{
	local fixture="$1"; shift

	# Name what this builds.  A test may run several segments, and
	# make_vmlinux_pair() repoints orig_obj at the image it links, so
	# without this the next run_diff() would still be reading that.
	orig_obj=orig.o
	patched_obj=patched.o

	build_one "$fixture" orig.o "$@"
	build_one "$fixture" patched.o "$@" -DPATCHED
}

# run_objtool_check <objtool arguments...>
#
# Run objtool's ordinary check pass over the pair, as the kernel build does.
#
# Some of what klp diff consumes is produced by this pass rather than by the
# compiler: .static_call_sites, .mcount_loc, .ibt_endbr_seal, ORC.
#
# Only module objects see it before klp-build -- with CONFIG_KLP_BUILD the
# per-object pass is deferred, so built-in objects reach klp diff exactly as
# the compiler left them.
run_objtool_check()
{
	local obj

	# This rewrites both objects, so checksums taken before it describe
	# something that no longer exists.  As in build_one(), drop the marker
	# so run_checksum() takes them again.
	rm -f "$workdir/.checksummed"

	for obj in "$orig_obj" "$patched_obj"; do
		"$OBJTOOL" "$@" "$workdir/$obj" ||
			fail "objtool $* failed on $obj"
	done
}

run_checksum()
{
	# Checksums live in the objects, and a test may ask for them more than
	# once -- diffing the same pair again with a different Module.symvers,
	# say.  objtool does the right thing when asked twice, leaving the
	# object alone, but it says so, and that warning would be most of what
	# a passing run prints.  Remember instead, and keep quiet.
	[ -e "$workdir/.checksummed" ] && return 0

	"$OBJTOOL" klp checksum "$workdir/$orig_obj" ||
		fail "klp checksum $orig_obj failed"
	"$OBJTOOL" klp checksum "$workdir/$patched_obj" ||
		fail "klp checksum $patched_obj failed"
	touch "$workdir/.checksummed"
}

# run_diff [expected exit status]
run_diff()
{
	local expect="${1:-0}" rc=0

	run_checksum

	# klp diff looks for Module.symvers relative to the working directory.
	( cd "$workdir" && "$OBJTOOL" klp diff "$orig_obj" "$patched_obj" out.o ) \
		> "$workdir/diff.log" 2>&1 || rc=$?

	[ "$rc" = "$expect" ] ||
		fail "klp diff exited $rc, expected $expect: $(tail -2 "$workdir/diff.log")"
}

cc_supports()
{
	echo 'int f(void) { return 0; }' > "$workdir/flagtest.c"
	$CC $1 -c "$workdir/flagtest.c" -o "$workdir/flagtest.o" 2>/dev/null
}

# partial_link <output> <object...>
#
# "ld -r" through the compiler driver so the link targets the same
# architecture as the objects.
partial_link()
{
	local out="$1"; shift

	rm -f "$workdir/.checksummed"

	$CC -r -nostdlib -o "$out" "$@" 2>/dev/null ||
		$CC -r -nostdlib -fuse-ld=lld -o "$out" "$@" 2>/dev/null
}

# link_vmlinux <output> <object...>
#
# Link objects into an executable, the way the kernel's final link produces
# vmlinux from vmlinux.o.  Entry point 0 and no libc: nothing runs it, it only
# has to be a linked image with resolved addresses.
#
# The sub-sections have to come out in name order rather than object order,
# the way the kernel's linker script gathers .text.unlikely and .data.. apart
# from the rest.  That reordering is the entire reason .klp.symid exists: a
# link which preserves order cannot tell a correct sympos from one that merely
# counted, and the caller checks the two orders really did diverge.
#
# A linker script rather than --sort-section=name, because lld accepts that
# option and ignores it -- so on a host where only lld can link the target, the
# test would quietly stop testing the thing it is named for.
#
# Three attempts because a cross run has neither $LD nor the compiler's default
# linker able to touch the target: on an arm64 host linking x86 objects, only
# lld will do it.
link_vmlinux()
{
	local out="$1" lds="$workdir/sort.lds"; shift

	echo 'SECTIONS { .data : { *(SORT_BY_NAME(.data.*)) } }' > "$lds"

	$LD -e 0 -T "$lds" -o "$out" "$@" 2>/dev/null ||
		$CC -nostdlib -Wl,-e,0 -Wl,-T,"$lds" \
			-o "$out" "$@" 2>/dev/null ||
		$CC -nostdlib -fuse-ld=lld -Wl,-e,0 -Wl,-T,"$lds" \
			-o "$out" "$@" 2>/dev/null
}

# make_vmlinux_pair <orig object...> -- <patched object...>
#
# Build the vmlinux.o / vmlinux pair klp diff needs to resolve sympos the way
# it does for built-in code, and point the diff at it.
#
# For a module, sympos is a count in symbol table order, which klp diff can do
# from the object alone.  vmlinux is different: the final link reorders
# sub-sections, so the position comes from the linked image, bridged by
# .klp.symid.  klp diff only looks for that when the object it was handed is
# called vmlinux.o and a vmlinux sits beside it -- so both the name and the
# linked image matter.
make_vmlinux_pair()
{
	local orig=() patched=() seen= arg

	for arg in "$@"; do
		if [ "$arg" = -- ]; then seen=y; continue; fi
		if [ -n "$seen" ]; then patched+=( "$arg" ); else orig+=( "$arg" ); fi
	done

	# Both sides have to have been named.  Without this, forgetting the --
	# leaves one list empty, the link of nothing fails, and the test skips
	# saying the toolchain cannot link -- which is a test bug wearing the
	# costume of an environment one.
	[ "${#orig[@]}" -gt 0 ] && [ "${#patched[@]}" -gt 0 ] ||
		fail "make_vmlinux_pair needs objects either side of --"

	partial_link "$workdir/vmlinux.o" "${orig[@]}" ||
		probe_skip "partial link unavailable"
	partial_link "$workdir/patched.o" "${patched[@]}" ||
		probe_skip "partial link unavailable"

	"$OBJTOOL" --klp-symids --link "$workdir/vmlinux.o" ||
		fail "objtool --klp-symids failed"

	link_vmlinux "$workdir/vmlinux" "$workdir/vmlinux.o" ||
		probe_skip "cannot link a vmlinux here"

	orig_obj=vmlinux.o
}

# build_module_pair <fixture.c> <module name> [cflags...]
#
# Build the pair as objects belonging to a module rather than to vmlinux.  klp
# diff reads the object's module name from .modinfo, and that decides which
# object a relocation is attributed to and whether a reference counts as
# cross-module, so a good deal of the code has a module path the vmlinux
# fixtures never reach.
#
# The fixture defines its .modinfo name from MODNAME.  Passing that through
# -D needs two levels of quoting, which is easy to get wrong at the call site.
build_module_pair()
{
	local fixture="$1" modname="$2"; shift 2

	build_pair "$fixture" -DMODNAME="\"$modname\"" "$@"
}

# find_thinlto_toolchain
#
# Set $THIN_LD to an lld from the same LLVM release as $CC (or THIN_CC).  A
# mismatched pair fails with "Invalid summary version", which reads like a
# broken test rather than a broken environment.
#
# ThinLTO is clang-only; callers must use clang_only before calling this.
# Only $CC (or an explicit THIN_CC override) is consulted -- the harness does
# not search for a second compiler beside a gcc $CC.
find_thinlto_toolchain()
{
	local cc ver ld

	for cc in "${THIN_CC:-}" "$CC"; do
		[ -n "$cc" ] || continue
		command -v "${cc%% *}" >/dev/null 2>&1 || return 1

		ver=$($cc -dumpversion 2>/dev/null | cut -d. -f1)

		for ld in "${THIN_LD:-}" "ld.lld-$ver" ld.lld; do
			[ -n "$ld" ] || continue
			command -v "$ld" >/dev/null 2>&1 || continue

			echo 'int probe(void) { return 0; }' > "$workdir/probe.c"
			$cc -flto=thin -O2 -c "$workdir/probe.c" \
				-o "$workdir/probe.o" 2>/dev/null || continue
			"$ld" -r "$workdir/probe.o" -o "$workdir/probe.elf" \
				2>/dev/null || continue

			THIN_CC="$cc"
			THIN_LD="$ld"
			return 0
		done
	done

	return 1
}

out_sections() { $READELF -S -W "$workdir/out.o" 2>/dev/null; }
out_relocs()   { $READELF -r -W "$workdir/out.o" 2>/dev/null; }
out_symbols()  { $READELF -s -W "$workdir/out.o" 2>/dev/null; }
diff_log()     { cat "$workdir/diff.log"; }

# out_strings <section>
#
# The strings in one section of the output, for the names livepatch matches on.
out_strings() { $READELF -p "$1" "$workdir/out.o" 2>/dev/null; }

# Checks on the input objects, to run before klp diff.  The two forms differ in
# what an absent construct means:
#
#   require_*  the compiler cannot produce it here	-> skip
#   assert_*   the fixture is supposed to produce it	-> fail

in_sections() { $READELF -S -W "$workdir/$1" 2>/dev/null; }
in_symbols()  { $READELF -s -W "$workdir/$1" 2>/dev/null; }
in_relocs()   { $READELF -r -W "$workdir/$1" 2>/dev/null; }

# count_input_symbols <object> <name>
#
# How many object symbols of exactly that name the input has.  Deliberately not
# a grep: readelf lists section symbols too, and a newer binutils prints their
# name -- ".data.<name>" -- where an older one leaves the column blank.  A dot
# is not a word character, so "grep -w <name>" counts that line as well, and
# the same object gives a different answer depending on which readelf reads it.
count_input_symbols()
{
	in_symbols "$1" | awk -v n="$2" '$4 == "OBJECT" && $8 == n' | wc -l
}

# re_quote <string>
#
# A string as a literal basic regular expression.  Nearly every name these
# assertions match on contains a dot -- .text.target, .klp.rela.vmlinux -- and
# an unescaped dot matches any character, so an assertion for one section can be
# satisfied by a different one whose name merely lines up.
re_quote() { printf '%s' "$1" | sed 's|[].[^$*\\/]|\\&|g'; }

has_input_section() { in_sections "$1" | grep -q "[[:space:]]$(re_quote "$2")[[:space:]]"; }
has_input_symbol()  { in_symbols "$1" | awk -v n="$2" '$NF == n' | grep -q .; }

assert_input_section()
{
	local obj

	for obj in "$orig_obj" "$patched_obj"; do
		has_input_section "$obj" "$1" ||
			fail "fixture produced no section '$1' in $obj"
	done
}

assert_input_symbol()
{
	local obj

	for obj in "$orig_obj" "$patched_obj"; do
		has_input_symbol "$obj" "$1" ||
			fail "fixture produced no symbol '$1' in $obj"
	done
}

require_input_section()
{
	local obj

	for obj in "$orig_obj" "$patched_obj"; do
		has_input_section "$obj" "$1" ||
			probe_skip "compiler produced no section '$1' here"
	done
}

assert_section()
{
	out_sections | grep -q "[[:space:]]$(re_quote "$1")[[:space:]]" ||
		fail "expected section '$1' in output"
}

assert_no_section()
{
	out_sections | grep -q "[[:space:]]$(re_quote "$1")[[:space:]]" &&
		fail "unexpected section '$1' in output"
	return 0
}

assert_patched()
{
	assert_section ".text.$1"
}

assert_not_patched()
{
	out_sections | grep -q "[[:space:]]$(re_quote ".text.$1")[[:space:]]" &&
		fail "function '$1' should not have been cloned"
	return 0
}

# section_relocs <section>
#
# The relocations against one section.  readelf prints every relocation section
# in turn, so a test asking about ".smp_locks" has to cut its block out of the
# listing first.
section_relocs()
{
	local sec="${1//./\\.}"

	out_relocs | awk "/rela$sec'/,/^\$/"
}

assert_reloc_sym()
{
	section_relocs "$1" | awk -v n="$2" '$5 == n' | grep -q . ||
		fail "expected a relocation to '$2' in '$1'"
}

assert_no_reloc_sym()
{
	section_relocs "$1" | awk -v n="$2" '$5 == n' | grep -q . &&
		fail "unexpected relocation to '$2' in '$1'"
	return 0
}

# assert_reloc_count <section> <count>
#
# Counts relocation entries, not header or blank lines: whether a special
# section entry was extracted once, twice or not at all is usually the whole
# question.
#
# A count of zero is ambiguous on its own -- a section with no relocations and
# no section at all both read as zero -- so require the section to exist.  A
# test expecting nothing there wants assert_no_section.
assert_reloc_count()
{
	local n

	assert_section "$1"

	n="$(section_relocs "$1" | grep -cE '^[0-9a-f]{8,}')"
	[ "$n" = "$2" ] ||
		fail "expected $2 relocations in '$1', found $n"
}

# assert_klp_sym <symbol> [object]
#
# A klp symbol is named .klp.sym.<object>.<symbol>,<sympos>.  The object
# defaults to any, since most tests care that the reference was converted at
# all rather than which object it resolved against.
assert_klp_sym()
{
	out_symbols | grep -q "\.klp\.sym\.${2:-[^.]*}\.$(re_quote "$1")," ||
		fail "expected klp symbol for '$1'"
}

# assert_klp_sympos <symbol> <sympos>
#
# The number after the comma in .klp.sym.<object>.<symbol>,<sympos> says which
# of several same-named symbols livepatch should resolve to, counting from 1;
# 0 means the name is unique and no disambiguation is needed.  Resolving to the
# wrong one is not a load failure, it is a patch quietly wired to the wrong
# object.
assert_klp_sympos()
{
	out_symbols | grep -qE "\.klp\.sym\.[^.]+\.$(re_quote "$1"),$2([[:space:]]|\$)" ||
		fail "expected klp symbol for '$1' with sympos $2, found:$(
			out_symbols | grep -o "\.klp\.sym\.[^.]*\.$(re_quote "$1"),[0-9]*" |
			sort -u | tr '\n' ' ')"
}

assert_no_klp_sym()
{
	out_symbols | grep -q "\.klp\.sym\.${2:-[^.]*}\.$(re_quote "$1")," &&
		fail "unexpected klp symbol for '$1'"
	return 0
}

assert_tombstone()
{
	out_symbols | grep -qE "\.klp\.tombstone\.$(re_quote "$1")([[:space:]]|\$)" ||
		fail "expected a tombstone for '$1'"
}

assert_symbol()
{
	out_symbols | awk -v n="$1" '$NF == n' | grep -q . ||
		fail "expected symbol '$1' in output"
}

assert_no_symbol()
{
	out_symbols | awk -v n="$1" '$NF == n' | grep -q . &&
		fail "unexpected symbol '$1' in output"
	return 0
}

# assert_diff_log <regex>
#
# klp diff's combined output, for tests asserting on a diagnostic.  Error
# messages are part of the interface when the whole point is that a construct
# gets rejected, and a rejection for the wrong reason is not a pass.
assert_diff_log()
{
	diff_log | grep -qE -- "$1" ||
		fail "expected '$1' in klp diff output: $(tail -2 "$workdir/diff.log")"
}

# checksum_of <object> <symbol>
#
# The checksum "klp checksum" recorded for one symbol, as a hex string.
#
# .discard.sym_checksum is an array of { u64 addr; u64 checksum; }, where addr
# is the target of a relocation naming the symbol.  Nothing in the section
# itself says which symbol an entry belongs to, so the relocation is what
# locates the entry; the checksum is the eight bytes after it.
# Callers use this in a command substitution, where fail() would only exit the
# subshell and the test would carry on with an empty checksum.  So this returns
# non-zero and prints nothing, and the assertions below check for that.  For
# the same reason it does not run run_checksum() itself: that one does call
# fail(), and from in here the message would be captured as the checksum
# rather than ending the test.  The caller runs it first.
checksum_of()
{
	local obj="$workdir/$1" sym="$2" off

	off="$($READELF -rW "$obj" 2>/dev/null |
	       awk -v s="$sym" '/rela\.discard\.sym_checksum/,/^$/ {
			if ($5 == s) { print $1; exit }
	       }')"

	[ -n "$off" ] || return 1

	$OBJCOPY -O binary --only-section=.discard.sym_checksum \
		"$obj" "$workdir/checksums.bin" 2>/dev/null || return 1

	dd if="$workdir/checksums.bin" bs=1 skip=$((16#$off + 8)) count=8 \
		status=none | od -An -tx1 | tr -d ' \n'
}

# assert_checksum_differs <symbol> / assert_checksum_matches <symbol>
#
# Compare what klp checksum recorded for a symbol in the original against the
# patched object.  This is what decides whether klp diff treats a function as
# changed, so a test asserting only that the right functions were cloned cannot
# tell a correct checksum from one which happens to differ.
checksum_pair()
{
	run_checksum

	orig_checksum="$(checksum_of "$orig_obj" "$1")"
	patched_checksum="$(checksum_of "$patched_obj" "$1")"

	[ -n "$orig_checksum" ] ||
		fail "no checksum recorded for '$1' in $orig_obj"
	[ -n "$patched_checksum" ] ||
		fail "no checksum recorded for '$1' in $patched_obj"
}

assert_checksum_differs()
{
	checksum_pair "$1"

	[ "$orig_checksum" != "$patched_checksum" ] ||
		fail "checksum for '$1' unchanged at $orig_checksum, expected it to differ"
}

assert_checksum_matches()
{
	checksum_pair "$1"

	[ "$orig_checksum" = "$patched_checksum" ] ||
		fail "checksum for '$1' changed from $orig_checksum to" \
		     "$patched_checksum, expected no change"
}

# run_post_link [expected exit status]
#
# klp post-link runs last in a livepatch build, converting the intermediate
# __klp_relocs.* sections into the .klp.rela.* form the kernel consumes.  It
# needs nothing but an object containing those sections, which is what klp diff
# produces, so it runs on out.o here rather than on a built module.  Rewrites
# out.o in place, so the out_* helpers show the result afterwards.
run_post_link()
{
	local expect="${1:-0}" rc=0

	"$OBJTOOL" klp post-link "$workdir/out.o" \
		> "$workdir/post-link.log" 2>&1 || rc=$?

	[ "$rc" = "$expect" ] ||
		fail "klp post-link exited $rc, expected $expect:" \
		     "$(tail -2 "$workdir/post-link.log")"
}

# The flags readelf prints for a section, or nothing when it has none.  The
# leading "[nn]" index is stripped first so the columns can be counted.
section_flags()
{
	out_sections | sed 's/^ *\[[ 0-9]*\] *//' |
		awk -v s="$1" '$1 == s && $7 ~ /^[A-Za-z]+$/ { print $7 }'
}

# assert_section_flag <section> <letter>
#
# SHF_RELA_LIVEPATCH is OS-specific, so readelf renders it as "o".  A klp rela
# section which lost it is an ordinary rela section, which the linker may apply
# and the livepatch code will not.
assert_section_flag()
{
	local flags; flags="$(section_flags "$1")"

	[ -n "$flags" ] ||
		fail "section '$1' has no flags, expected '$2'"
	case "$flags" in
	*"$2"*)	;;
	*)	fail "section '$1' has flags '$flags', expected '$2'" ;;
	esac
}

# assert_klp_rela <object> <section>
#
# post-link names the converted sections .klp.rela.<object>.<section>, one per
# base section.  Also checks SHF_RELA_LIVEPATCH, since the name alone is not
# what makes the kernel process it.
assert_klp_rela()
{
	local name=".klp.rela.$1.$2"

	out_sections | grep -q "[[:space:]]$(re_quote "$name")[[:space:]]" ||
		fail "expected section '$name' in output"

	assert_section_flag "$name" o
}

# assert_livepatch_sym <symbol>
#
# Symbols a klp relocation resolves against live in SHN_LIVEPATCH, which
# readelf prints as "OS [0xff20]" -- llvm-readelf without the space, so match
# either.  The kernel resolves these itself at patch load; anything else is a
# symbol the module loader will try, and fail, to resolve normally.
assert_livepatch_sym()
{
	out_symbols | grep -E 'OS ?\[0xff20\]' |
		grep -qE "\.klp\.sym\.[^.]+\.$(re_quote "$1")," ||
		fail "expected a klp symbol for '$1' in SHN_LIVEPATCH"
}
