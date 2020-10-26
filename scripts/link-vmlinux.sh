#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# link vmlinux
#
# vmlinux is linked from the objects selected by $(KBUILD_VMLINUX_OBJS) and
# $(KBUILD_VMLINUX_LIBS). Most are built-in.a files from top-level directories
# in the kernel tree, others are specified in arch/$(ARCH)/Makefile.
# $(KBUILD_VMLINUX_LIBS) are archives which are linked conditionally
# (not within --whole-archive), and do not require symbol indexes added.
#
# vmlinux
#   ^
#   |
#   +--< $(KBUILD_VMLINUX_OBJS)
#   |    +--< init/built-in.a drivers/built-in.a mm/built-in.a + more
#   |
#   +--< $(KBUILD_VMLINUX_LIBS)
#   |    +--< lib/lib.a + more
#   |
#   +-< ${kallsymso} (see description in KALLSYMS section)
#
# vmlinux version (uname -v) cannot be updated during normal
# descending-into-subdirs phase since we do not yet know if we need to
# update vmlinux.
# Therefore this step is delayed until just before final link of vmlinux.
#
# System.map is generated to document addresses of all kernel symbols

# Error out on error
set -e

LD="$1"
KBUILD_LDFLAGS="$2"
LDFLAGS_vmlinux="$3"

# Nice output in kbuild format
# Will be supressed by "make -s"
info()
{
	if [ "${quiet}" != "silent_" ]; then
		printf "  %-7s %s\n" "${1}" "${2}"
	fi
}

# Link of vmlinux.o used for section mismatch analysis
# ${1} output file
modpost_link()
{
	local objects

	objects="--whole-archive				\
		${KBUILD_VMLINUX_OBJS}				\
		--no-whole-archive				\
		--start-group					\
		${KBUILD_VMLINUX_LIBS}				\
		--end-group"

	${LD} ${KBUILD_LDFLAGS} -r -o ${1} ${objects}
}

objtool_link()
{
	local objtoolopt;

	if [ -n "${CONFIG_VMLINUX_VALIDATION}" ]; then
		objtoolopt="check"
		if [ -z "${CONFIG_FRAME_POINTER}" ]; then
			objtoolopt="${objtoolopt} --no-fp"
		fi
		if [ -n "${CONFIG_GCOV_KERNEL}" ]; then
			objtoolopt="${objtoolopt} --no-unreachable"
		fi
		if [ -n "${CONFIG_RETPOLINE}" ]; then
			objtoolopt="${objtoolopt} --retpoline"
		fi
		if [ -n "${CONFIG_X86_SMAP}" ]; then
			objtoolopt="${objtoolopt} --uaccess"
		fi
		info OBJTOOL ${1}
		tools/objtool/objtool ${objtoolopt} ${1}
	fi
}

