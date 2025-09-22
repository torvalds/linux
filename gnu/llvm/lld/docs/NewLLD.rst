The ELF, COFF and Wasm Linkers
==============================

The ELF Linker as a Library
---------------------------

You can embed LLD to your program by linking against it and calling the linker's
entry point function ``lld::lldMain``.

The current policy is that it is your responsibility to give trustworthy object
files. The function is guaranteed to return as long as you do not pass corrupted
or malicious object files. A corrupted file could cause a fatal error or SEGV.
That being said, you don't need to worry too much about it if you create object
files in the usual way and give them to the linker. It is naturally expected to
work, or otherwise it's a linker's bug.

Design
======

We will describe the design of the linkers in the rest of the document.

Key Concepts
------------

Linkers are fairly large pieces of software.
There are many design choices you have to make to create a complete linker.

This is a list of design choices we've made for ELF and COFF LLD.
We believe that these high-level design choices achieved a right balance
between speed, simplicity and extensibility.

* Implement as native linkers

  We implemented the linkers as native linkers for each file format.

  The linkers share the same design but share very little code.
  Sharing code makes sense if the benefit is worth its cost.
  In our case, the object formats are different enough that we thought the layer
  to abstract the differences wouldn't be worth its complexity and run-time
  cost.  Elimination of the abstract layer has greatly simplified the
  implementation.

* Speed by design

  One of the most important things in archiving high performance is to
  do less rather than do it efficiently.
  Therefore, the high-level design matters more than local optimizations.
  Since we are trying to create a high-performance linker,
  it is very important to keep the design as efficient as possible.

  Broadly speaking, we do not do anything until we have to do it.
  For example, we do not read section contents or relocations
  until we need them to continue linking.
  When we need to do some costly operation (such as looking up
  a hash table for each symbol), we do it only once.
  We obtain a handle (which is typically just a pointer to actual data)
  on the first operation and use it throughout the process.

* Efficient archive file handling

  LLD's handling of archive files (the files with ".a" file extension) is
  different from the traditional Unix linkers and similar to Windows linkers.
  We'll describe how the traditional Unix linker handles archive files, what the
  problem is, and how LLD approached the problem.

  The traditional Unix linker maintains a set of undefined symbols during
  linking.  The linker visits each file in the order as they appeared in the
  command line until the set becomes empty. What the linker would do depends on
  file type.

  - If the linker visits an object file, the linker links object files to the
    result, and undefined symbols in the object file are added to the set.

  - If the linker visits an archive file, it checks for the archive file's
    symbol table and extracts all object files that have definitions for any
    symbols in the set.

  This algorithm sometimes leads to a counter-intuitive behavior.  If you give
  archive files before object files, nothing will happen because when the linker
  visits archives, there is no undefined symbols in the set.  As a result, no
  files are extracted from the first archive file, and the link is done at that
  point because the set is empty after it visits one file.

  You can fix the problem by reordering the files,
  but that cannot fix the issue of mutually-dependent archive files.

  Linking mutually-dependent archive files is tricky.  You may specify the same
  archive file multiple times to let the linker visit it more than once.  Or,
  you may use the special command line options, `--start-group` and
  `--end-group`, to let the linker loop over the files between the options until
  no new symbols are added to the set.

  Visiting the same archive files multiple times makes the linker slower.

  Here is how LLD approaches the problem. Instead of memorizing only undefined
  symbols, we program LLD so that it memorizes all symbols.  When it sees an
  undefined symbol that can be resolved by extracting an object file from an
  archive file it previously visited, it immediately extracts the file and links
  it.  It is doable because LLD does not forget symbols it has seen in archive
  files.

  We believe that LLD's way is efficient and easy to justify.

  The semantics of LLD's archive handling are different from the traditional
  Unix's.  You can observe it if you carefully craft archive files to exploit
  it.  However, in reality, we don't know any program that cannot link with our
  algorithm so far, so it's not going to cause trouble.

Numbers You Want to Know
------------------------

To give you intuition about what kinds of data the linker is mainly working on,
I'll give you the list of objects and their numbers LLD has to read and process
in order to link a very large executable. In order to link Chrome with debug
info, which is roughly 2 GB in output size, LLD reads

- 17,000 files,
- 1,800,000 sections,
- 6,300,000 symbols, and
- 13,000,000 relocations.

LLD produces the 2 GB executable in 15 seconds.

These numbers vary depending on your program, but in general,
you have a lot of relocations and symbols for each file.
If your program is written in C++, symbol names are likely to be
pretty long because of name mangling.

It is important to not waste time on relocations and symbols.

In the above case, the total amount of symbol strings is 450 MB,
and inserting all of them to a hash table takes 1.5 seconds.
Therefore, if you causally add a hash table lookup for each symbol,
it would slow down the linker by 10%. So, don't do that.

On the other hand, you don't have to pursue efficiency
when handling files.

Important Data Structures
-------------------------

We will describe the key data structures in LLD in this section.  The linker can
be understood as the interactions between them.  Once you understand their
functions, the code of the linker should look obvious to you.

