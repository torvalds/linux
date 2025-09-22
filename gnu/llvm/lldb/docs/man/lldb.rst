:orphan:

lldb -- The Debugger
====================

.. program:: lldb

SYNOPSIS
--------

| :program:`lldb` [*options*] *executable*

DESCRIPTION
-----------

:program:`lldb` is a next generation, high-performance debugger. It is built as
a set of reusable components which highly leverage existing libraries in the
larger LLVM Project, such as the Clang expression parser and LLVM disassembler.

:program:`lldb` is the default debugger in Xcode on macOS and supports
debugging C, Objective-C and C++ on the desktop and iOS devices and simulator.

All of the code in the LLDB project is available under the Apache 2.0 License
with LLVM exceptions.

ATTACHING
---------

.. option:: --attach-name <name>

 Tells the debugger to attach to a process with the given name.

.. option:: --attach-pid <pid>

 Tells the debugger to attach to a process with the given pid.

.. option:: -n <value>

 Alias for --attach-name

.. option:: -p <value>

 Alias for --attach-pid

.. option:: --wait-for

 Tells the debugger to wait for a process with the given pid or name to launch before attaching.

.. option:: -w

 Alias for --wait-for

COMMANDS
--------

.. option:: --batch

 Tells the debugger to run the commands from -s, -S, -o & -O, and then quit.

.. option:: -b

 Alias for --batch

.. option:: -K <value>

 Alias for --source-on-crash

.. option:: -k <value>

 Alias for --one-line-on-crash

.. option:: --local-lldbinit

 Allow the debugger to parse the .lldbinit files in the current working directory, unless --no-lldbinit is passed.

.. option:: --no-lldbinit

 Do not automatically parse any '.lldbinit' files.

.. option:: --one-line-before-file <command>

 Tells the debugger to execute this one-line lldb command before any file provided on the command line has been loaded.

.. option::  --one-line-on-crash <command>

 When in batch mode, tells the debugger to run this one-line lldb command if the target crashes.

.. option:: --one-line <command>

 Tells the debugger to execute this one-line lldb command after any file provided on the command line has been loaded.

.. option:: -O <value>

 Alias for --one-line-before-file

.. option:: -o <value>

 Alias for --one-line

.. option:: -Q

 Alias for --source-quietly

.. option:: --source-before-file <file>

 Tells the debugger to read in and execute the lldb commands in the given file, before any file has been loaded.

.. option:: --source-on-crash <file>

 When in batch mode, tells the debugger to source this file of lldb commands if the target crashes.

.. option:: --source-quietly

 Tells the debugger not to echo commands while sourcing files or one-line commands provided on the command line.

.. option:: --source <file>

 Tells the debugger to read in and execute the lldb commands in the given file, after any file has been loaded.

.. option:: -S <value>

 Alias for --source-before-file

.. option:: -s <value>

 Alias for --source

.. option:: -x

 Alias for --no-lldbinit

OPTIONS
-------

.. option:: --arch <architecture>

 Tells the debugger to use the specified architecture when starting and running the program.

.. option:: -a <value>

 Alias for --arch

.. option:: --capture-path <filename>

 Tells the debugger to use the given filename for the reproducer.

.. option:: --capture

 Tells the debugger to capture a reproducer.

.. option:: --core <filename>

 Tells the debugger to use the full path to <filename> as the core file.

.. option:: -c <value>

 Alias for --core

.. option:: --debug

 Tells the debugger to print out extra information for debugging itself.

.. option:: -d

 Alias for --debug

.. option:: --editor

 Tells the debugger to open source files using the host's "external editor" mechanism.

.. option:: -e

 Alias for --editor

.. option:: --file <filename>

 Tells the debugger to use the file <filename> as the program to be debugged.

.. option:: -f <value>

 Alias for --file

.. option:: --help

 Prints out the usage information for the LLDB debugger.

.. option:: -h

 Alias for --help

.. option:: --no-use-colors

 Do not use colors.

.. option:: --replay <filename>

 Tells the debugger to replay a reproducer from <filename>.

.. option:: --version

 Prints out the current version number of the LLDB debugger.

