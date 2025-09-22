diagtool - clang diagnostics tool
=================================

SYNOPSIS
--------

:program:`diagtool` *command* [*args*]

DESCRIPTION
-----------

:program:`diagtool` is a combination of four tools for dealing with diagnostics in :program:`clang`.

SUBCOMMANDS
-----------

:program:`diagtool` is separated into several subcommands each tailored to a
different purpose. A brief summary of each command follows, with more detail in
the sections that follow.

  * :ref:`find_diagnostic_id` - Print the id of the given diagnostic.
  * :ref:`list_warnings` - List warnings and their corresponding flags.
  * :ref:`show_enabled` - Show which warnings are enabled for a given command line.
  * :ref:`tree` - Show warning flags in a tree view.

.. _find_diagnostic_id:

find-diagnostic-id
~~~~~~~~~~~~~~~~~~

:program:`diagtool` find-diagnostic-id *diagnostic-name*

.. _list_warnings:

list-warnings
~~~~~~~~~~~~~

:program:`diagtool` list-warnings

.. _show_enabled:

show-enabled
~~~~~~~~~~~~

:program:`diagtool` show-enabled [*options*] *filename ...*

.. _tree:

tree
~~~~

:program:`diagtool` tree [*diagnostic-group*]