* Symbol

  This class represents a symbol.
  They are created for symbols in object files or archive files.
  The linker creates linker-defined symbols as well.

  There are basically three types of Symbols: Defined, Undefined, or Lazy.

  - Defined symbols are for all symbols that are considered as "resolved",
    including real defined symbols, COMDAT symbols, common symbols,
    absolute symbols, linker-created symbols, etc.
  - Undefined symbols represent undefined symbols, which need to be replaced by
    Defined symbols by the resolver until the link is complete.
  - Lazy symbols represent symbols we found in archive file headers
    which can turn into Defined if we read archive members.

  There's only one Symbol instance for each unique symbol name. This uniqueness
  is guaranteed by the symbol table. As the resolver reads symbols from input
  files, it replaces an existing Symbol with the "best" Symbol for its symbol
  name using the placement new.

  The above mechanism allows you to use pointers to Symbols as a very cheap way
  to access name resolution results. Assume for example that you have a pointer
  to an undefined symbol before name resolution. If the symbol is resolved to a
  defined symbol by the resolver, the pointer will "automatically" point to the
  defined symbol, because the undefined symbol the pointer pointed to will have
  been replaced by the defined symbol in-place.

* SymbolTable

  SymbolTable is basically a hash table from strings to Symbols
  with logic to resolve symbol conflicts. It resolves conflicts by symbol type.

  - If we add Defined and Undefined symbols, the symbol table will keep the
    former.
  - If we add Defined and Lazy symbols, it will keep the former.
  - If we add Lazy and Undefined, it will keep the former,
    but it will also trigger the Lazy symbol to load the archive member
    to actually resolve the symbol.

* Chunk (COFF specific)

  Chunk represents a chunk of data that will occupy space in an output.
  Each regular section becomes a chunk.
  Chunks created for common or BSS symbols are not backed by sections.
  The linker may create chunks to append additional data to an output as well.

  Chunks know about their size, how to copy their data to mmap'ed outputs,
  and how to apply relocations to them.
  Specifically, section-based chunks know how to read relocation tables
  and how to apply them.

* InputSection (ELF specific)

  Since we have less synthesized data for ELF, we don't abstract slices of
  input files as Chunks for ELF. Instead, we directly use the input section
  as an internal data type.

  InputSection knows about their size and how to copy themselves to
  mmap'ed outputs, just like COFF Chunks.

* OutputSection

  OutputSection is a container of InputSections (ELF) or Chunks (COFF).
  An InputSection or Chunk belongs to at most one OutputSection.

There are mainly three actors in this linker.

* InputFile

  InputFile is a superclass of file readers.
  We have a different subclass for each input file type,
  such as regular object file, archive file, etc.
  They are responsible for creating and owning Symbols and InputSections/Chunks.

* Writer

  The writer is responsible for writing file headers and InputSections/Chunks to
  a file.  It creates OutputSections, put all InputSections/Chunks into them,
  assign unique, non-overlapping addresses and file offsets to them, and then
  write them down to a file.

* Driver

  The linking process is driven by the driver. The driver:

  - processes command line options,
  - creates a symbol table,
  - creates an InputFile for each input file and puts all symbols within into
    the symbol table,
  - checks if there's no remaining undefined symbols,
  - creates a writer,
  - and passes the symbol table to the writer to write the result to a file.

Link-Time Optimization
----------------------

LTO is implemented by handling LLVM bitcode files as object files.
The linker resolves symbols in bitcode files normally. If all symbols
are successfully resolved, it then runs LLVM passes
with all bitcode files to convert them to one big regular ELF/COFF file.
Finally, the linker replaces bitcode symbols with ELF/COFF symbols,
so that they are linked as if they were in the native format from the beginning.

The details are described in this document.
https://llvm.org/docs/LinkTimeOptimization.html

Glossary
--------

* RVA (COFF)

  Short for Relative Virtual Address.

  Windows executables or DLLs are not position-independent; they are
  linked against a fixed address called an image base. RVAs are
  offsets from an image base.

  Default image bases are 0x140000000 for executables and 0x18000000
  for DLLs. For example, when we are creating an executable, we assume
  that the executable will be loaded at address 0x140000000 by the
  loader, so we apply relocations accordingly. Result texts and data
  will contain raw absolute addresses.

* VA

  Short for Virtual Address. For COFF, it is equivalent to RVA + image base.

* Base relocations (COFF)

  Relocation information for the loader. If the loader decides to map
  an executable or a DLL to a different address than their image
  bases, it fixes up binaries using information contained in the base
  relocation table. A base relocation table consists of a list of
  locations containing addresses. The loader adds a difference between
  RVA and actual load address to all locations listed there.

  Note that this run-time relocation mechanism is much simpler than ELF.
  There's no PLT or GOT. Images are relocated as a whole just
  by shifting entire images in memory by some offsets. Although doing
  this breaks text sharing, I think this mechanism is not actually bad
  on today's computers.

* ICF

  Short for Identical COMDAT Folding (COFF) or Identical Code Folding (ELF).

  ICF is an optimization to reduce output size by merging read-only sections
  by not only their names but by their contents. If two read-only sections
  happen to have the same metadata, actual contents and relocations,
  they are merged by ICF. It is known as an effective technique,
  and it usually reduces C++ program's size by a few percent or more.

  Note that this is not an entirely sound optimization. C/C++ require
  different functions have different addresses. If a program depends on
  that property, it would fail at runtime.

  On Windows, that's not really an issue because MSVC link.exe enabled
  the optimization by default. As long as your program works
  with the linker's default settings, your program should be safe with ICF.

  On Unix, your program is generally not guaranteed to be safe with ICF,
  although large programs happen to work correctly.
  LLD works fine with ICF for example.

Other Info
----------

.. toctree::
   :maxdepth: 1

   missingkeyfunction
