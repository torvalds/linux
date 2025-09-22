llvm-otool - Mach-O dumping tool
================================

.. program:: llvm-otool

SYNOPSIS
--------

:program:`llvm-otool` [*option...*] *[file...]*

DESCRIPTION
-----------

:program:`llvm-otool` is a tool for dumping Mach-O files.

It attempts to be command-line-compatible and output-compatible with macOS's
:program:`otool`.

OPTIONS
-------

.. option:: -arch <value>

 Select slice of universal Mach-O file.

.. option:: -chained_fixups

 Print chained fixup information.

.. option:: -C

 Print linker optimization hints.

.. option:: -dyld_info

  Print bind and rebase information.

.. option:: -D

 Print shared library id.

.. option:: -d

 Print data section.

.. option:: -f

 Print universal headers.

.. option:: -G

 Print data-in-code table.

.. option:: --help-hidden

 Print help for hidden flags.

.. option:: --help

 Print help.

.. option:: -h

 Print mach header.

.. option:: -I

 Print indirect symbol table.

.. option:: -j

 Print opcode bytes.

.. option:: -L

 Print used shared libraries.

.. option:: -l

 Print load commands.

.. option:: -mcpu=<value>

 Select cpu for disassembly.

.. option:: -o

 Print Objective-C segment.

.. option:: -P

 Print __TEXT,__info_plist section as strings.

.. option:: -p <function name>

 Start disassembly at <function name>.

.. option:: -r

 Print relocation entries.

.. option:: -s <segname> <sectname>

 Print contents of section.

.. option:: -t

 Print text section.

.. option:: --version

 Print version.

.. option:: -V

 Symbolize disassembled operands (implies :option:`-v`).

.. option:: -v

 Verbose output / disassemble when printing text sections.

.. option:: -X

 Omit leading addresses or headers.

.. option:: -x

 Print all text sections.

.. option:: @<FILE>

 Read command-line options and commands from response file `<FILE>`.

EXIT STATUS
-----------

:program:`llvm-otool` exits with a non-zero exit code if there is an error.
Otherwise, it exits with code 0.

BUGS
----

To report bugs, please visit <https://github.com/llvm/llvm-project/labels/tools:llvm-objdump/>.

SEE ALSO
--------

:manpage:`llvm-nm(1)`, :manpage:`llvm-objdump(1)`
