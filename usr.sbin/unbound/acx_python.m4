AC_DEFUN([AC_PYTHON_DEVEL],[
        #
        # Allow the use of a (user set) custom python version
        #
        AC_ARG_VAR([PYTHON_VERSION],[The installed Python
                version to use, for example '2.3'. This string
                will be appended to the Python interpreter
                canonical name.])

        AC_PATH_PROG([PYTHON],[python[$PYTHON_VERSION]])
        if test -z "$PYTHON"; then
           AC_MSG_ERROR([Cannot find 'python$PYTHON_VERSION' in your system path. You can use the environment variable 'PYTHON_VERSION=version_number' for an explicit version.])
           PYTHON_VERSION=""
        fi

        if test -z "$PYTHON_VERSION"; then
		PYTHON_VERSION=`$PYTHON -c "import sys; \
			print(sys.version.split()[[0]])"`
	fi
	# calculate the version number components.
	[
	v="$PYTHON_VERSION"
	PYTHON_VERSION_MAJOR=`echo $v | sed 's/[^0-9].*//'`
	if test -z "$PYTHON_VERSION_MAJOR"; then PYTHON_VERSION_MAJOR="0"; fi
	v=`echo $v | sed -e 's/^[0-9]*$//' -e 's/[0-9]*[^0-9]//'`
	PYTHON_VERSION_MINOR=`echo $v | sed 's/[^0-9].*//'`
	if test -z "$PYTHON_VERSION_MINOR"; then PYTHON_VERSION_MINOR="0"; fi
	v=`echo $v | sed -e 's/^[0-9]*$//' -e 's/[0-9]*[^0-9]//'`
	PYTHON_VERSION_PATCH=`echo $v | sed 's/[^0-9].*//'`
	if test -z "$PYTHON_VERSION_PATCH"; then PYTHON_VERSION_PATCH="0"; fi
	]

	# For some systems, sysconfig exists, but has the wrong paths,
	# on Debian 10, for python 2.7 and 3.7. So, we check the version,
	# and for older versions try distutils.sysconfig first. For newer
	# versions>=3.10, where distutils.sysconfig is deprecated, use
	# sysconfig first and then attempt the other one.
	py_distutils_first="no"
	if test $PYTHON_VERSION_MAJOR -lt 3; then
		py_distutils_first="yes"
	fi
	if test $PYTHON_VERSION_MAJOR -eq 3 -a $PYTHON_VERSION_MINOR -lt 10; then
		py_distutils_first="yes"
	fi

	# Check if you have the first module
	if test "$py_distutils_first" = "yes"; then m="distutils"; else m="sysconfig"; fi
	sysconfig_module=""
	AC_MSG_CHECKING([for the $m Python module])
        if ac_modulecheck_result1=`$PYTHON -c "import $m" 2>&1`; then
                AC_MSG_RESULT([yes])
		sysconfig_module="$m"
	else
                AC_MSG_RESULT([no])
	fi

	# if not found, try the other one.
	if test -z "$sysconfig_module"; then
		if test "$py_distutils_first" = "yes"; then m2="sysconfig"; else m2="distutils"; fi
		AC_MSG_CHECKING([for the $m2 Python module])
		if ac_modulecheck_result2=`$PYTHON -c "import $m2" 2>&1`; then
			AC_MSG_RESULT([yes])
			sysconfig_module="$m2"
		else
			AC_MSG_RESULT([no])
			AC_MSG_ERROR([cannot import Python module "$m", or "$m2".
	Please check your Python installation. The errors are:
	$m
	$ac_modulecheck_result1
	$m2
	$ac_modulecheck_result2])
			PYTHON_VERSION=""
		fi
	fi
	if test "$sysconfig_module" = "distutils"; then sysconfig_module="distutils.sysconfig"; fi

        #
        # Check for Python include path
        #
        AC_MSG_CHECKING([for Python include path])
        if test -z "$PYTHON_CPPFLAGS"; then
		if test "$sysconfig_module" = "sysconfig"; then
			python_path=`$PYTHON -c 'import sysconfig; \
				print(sysconfig.get_path("include"));'`
		else
			python_path=`$PYTHON -c "import distutils.sysconfig; \
				print(distutils.sysconfig.get_python_inc());"`
		fi
                if test -n "${python_path}"; then
                        python_path="-I$python_path"
                fi
                PYTHON_CPPFLAGS=$python_path
        fi
        AC_MSG_RESULT([$PYTHON_CPPFLAGS])
        AC_SUBST([PYTHON_CPPFLAGS])

        #
        # Check for Python library path
        #
        AC_MSG_CHECKING([for Python library path])
        if test -z "$PYTHON_LDFLAGS"; then
                PYTHON_LDFLAGS=`$PYTHON -c "from $sysconfig_module import *; \
                        print('-L'+get_config_var('LIBDIR')+' -L'+get_config_var('LIBDEST')+' '+get_config_var('BLDLIBRARY'));"`
        fi
        AC_MSG_RESULT([$PYTHON_LDFLAGS])
        AC_SUBST([PYTHON_LDFLAGS])

        if test -z "$PYTHON_LIBDIR"; then
                PYTHON_LIBDIR=`$PYTHON -c "from $sysconfig_module import *; \
                        print(get_config_var('LIBDIR'));"`
        fi

        #
        # Check for site packages
        #
        AC_MSG_CHECKING([for Python site-packages path])
        if test -z "$PYTHON_SITE_PKG"; then
		if test "$sysconfig_module" = "sysconfig"; then
			PYTHON_SITE_PKG=`$PYTHON -c 'import sysconfig; \
				print(sysconfig.get_path("platlib"));'`
		else
			PYTHON_SITE_PKG=`$PYTHON -c "import distutils.sysconfig; \
				print(distutils.sysconfig.get_python_lib(1,0));"`
		fi
        fi
        AC_MSG_RESULT([$PYTHON_SITE_PKG])
        AC_SUBST([PYTHON_SITE_PKG])

        #
        # final check to see if everything compiles alright
        #
        AC_MSG_CHECKING([consistency of all components of python development environment])
        AC_LANG_PUSH([C])
        # save current global flags
        ac_save_LIBS="$LIBS"
        ac_save_CPPFLAGS="$CPPFLAGS"

        LIBS="$LIBS $PYTHON_LDFLAGS"
        CPPFLAGS="$CPPFLAGS $PYTHON_CPPFLAGS"
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[
                #include <Python.h>
        ]],[[
                Py_Initialize();
        ]])],[pythonexists=yes],[pythonexists=no])

        AC_MSG_RESULT([$pythonexists])

        if test ! "$pythonexists" = "yes"; then
           AC_MSG_ERROR([
  Could not link test program to Python. Maybe the main Python library has been
  installed in some non-standard library path. If so, pass it to configure,
  via the LDFLAGS environment variable.
  Example: ./configure LDFLAGS="-L/usr/non-standard-path/python/lib"
  ============================================================================
   ERROR!
   You probably have to install the development version of the Python package
   for your distribution.  The exact name of this package varies among them.
  ============================================================================
           ])
          PYTHON_VERSION=""
        fi
        AC_LANG_POP
        # turn back to default flags
        CPPFLAGS="$ac_save_CPPFLAGS"
        LIBS="$ac_save_LIBS"

        #
        # all done!
        #
])

