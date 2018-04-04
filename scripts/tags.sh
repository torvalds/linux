#!/bin/bash
# Generate tags or cscope files
# Usage tags.sh <mode>
#
# mode may be any of: tags, TAGS, cscope
#
# Uses the following environment variables:
# ARCH, SUBARCH, SRCARCH, srctree, src, obj

if [ "$KBUILD_VERBOSE" = "1" ]; then
	set -x
fi

# RCS_FIND_IGNORE has escaped ()s -- remove them.
ignore="$(echo "$RCS_FIND_IGNORE" | sed 's|\\||g' )"
# tags and cscope files should also ignore MODVERSION *.mod.c files
ignore="$ignore ( -name *.mod.c ) -prune -o"

# Do not use full path if we do not use O=.. builds
# Use make O=. {tags|cscope}
# to force full paths for a non-O= build
if [ "${KBUILD_SRC}" = "" ]; then
	tree=
else
	tree=${srctree}/
fi

# ignore userspace tools
ignore="$ignore ( -path ${tree}tools ) -prune -o"

# Find all available archs
find_all_archs()
{
	ALLSOURCE_ARCHS=""
	for arch in `ls ${tree}arch`; do
		ALLSOURCE_ARCHS="${ALLSOURCE_ARCHS} "${arch##\/}
	done
}

# Detect if ALLSOURCE_ARCHS is set. If not, we assume SRCARCH
if [ "${ALLSOURCE_ARCHS}" = "" ]; then
	ALLSOURCE_ARCHS=${SRCARCH}
elif [ "${ALLSOURCE_ARCHS}" = "all" ]; then
	find_all_archs
fi

# find sources in arch/$ARCH
find_arch_sources()
{
	for i in $archincludedir; do
		prune="$prune -wholename $i -prune -o"
	done
	find ${tree}arch/$1 $ignore $subarchprune $prune -name "$2" \
		-not -type l -print;
}

# find sources in arch/$1/include
find_arch_include_sources()
{
	include=$(find ${tree}arch/$1/ $subarchprune \
					-name include -type d -print);
	if [ -n "$include" ]; then
		archincludedir="$archincludedir $include"
		find $include $ignore -name "$2" -not -type l -print;
	fi
}

# find sources in include/
find_include_sources()
{
	find ${tree}include $ignore -name config -prune -o -name "$1" \
		-not -type l -print;
}

# find sources in rest of tree
# we could benefit from a list of dirs to search in here
find_other_sources()
{
	find ${tree}* $ignore \
	     \( -path ${tree}include -o -path ${tree}arch -o -name '.tmp_*' \) -prune -o \
	       -name "$1" -not -type l -print;
}

find_sources()
{
	find_arch_sources $1 "$2"
}

all_sources()
{
	find_arch_include_sources ${SRCARCH} '*.[chS]'
	if [ ! -z "$archinclude" ]; then
		find_arch_include_sources $archinclude '*.[chS]'
	fi
	find_include_sources '*.[chS]'
	for arch in $ALLSOURCE_ARCHS
	do
		find_sources $arch '*.[chS]'
	done
	find_other_sources '*.[chS]'
}

all_compiled_sources()
{
	for i in $(all_sources); do
		case "$i" in
			*.[cS])
				j=${i/\.[cS]/\.o}
				j="${j#$tree}"
				if [ -e $j ]; then
					echo $i
				fi
				;;
			*)
				echo $i
				;;
		esac
	done
}

all_target_sources()
{
	if [ -n "$COMPILED_SOURCE" ]; then
		all_compiled_sources
	else
		all_sources
	fi
}

all_kconfigs()
{
	find ${tree}arch/ -maxdepth 1 $ignore \
	       -name "Kconfig*" -not -type l -print;
	for arch in $ALLSOURCE_ARCHS; do
		find_sources $arch 'Kconfig*'
	done
	find_other_sources 'Kconfig*'
}

