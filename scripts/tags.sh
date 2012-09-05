#!/bin/sh
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

# This is a duplicate of RCS_FIND_IGNORE without escaped '()'
ignore="( -name SCCS -o -name BitKeeper -o -name .svn -o \
          -name CVS  -o -name .pc       -o -name .hg  -o \
          -name .git )                                   \
          -prune -o"

# Do not use full path if we do not use O=.. builds
# Use make O=. {tags|cscope}
# to force full paths for a non-O= build
if [ "${KBUILD_SRC}" = "" ]; then
	tree=
else
	tree=${srctree}/
fi

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
	find ${tree}arch/$1 $ignore $prune -name "$2" -print;
}

# find sources in arch/$1/include
find_arch_include_sources()
{
	include=$(find ${tree}arch/$1/ -name include -type d);
	if [ -n "$include" ]; then
		archincludedir="$archincludedir $include"
		find $include $ignore -name "$2" -print;
	fi
}

# find sources in include/
find_include_sources()
{
	find ${tree}include $ignore -name config -prune -o -name "$1" -print;
}

# find sources in rest of tree
# we could benefit from a list of dirs to search in here
find_other_sources()
{
	find ${tree}* $ignore \
	     \( -name include -o -name arch -o -name '.tmp_*' \) -prune -o \
	       -name "$1" -print;
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

all_kconfigs()
{
	for arch in $ALLSOURCE_ARCHS; do
		find_sources $arch 'Kconfig*'
	done
	find_other_sources 'Kconfig*'
}

all_defconfigs()
{
	find_sources $ALLSOURCE_ARCHS "defconfig"
}

docscope()
{
	(echo \-k; echo \-q; all_sources) > cscope.files
	cscope -b -f cscope.out
}

dogtags()
{
	all_sources | gtags -i -f -
}

exuberant()
{
	all_sources | xargs $1 -a                               \
	-I __initdata,__exitdata,__acquires,__releases          \
	-I __read_mostly,____cacheline_aligned                  \
	-I ____cacheline_aligned_in_smp                         \
	-I ____cacheline_internodealigned_in_smp                \
	-I EXPORT_SYMBOL,EXPORT_SYMBOL_GPL                      \
	-I DEFINE_TRACE,EXPORT_TRACEPOINT_SYMBOL,EXPORT_TRACEPOINT_SYMBOL_GPL \
	--extra=+f --c-kinds=+px                                \
	--regex-asm='/^(ENTRY|_GLOBAL)\(([^)]*)\).*/\2/'        \
	--regex-c='/^SYSCALL_DEFINE[[:digit:]]?\(([^,)]*).*/sys_\1/' \
	--regex-c++='/^TRACE_EVENT\(([^,)]*).*/trace_\1/'		\
	--regex-c++='/^DEFINE_EVENT\([^,)]*, *([^,)]*).*/trace_\1/'	\
	--regex-c++='/PAGEFLAG\(([^,)]*).*/Page\1/'			\
	--regex-c++='/PAGEFLAG\(([^,)]*).*/SetPage\1/'			\
	--regex-c++='/PAGEFLAG\(([^,)]*).*/ClearPage\1/'		\
	--regex-c++='/TESTSETFLAG\(([^,)]*).*/TestSetPage\1/'		\
	--regex-c++='/TESTPAGEFLAG\(([^,)]*).*/Page\1/'			\
	--regex-c++='/SETPAGEFLAG\(([^,)]*).*/SetPage\1/'		\
	--regex-c++='/__SETPAGEFLAG\(([^,)]*).*/__SetPage\1/'		\
	--regex-c++='/TESTCLEARFLAG\(([^,)]*).*/TestClearPage\1/'	\
	--regex-c++='/__TESTCLEARFLAG\(([^,)]*).*/TestClearPage\1/'	\
	--regex-c++='/CLEARPAGEFLAG\(([^,)]*).*/ClearPage\1/'		\
	--regex-c++='/__CLEARPAGEFLAG\(([^,)]*).*/__ClearPage\1/'	\
	--regex-c++='/__PAGEFLAG\(([^,)]*).*/__SetPage\1/'		\
	--regex-c++='/__PAGEFLAG\(([^,)]*).*/__ClearPage\1/'		\
	--regex-c++='/PAGEFLAG_FALSE\(([^,)]*).*/Page\1/'		\
	--regex-c++='/TESTSCFLAG\(([^,)]*).*/TestSetPage\1/'		\
	--regex-c++='/TESTSCFLAG\(([^,)]*).*/TestClearPage\1/'		\
	--regex-c++='/SETPAGEFLAG_NOOP\(([^,)]*).*/SetPage\1/'		\
	--regex-c++='/CLEARPAGEFLAG_NOOP\(([^,)]*).*/ClearPage\1/'	\
	--regex-c++='/__CLEARPAGEFLAG_NOOP\(([^,)]*).*/__ClearPage\1/'	\
	--regex-c++='/TESTCLEARFLAG_FALSE\(([^,)]*).*/TestClearPage\1/' \
	--regex-c++='/__TESTCLEARFLAG_FALSE\(([^,)]*).*/__TestClearPage\1/' \
	--regex-c++='/_PE\(([^,)]*).*/PEVENT_ERRNO__\1/'

	all_kconfigs | xargs $1 -a                              \
	--langdef=kconfig --language-force=kconfig              \
	--regex-kconfig='/^[[:blank:]]*(menu|)config[[:blank:]]+([[:alnum:]_]+)/\2/'

	all_kconfigs | xargs $1 -a                              \
	--langdef=kconfig --language-force=kconfig              \
	--regex-kconfig='/^[[:blank:]]*(menu|)config[[:blank:]]+([[:alnum:]_]+)/CONFIG_\2/'

	all_defconfigs | xargs -r $1 -a                         \
	--langdef=dotconfig --language-force=dotconfig          \
	--regex-dotconfig='/^#?[[:blank:]]*(CONFIG_[[:alnum:]_]+)/\1/'
}

emacs()
{
	all_sources | xargs $1 -a                               \
	--regex='/^(ENTRY|_GLOBAL)(\([^)]*\)).*/\2/'            \
	--regex='/^SYSCALL_DEFINE[0-9]?(\([^,)]*\).*/sys_\1/'   \
	--regex='/^TRACE_EVENT(\([^,)]*\).*/trace_\1/'		\
	--regex='/^DEFINE_EVENT([^,)]*, *\([^,)]*\).*/trace_\1/' \
	--regex='/PAGEFLAG\(([^,)]*).*/Page\1/'			\
	--regex='/PAGEFLAG\(([^,)]*).*/SetPage\1/'		\
	--regex='/PAGEFLAG\(([^,)]*).*/ClearPage\1/'		\
	--regex='/TESTSETFLAG\(([^,)]*).*/TestSetPage\1/'	\
	--regex='/TESTPAGEFLAG\(([^,)]*).*/Page\1/'		\
	--regex='/SETPAGEFLAG\(([^,)]*).*/SetPage\1/'		\
	--regex='/__SETPAGEFLAG\(([^,)]*).*/__SetPage\1/'	\
	--regex='/TESTCLEARFLAG\(([^,)]*).*/TestClearPage\1/'	\
	--regex='/__TESTCLEARFLAG\(([^,)]*).*/TestClearPage\1/'	\
	--regex='/CLEARPAGEFLAG\(([^,)]*).*/ClearPage\1/'	\
	--regex='/__CLEARPAGEFLAG\(([^,)]*).*/__ClearPage\1/'	\
	--regex='/__PAGEFLAG\(([^,)]*).*/__SetPage\1/'		\
	--regex='/__PAGEFLAG\(([^,)]*).*/__ClearPage\1/'	\
	--regex='/PAGEFLAG_FALSE\(([^,)]*).*/Page\1/'		\
	--regex='/TESTSCFLAG\(([^,)]*).*/TestSetPage\1/'	\
	--regex='/TESTSCFLAG\(([^,)]*).*/TestClearPage\1/'	\
	--regex='/SETPAGEFLAG_NOOP\(([^,)]*).*/SetPage\1/'	\
	--regex='/CLEARPAGEFLAG_NOOP\(([^,)]*).*/ClearPage\1/'	\
	--regex='/__CLEARPAGEFLAG_NOOP\(([^,)]*).*/__ClearPage\1/' \
	--regex='/TESTCLEARFLAG_FALSE\(([^,)]*).*/TestClearPage\1/' \
	--regex='/__TESTCLEARFLAG_FALSE\(([^,)]*).*/__TestClearPage\1/' \
	--regex='/_PE\(([^,)]*).*/PEVENT_ERRNO__\1/'

	all_kconfigs | xargs $1 -a                              \
	--regex='/^[ \t]*\(\(menu\)*config\)[ \t]+\([a-zA-Z0-9_]+\)/\3/'

	all_kconfigs | xargs $1 -a                              \
	--regex='/^[ \t]*\(\(menu\)*config\)[ \t]+\([a-zA-Z0-9_]+\)/CONFIG_\3/'

	all_defconfigs | xargs -r $1 -a                         \
	--regex='/^#?[ \t]?\(CONFIG_[a-zA-Z0-9_]+\)/\1/'
}

xtags()
{
	if $1 --version 2>&1 | grep -iq exuberant; then
		exuberant $1
	elif $1 --version 2>&1 | grep -iq emacs; then
		emacs $1
	else
		all_sources | xargs $1 -a
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
