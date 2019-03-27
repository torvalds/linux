# ===========================================================================
#    https://www.gnu.org/software/autoconf-archive/ax_valgrind_check.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_VALGRIND_DFLT(memcheck|helgrind|drd|sgcheck, on|off)
#   AX_VALGRIND_CHECK()
#
# DESCRIPTION
#
#   AX_VALGRIND_CHECK checks whether Valgrind is present and, if so, allows
#   running `make check` under a variety of Valgrind tools to check for
#   memory and threading errors.
#
#   Defines VALGRIND_CHECK_RULES which should be substituted in your
#   Makefile; and $enable_valgrind which can be used in subsequent configure
#   output. VALGRIND_ENABLED is defined and substituted, and corresponds to
#   the value of the --enable-valgrind option, which defaults to being
#   enabled if Valgrind is installed and disabled otherwise. Individual
#   Valgrind tools can be disabled via --disable-valgrind-<tool>, the
#   default is configurable via the AX_VALGRIND_DFLT command or is to use
#   all commands not disabled via AX_VALGRIND_DFLT. All AX_VALGRIND_DFLT
#   calls must be made before the call to AX_VALGRIND_CHECK.
#
#   If unit tests are written using a shell script and automake's
#   LOG_COMPILER system, the $(VALGRIND) variable can be used within the
#   shell scripts to enable Valgrind, as described here:
#
#     https://www.gnu.org/software/gnulib/manual/html_node/Running-self_002dtests-under-valgrind.html
#
#   Usage example:
#
#   configure.ac:
#
#     AX_VALGRIND_DFLT([sgcheck], [off])
#     AX_VALGRIND_CHECK
#
#   Makefile.am:
#
#     @VALGRIND_CHECK_RULES@
#     VALGRIND_SUPPRESSIONS_FILES = my-project.supp
#     EXTRA_DIST = my-project.supp
#
#   This results in a "check-valgrind" rule being added to any Makefile.am
#   which includes "@VALGRIND_CHECK_RULES@" (assuming the module has been
#   configured with --enable-valgrind). Running `make check-valgrind` in
#   that directory will run the module's test suite (`make check`) once for
#   each of the available Valgrind tools (out of memcheck, helgrind and drd)
#   while the sgcheck will be skipped unless enabled again on the
#   commandline with --enable-valgrind-sgcheck. The results for each check
#   will be output to test-suite-$toolname.log. The target will succeed if
#   there are zero errors and fail otherwise.
#
#   Alternatively, a "check-valgrind-$TOOL" rule will be added, for $TOOL in
#   memcheck, helgrind, drd and sgcheck. These are useful because often only
#   some of those tools can be ran cleanly on a codebase.
#
#   The macro supports running with and without libtool.
#
# LICENSE
#
#   Copyright (c) 2014, 2015, 2016 Philip Withnall <philip.withnall@collabora.co.uk>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.  This file is offered as-is, without any
#   warranty.

#serial 15

dnl Configured tools
m4_define([valgrind_tool_list], [[memcheck], [helgrind], [drd], [sgcheck]])
m4_set_add_all([valgrind_exp_tool_set], [sgcheck])
m4_foreach([vgtool], [valgrind_tool_list],
           [m4_define([en_dflt_valgrind_]vgtool, [on])])

AC_DEFUN([AX_VALGRIND_DFLT],[
	m4_define([en_dflt_valgrind_$1], [$2])
])dnl

