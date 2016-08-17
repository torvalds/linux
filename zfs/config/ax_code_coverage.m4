# ===========================================================================
#     https://www.gnu.org/software/autoconf-archive/ax_code_coverage.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_CODE_COVERAGE()
#
# DESCRIPTION
#
#   Defines CODE_COVERAGE_CPPFLAGS, CODE_COVERAGE_CFLAGS,
#   CODE_COVERAGE_CXXFLAGS and CODE_COVERAGE_LIBS which should be included
#   in the CPPFLAGS, CFLAGS CXXFLAGS and LIBS/LIBADD variables of every
#   build target (program or library) which should be built with code
#   coverage support. Also defines CODE_COVERAGE_RULES which should be
#   substituted in your Makefile; and $enable_code_coverage which can be
#   used in subsequent configure output. CODE_COVERAGE_ENABLED is defined
#   and substituted, and corresponds to the value of the
#   --enable-code-coverage option, which defaults to being disabled.
#
#   Test also for gcov program and create GCOV variable that could be
#   substituted.
#
#   Note that all optimization flags in CFLAGS must be disabled when code
#   coverage is enabled.
#
#   Usage example:
#
#   configure.ac:
#
#     AX_CODE_COVERAGE
#
#   Makefile.am:
#
#     @CODE_COVERAGE_RULES@
#     my_program_LIBS = ... $(CODE_COVERAGE_LIBS) ...
#     my_program_CPPFLAGS = ... $(CODE_COVERAGE_CPPFLAGS) ...
#     my_program_CFLAGS = ... $(CODE_COVERAGE_CFLAGS) ...
#     my_program_CXXFLAGS = ... $(CODE_COVERAGE_CXXFLAGS) ...
#
#   This results in a "check-code-coverage" rule being added to any
#   Makefile.am which includes "@CODE_COVERAGE_RULES@" (assuming the module
#   has been configured with --enable-code-coverage). Running `make
#   check-code-coverage` in that directory will run the module's test suite
#   (`make check`) and build a code coverage report detailing the code which
#   was touched, then print the URI for the report.
#
#   In earlier versions of this macro, CODE_COVERAGE_LDFLAGS was defined
#   instead of CODE_COVERAGE_LIBS. They are both still defined, but use of
#   CODE_COVERAGE_LIBS is preferred for clarity; CODE_COVERAGE_LDFLAGS is
#   deprecated. They have the same value.
#
#   This code was derived from Makefile.decl in GLib, originally licenced
#   under LGPLv2.1+.
#
# LICENSE
#
#   Copyright (c) 2012, 2016 Philip Withnall
#   Copyright (c) 2012 Xan Lopez
#   Copyright (c) 2012 Christian Persch
#   Copyright (c) 2012 Paolo Borelli
#   Copyright (c) 2012 Dan Winship
#   Copyright (c) 2015 Bastien ROUCARIES
#
#   This library is free software; you can redistribute it and/or modify it
#   under the terms of the GNU Lesser General Public License as published by
#   the Free Software Foundation; either version 2.1 of the License, or (at
#   your option) any later version.
#
#   This library is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser
#   General Public License for more details.
#
#   You should have received a copy of the GNU Lesser General Public License
#   along with this program. If not, see <https://www.gnu.org/licenses/>.

#serial 25

