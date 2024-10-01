#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Translate stack dump function offsets.
#
# addr2line doesn't work with KASLR addresses.  This works similarly to
# addr2line, but instead takes the 'func+0x123' format as input:
#
#   $ ./scripts/faddr2line ~/k/vmlinux meminfo_proc_show+0x5/0x568
#   meminfo_proc_show+0x5/0x568:
#   meminfo_proc_show at fs/proc/meminfo.c:27
#
# If the address is part of an inlined function, the full inline call chain is
# printed:
#
#   $ ./scripts/faddr2line ~/k/vmlinux native_write_msr+0x6/0x27
#   native_write_msr+0x6/0x27:
#   arch_static_branch at arch/x86/include/asm/msr.h:121
#    (inlined by) static_key_false at include/linux/jump_label.h:125
#    (inlined by) native_write_msr at arch/x86/include/asm/msr.h:125
#
# The function size after the '/' in the input is optional, but recommended.
# It's used to help disambiguate any duplicate symbol names, which can occur
# rarely.  If the size is omitted for a duplicate symbol then it's possible for
# multiple code sites to be printed:
#
#   $ ./scripts/faddr2line ~/k/vmlinux raw_ioctl+0x5
#   raw_ioctl+0x5/0x20:
#   raw_ioctl at drivers/char/raw.c:122
#
#   raw_ioctl+0x5/0xb1:
#   raw_ioctl at net/ipv4/raw.c:876
#
# Multiple addresses can be specified on a single command line:
#
#   $ ./scripts/faddr2line ~/k/vmlinux type_show+0x10/45 free_reserved_area+0x90
#   type_show+0x10/0x2d:
#   type_show at drivers/video/backlight/backlight.c:213
#
#   free_reserved_area+0x90/0x123:
#   free_reserved_area at mm/page_alloc.c:6429 (discriminator 2)


set -o errexit
set -o nounset

usage() {
	echo "usage: faddr2line [--list] <object file> <func+offset> <func+offset>..." >&2
	exit 1
}

warn() {
	echo "$1" >&2
}

die() {
	echo "ERROR: $1" >&2
	exit 1
}

UTIL_SUFFIX=""
if [[ "${LLVM:-}" == "" ]]; then
	UTIL_PREFIX=${CROSS_COMPILE:-}
else
	UTIL_PREFIX=llvm-

	if [[ "${LLVM}" == *"/" ]]; then
		UTIL_PREFIX=${LLVM}${UTIL_PREFIX}
	elif [[ "${LLVM}" == "-"* ]]; then
		UTIL_SUFFIX=${LLVM}
	fi
fi

READELF="${UTIL_PREFIX}readelf${UTIL_SUFFIX}"
ADDR2LINE="${UTIL_PREFIX}addr2line${UTIL_SUFFIX}"
AWK="awk"
GREP="grep"

command -v ${AWK} >/dev/null 2>&1 || die "${AWK} isn't installed"
command -v ${READELF} >/dev/null 2>&1 || die "${READELF} isn't installed"
command -v ${ADDR2LINE} >/dev/null 2>&1 || die "${ADDR2LINE} isn't installed"

