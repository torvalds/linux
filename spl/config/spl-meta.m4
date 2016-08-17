dnl #
dnl # DESCRIPTION:
dnl # Read meta data from the META file.  When building from a git repository
dnl # the SPL_META_RELEASE field will be overwritten if there is an annotated
dnl # tag matching the form SPL_META_NAME-SPL_META_VERSION-*.  This allows
dnl # for working builds to be uniquely identified using the git commit hash.
dnl #
dnl #    The META file format is as follows:
dnl #      ^[ ]*KEY:[ \t]+VALUE$
dnl #
dnl #    In other words:
dnl #    - KEY is separated from VALUE by a colon and one or more spaces/tabs.
dnl #    - KEY and VALUE are case sensitive.
dnl #    - Leading spaces are ignored.
dnl #    - First match wins for duplicate keys.
dnl #
dnl #    A line can be commented out by preceding it with a '#' (or technically
dnl #    any non-space character since that will prevent the regex from
dnl #    matching).
dnl #
dnl # WARNING:
dnl #   Placing a colon followed by a space or tab (ie, ":[ \t]+") within the
dnl #   VALUE will prematurely terminate the string since that sequence is
dnl #   used as the awk field separator.
dnl #
dnl # KEYS:
dnl #   The following META keys are recognized:
dnl #     Name, Version, Release, Date, Author, LT_Current, LT_Revision, LT_Age
dnl #
dnl # Written by Chris Dunlap <cdunlap@llnl.gov>.
dnl # Modified by Brian Behlendorf <behlendorf1@llnl.gov>.
dnl #
AC_DEFUN([SPL_AC_META], [
	AC_PROG_AWK
	AC_MSG_CHECKING([metadata])

	META="$srcdir/META"
	_spl_ac_meta_type="none"
	if test -f "$META"; then
		_spl_ac_meta_type="META file"

		SPL_META_NAME=_SPL_AC_META_GETVAL([(Name|Project|Package)]);
		if test -n "$SPL_META_NAME"; then
			AC_DEFINE_UNQUOTED([SPL_META_NAME], ["$SPL_META_NAME"],
				[Define the project name.]
			)
			AC_SUBST([SPL_META_NAME])
		fi

		SPL_META_VERSION=_SPL_AC_META_GETVAL([Version]);
		if test -n "$SPL_META_VERSION"; then
			AC_DEFINE_UNQUOTED([SPL_META_VERSION], ["$SPL_META_VERSION"],
				[Define the project version.]
			)
			AC_SUBST([SPL_META_VERSION])
		fi

		SPL_META_RELEASE=_SPL_AC_META_GETVAL([Release]);
		if test ! -f ".nogitrelease" && git rev-parse --git-dir > /dev/null 2>&1; then
			_match="${SPL_META_NAME}-${SPL_META_VERSION}"
			_alias=$(git describe --match=${_match} 2>/dev/null)
			_release=$(echo ${_alias}|cut -f3- -d'-'|sed 's/-/_/g')
			if test -n "${_release}"; then
				SPL_META_RELEASE=${_release}
				_spl_ac_meta_type="git describe"
			fi
		fi

		if test -n "$SPL_META_RELEASE"; then
			AC_DEFINE_UNQUOTED([SPL_META_RELEASE], ["$SPL_META_RELEASE"],
				[Define the project release.]
			)
			AC_SUBST([SPL_META_RELEASE])

			RELEASE="$SPL_META_RELEASE"
			AC_SUBST([RELEASE])
		fi

		SPL_META_LICENSE=_SPL_AC_META_GETVAL([License]);
		if test -n "$SPL_META_LICENSE"; then
			AC_DEFINE_UNQUOTED([SPL_META_LICENSE], ["$SPL_META_LICENSE"],
				[Define the project license.]
			)
			AC_SUBST([SPL_META_LICENSE])
		fi

		if test -n "$SPL_META_NAME" -a -n "$SPL_META_VERSION"; then
				SPL_META_ALIAS="$SPL_META_NAME-$SPL_META_VERSION"
				test -n "$SPL_META_RELEASE" && 
				        SPL_META_ALIAS="$SPL_META_ALIAS-$SPL_META_RELEASE"
				AC_DEFINE_UNQUOTED([SPL_META_ALIAS],
					["$SPL_META_ALIAS"],
					[Define the project alias string.] 
				)
				AC_SUBST([SPL_META_ALIAS])
		fi

		SPL_META_DATA=_SPL_AC_META_GETVAL([Date]);
		if test -n "$SPL_META_DATA"; then
			AC_DEFINE_UNQUOTED([SPL_META_DATA], ["$SPL_META_DATA"],
				[Define the project release date.] 
			)
			AC_SUBST([SPL_META_DATA])
		fi

		SPL_META_AUTHOR=_SPL_AC_META_GETVAL([Author]);
		if test -n "$SPL_META_AUTHOR"; then
			AC_DEFINE_UNQUOTED([SPL_META_AUTHOR], ["$SPL_META_AUTHOR"],
				[Define the project author.]
			)
			AC_SUBST([SPL_META_AUTHOR])
		fi

		m4_pattern_allow([^LT_(CURRENT|REVISION|AGE)$])
		SPL_META_LT_CURRENT=_SPL_AC_META_GETVAL([LT_Current]);
		SPL_META_LT_REVISION=_SPL_AC_META_GETVAL([LT_Revision]);
		SPL_META_LT_AGE=_SPL_AC_META_GETVAL([LT_Age]);
		if test -n "$SPL_META_LT_CURRENT" \
				 -o -n "$SPL_META_LT_REVISION" \
				 -o -n "$SPL_META_LT_AGE"; then
			test -n "$SPL_META_LT_CURRENT" || SPL_META_LT_CURRENT="0"
			test -n "$SPL_META_LT_REVISION" || SPL_META_LT_REVISION="0"
			test -n "$SPL_META_LT_AGE" || SPL_META_LT_AGE="0"
			AC_DEFINE_UNQUOTED([SPL_META_LT_CURRENT],
				["$SPL_META_LT_CURRENT"],
				[Define the libtool library 'current'
				 version information.]
			)
			AC_DEFINE_UNQUOTED([SPL_META_LT_REVISION],
				["$SPL_META_LT_REVISION"],
				[Define the libtool library 'revision'
				 version information.]
			)
			AC_DEFINE_UNQUOTED([SPL_META_LT_AGE], ["$SPL_META_LT_AGE"],
				[Define the libtool library 'age' 
				 version information.]
			)
			AC_SUBST([SPL_META_LT_CURRENT])
			AC_SUBST([SPL_META_LT_REVISION])
			AC_SUBST([SPL_META_LT_AGE])
		fi
	fi

	AC_MSG_RESULT([$_spl_ac_meta_type])
	]
)

dnl # _SPL_AC_META_GETVAL (KEY_NAME_OR_REGEX)
dnl #
dnl # Returns the META VALUE associated with the given KEY_NAME_OR_REGEX expr.
dnl #
dnl # Despite their resemblance to line noise,
dnl #   the "@<:@" and "@:>@" constructs are quadrigraphs for "[" and "]".
dnl #   <www.gnu.org/software/autoconf/manual/autoconf.html#Quadrigraphs>
dnl #
dnl # The "$[]1" and "$[]2" constructs prevent M4 parameter expansion
dnl #   so a literal $1 and $2 will be passed to the resulting awk script,
dnl #   whereas the "$1" will undergo M4 parameter expansion for the META key.
dnl #
AC_DEFUN([_SPL_AC_META_GETVAL],
	[`$AWK -F ':@<:@ \t@:>@+' '$[]1 ~ /^ *$1$/ { print $[]2; exit }' $META`]dnl
)