docscope()
{
	(echo \-k; echo \-q; all_target_sources) > cscope.files
	cscope -b -f cscope.out
}

dogtags()
{
	all_target_sources | gtags -i -f -
}

# Basic regular expressions with an optional /kind-spec/ for ctags and
# the following limitations:
# - No regex modifiers
# - Use \{0,1\} instead of \?, because etags expects an unescaped ?
# - \s is not working with etags, use a space or [ \t]
# - \w works, but does not match underscores in etags
# - etags regular expressions have to match at the start of a line;
#   a ^[^#] is prepended by setup_regex unless an anchor is already present
regex_asm=(
	'/^\(ENTRY\|_GLOBAL\)(\([[:alnum:]_\\]*\)).*/\2/'
)
regex_c=(
	'/^SYSCALL_DEFINE[0-9](\([[:alnum:]_]*\).*/sys_\1/'
	'/^COMPAT_SYSCALL_DEFINE[0-9](\([[:alnum:]_]*\).*/compat_sys_\1/'
	'/^TRACE_EVENT(\([[:alnum:]_]*\).*/trace_\1/'
	'/^TRACE_EVENT(\([[:alnum:]_]*\).*/trace_\1_rcuidle/'
	'/^DEFINE_EVENT([^,)]*, *\([[:alnum:]_]*\).*/trace_\1/'
	'/^DEFINE_EVENT([^,)]*, *\([[:alnum:]_]*\).*/trace_\1_rcuidle/'
	'/^DEFINE_INSN_CACHE_OPS(\([[:alnum:]_]*\).*/get_\1_slot/'
	'/^DEFINE_INSN_CACHE_OPS(\([[:alnum:]_]*\).*/free_\1_slot/'
	'/^PAGEFLAG(\([[:alnum:]_]*\).*/Page\1/'
	'/^PAGEFLAG(\([[:alnum:]_]*\).*/SetPage\1/'
	'/^PAGEFLAG(\([[:alnum:]_]*\).*/ClearPage\1/'
	'/^TESTSETFLAG(\([[:alnum:]_]*\).*/TestSetPage\1/'
	'/^TESTPAGEFLAG(\([[:alnum:]_]*\).*/Page\1/'
	'/^SETPAGEFLAG(\([[:alnum:]_]*\).*/SetPage\1/'
	'/\<__SETPAGEFLAG(\([[:alnum:]_]*\).*/__SetPage\1/'
	'/\<TESTCLEARFLAG(\([[:alnum:]_]*\).*/TestClearPage\1/'
	'/\<__TESTCLEARFLAG(\([[:alnum:]_]*\).*/TestClearPage\1/'
	'/\<CLEARPAGEFLAG(\([[:alnum:]_]*\).*/ClearPage\1/'
	'/\<__CLEARPAGEFLAG(\([[:alnum:]_]*\).*/__ClearPage\1/'
	'/^__PAGEFLAG(\([[:alnum:]_]*\).*/__SetPage\1/'
	'/^__PAGEFLAG(\([[:alnum:]_]*\).*/__ClearPage\1/'
	'/^PAGEFLAG_FALSE(\([[:alnum:]_]*\).*/Page\1/'
	'/\<TESTSCFLAG(\([[:alnum:]_]*\).*/TestSetPage\1/'
	'/\<TESTSCFLAG(\([[:alnum:]_]*\).*/TestClearPage\1/'
	'/\<SETPAGEFLAG_NOOP(\([[:alnum:]_]*\).*/SetPage\1/'
	'/\<CLEARPAGEFLAG_NOOP(\([[:alnum:]_]*\).*/ClearPage\1/'
	'/\<__CLEARPAGEFLAG_NOOP(\([[:alnum:]_]*\).*/__ClearPage\1/'
	'/\<TESTCLEARFLAG_FALSE(\([[:alnum:]_]*\).*/TestClearPage\1/'
	'/^PAGE_MAPCOUNT_OPS(\([[:alnum:]_]*\).*/Page\1/'
	'/^PAGE_MAPCOUNT_OPS(\([[:alnum:]_]*\).*/__SetPage\1/'
	'/^PAGE_MAPCOUNT_OPS(\([[:alnum:]_]*\).*/__ClearPage\1/'
	'/^TASK_PFA_TEST([^,]*, *\([[:alnum:]_]*\))/task_\1/'
	'/^TASK_PFA_SET([^,]*, *\([[:alnum:]_]*\))/task_set_\1/'
	'/^TASK_PFA_CLEAR([^,]*, *\([[:alnum:]_]*\))/task_clear_\1/'
	'/^DEF_MMIO_\(IN\|OUT\)_[XD](\([[:alnum:]_]*\),[^)]*)/\2/'
	'/^DEBUGGER_BOILERPLATE(\([[:alnum:]_]*\))/\1/'
	'/^DEF_PCI_AC_\(\|NO\)RET(\([[:alnum:]_]*\).*/\2/'
	'/^PCI_OP_READ(\(\w*\).*[1-4])/pci_bus_read_config_\1/'
	'/^PCI_OP_WRITE(\(\w*\).*[1-4])/pci_bus_write_config_\1/'
	'/\<DEFINE_\(MUTEX\|SEMAPHORE\|SPINLOCK\)(\([[:alnum:]_]*\)/\2/v/'
	'/\<DEFINE_\(RAW_SPINLOCK\|RWLOCK\|SEQLOCK\)(\([[:alnum:]_]*\)/\2/v/'
	'/\<DECLARE_\(RWSEM\|COMPLETION\)(\([[:alnum:]_]\+\)/\2/v/'
	'/\<DECLARE_BITMAP(\([[:alnum:]_]*\)/\1/v/'
	'/\(^\|\s\)\(\|L\|H\)LIST_HEAD(\([[:alnum:]_]*\)/\3/v/'
	'/\(^\|\s\)RADIX_TREE(\([[:alnum:]_]*\)/\2/v/'
	'/\<DEFINE_PER_CPU([^,]*, *\([[:alnum:]_]*\)/\1/v/'
	'/\<DEFINE_PER_CPU_SHARED_ALIGNED([^,]*, *\([[:alnum:]_]*\)/\1/v/'
	'/\<DECLARE_WAIT_QUEUE_HEAD(\([[:alnum:]_]*\)/\1/v/'
	'/\<DECLARE_\(TASKLET\|WORK\|DELAYED_WORK\)(\([[:alnum:]_]*\)/\2/v/'
	'/\(^\s\)OFFSET(\([[:alnum:]_]*\)/\2/v/'
	'/\(^\s\)DEFINE(\([[:alnum:]_]*\)/\2/v/'
	'/\<DEFINE_HASHTABLE(\([[:alnum:]_]*\)/\1/v/'
)
regex_kconfig=(
	'/^[[:blank:]]*\(menu\|\)config[[:blank:]]\+\([[:alnum:]_]\+\)/\2/'
	'/^[[:blank:]]*\(menu\|\)config[[:blank:]]\+\([[:alnum:]_]\+\)/CONFIG_\2/'
)
setup_regex()
{
	local mode=$1 lang tmp=() r
	shift

	regex=()
	for lang; do
		case "$lang" in
		asm)       tmp=("${regex_asm[@]}") ;;
		c)         tmp=("${regex_c[@]}") ;;
		kconfig)   tmp=("${regex_kconfig[@]}") ;;
		esac
		for r in "${tmp[@]}"; do
			if test "$mode" = "exuberant"; then
				regex[${#regex[@]}]="--regex-$lang=${r}b"
			else
				# Remove ctags /kind-spec/
				case "$r" in
				/*/*/?/)
					r=${r%?/}
				esac
				# Prepend ^[^#] unless already anchored
				case "$r" in
				/^*) ;;
				*)
					r="/^[^#]*${r#/}"
				esac
				regex[${#regex[@]}]="--regex=$r"
			fi
		done
	done
}

exuberant()
{
	setup_regex exuberant asm c
	all_target_sources | xargs $1 -a                        \
	-I __initdata,__exitdata,__initconst,			\
	-I __initdata_memblock					\
	-I __refdata,__attribute,__maybe_unused,__always_unused \
	-I __acquires,__releases,__deprecated			\
	-I __read_mostly,__aligned,____cacheline_aligned        \
	-I ____cacheline_aligned_in_smp                         \
	-I __cacheline_aligned,__cacheline_aligned_in_smp	\
	-I ____cacheline_internodealigned_in_smp                \
	-I __used,__packed,__packed2__,__must_check,__must_hold	\
	-I EXPORT_SYMBOL,EXPORT_SYMBOL_GPL,ACPI_EXPORT_SYMBOL   \
	-I DEFINE_TRACE,EXPORT_TRACEPOINT_SYMBOL,EXPORT_TRACEPOINT_SYMBOL_GPL \
	-I static,const						\
	--extra=+fq --c-kinds=+px --fields=+iaS --langmap=c:+.h \
	"${regex[@]}"

	setup_regex exuberant kconfig
	all_kconfigs | xargs $1 -a                              \
	--langdef=kconfig --language-force=kconfig "${regex[@]}"

}

emacs()
{
	setup_regex emacs asm c
	all_target_sources | xargs $1 -a "${regex[@]}"

	setup_regex emacs kconfig
	all_kconfigs | xargs $1 -a "${regex[@]}"
}

xtags()
{
	if $1 --version 2>&1 | grep -iq exuberant; then
		exuberant $1
	elif $1 --version 2>&1 | grep -iq emacs; then
		emacs $1
	else
		all_target_sources | xargs $1 -a
	fi
}

# Support um (which uses SUBARCH)
if [ "${ARCH}" = "um" ]; then
	if [ "$SUBARCH" = "i386" ]; then
		archinclude=x86
	elif [ "$SUBARCH" = "x86_64" ]; then
		archinclude=x86
	else
		archinclude=${SUBARCH}
	fi
elif [ "${SRCARCH}" = "arm" -a "${SUBARCH}" != "" ]; then
	subarchdir=$(find ${tree}arch/$SRCARCH/ -name "mach-*" -type d -o \
							-name "plat-*" -type d);
	mach_suffix=$SUBARCH
	plat_suffix=$SUBARCH

	# Special cases when $plat_suffix != $mach_suffix
	case $mach_suffix in
		"omap1" | "omap2")
			plat_suffix="omap"
			;;
	esac

	if [ ! -d ${tree}arch/$SRCARCH/mach-$mach_suffix ]; then
		echo "Warning: arch/arm/mach-$mach_suffix/ not found." >&2
		echo "         Fix your \$SUBARCH appropriately" >&2
	fi

	for i in $subarchdir; do
		case "$i" in
			*"mach-"${mach_suffix})
				;;
			*"plat-"${plat_suffix})
				;;
			*)
				subarchprune="$subarchprune \
						-wholename $i -prune -o"
				;;
		esac
	done
fi

remove_structs=
case "$1" in
	"cscope")
		docscope
		;;

	"gtags")
		dogtags
		;;

	"tags")
		rm -f tags
		xtags ctags
		remove_structs=y
		;;

	"TAGS")
		rm -f TAGS
		xtags etags
		remove_structs=y
		;;
esac

# Remove structure forward declarations.
if [ -n "$remove_structs" ]; then
    LANG=C sed -i -e '/^\([a-zA-Z_][a-zA-Z0-9_]*\)\t.*\t\/\^struct \1;.*\$\/;"\tx$/d' $1
fi