AC_DEFUN([AX_CODE_COVERAGE],[
	dnl Check for --enable-code-coverage
	AC_REQUIRE([AC_PROG_SED])

	# allow to override gcov location
	AC_ARG_WITH([gcov],
	  [AS_HELP_STRING([--with-gcov[=GCOV]], [use given GCOV for coverage (GCOV=gcov).])],
	  [_AX_CODE_COVERAGE_GCOV_PROG_WITH=$with_gcov],
	  [_AX_CODE_COVERAGE_GCOV_PROG_WITH=gcov])

	AC_MSG_CHECKING([whether to build with code coverage support])
	AC_ARG_ENABLE([code-coverage],
	  AS_HELP_STRING([--enable-code-coverage],
	  [Whether to enable code coverage support]),,
	  enable_code_coverage=no)

	AM_CONDITIONAL([CODE_COVERAGE_ENABLED], [test x$enable_code_coverage = xyes])
	AC_SUBST([CODE_COVERAGE_ENABLED], [$enable_code_coverage])
	AC_MSG_RESULT($enable_code_coverage)

	AS_IF([ test "$enable_code_coverage" = "yes" ], [
		# check for gcov
		AC_CHECK_TOOL([GCOV],
		  [$_AX_CODE_COVERAGE_GCOV_PROG_WITH],
		  [:])
		AS_IF([test "X$GCOV" = "X:"],
		  [AC_MSG_ERROR([gcov is needed to do coverage])])
		AC_SUBST([GCOV])

		dnl Check if gcc is being used
		AS_IF([ test "$GCC" = "no" ], [
			AC_MSG_ERROR([not compiling with gcc, which is required for gcov code coverage])
		])

		AC_CHECK_PROG([LCOV], [lcov], [lcov])
		AC_CHECK_PROG([GENHTML], [genhtml], [genhtml])

		AS_IF([ test -z "$LCOV" ], [
			AC_MSG_ERROR([To enable code coverage reporting you must have lcov installed])
		])

		AS_IF([ test -z "$GENHTML" ], [
			AC_MSG_ERROR([Could not find genhtml from the lcov package])
		])

		dnl Build the code coverage flags
		dnl Define CODE_COVERAGE_LDFLAGS for backwards compatibility
		CODE_COVERAGE_CPPFLAGS=""
		CODE_COVERAGE_CFLAGS="-O0 -g -fprofile-arcs -ftest-coverage"
		CODE_COVERAGE_CXXFLAGS="-O0 -g -fprofile-arcs -ftest-coverage"
		CODE_COVERAGE_LIBS="-lgcov"
		CODE_COVERAGE_LDFLAGS="$CODE_COVERAGE_LIBS"

		AC_SUBST([CODE_COVERAGE_CPPFLAGS])
		AC_SUBST([CODE_COVERAGE_CFLAGS])
		AC_SUBST([CODE_COVERAGE_CXXFLAGS])
		AC_SUBST([CODE_COVERAGE_LIBS])
		AC_SUBST([CODE_COVERAGE_LDFLAGS])

		[CODE_COVERAGE_RULES_CHECK='
	-$(A''M_V_at)$(MAKE) $(AM_MAKEFLAGS) -k check
	$(A''M_V_at)$(MAKE) $(AM_MAKEFLAGS) code-coverage-capture
']
		[CODE_COVERAGE_RULES_CAPTURE='
	$(code_coverage_v_lcov_cap)$(LCOV) $(code_coverage_quiet) $(addprefix --directory ,$(CODE_COVERAGE_DIRECTORY)) --capture --output-file "$(CODE_COVERAGE_OUTPUT_FILE).tmp" --test-name "$(call code_coverage_sanitize,$(PACKAGE_NAME)-$(PACKAGE_VERSION))" --no-checksum --compat-libtool $(CODE_COVERAGE_LCOV_SHOPTS) $(CODE_COVERAGE_LCOV_OPTIONS)
	$(code_coverage_v_lcov_ign)$(LCOV) $(code_coverage_quiet) $(addprefix --directory ,$(CODE_COVERAGE_DIRECTORY)) --remove "$(CODE_COVERAGE_OUTPUT_FILE).tmp" "/tmp/*" $(CODE_COVERAGE_IGNORE_PATTERN) --output-file "$(CODE_COVERAGE_OUTPUT_FILE)" $(CODE_COVERAGE_LCOV_SHOPTS) $(CODE_COVERAGE_LCOV_RMOPTS)
	-@rm -f $(CODE_COVERAGE_OUTPUT_FILE).tmp
	$(code_coverage_v_genhtml)LANG=C $(GENHTML) $(code_coverage_quiet) $(addprefix --prefix ,$(CODE_COVERAGE_DIRECTORY)) --output-directory "$(CODE_COVERAGE_OUTPUT_DIRECTORY)" --title "$(PACKAGE_NAME)-$(PACKAGE_VERSION) Code Coverage" --legend --show-details "$(CODE_COVERAGE_OUTPUT_FILE)" $(CODE_COVERAGE_GENHTML_OPTIONS)
	@echo "file://$(abs_builddir)/$(CODE_COVERAGE_OUTPUT_DIRECTORY)/index.html"
']
		[CODE_COVERAGE_RULES_CLEAN='
clean: code-coverage-clean
distclean: code-coverage-clean
code-coverage-clean:
	-$(LCOV) --directory $(top_builddir) -z
	-rm -rf $(CODE_COVERAGE_OUTPUT_FILE) $(CODE_COVERAGE_OUTPUT_FILE).tmp $(CODE_COVERAGE_OUTPUT_DIRECTORY)
	-find . \( -name "*.gcda" -o -name "*.gcno" -o -name "*.gcov" \) -delete
']
	], [
		[CODE_COVERAGE_RULES_CHECK='
	@echo "Need to reconfigure with --enable-code-coverage"
']
		CODE_COVERAGE_RULES_CAPTURE="$CODE_COVERAGE_RULES_CHECK"
		CODE_COVERAGE_RULES_CLEAN=''
	])

[CODE_COVERAGE_RULES='
# Code coverage
#
# Optional:
#  - CODE_COVERAGE_DIRECTORY: Top-level directory for code coverage reporting.
#    Multiple directories may be specified, separated by whitespace.
#    (Default: $(top_builddir))
#  - CODE_COVERAGE_OUTPUT_FILE: Filename and path for the .info file generated
#    by lcov for code coverage. (Default:
#    $(PACKAGE_NAME)-$(PACKAGE_VERSION)-coverage.info)
#  - CODE_COVERAGE_OUTPUT_DIRECTORY: Directory for generated code coverage
#    reports to be created. (Default:
#    $(PACKAGE_NAME)-$(PACKAGE_VERSION)-coverage)
#  - CODE_COVERAGE_BRANCH_COVERAGE: Set to 1 to enforce branch coverage,
#    set to 0 to disable it and leave empty to stay with the default.
#    (Default: empty)
#  - CODE_COVERAGE_LCOV_SHOPTS_DEFAULT: Extra options shared between both lcov
#    instances. (Default: based on $CODE_COVERAGE_BRANCH_COVERAGE)
#  - CODE_COVERAGE_LCOV_SHOPTS: Extra options to shared between both lcov
#    instances. (Default: $CODE_COVERAGE_LCOV_SHOPTS_DEFAULT)
#  - CODE_COVERAGE_LCOV_OPTIONS_GCOVPATH: --gcov-tool pathtogcov
#  - CODE_COVERAGE_LCOV_OPTIONS_DEFAULT: Extra options to pass to the
#    collecting lcov instance. (Default: $CODE_COVERAGE_LCOV_OPTIONS_GCOVPATH)
#  - CODE_COVERAGE_LCOV_OPTIONS: Extra options to pass to the collecting lcov
#    instance. (Default: $CODE_COVERAGE_LCOV_OPTIONS_DEFAULT)
#  - CODE_COVERAGE_LCOV_RMOPTS_DEFAULT: Extra options to pass to the filtering
#    lcov instance. (Default: empty)
#  - CODE_COVERAGE_LCOV_RMOPTS: Extra options to pass to the filtering lcov
#    instance. (Default: $CODE_COVERAGE_LCOV_RMOPTS_DEFAULT)
#  - CODE_COVERAGE_GENHTML_OPTIONS_DEFAULT: Extra options to pass to the
#    genhtml instance. (Default: based on $CODE_COVERAGE_BRANCH_COVERAGE)
#  - CODE_COVERAGE_GENHTML_OPTIONS: Extra options to pass to the genhtml
#    instance. (Default: $CODE_COVERAGE_GENHTML_OPTIONS_DEFAULT)
#  - CODE_COVERAGE_IGNORE_PATTERN: Extra glob pattern of files to ignore
#
# The generated report will be titled using the $(PACKAGE_NAME) and
# $(PACKAGE_VERSION). In order to add the current git hash to the title,
# use the git-version-gen script, available online.

# Optional variables
CODE_COVERAGE_DIRECTORY ?= $(top_builddir)
CODE_COVERAGE_OUTPUT_FILE ?= $(PACKAGE_NAME)-$(PACKAGE_VERSION)-coverage.info
CODE_COVERAGE_OUTPUT_DIRECTORY ?= $(PACKAGE_NAME)-$(PACKAGE_VERSION)-coverage
CODE_COVERAGE_BRANCH_COVERAGE ?=
CODE_COVERAGE_LCOV_SHOPTS_DEFAULT ?= $(if $(CODE_COVERAGE_BRANCH_COVERAGE),\
--rc lcov_branch_coverage=$(CODE_COVERAGE_BRANCH_COVERAGE))
CODE_COVERAGE_LCOV_SHOPTS ?= $(CODE_COVERAGE_LCOV_SHOPTS_DEFAULT)
CODE_COVERAGE_LCOV_OPTIONS_GCOVPATH ?= --gcov-tool "$(GCOV)"
CODE_COVERAGE_LCOV_OPTIONS_DEFAULT ?= $(CODE_COVERAGE_LCOV_OPTIONS_GCOVPATH)
CODE_COVERAGE_LCOV_OPTIONS ?= $(CODE_COVERAGE_LCOV_OPTIONS_DEFAULT)
CODE_COVERAGE_LCOV_RMOPTS_DEFAULT ?=
CODE_COVERAGE_LCOV_RMOPTS ?= $(CODE_COVERAGE_LCOV_RMOPTS_DEFAULT)
CODE_COVERAGE_GENHTML_OPTIONS_DEFAULT ?=\
$(if $(CODE_COVERAGE_BRANCH_COVERAGE),\
--rc genhtml_branch_coverage=$(CODE_COVERAGE_BRANCH_COVERAGE))
CODE_COVERAGE_GENHTML_OPTIONS ?= $(CODE_COVERAGE_GENHTML_OPTIONS_DEFAULT)
CODE_COVERAGE_IGNORE_PATTERN ?=

GITIGNOREFILES ?=
GITIGNOREFILES += $(CODE_COVERAGE_OUTPUT_FILE) $(CODE_COVERAGE_OUTPUT_DIRECTORY)

code_coverage_v_lcov_cap = $(code_coverage_v_lcov_cap_$(V))
code_coverage_v_lcov_cap_ = $(code_coverage_v_lcov_cap_$(AM_DEFAULT_VERBOSITY))
code_coverage_v_lcov_cap_0 = @echo "  LCOV   --capture"\
 $(CODE_COVERAGE_OUTPUT_FILE);
code_coverage_v_lcov_ign = $(code_coverage_v_lcov_ign_$(V))
code_coverage_v_lcov_ign_ = $(code_coverage_v_lcov_ign_$(AM_DEFAULT_VERBOSITY))
code_coverage_v_lcov_ign_0 = @echo "  LCOV   --remove /tmp/*"\
 $(CODE_COVERAGE_IGNORE_PATTERN);
code_coverage_v_genhtml = $(code_coverage_v_genhtml_$(V))
code_coverage_v_genhtml_ = $(code_coverage_v_genhtml_$(AM_DEFAULT_VERBOSITY))
code_coverage_v_genhtml_0 = @echo "  GEN   " $(CODE_COVERAGE_OUTPUT_DIRECTORY);
code_coverage_quiet = $(code_coverage_quiet_$(V))
code_coverage_quiet_ = $(code_coverage_quiet_$(AM_DEFAULT_VERBOSITY))
code_coverage_quiet_0 = --quiet

# sanitizes the test-name: replaces with underscores: dashes and dots
code_coverage_sanitize = $(subst -,_,$(subst .,_,$(1)))

# Use recursive makes in order to ignore errors during check
check-code-coverage:'"$CODE_COVERAGE_RULES_CHECK"'

# Capture code coverage data
code-coverage-capture: code-coverage-capture-hook'"$CODE_COVERAGE_RULES_CAPTURE"'

# Hook rule executed before code-coverage-capture, overridable by the user
code-coverage-capture-hook:

'"$CODE_COVERAGE_RULES_CLEAN"'

A''M_DISTCHECK_CONFIGURE_FLAGS ?=
A''M_DISTCHECK_CONFIGURE_FLAGS += --disable-code-coverage

.PHONY: check-code-coverage code-coverage-capture code-coverage-capture-hook code-coverage-clean
']

	AC_SUBST([CODE_COVERAGE_RULES])
	m4_ifdef([_AM_SUBST_NOTMAKE], [_AM_SUBST_NOTMAKE([CODE_COVERAGE_RULES])])
])
