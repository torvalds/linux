#!/bin/sh
# SPDX-License-Identifier: GPL-2.0+

#
# Runs an individual test module.
#
# kselftest expects a separate executable for each test, this can be
# created by adding a script like this:
#
#   #!/bin/sh
#   SPDX-License-Identifier: GPL-2.0+
#   $(dirname $0)/../kselftest_module.sh "description" module_name
#
# Example: tools/testing/selftests/lib/printf.sh

desc=""				# Output prefix.
module=""			# Filename (without the .ko).
args=""				# modprobe arguments.

modprobe="/sbin/modprobe"

main() {
    parse_args "$@"
    assert_root
    assert_have_module
    run_module
}

parse_args() {
    script=${0##*/}

    if [ $# -lt 2 ]; then
	echo "Usage: $script <description> <module_name> [FAIL]"
	exit 1
    fi

    desc="$1"
    shift || true
    module="$1"
    shift || true
    args="$@"
}

assert_root() {
    if [ ! -w /dev ]; then
	skip "please run as root"
    fi
}

assert_have_module() {
    if ! $modprobe -q -n $module; then
	skip "module $module is not found"
    fi
}

run_module() {
    if $modprobe -q $module $args; then
	$modprobe -q -r $module
	say "ok"
    else
	fail ""
    fi
}

say() {
    echo "$desc: $1"
}


fail() {
    say "$1 [FAIL]" >&2
    exit 1
}

skip() {
    say "$1 [SKIP]" >&2
    # Kselftest framework requirement - SKIP code is 4.
    exit 4
}

#
# Main script
#
main "$@"
