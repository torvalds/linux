Frame and Thread Format
=======================

LLDB has a facility to allow users to define the format of the information that
generates the descriptions for threads and stack frames. Typically when your
program stops at a breakpoint you will get two lines that describes why your
thread stopped and where:

::

   * thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
       frame #0: test`main at test.c:5

Stack backtraces frames also have a similar information line:

::

   (lldb) thread backtrace
   * thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
       frame #0: 0x0000000100000e85 a.out`main + 4 at test.c:19
       frame #1: 0x0000000100000e40 a.out`start + 52

The two format strings that govern the printing in these output forms can
currently be set using the settings set command:

::

   (lldb) settings set thread-stop-format STRING
   (lldb) settings set frame-format STRING

The first of these is an abbreviated thread output, that just contains data
about the thread, and not the stop frame. It will always get used in situations
where the frame output follows immediately, so that information would be
redundant. The second is the frame printing.

There is another thread format used for commands like thread list where the
thread information isn't followed by frame info. In that case, it is convenient
to have frame zero information in the thread output. That format is set by:

::

   (lldb) settings set thread-format STRING


Format Strings
--------------

So what is the format of the format strings? Format strings can contain plain
text, control characters and variables that have access to the current program
state.

Normal characters are any text that doesn't contain a ``{``, ``}``, ``$``, or
``\`` character.

Variable names are found in between a ``${`` prefix, and end with a ``}``
suffix. In other words, a variable looks like ``${frame.pc}``.

Variables
---------

A complete list of currently supported format string variables is listed below:

+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| **Variable Name**                                 | **Description**                                                                                                                                                                                                                                                                             |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``file.basename``                                 | The current compile unit file basename for the current frame.                                                                                                                                                                                                                               |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``file.fullpath``                                 | The current compile unit file fullpath for the current frame.                                                                                                                                                                                                                               |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``language``                                      | The current compile unit language for the current frame.                                                                                                                                                                                                                                    |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``frame.index``                                   | The frame index (0, 1, 2, 3...)                                                                                                                                                                                                                                                             |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``frame.no-debug``                                | Evaluates to true if the frame has no debug info.                                                                                                                                                                                                                                           |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``frame.pc``                                      | The generic frame register for the program counter.                                                                                                                                                                                                                                         |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``frame.sp``                                      | The generic frame register for the stack pointer.                                                                                                                                                                                                                                           |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``frame.fp``                                      | The generic frame register for the frame pointer.                                                                                                                                                                                                                                           |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``frame.flags``                                   | The generic frame register for the flags register.                                                                                                                                                                                                                                          |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``frame.reg.NAME``                                | Access to any platform specific register by name (replace ``NAME`` with the name of the desired register).                                                                                                                                                                                  |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``function.name``                                 | The name of the current function or symbol.                                                                                                                                                                                                                                                 |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``function.name-with-args``                       | The name of the current function with arguments and values or the symbol name.                                                                                                                                                                                                              |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``function.name-without-args``                    | The name of the current function without arguments and values (used to include a function name in-line in the ``disassembly-format``)                                                                                                                                                       |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``function.mangled-name``                         | The mangled name of the current function or symbol.                                                                                                                                                                                                                                         |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``function.pc-offset``                            | The program counter offset within the current function or symbol                                                                                                                                                                                                                            |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``function.addr-offset``                          | The offset in bytes of the current function, formatted as " + dddd"                                                                                                                                                                                                                         |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``function.concrete-only-addr-offset-no-padding`` | Similar to ``function.addr-offset`` except that there are no spaces in the output (e.g. "+dddd") and the offset is computed from the nearest concrete function -- inlined functions are not included                                                                                        |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``function.changed``                              | Will evaluate to true when the line being formatted is a different symbol context from the previous line (may be used in ``disassembly-format`` to print the new function name on a line by itself at the start of a new function).  Inlined functions are not considered for this variable |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``function.initial-function``                     | Will evaluate to true if this is the start of the first function, as opposed to a change of functions (may be used in ``disassembly-format`` to print the function name for the first function being disassembled)                                                                          |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``line.file.basename``                            | The line table entry basename to the file for the current line entry in the current frame.                                                                                                                                                                                                  |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``line.file.fullpath``                            | The line table entry fullpath to the file for the current line entry in the current frame.                                                                                                                                                                                                  |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``line.number``                                   | The line table entry line number for the current line entry in the current frame.                                                                                                                                                                                                           |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``line.start-addr``                               | The line table entry start address for the current line entry in the current frame.                                                                                                                                                                                                         |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``line.end-addr``                                 | The line table entry end address for the current line entry in the current frame.                                                                                                                                                                                                           |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``module.file.basename``                          | The basename of the current module (shared library or executable)                                                                                                                                                                                                                           |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``module.file.fullpath``                          | The basename of the current module (shared library or executable)                                                                                                                                                                                                                           |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``process.file.basename``                         | The basename of the file for the process                                                                                                                                                                                                                                                    |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``process.file.fullpath``                         | The fullname of the file for the process                                                                                                                                                                                                                                                    |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``process.id``                                    | The process ID native to the system on which the inferior runs.                                                                                                                                                                                                                             |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``process.name``                                  | The name of the process at runtime                                                                                                                                                                                                                                                          |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``thread.id``                                     | The thread identifier for the current thread                                                                                                                                                                                                                                                |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``thread.index``                                  | The unique one based thread index ID which is guaranteed to be unique as threads come and go.                                                                                                                                                                                               |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``thread.name``                                   | The name of the thread if the target OS supports naming threads                                                                                                                                                                                                                             |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``thread.queue``                                  | The queue name of the thread if the target OS supports dispatch queues                                                                                                                                                                                                                      |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``thread.stop-reason``                            | A textual reason why the thread stopped. If the thread have a recognized frame, this displays its recognized stop reason. Otherwise, gets the stop info description.                                                                                                                        |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``thread.stop-reason-raw``                        | A textual reason why the thread stopped. Always returns stop info description.                                                                                                                                                                                                              |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``thread.return-value``                           | The return value of the latest step operation (currently only for step-out.)                                                                                                                                                                                                                |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``thread.completed-expression``                   | The expression result for a thread that just finished an interrupted expression evaluation.                                                                                                                                                                                                 |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``target.arch``                                   | The architecture of the current target                                                                                                                                                                                                                                                      |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``script.target:python_func``                     | Use a Python function to generate a piece of textual output                                                                                                                                                                                                                                 |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``script.process:python_func``                    | Use a Python function to generate a piece of textual output                                                                                                                                                                                                                                 |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``script.thread:python_func``                     | Use a Python function to generate a piece of textual output                                                                                                                                                                                                                                 |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``script.frame:python_func``                      | Use a Python function to generate a piece of textual output                                                                                                                                                                                                                                 |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``current-pc-arrow``                              | Prints either ``->`` or `` `` if the current pc value is matched (used in ``disassembly-format``)                                                                                                                                                                                           |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+
| ``addr-file-or-load``                             | Formats an address either as a load address, or if process has not yet been launched, as a load address (used in ``disassembly-format``)                                                                                                                                                    |
+---------------------------------------------------+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------+

Control Characters
------------------

Control characters include ``{``, ``}``, and ``\``.

The ``{`` and ``}`` are used for scoping blocks, and the ``\`` character allows
you to desensitize control characters and also emit non-printable characters.

Desensitizing Characters in the Format String
---------------------------------------------

The backslash control character allows your to enter the typical ``\a``,
``\b``, ``\f``, ``\n``, ``\r``, ``\t``, ``\v``, ``\\``, characters and along
with the standard octal representation ``\0123`` and hex ``\xAB`` characters.
This allows you to enter escape characters into your format strings and will
allow colorized output for terminals that support color.

Scoping
-------

Many times the information that you might have in your prompt might not be
available and you won``t want it to print out if it isn``t valid. To take care
of this you can enclose everything that must resolve into a scope. A scope is
starts with ``{`` and ends with ``}``. For example in order to only display the
current frame line table entry basename and line number when the information is
available for the current frame:

::

   "{ at {$line.file.basename}:${line.number}}"


Broken down this is:

- The start the scope: ``{`` ,
- format whose content will only be displayed if all information is available: ``at {$line.file.basename}:${line.number}``
- end the scope: ``}``

Making the Frame Format
-----------------------

The information that we see when stopped in a frame:

::

   frame #0: 0x0000000100000e85 a.out`main + 4 at test.c:19

can be displayed with the following format:

::

   "frame #${frame.index}: ${frame.pc}{ ${module.file.basename}`${function.name}{${function.pc-offset}}}{ at ${line.file.basename}:${line.number}}\n"

This breaks down to:

- Always print the frame index and frame PC: ``frame #${frame.index}: ${frame.pc}``,
- only print the module followed by a tick if there is a valid module for the current frame: ``{ ${module.file.basename}`}``,
- print the function name with optional offset: ``{${function.name}{${function.pc-offset}}}``,
- print the line info if it is available: ``{ at ${line.file.basename}:${line.number}}``,
- then finish off with a newline: ``\n``.

Making Your own Formats
-----------------------

When modifying your own format strings, it is useful to start with the default
values for the frame and thread format strings. These can be accessed with the
``settings show`` command:

::

   (lldb) settings show thread-format
   thread-format (format-string) = "thread #${thread.index}: tid = ${thread.id%tid}{, ${frame.pc}}{ ${module.file.basename}{`${function.name-with-args}{${frame.no-debug}${function.pc-offset}}}}{ at ${line.file.basename}:${line.number}}{, name = '${thread.name}'}{, queue = '${thread.queue}'}{, activity = '${thread.info.activity.name}'}{, ${thread.info.trace_messages} messages}{, stop reason = ${thread.stop-reason}}{\nReturn value: ${thread.return-value}}{\nCompleted expression: ${thread.completed-expression}}\n"
   (lldb) settings show frame-format
   frame-format (format-string) = "frame #${frame.index}:{ ${frame.no-debug}${frame.pc}}{ ${module.file.basename}{`${function.name-with-args}{${frame.no-debug}${function.pc-offset}}}}{ at ${line.file.basename}:${line.number}}{${function.is-optimized} [opt]}\n"

When making thread formats, you will need surround any of the information that
comes from a stack frame with scopes ({ frame-content }) as the thread format
doesn't always want to show frame information. When displaying the backtrace
for a thread, we don't need to duplicate the information for frame zero in the
thread information:

::

  (lldb) thread backtrace
  thread #1: tid = 0x2e03, stop reason = breakpoint 1.1 2.1
    frame #0: 0x0000000100000e85 a.out`main + 4 at test.c:19
    frame #1: 0x0000000100000e40 a.out`start + 52

The frame related variables are:

- ``${file.*}``
- ``${frame.*}``
- ``${function.*}``
- ``${line.*}``
- ``${module.*}``


Looking at the default format for the thread, and underlining the frame
information:

::

   thread #${thread.index}: tid = ${thread.id}{, ${frame.pc}}{ ${module.file.basename}`${function.name}{${function.pc-offset}}}{, stop reason = ${thread.stop-reason}}{, name = ${thread.name}}{, queue = ${thread.queue}}\n


We can see that all frame information is contained in scopes so that when the
thread information is displayed in a context where we only want to show thread
information, we can do so.

For both thread and frame formats, you can use ${script.target:python_func},
${script.process:python_func} and ${script.thread:python_func} (and of course
${script.frame:python_func} for frame formats) In all cases, the signature of
python_func is expected to be:

::

   def python_func(object,unused):
     ...
     return string

Where object is an instance of the SB class associated to the keyword you are
using.

e.g. Assuming your function looks like:

::

   def thread_printer_func (thread,unused):
     return "Thread %s has %d frames\n" % (thread.name, thread.num_frames)

And you set it up with:

::

   (lldb) settings set thread-format "${script.thread:thread_printer_func}"

you would see output like:

::

   * Thread main has 21 frames