# Link of vmlinux
# ${1} - output file
# ${2}, ${3}, ... - optional extra .o files
vmlinux_link()
{
	local lds="${objtree}/${KBUILD_LDS}"
	local output=${1}
	local objects
	local strip_debug

	info LD ${output}

	# skip output file argument
	shift

	# The kallsyms linking does not need debug symbols included.
	if [ "$output" != "${output#.tmp_vmlinux.kallsyms}" ] ; then
		strip_debug=-Wl,--strip-debug
	fi

	if [ "${SRCARCH}" != "um" ]; then
		objects="--whole-archive			\
			${KBUILD_VMLINUX_OBJS}			\
			--no-whole-archive			\
			--start-group				\
			${KBUILD_VMLINUX_LIBS}			\
			--end-group				\
			${@}"

		${LD} ${KBUILD_LDFLAGS} ${LDFLAGS_vmlinux}	\
			${strip_debug#-Wl,}			\
			-o ${output}				\
			-T ${lds} ${objects}
	else
		objects="-Wl,--whole-archive			\
			${KBUILD_VMLINUX_OBJS}			\
			-Wl,--no-whole-archive			\
			-Wl,--start-group			\
			${KBUILD_VMLINUX_LIBS}			\
			-Wl,--end-group				\
			${@}"

		${CC} ${CFLAGS_vmlinux}				\
			${strip_debug}				\
			-o ${output}				\
			-Wl,-T,${lds}				\
			${objects}				\
			-lutil -lrt -lpthread
		rm -f linux
	fi
}

# generate .BTF typeinfo from DWARF debuginfo
# ${1} - vmlinux image
# ${2} - file to dump raw BTF data into
gen_btf()
{
	local pahole_ver

	if ! [ -x "$(command -v ${PAHOLE})" ]; then
		echo >&2 "BTF: ${1}: pahole (${PAHOLE}) is not available"
		return 1
	fi

	pahole_ver=$(${PAHOLE} --version | sed -E 's/v([0-9]+)\.([0-9]+)/\1\2/')
	if [ "${pahole_ver}" -lt "116" ]; then
		echo >&2 "BTF: ${1}: pahole version $(${PAHOLE} --version) is too old, need at least v1.16"
		return 1
	fi

	vmlinux_link ${1}

	info "BTF" ${2}
	LLVM_OBJCOPY=${OBJCOPY} ${PAHOLE} -J ${1}

	# Create ${2} which contains just .BTF section but no symbols. Add
	# SHF_ALLOC because .BTF will be part of the vmlinux image. --strip-all
	# deletes all symbols including __start_BTF and __stop_BTF, which will
	# be redefined in the linker script. Add 2>/dev/null to suppress GNU
	# objcopy warnings: "empty loadable segment detected at ..."
	${OBJCOPY} --only-section=.BTF --set-section-flags .BTF=alloc,readonly \
		--strip-all ${1} ${2} 2>/dev/null
	# Change e_type to ET_REL so that it can be used to link final vmlinux.
	# Unlike GNU ld, lld does not allow an ET_EXEC input.
	printf '\1' | dd of=${2} conv=notrunc bs=1 seek=16 status=none
}

# Create ${2} .o file with all symbols from the ${1} object file
kallsyms()
{
	info KSYM ${2}
	local kallsymopt;

	if [ -n "${CONFIG_KALLSYMS_ALL}" ]; then
		kallsymopt="${kallsymopt} --all-symbols"
	fi

	if [ -n "${CONFIG_KALLSYMS_ABSOLUTE_PERCPU}" ]; then
		kallsymopt="${kallsymopt} --absolute-percpu"
	fi

	if [ -n "${CONFIG_KALLSYMS_BASE_RELATIVE}" ]; then
		kallsymopt="${kallsymopt} --base-relative"
	fi

	local aflags="${KBUILD_AFLAGS} ${KBUILD_AFLAGS_KERNEL}               \
		      ${NOSTDINC_FLAGS} ${LINUXINCLUDE} ${KBUILD_CPPFLAGS}"

	local afile="`basename ${2} .o`.S"

	${NM} -n ${1} | scripts/kallsyms ${kallsymopt} > ${afile}
	${CC} ${aflags} -c -o ${2} ${afile}
}

# Perform one step in kallsyms generation, including temporary linking of
# vmlinux.
kallsyms_step()
{
	kallsymso_prev=${kallsymso}
	kallsyms_vmlinux=.tmp_vmlinux.kallsyms${1}
	kallsymso=${kallsyms_vmlinux}.o

	vmlinux_link ${kallsyms_vmlinux} "${kallsymso_prev}" ${btf_vmlinux_bin_o}
	kallsyms ${kallsyms_vmlinux} ${kallsymso}
}

# Create map file with all symbols from ${1}
# See mksymap for additional details
mksysmap()
{
	${CONFIG_SHELL} "${srctree}/scripts/mksysmap" ${1} ${2}
}

sorttable()
{
	${objtree}/scripts/sorttable ${1}
}

# Delete output files in case of error
cleanup()
{
	rm -f .btf.*
	rm -f .tmp_System.map
	rm -f .tmp_vmlinux*
	rm -f System.map
	rm -f vmlinux
	rm -f vmlinux.o
}

on_exit()
{
	if [ $? -ne 0 ]; then
		cleanup
	fi
}
trap on_exit EXIT

on_signals()
{
	exit 1
}
trap on_signals HUP INT QUIT TERM

# Use "make V=1" to debug this script
case "${KBUILD_VERBOSE}" in
*1*)
	set -x
	;;
esac

if [ "$1" = "clean" ]; then
	cleanup
	exit 0
fi

# We need access to CONFIG_ symbols
. include/config/auto.conf

