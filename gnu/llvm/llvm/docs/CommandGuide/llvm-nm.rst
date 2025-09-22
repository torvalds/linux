llvm-nm - list LLVM bitcode and object file's symbol table
==========================================================

.. program:: llvm-nm

SYNOPSIS
--------

:program:`llvm-nm` [*options*] [*filenames...*]

DESCRIPTION
-----------

The :program:`llvm-nm` utility lists the names of symbols from LLVM bitcode
files, object files, and archives. Each symbol is listed along with some simple
information about its provenance. If no filename is specified, *a.out* is used
as the input. If *-* is used as a filename, :program:`llvm-nm` will read a file
from its standard input stream.

:program:`llvm-nm`'s default output format is the traditional BSD :program:`nm`
output format. Each such output record consists of an (optional) 8-digit
hexadecimal address, followed by a type code character, followed by a name, for
each symbol. One record is printed per line; fields are separated by spaces.
When the address is omitted, it is replaced by 8 spaces.

The supported type code characters are as follows. Where both lower and
upper-case characters are listed for the same meaning, a lower-case character
represents a local symbol, whilst an upper-case character represents a global
(external) symbol:

a, A

 Absolute symbol.

b, B

 Uninitialized data (bss) object.

C

 Common symbol. Multiple definitions link together into one definition.

d, D

 Writable data object.

i, I

 COFF: .idata symbol or symbol in a section with IMAGE_SCN_LNK_INFO set.

n

 ELF: local symbol from non-alloc section.

 COFF: debug symbol.

N

 ELF: debug section symbol, or global symbol from non-alloc section.

s, S

 COFF: section symbol.

 Mach-O: absolute symbol or symbol from a section other than __TEXT_EXEC __text,
 __TEXT __text, __DATA __data, or __DATA __bss.

r, R

 Read-only data object.

t, T

 Code (text) object.

u

 ELF: GNU unique symbol.

U

 Named object is undefined in this file.

v

 ELF: Undefined weak object. It is not a link failure if the object is not
 defined.

V

 ELF: Defined weak object symbol. This definition will only be used if no
 regular definitions exist in a link. If multiple weak definitions and no
 regular definitions exist, one of the weak definitions will be used.

w

 Undefined weak symbol other than an ELF object symbol. It is not a link failure
 if the symbol is not defined.

W

 Defined weak symbol other than an ELF object symbol. This definition will only
 be used if no regular definitions exist in a link. If multiple weak definitions
 and no regular definitions exist, one of the weak definitions will be used.

\-

 Mach-O: N_STAB symbol.

?

 Something unrecognizable.

Because LLVM bitcode files typically contain objects that are not considered to
have addresses until they are linked into an executable image or dynamically
compiled "just-in-time", :program:`llvm-nm` does not print an address for any
symbol in an LLVM bitcode file, even symbols which are defined in the bitcode
file.

OPTIONS
-------

.. program:: llvm-nm

.. option:: -B

 Use BSD output format. Alias for ``--format=bsd``.

.. option:: -X

 Specify the type of XCOFF object file, ELF object file, or IR object file input
 from command line or from archive files that llvm-nm should examine. The
 mode must be one of the following:
 
   32
         Process only 32-bit object files.
   64
         Process only 64-bit object files.
   32_64
         Process both 32-bit and 64-bit object files.
   any
         Process all the supported object files.

  On AIX OS, the default is to process 32-bit object files only and to ignore
  64-bit objects. The can be changed by setting the OBJECT_MODE environment
  variable. For example, OBJECT_MODE=64 causes :program:`llvm-nm` to process
  64-bit objects and ignore 32-bit objects. The -X flag overrides the OBJECT_MODE
  variable.

  On other operating systems, the default is to process all object files: the
  OBJECT_MODE environment variable is not supported.

.. option:: --debug-syms, -a

 Show all symbols, even those usually suppressed.

.. option:: --defined-only, -U

 Print only symbols defined in this file.

.. option:: --demangle, -C

 Demangle symbol names.

.. option:: --dynamic, -D

 Display dynamic symbols instead of normal symbols.

.. option:: --export-symbols

 Print sorted symbols with their visibility (if applicable), with duplicates
 removed.

.. option:: --extern-only, -g

 Print only symbols whose definitions are external; that is, accessible from
 other files.

.. option:: --format=<format>, -f

 Select an output format; *format* may be *sysv*, *posix*, *darwin*, *bsd* or
 *just-symbols*.
 The default is *bsd*.

.. option:: --help, -h

 Print a summary of command-line options and their meanings.

.. option:: -j

 Print just the symbol names. Alias for `--format=just-symbols``.

.. option:: --line-numbers, -l

 Use debugging information to print the filenames and line numbers where
 symbols are defined. Undefined symbols have the location of their first
 relocation printed instead.

.. option:: -m

 Use Darwin format. Alias for ``--format=darwin``.

.. option:: --no-demangle

 Don't demangle symbol names. This is the default.

.. option:: --no-llvm-bc

 Disable the LLVM bitcode reader.

.. option:: --no-sort, -p

 Show symbols in the order encountered.

.. option:: --no-weak, -W

 Don't print weak symbols.

.. option:: --numeric-sort, -n, -v

 Sort symbols by address.

.. option:: --portability, -P

 Use POSIX.2 output format.  Alias for ``--format=posix``.

.. option:: --print-armap

 Print the archive symbol table, in addition to the symbols.

.. option:: --print-file-name, -A, -o

 Precede each symbol with the file it came from.

.. option:: --print-size, -S

 Show symbol size as well as address (not applicable for Mach-O).

.. option:: --quiet

 Suppress 'no symbols' diagnostic.

.. option:: --radix=<RADIX>, -t

 Specify the radix of the symbol address(es). Values accepted are *d* (decimal),
 *x* (hexadecimal) and *o* (octal).

.. option:: --reverse-sort, -r

 Sort symbols in reverse order.

.. option:: --size-sort

 Sort symbols by size.

.. option:: --special-syms

 Do not filter special symbols from the output.

.. option:: --undefined-only, -u

 Print only undefined symbols.

.. option:: --version, -V

 Display the version of the :program:`llvm-nm` executable, then exit. Does not
 stack with other commands.

.. option:: @<FILE>

 Read command-line options from response file `<FILE>`.

MACH-O SPECIFIC OPTIONS
-----------------------

.. option:: --add-dyldinfo

 Add symbols from the dyldinfo, if they are not already in the symbol table.
 This is the default.

.. option:: --add-inlinedinfo

 Add symbols from the inlined libraries, TBD file inputs only.

.. option:: --arch=<arch1[,arch2,...]>

 Dump the symbols from the specified architecture(s).

.. option:: --dyldinfo-only

 Dump only symbols from the dyldinfo.

.. option:: --no-dyldinfo

 Do not add any symbols from the dyldinfo.

.. option:: -s <segment> <section>

 Dump only symbols from this segment and section name.

.. option:: -x

 Print symbol entry in hex.

XCOFF SPECIFIC OPTIONS
----------------------

.. option:: --no-rsrc

  Exclude resource file symbols (``__rsrc``) from export symbol list.

BUGS
----

 * :program:`llvm-nm` does not support the full set of arguments that GNU
   :program:`nm` does.

EXIT STATUS
-----------

:program:`llvm-nm` exits with an exit code of zero.

SEE ALSO
--------

:manpage:`llvm-ar(1)`, :manpage:`llvm-objdump(1)`, :manpage:`llvm-readelf(1)`,
:manpage:`llvm-readobj(1)`
