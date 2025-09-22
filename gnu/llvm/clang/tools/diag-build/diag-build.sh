#!/usr/bin/env bash

# diag-build: a tool showing enabled warnings in a project.
#
# diag-build acts as a wrapper for 'diagtool show-enabled', in the same way
# that scan-build acts as a wrapper for the static analyzer. The common case is
# simple: use 'diag-build make' or 'diag-build xcodebuild' to list the warnings
# enabled for the first compilation command we see. Other build systems require
# you to manually specify "dry-run" and "use $CC and $CXX"; if there is a build
# system you are interested in, please add it to the switch statement.

print_usage () {
    echo 'Usage: diag-build.sh [-v] xcodebuild [flags]'
    echo '       diag-build.sh [-v] make [flags]'
    echo '       diag-build.sh [-v] <other build command>'
    echo
    echo 'diagtool must be in your PATH'
    echo 'If using an alternate build command, you must ensure that'
    echo 'the compiler used matches the CC environment variable.'
}

# Mac OS X's BSD sed uses -E for extended regular expressions,
# but GNU sed uses -r. Find out which one this system accepts.
EXTENDED_SED_FLAG='-E'
echo -n | sed $EXTENDED_SED_FLAG 's/a/b/' 2>/dev/null || EXTENDED_SED_FLAG='-r'

if [[ "$1" == "-v" ]]; then
    verbose=$1
    shift
fi

guessing_cc=0

if [[ -z "$CC" ]]; then
    guessing_cc=1
    if [[ -x $(dirname $0)/clang ]]; then
	CC=$(dirname $0)/clang
    elif [[ ! -z $(which clang) ]]; then
	CC=$(which clang)
    else
	echo -n 'Error: could not find an appropriate compiler'
	echo ' to generate build commands.' 1>&2
	echo 'Use the CC environment variable to set one explicitly.' 1>&2
	exit 1
    fi
fi

if [[ -z "$CXX" ]]; then
    if [[ -x $(dirname $0)/clang++ ]]; then
	CXX=$(dirname $0)/clang++
    elif [[ ! -z $(which clang++) ]]; then
	CXX=$(which clang++)
    else
	CXX=$CC
    fi
fi

diagtool=$(which diagtool)
if [[ -z "$diagtool" ]]; then
    if [[ -x $(dirname $0)/diagtool ]]; then
	diagtool=$(dirname $0)/diagtool
    else
	echo 'Error: could not find diagtool.' 1>&2
	exit 1
    fi
fi


tool=$1
shift

if [[ -z "$tool" ]]; then
    print_usage
    exit 1
elif [[ "$tool" == "xcodebuild" ]]; then
    dry_run='-dry-run'
    set_compiler="CC='$CC' CXX='$CXX'"
elif [[ "$tool" == "make" ]]; then
    dry_run='-n'
    set_compiler="CC='$CC' CXX='$CXX'"
else
    echo "Warning: unknown build system '$tool'" 1>&2
    if [[ $guessing_cc -eq 1 ]]; then
	# FIXME: We really only need $CC /or/ $CXX
	echo 'Error: $CC must be set for other build systems' 1>&2
	exit 1
    fi
fi

escape () {
    echo $@ | sed 's:[]:\\|/.+*?^$(){}[]:\\&:g'
}

escCC=$(escape $CC)
escCXX=$(escape $CXX)
command=$(
    eval $tool $dry_run $set_compiler $@ 2>/dev/null |
    # Remove "if" early on so we can find the right command line.
    sed $EXTENDED_SED_FLAG "s:^[[:blank:]]*if[[:blank:]]{1,}::g" |
    # Combine lines with trailing backslashes
    sed -e :a -e '/\\$/N; s/\\\n//; ta' |
    grep -E "^[[:blank:]]*($escCC|$escCXX)" |
    head -n1 |
    sed $EXTENDED_SED_FLAG "s:($escCC|$escCXX):${diagtool//:/\\:} show-enabled:g"
)

if [[ -z "$command" ]]; then
    echo 'Error: could not find any build commands.' 1>&2
    if [[ "$tool" != "xcodebuild" ]]; then
	# xcodebuild always echoes the compile commands on their own line,
	# but other tools give no such guarantees.
	echo -n 'This may occur if your build system embeds the call to ' 2>&1
	echo -n 'the compiler in a larger expression. ' 2>&1
    fi
    exit 2
fi

# Chop off trailing '&&', '||', and ';'
command=${command%%&&*}
command=${command%%||*}
command=${command%%;*}

[[ -n "$verbose" ]] && echo $command
eval $command
