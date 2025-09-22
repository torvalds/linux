llvm-install-name-tool - LLVM tool for manipulating install-names and rpaths
============================================================================

.. program:: llvm-install-name-tool

SYNOPSIS
--------

:program:`llvm-install-name-tool` [*options*] *input*

DESCRIPTION
-----------

:program:`llvm-install-name-tool` is a tool to manipulate dynamic shared library
install names and rpaths listed in a Mach-O binary.

For most scenarios, it works as a drop-in replacement for Apple's
:program:`install_name_tool`.

OPTIONS
--------
At least one of the following options are required, and some options can be
combined with other options. Options :option:`-add_rpath`, :option:`-delete_rpath`,
and :option:`-rpath` can be combined in an invocation only if they do not share
the same `<rpath>` value.

.. option:: -add_rpath <rpath>

 Add an rpath named ``<rpath>`` to the specified binary. Can be specified multiple
 times to add multiple rpaths. Throws an error if ``<rpath>`` is already listed in
 the binary.

.. option:: -change <old_install_name> <new_install_name>

 Change an install name ``<old_install_name>`` to ``<new_install_name>`` in the
 specified binary. Can be specified multiple times to change multiple dependent shared
 library install names. Option is ignored if ``<old_install_name>`` is not listed
 in the specified binary.

.. option:: -delete_rpath <rpath>

 Delete an rpath named ``<rpath>`` from the specified binary. Can be specified multiple
 times to delete multiple rpaths. Throws an error if ``<rpath>`` is not listed in
 the binary.

.. option:: -delete_all_rpaths

  Deletes all rpaths from the binary.

.. option:: --help, -h

 Print a summary of command line options.

.. option:: -id <name>

 Change shared library's identification name under LC_ID_DYLIB to ``<name>`` in the
 specified binary. If specified multiple times, only the last :option:`-id` option is
 selected. Option is ignored if the specified Mach-O binary is not a dynamic shared library.

.. option:: -rpath <old_rpath> <new_rpath>

 Change an rpath named ``<old_rpath>`` to ``<new_rpath>`` in the specified binary. Can be specified
 multiple times to change multiple rpaths. Throws an error if ``<old_rpath>`` is not listed
 in the binary or ``<new_rpath>`` is already listed in the binary.

.. option:: --version, -V

 Display the version of the :program:`llvm-install-name-tool` executable.

EXIT STATUS
-----------

:program:`llvm-install-name-tool` exits with a non-zero exit code if there is an error.
Otherwise, it exits with code 0.

BUGS
----

To report bugs, please visit <https://github.com/llvm/llvm-project/labels/tools:llvm-objcopy/strip/>.

SEE ALSO
--------

:manpage:`llvm-objcopy(1)`