AC_DEFUN([AX_VALGRIND_CHECK],[
	dnl Check for --enable-valgrind
	AC_ARG_ENABLE([valgrind],
				  [AS_HELP_STRING([--enable-valgrind], [Whether to enable Valgrind on the unit tests (requires GNU make)])],
				  [enable_valgrind=$enableval],[enable_valgrind=no])

	AS_IF([test "$enable_valgrind" != "no"],[
		# Check for Valgrind.
		AC_CHECK_PROG([VALGRIND],[valgrind],[valgrind])
		AS_IF([test "$VALGRIND" = ""],[
			AS_IF([test "$enable_valgrind" = "yes"],[
				AC_MSG_ERROR([Could not find valgrind; either install it or reconfigure with --disable-valgrind])
			],[
				enable_valgrind=no
			])
		],[
			enable_valgrind=yes
		])
	])

	AM_CONDITIONAL([VALGRIND_ENABLED],[test "$enable_valgrind" = "yes"])
	AC_SUBST([VALGRIND_ENABLED],[$enable_valgrind])

	# Check for Valgrind tools we care about.
	[valgrind_enabled_tools=]
	m4_foreach([vgtool],[valgrind_tool_list],[
		AC_ARG_ENABLE([valgrind-]vgtool,
		    m4_if(m4_defn([en_dflt_valgrind_]vgtool),[off],dnl
[AS_HELP_STRING([--enable-valgrind-]vgtool, [Whether to use ]vgtool[ during the Valgrind tests])],dnl
[AS_HELP_STRING([--disable-valgrind-]vgtool, [Whether to skip ]vgtool[ during the Valgrind tests])]),
		              [enable_valgrind_]vgtool[=$enableval],
		              [enable_valgrind_]vgtool[=])
		AS_IF([test "$enable_valgrind" = "no"],[
			enable_valgrind_]vgtool[=no],
		      [test "$enable_valgrind_]vgtool[" ]dnl
m4_if(m4_defn([en_dflt_valgrind_]vgtool), [off], [= "yes"], [!= "no"]),[
			AC_CACHE_CHECK([for Valgrind tool ]vgtool,
			               [ax_cv_valgrind_tool_]vgtool,[
				ax_cv_valgrind_tool_]vgtool[=no
				m4_set_contains([valgrind_exp_tool_set],vgtool,
				    [m4_define([vgtoolx],[exp-]vgtool)],
				    [m4_define([vgtoolx],vgtool)])
				AS_IF([`$VALGRIND --tool=]vgtoolx[ --help >/dev/null 2>&1`],[
					ax_cv_valgrind_tool_]vgtool[=yes
				])
			])
			AS_IF([test "$ax_cv_valgrind_tool_]vgtool[" = "no"],[
				AS_IF([test "$enable_valgrind_]vgtool[" = "yes"],[
					AC_MSG_ERROR([Valgrind does not support ]vgtool[; reconfigure with --disable-valgrind-]vgtool)
				],[
					enable_valgrind_]vgtool[=no
				])
			],[
				enable_valgrind_]vgtool[=yes
			])
		])
		AS_IF([test "$enable_valgrind_]vgtool[" = "yes"],[
			valgrind_enabled_tools="$valgrind_enabled_tools ]m4_bpatsubst(vgtool,[^exp-])["
		])
		AC_SUBST([ENABLE_VALGRIND_]vgtool,[$enable_valgrind_]vgtool)
	])
	AC_SUBST([valgrind_tools],["]m4_join([ ], valgrind_tool_list)["])
	AC_SUBST([valgrind_enabled_tools],[$valgrind_enabled_tools])

[VALGRIND_CHECK_RULES='
# Valgrind check
#
# Optional:
#  - VALGRIND_SUPPRESSIONS_FILES: Space-separated list of Valgrind suppressions
#    files to load. (Default: empty)
#  - VALGRIND_FLAGS: General flags to pass to all Valgrind tools.
#    (Default: --num-callers=30)
#  - VALGRIND_$toolname_FLAGS: Flags to pass to Valgrind $toolname (one of:
#    memcheck, helgrind, drd, sgcheck). (Default: various)

# Optional variables
VALGRIND_SUPPRESSIONS ?= $(addprefix --suppressions=,$(VALGRIND_SUPPRESSIONS_FILES))
VALGRIND_FLAGS ?= --num-callers=30
VALGRIND_memcheck_FLAGS ?= --leak-check=full --show-reachable=no
VALGRIND_helgrind_FLAGS ?= --history-level=approx
VALGRIND_drd_FLAGS ?=
VALGRIND_sgcheck_FLAGS ?=

# Internal use
valgrind_log_files = $(addprefix test-suite-,$(addsuffix .log,$(valgrind_tools)))

valgrind_memcheck_flags = --tool=memcheck $(VALGRIND_memcheck_FLAGS)
valgrind_helgrind_flags = --tool=helgrind $(VALGRIND_helgrind_FLAGS)
valgrind_drd_flags = --tool=drd $(VALGRIND_drd_FLAGS)
valgrind_sgcheck_flags = --tool=exp-sgcheck $(VALGRIND_sgcheck_FLAGS)

valgrind_quiet = $(valgrind_quiet_$(V))
valgrind_quiet_ = $(valgrind_quiet_$(AM_DEFAULT_VERBOSITY))
valgrind_quiet_0 = --quiet
valgrind_v_use   = $(valgrind_v_use_$(V))
valgrind_v_use_  = $(valgrind_v_use_$(AM_DEFAULT_VERBOSITY))
valgrind_v_use_0 = @echo "  USE   " $(patsubst check-valgrind-%,%,$''@):;

# Support running with and without libtool.
ifneq ($(LIBTOOL),)
valgrind_lt = $(LIBTOOL) $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=execute
else
valgrind_lt =
endif

# Use recursive makes in order to ignore errors during check
check-valgrind:
ifeq ($(VALGRIND_ENABLED),yes)
	$(A''M_V_at)$(MAKE) $(AM_MAKEFLAGS) -k \
		$(foreach tool, $(valgrind_enabled_tools), check-valgrind-$(tool))
else
	@echo "Need to use GNU make and reconfigure with --enable-valgrind"
endif

# Valgrind running
VALGRIND_TESTS_ENVIRONMENT = \
	$(TESTS_ENVIRONMENT) \
	env VALGRIND=$(VALGRIND) \
	G_SLICE=always-malloc,debug-blocks \
	G_DEBUG=fatal-warnings,fatal-criticals,gc-friendly

VALGRIND_LOG_COMPILER = \
	$(valgrind_lt) \
	$(VALGRIND) $(VALGRIND_SUPPRESSIONS) --error-exitcode=1 $(VALGRIND_FLAGS)

define valgrind_tool_rule =
check-valgrind-$(1):
ifeq ($$(VALGRIND_ENABLED)-$$(ENABLE_VALGRIND_$(1)),yes-yes)
	$$(valgrind_v_use)$$(MAKE) check-TESTS \
		TESTS_ENVIRONMENT="$$(VALGRIND_TESTS_ENVIRONMENT)" \
		LOG_COMPILER="$$(VALGRIND_LOG_COMPILER)" \
		LOG_FLAGS="$$(valgrind_$(1)_flags)" \
		TEST_SUITE_LOG=test-suite-$(1).log
else ifeq ($$(VALGRIND_ENABLED),yes)
	@echo "Need to reconfigure with --enable-valgrind-$(1)"
else
	@echo "Need to reconfigure with --enable-valgrind"
endif
endef

$(foreach tool,$(valgrind_tools),$(eval $(call valgrind_tool_rule,$(tool))))

A''M_DISTCHECK_CONFIGURE_FLAGS ?=
A''M_DISTCHECK_CONFIGURE_FLAGS += --disable-valgrind

MOSTLYCLEANFILES ?=
MOSTLYCLEANFILES += $(valgrind_log_files)

.PHONY: check-valgrind $(add-prefix check-valgrind-,$(valgrind_tools))
']

	AS_IF([test "$enable_valgrind" != "yes"], [
		VALGRIND_CHECK_RULES='
check-valgrind:
	@echo "Need to use GNU make and reconfigure with --enable-valgrind"'
	])

	AC_SUBST([VALGRIND_CHECK_RULES])
	m4_ifdef([_AM_SUBST_NOTMAKE], [_AM_SUBST_NOTMAKE([VALGRIND_CHECK_RULES])])
])
