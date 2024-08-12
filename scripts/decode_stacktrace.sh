#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# (c) 2014, Sasha Levin <sasha.levin@oracle.com>
#set -x

usage() {
	echo "Usage:"
	echo "	$0 -r <release> | <vmlinux> [<base path>|auto] [<modules path>]"
}

# Try to find a Rust demangler
if type llvm-cxxfilt >/dev/null 2>&1 ; then
	cppfilt=llvm-cxxfilt
elif type c++filt >/dev/null 2>&1 ; then
	cppfilt=c++filt
	cppfilt_opts=-i
fi

UTIL_SUFFIX=
if [[ -z ${LLVM:-} ]]; then
	UTIL_PREFIX=${CROSS_COMPILE:-}
else
	UTIL_PREFIX=llvm-
	if [[ ${LLVM} == */ ]]; then
		UTIL_PREFIX=${LLVM}${UTIL_PREFIX}
	elif [[ ${LLVM} == -* ]]; then
		UTIL_SUFFIX=${LLVM}
	fi
fi

READELF=${UTIL_PREFIX}readelf${UTIL_SUFFIX}
ADDR2LINE=${UTIL_PREFIX}addr2line${UTIL_SUFFIX}
NM=${UTIL_PREFIX}nm${UTIL_SUFFIX}

if [[ $1 == "-r" ]] ; then
	vmlinux=""
	basepath="auto"
	modpath=""
	release=$2

	for fn in {,/usr/lib/debug}/boot/vmlinux-$release{,.debug} /lib/modules/$release{,/build}/vmlinux ; do
		if [ -e "$fn" ] ; then
			vmlinux=$fn
			break
		fi
	done

	if [[ $vmlinux == "" ]] ; then
		echo "ERROR! vmlinux image for release $release is not found" >&2
		usage
		exit 2
	fi
else
	vmlinux=$1
	basepath=${2-auto}
	modpath=$3
	release=""
	debuginfod=

	# Can we use debuginfod-find?
	if type debuginfod-find >/dev/null 2>&1 ; then
		debuginfod=${1-only}
	fi

	if [[ $vmlinux == "" && -z $debuginfod ]] ; then
		echo "ERROR! vmlinux image must be specified" >&2
		usage
		exit 1
	fi
fi

declare aarray_support=true
declare -A cache 2>/dev/null
if [[ $? != 0 ]]; then
	aarray_support=false
else
	declare -A modcache
fi

find_module() {
	if [[ -n $debuginfod ]] ; then
		if [[ -n $modbuildid ]] ; then
			debuginfod-find debuginfo $modbuildid && return
		fi

		# Only using debuginfod so don't try to find vmlinux module path
		if [[ $debuginfod == "only" ]] ; then
			return
		fi
	fi

	if [[ "$modpath" != "" ]] ; then
		for fn in $(find "$modpath" -name "${module//_/[-_]}.ko*") ; do
			if ${READELF} -WS "$fn" | grep -qwF .debug_line ; then
				echo $fn
				return
			fi
		done
		return 1
	fi

	modpath=$(dirname "$vmlinux")
	find_module && return

	if [[ $release == "" ]] ; then
		release=$(gdb -ex 'print init_uts_ns.name.release' -ex 'quit' -quiet -batch "$vmlinux" 2>/dev/null | sed -n 's/\$1 = "\(.*\)".*/\1/p')
	fi

	for dn in {/usr/lib/debug,}/lib/modules/$release ; do
		if [ -e "$dn" ] ; then
			modpath="$dn"
			find_module && return
		fi
	done

	modpath=""
	return 1
}