# Try to figure out the source directory prefix so we can remove it from the
# addr2line output.  HACK ALERT: This assumes that start_kernel() is in
# init/main.c!  This only works for vmlinux.  Otherwise it falls back to
# printing the absolute path.
find_dir_prefix() {
	local start_kernel_addr=$(echo "${ELF_SYMS}" | sed 's/\[.*\]//' |
		${AWK} '$8 == "start_kernel" {printf "0x%s", $2}')
	[[ -z $start_kernel_addr ]] && return

	run_addr2line ${start_kernel_addr} ""
	[[ -z $ADDR2LINE_OUT ]] && return

	local file_line=${ADDR2LINE_OUT#* at }
	if [[ -z $file_line ]] || [[ $file_line = $ADDR2LINE_OUT ]]; then
		return
	fi
	local prefix=${file_line%init/main.c:*}
	if [[ -z $prefix ]] || [[ $prefix = $file_line ]]; then
		return
	fi

	DIR_PREFIX=$prefix
	return 0
}

run_readelf() {
	local objfile=$1
	local out=$(${READELF} --file-header --section-headers --symbols --wide $objfile)

	# This assumes that readelf first prints the file header, then the section headers, then the symbols.
	# Note: It seems that GNU readelf does not prefix section headers with the "There are X section headers"
	# line when multiple options are given, so let's also match with the "Section Headers:" line.
	ELF_FILEHEADER=$(echo "${out}" | sed -n '/There are [0-9]* section headers, starting at offset\|Section Headers:/q;p')
	ELF_SECHEADERS=$(echo "${out}" | sed -n '/There are [0-9]* section headers, starting at offset\|Section Headers:/,$p' | sed -n '/Symbol table .* contains [0-9]* entries:/q;p')
	ELF_SYMS=$(echo "${out}" | sed -n '/Symbol table .* contains [0-9]* entries:/,$p')
}

check_vmlinux() {
	# vmlinux uses absolute addresses in the section table rather than
	# section offsets.
	IS_VMLINUX=0
	local file_type=$(echo "${ELF_FILEHEADER}" |
		${AWK} '$1 == "Type:" { print $2; exit }')
	if [[ $file_type = "EXEC" ]] || [[ $file_type == "DYN" ]]; then
		IS_VMLINUX=1
	fi
}

init_addr2line() {
	local objfile=$1

	check_vmlinux

	ADDR2LINE_ARGS="--functions --pretty-print --inlines --addresses --exe=$objfile"
	if [[ $IS_VMLINUX = 1 ]]; then
		# If the executable file is vmlinux, we don't pass section names to
		# addr2line, so we can launch it now as a single long-running process.
		coproc ADDR2LINE_PROC (${ADDR2LINE} ${ADDR2LINE_ARGS})
	fi
}

run_addr2line() {
	local addr=$1
	local sec_name=$2

	if [[ $IS_VMLINUX = 1 ]]; then
		# We send to the addr2line process: (1) the address, then (2) a sentinel
		# value, i.e., something that can't be interpreted as a valid address
		# (i.e., ","). This causes addr2line to write out: (1) the answer for
		# our address, then (2) either "?? ??:0" or "0x0...0: ..." (if
		# using binutils' addr2line), or "," (if using LLVM's addr2line).
		echo ${addr} >& "${ADDR2LINE_PROC[1]}"
		echo "," >& "${ADDR2LINE_PROC[1]}"
		local first_line
		read -r first_line <& "${ADDR2LINE_PROC[0]}"
		ADDR2LINE_OUT=$(echo "${first_line}" | sed 's/^0x[0-9a-fA-F]*: //')
		while read -r line <& "${ADDR2LINE_PROC[0]}"; do
			if [[ "$line" == "?? ??:0" ]] || [[ "$line" == "," ]] || [[ $(echo "$line" | ${GREP} "^0x00*: ") ]]; then
				break
			fi
			ADDR2LINE_OUT+=$'\n'$(echo "$line" | sed 's/^0x[0-9a-fA-F]*: //')
		done
	else
		# Run addr2line as a single invocation.
		local sec_arg
		[[ -z $sec_name ]] && sec_arg="" || sec_arg="--section=${sec_name}"
		ADDR2LINE_OUT=$(${ADDR2LINE} ${ADDR2LINE_ARGS} ${sec_arg} ${addr} | sed 's/^0x[0-9a-fA-F]*: //')
	fi
}

__faddr2line() {
	local objfile=$1
	local func_addr=$2
	local dir_prefix=$3
	local print_warnings=$4

	local sym_name=${func_addr%+*}
	local func_offset=${func_addr#*+}
	func_offset=${func_offset%/*}
	local user_size=
	[[ $func_addr =~ "/" ]] && user_size=${func_addr#*/}

	if [[ -z $sym_name ]] || [[ -z $func_offset ]] || [[ $sym_name = $func_addr ]]; then
		warn "bad func+offset $func_addr"
		DONE=1
		return
	fi

	# Go through each of the object's symbols which match the func name.
	# In rare cases there might be duplicates, in which case we print all
	# matches.
	while read line; do
		local fields=($line)
		local sym_addr=0x${fields[1]}
		local sym_elf_size=${fields[2]}
		local sym_sec=${fields[6]}
		local sec_size
		local sec_name

		# Get the section size:
		sec_size=$(echo "${ELF_SECHEADERS}" | sed 's/\[ /\[/' |
			${AWK} -v sec=$sym_sec '$1 == "[" sec "]" { print "0x" $6; exit }')

		if [[ -z $sec_size ]]; then
			warn "bad section size: section: $sym_sec"
			DONE=1
			return
		fi

		# Get the section name:
		sec_name=$(echo "${ELF_SECHEADERS}" | sed 's/\[ /\[/' |
			${AWK} -v sec=$sym_sec '$1 == "[" sec "]" { print $2; exit }')

		if [[ -z $sec_name ]]; then
			warn "bad section name: section: $sym_sec"
			DONE=1
			return
		fi

		# Calculate the symbol size.
		#
		# Unfortunately we can't use the ELF size, because kallsyms
		# also includes the padding bytes in its size calculation.  For
		# kallsyms, the size calculation is the distance between the
		# symbol and the next symbol in a sorted list.
		local sym_size
		local cur_sym_addr
		local found=0
		while read line; do
			local fields=($line)
			cur_sym_addr=0x${fields[1]}
			local cur_sym_elf_size=${fields[2]}
			local cur_sym_name=${fields[7]:-}

			# is_mapping_symbol(cur_sym_name)
			if [[ ${cur_sym_name} =~ ^(\.L|L0|\$) ]]; then
				continue
			fi

			if [[ $cur_sym_addr = $sym_addr ]] &&
			   [[ $cur_sym_elf_size = $sym_elf_size ]] &&
			   [[ $cur_sym_name = $sym_name ]]; then
				found=1
				continue
			fi

			if [[ $found = 1 ]]; then
				sym_size=$(($cur_sym_addr - $sym_addr))
				[[ $sym_size -lt $sym_elf_size ]] && continue;
				found=2
				break
			fi
		done < <(echo "${ELF_SYMS}" | sed 's/\[.*\]//' | ${AWK} -v sec=$sym_sec '$7 == sec' | sort --key=2 | ${GREP} -A1 --no-group-separator " ${sym_name}$")

		if [[ $found = 0 ]]; then
			warn "can't find symbol: sym_name: $sym_name sym_sec: $sym_sec sym_addr: $sym_addr sym_elf_size: $sym_elf_size"
			DONE=1
			return
		fi

		# If nothing was found after the symbol, assume it's the last
		# symbol in the section.
		[[ $found = 1 ]] && sym_size=$(($sec_size - $sym_addr))

		if [[ -z $sym_size ]] || [[ $sym_size -le 0 ]]; then
			warn "bad symbol size: sym_addr: $sym_addr cur_sym_addr: $cur_sym_addr"
			DONE=1
			return
		fi

		sym_size=0x$(printf %x $sym_size)

		# Calculate the address from user-supplied offset:
		local addr=$(($sym_addr + $func_offset))
		if [[ -z $addr ]] || [[ $addr = 0 ]]; then
			warn "bad address: $sym_addr + $func_offset"
			DONE=1
			return
		fi
		addr=0x$(printf %x $addr)

		# If the user provided a size, make sure it matches the symbol's size:
		if [[ -n $user_size ]] && [[ $user_size -ne $sym_size ]]; then
			[[ $print_warnings = 1 ]] &&
				echo "skipping $sym_name address at $addr due to size mismatch ($user_size != $sym_size)"
			continue;
		fi

		# Make sure the provided offset is within the symbol's range:
		if [[ $func_offset -gt $sym_size ]]; then
			[[ $print_warnings = 1 ]] &&
				echo "skipping $sym_name address at $addr due to size mismatch ($func_offset > $sym_size)"
			continue
		fi

		# In case of duplicates or multiple addresses specified on the
		# cmdline, separate multiple entries with a blank line:
		[[ $FIRST = 0 ]] && echo
		FIRST=0

		echo "$sym_name+$func_offset/$sym_size:"

		# Pass section address to addr2line and strip absolute paths
		# from the output:
		run_addr2line $addr $sec_name
		local output=$(echo "${ADDR2LINE_OUT}" | sed "s; $dir_prefix\(\./\)*; ;")
		[[ -z $output ]] && continue

		# Default output (non --list):
		if [[ $LIST = 0 ]]; then
			echo "$output" | while read -r line
			do
				echo $line
			done
			DONE=1;
			continue
		fi

		# For --list, show each line with its corresponding source code:
		echo "$output" | while read -r line
		do
			echo
			echo $line
			n=$(echo $line | sed 's/.*:\([0-9]\+\).*/\1/g')
			n1=$[$n-5]
			n2=$[$n+5]
			f=$(echo $line | sed 's/.*at \(.\+\):.*/\1/g')
			${AWK} 'NR>=strtonum("'$n1'") && NR<=strtonum("'$n2'") { if (NR=='$n') printf(">%d<", NR); else printf(" %d ", NR); printf("\t%s\n", $0)}' $f
		done

		DONE=1

	done < <(echo "${ELF_SYMS}" | sed 's/\[.*\]//' | ${AWK} -v fn=$sym_name '$8 == fn')
}

[[ $# -lt 2 ]] && usage

objfile=$1

LIST=0
[[ "$objfile" == "--list" ]] && LIST=1 && shift && objfile=$1

[[ ! -f $objfile ]] && die "can't find objfile $objfile"
shift

run_readelf $objfile

echo "${ELF_SECHEADERS}" | ${GREP} -q '\.debug_info' || die "CONFIG_DEBUG_INFO not enabled"

init_addr2line $objfile

DIR_PREFIX=supercalifragilisticexpialidocious
find_dir_prefix

FIRST=1
while [[ $# -gt 0 ]]; do
	func_addr=$1
	shift

	# print any matches found
	DONE=0
	__faddr2line $objfile $func_addr $DIR_PREFIX 0

	# if no match was found, print warnings
	if [[ $DONE = 0 ]]; then
		__faddr2line $objfile $func_addr $DIR_PREFIX 1
		warn "no match for $func_addr"
	fi
done