# Update version
info GEN .version
if [ -r .version ]; then
	VERSION=$(expr 0$(cat .version) + 1)
	echo $VERSION > .version
else
	rm -f .version
	echo 1 > .version
fi;

# final build of init/
${MAKE} -f "${srctree}/scripts/Makefile.build" obj=init need-builtin=1

#link vmlinux.o
info LD vmlinux.o
modpost_link vmlinux.o
objtool_link vmlinux.o

# modpost vmlinux.o to check for section mismatches
${MAKE} -f "${srctree}/scripts/Makefile.modpost" MODPOST_VMLINUX=1

info MODINFO modules.builtin.modinfo
${OBJCOPY} -j .modinfo -O binary vmlinux.o modules.builtin.modinfo
info GEN modules.builtin
# The second line aids cases where multiple modules share the same object.
tr '\0' '\n' < modules.builtin.modinfo | sed -n 's/^[[:alnum:]:_]*\.file=//p' |
	tr ' ' '\n' | uniq | sed -e 's:^:kernel/:' -e 's/$/.ko/' > modules.builtin

btf_vmlinux_bin_o=""
if [ -n "${CONFIG_DEBUG_INFO_BTF}" ]; then
	btf_vmlinux_bin_o=.btf.vmlinux.bin.o
	if ! gen_btf .tmp_vmlinux.btf $btf_vmlinux_bin_o ; then
		echo >&2 "Failed to generate BTF for vmlinux"
		echo >&2 "Try to disable CONFIG_DEBUG_INFO_BTF"
		exit 1
	fi
fi

kallsymso=""
kallsymso_prev=""
kallsyms_vmlinux=""
if [ -n "${CONFIG_KALLSYMS}" ]; then

	# kallsyms support
	# Generate section listing all symbols and add it into vmlinux
	# It's a three step process:
	# 1)  Link .tmp_vmlinux1 so it has all symbols and sections,
	#     but __kallsyms is empty.
	#     Running kallsyms on that gives us .tmp_kallsyms1.o with
	#     the right size
	# 2)  Link .tmp_vmlinux2 so it now has a __kallsyms section of
	#     the right size, but due to the added section, some
	#     addresses have shifted.
	#     From here, we generate a correct .tmp_kallsyms2.o
	# 3)  That link may have expanded the kernel image enough that
	#     more linker branch stubs / trampolines had to be added, which
	#     introduces new names, which further expands kallsyms. Do another
	#     pass if that is the case. In theory it's possible this results
	#     in even more stubs, but unlikely.
	#     KALLSYMS_EXTRA_PASS=1 may also used to debug or work around
	#     other bugs.
	# 4)  The correct ${kallsymso} is linked into the final vmlinux.
	#
	# a)  Verify that the System.map from vmlinux matches the map from
	#     ${kallsymso}.

	kallsyms_step 1
	kallsyms_step 2

	# step 3
	size1=$(${CONFIG_SHELL} "${srctree}/scripts/file-size.sh" ${kallsymso_prev})
	size2=$(${CONFIG_SHELL} "${srctree}/scripts/file-size.sh" ${kallsymso})

	if [ $size1 -ne $size2 ] || [ -n "${KALLSYMS_EXTRA_PASS}" ]; then
		kallsyms_step 3
	fi
fi

vmlinux_link vmlinux "${kallsymso}" ${btf_vmlinux_bin_o}

# fill in BTF IDs
if [ -n "${CONFIG_DEBUG_INFO_BTF}" -a -n "${CONFIG_BPF}" ]; then
	info BTFIDS vmlinux
	${RESOLVE_BTFIDS} vmlinux
fi

if [ -n "${CONFIG_BUILDTIME_TABLE_SORT}" ]; then
	info SORTTAB vmlinux
	if ! sorttable vmlinux; then
		echo >&2 Failed to sort kernel tables
		exit 1
	fi
fi

info SYSMAP System.map
mksysmap vmlinux System.map

# step a (see comment above)
if [ -n "${CONFIG_KALLSYMS}" ]; then
	mksysmap ${kallsyms_vmlinux} .tmp_System.map

	if ! cmp -s System.map .tmp_System.map; then
		echo >&2 Inconsistent kallsyms data
		echo >&2 Try "make KALLSYMS_EXTRA_PASS=1" as a workaround
		exit 1
	fi
fi