parse_symbol() {
	# The structure of symbol at this point is:
	#   ([name]+[offset]/[total length])
	#
	# For example:
	#   do_basic_setup+0x9c/0xbf

	if [[ $module == "" ]] ; then
		local objfile=$vmlinux
	elif [[ $aarray_support == true && "${modcache[$module]+isset}" == "isset" ]]; then
		local objfile=${modcache[$module]}
	else
		local objfile=$(find_module)
		if [[ $objfile == "" ]] ; then
			echo "WARNING! Modules path isn't set, but is needed to parse this symbol" >&2
			return
		fi
		if [[ $aarray_support == true ]]; then
			modcache[$module]=$objfile
		fi
	fi

	# Remove the englobing parenthesis
	symbol=${symbol#\(}
	symbol=${symbol%\)}

	# Strip segment
	local segment
	if [[ $symbol == *:* ]] ; then
		segment=${symbol%%:*}:
		symbol=${symbol#*:}
	fi

	# Strip the symbol name so that we could look it up
	local name=${symbol%+*}

	# Use 'nm vmlinux' to figure out the base address of said symbol.
	# It's actually faster to call it every time than to load it
	# all into bash.
	if [[ $aarray_support == true && "${cache[$module,$name]+isset}" == "isset" ]]; then
		local base_addr=${cache[$module,$name]}
	else
		local base_addr=$(${NM} "$objfile" 2>/dev/null | awk '$3 == "'$name'" && ($2 == "t" || $2 == "T") {print $1; exit}')
		if [[ $base_addr == "" ]] ; then
			# address not found
			return
		fi
		if [[ $aarray_support == true ]]; then
			cache[$module,$name]="$base_addr"
		fi
	fi
	# Let's start doing the math to get the exact address into the
	# symbol. First, strip out the symbol total length.
	local expr=${symbol%/*}

	# Now, replace the symbol name with the base address we found
	# before.
	expr=${expr/$name/0x$base_addr}

	# Evaluate it to find the actual address
	expr=$((expr))
	local address=$(printf "%x\n" "$expr")

	# Pass it to addr2line to get filename and line number
	# Could get more than one result
	if [[ $aarray_support == true && "${cache[$module,$address]+isset}" == "isset" ]]; then
		local code=${cache[$module,$address]}
	else
		local code=$(${ADDR2LINE} -i -e "$objfile" "$address" 2>/dev/null)
		if [[ $aarray_support == true ]]; then
			cache[$module,$address]=$code
		fi
	fi

	# addr2line doesn't return a proper error code if it fails, so
	# we detect it using the value it prints so that we could preserve
	# the offset/size into the function and bail out
	if [[ $code == "??:0" ]]; then
		return
	fi

	# Strip out the base of the path on each line
	code=$(while read -r line; do echo "${line#$basepath/}"; done <<< "$code")

	# In the case of inlines, move everything to same line
	code=${code//$'\n'/' '}

	# Demangle if the name looks like a Rust symbol and if
	# we got a Rust demangler
	if [[ $name =~ ^_R && $cppfilt != "" ]] ; then
		name=$("$cppfilt" "$cppfilt_opts" "$name")
	fi

	# Replace old address with pretty line numbers
	symbol="$segment$name ($code)"
}

debuginfod_get_vmlinux() {
	local vmlinux_buildid=${1##* }

	if [[ $vmlinux != "" ]]; then
		return
	fi

	if [[ $vmlinux_buildid =~ ^[0-9a-f]+ ]]; then
		vmlinux=$(debuginfod-find debuginfo $vmlinux_buildid)
		if [[ $? -ne 0 ]] ; then
			echo "ERROR! vmlinux image not found via debuginfod-find" >&2
			usage
			exit 2
		fi
		return
	fi
	echo "ERROR! Build ID for vmlinux not found. Try passing -r or specifying vmlinux" >&2
	usage
	exit 2
}

decode_code() {
	local scripts=`dirname "${BASH_SOURCE[0]}"`

	echo "$1" | $scripts/decodecode
}

handle_line() {
	if [[ $basepath == "auto" && $vmlinux != "" ]] ; then
		module=""
		symbol="kernel_init+0x0/0x0"
		parse_symbol
		basepath=${symbol#kernel_init (}
		basepath=${basepath%/init/main.c:*)}
	fi

	local words

	# Tokenize
	read -a words <<<"$1"

	# Remove hex numbers. Do it ourselves until it happens in the
	# kernel

	# We need to know the index of the last element before we
	# remove elements because arrays are sparse
	local last=$(( ${#words[@]} - 1 ))

	for i in "${!words[@]}"; do
		# Remove the address
		if [[ ${words[$i]} =~ \[\<([^]]+)\>\] ]]; then
			unset words[$i]
		fi

		# Format timestamps with tabs
		if [[ ${words[$i]} == \[ && ${words[$i+1]} == *\] ]]; then
			unset words[$i]
			words[$i+1]=$(printf "[%13s\n" "${words[$i+1]}")
		fi
	done

	if [[ ${words[$last]} =~ ^[0-9a-f]+\] ]]; then
		words[$last-1]="${words[$last-1]} ${words[$last]}"
		unset words[$last]
		last=$(( $last - 1 ))
	fi

	if [[ ${words[$last]} =~ \[([^]]+)\] ]]; then
		module=${words[$last]}
		# some traces format is "(%pS)", which like "(foo+0x0/0x1 [bar])"
		# so $module may like "[bar])". Strip the right parenthesis firstly
		module=${module%\)}
		module=${module#\[}
		module=${module%\]}
		modbuildid=${module#* }
		module=${module% *}
		if [[ $modbuildid == $module ]]; then
			modbuildid=
		fi
		symbol=${words[$last-1]}
		unset words[$last-1]
	else
		# The symbol is the last element, process it
		symbol=${words[$last]}
		module=
		modbuildid=
	fi

	unset words[$last]
	parse_symbol # modifies $symbol

	# Add up the line number to the symbol
	echo "${words[@]}" "$symbol $module"
}

while read line; do
	# Strip unexpected carriage return at end of line
	line=${line%$'\r'}

	# Let's see if we have an address in the line
	if [[ $line =~ \[\<([^]]+)\>\] ]] ||
	   [[ $line =~ [^+\ ]+\+0x[0-9a-f]+/0x[0-9a-f]+ ]]; then
		# Translate address to line numbers
		handle_line "$line"
	# Is it a code line?
	elif [[ $line == *Code:* ]]; then
		decode_code "$line"
	# Is it a version line?
	elif [[ -n $debuginfod && $line =~ PID:\ [0-9]+\ Comm: ]]; then
		debuginfod_get_vmlinux "$line"
	else
		# Nothing special in this line, show it as is
		echo "$line"
	fi
done