.. option:: -v

 Alias for --version

.. option:: -X

 Alias for --no-use-color

REPL
----

.. option:: -r=<flags>

 Alias for --repl=<flags>

.. option:: --repl-language <language>

 Chooses the language for the REPL.

.. option:: --repl=<flags>

 Runs lldb in REPL mode with a stub process with the given flags.

.. option:: -R <value>

 Alias for --repl-language

SCRIPTING
---------

.. option:: -l <value>

 Alias for --script-language

.. option:: --print-script-interpreter-info

  Prints out a json dictionary with information about the scripting language interpreter.

.. option:: --python-path

 Prints out the path to the lldb.py file for this version of lldb.

.. option:: -P

 Alias for --python-path

.. option:: --script-language <language>

 Tells the debugger to use the specified scripting language for user-defined scripts.

EXAMPLES
--------

The debugger can be started in several modes.

Passing an executable as a positional argument prepares lldb to debug the given
executable. To disambiguate between arguments passed to lldb and arguments
passed to the debugged executable, arguments starting with a - must be passed
after --.

  lldb --arch x86_64 /path/to/program program argument -- --arch armv7

For convenience, passing the executable after -- is also supported.

  lldb --arch x86_64 -- /path/to/program program argument --arch armv7

Passing one of the attach options causes :program:`lldb` to immediately attach
to the given process.

  lldb -p <pid>
  lldb -n <process-name>

Passing --repl starts :program:`lldb` in REPL mode.

  lldb -r

Passing --core causes :program:`lldb` to debug the core file.

  lldb -c /path/to/core

Command options can be combined with these modes and cause :program:`lldb` to
run the specified commands before or after events, like loading the file or
crashing, in the order provided on the command line.

  lldb -O 'settings set stop-disassembly-count 20' -o 'run' -o 'bt'
  lldb -S /source/before/file -s /source/after/file
  lldb -K /source/before/crash -k /source/after/crash

Note: In REPL mode no file is loaded, so commands specified to run after
loading the file (via -o or -s) will be ignored.

USING LLDB
----------

In :program:`lldb` there is a help command which can be used to find
descriptions and examples of all :program:`lldb` commands.  To get help on
"breakpoint set" you would type "help breakpoint set".

There is also an apropos command which will search the help text of all
commands for a given term ‐‐ this is useful for locating a command by topic.
For instance, "apropos breakpoint" will list any command that has the word
"breakpoint" in its help text.

CONFIGURATION FILES
-------------------

:program:`lldb` reads things like settings, aliases and commands from the
.lldbinit file.

First, :program:`lldb` will try to read the application specific init file
whose name is ~/.lldbinit followed by a "-" and the name of the current
program. This would be ~/.lldbinit-lldb for the command line :program:`lldb`
and ~/.lldbinit-Xcode for Xcode. If there is no application specific init
file, :program:`lldb` will look for an init file in the home directory.
If launched with a `REPL`_ option, it will first look for a REPL configuration
file, specific to the REPL language. The init file should be named as follow:
``.lldbinit-<language>-repl`` (i.e. ``.lldbinit-swift-repl``). If this file doesn't
exist, or :program:`lldb` wasn't launch with `REPL`_, meaning there is neither
a REPL init file nor an application specific init file, ``lldb`` will fallback to
the global ~/.lldbinit.

Secondly, it will look for an .lldbinit file in the current working directory.
For security reasons, :program:`lldb` will print a warning and not source this
file by default. This behavior can be changed by changing the
target.load-cwd-lldbinit setting.

To always load the .lldbinit file in the current working directory, add the
following command to ~/.lldbinit:

  settings set target.load-cwd-lldbinit true

To never load the .lldbinit file in the current working directory and silence
the warning, add the following command to ~/.lldbinit:

  settings set target.load-cwd-lldbinit false

SEE ALSO
--------

The LLDB project page https://lldb.llvm.org has many different resources
for :program:`lldb` users ‐‐ the gdb/lldb command equivalence page
https://lldb.llvm.org/use/map.html can be especially helpful for users
coming from gdb.
