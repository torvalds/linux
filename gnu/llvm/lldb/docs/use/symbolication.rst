Symbolication
=============

LLDB is separated into a shared library that contains the core of the debugger,
and a driver that implements debugging and a command interpreter. LLDB can be
used to symbolicate your crash logs and can often provide more information than
other symbolication programs:

- Inlined functions
- Variables that are in scope for an address, along with their locations

The simplest form of symbolication is to load an executable:

.. code-block:: text

   (lldb) target create --no-dependents --arch x86_64 /tmp/a.out

We use the ``--no-dependents`` flag with the ``target create`` command so that
we don't load all of the dependent shared libraries from the current system.
When we symbolicate, we are often symbolicating a binary that was running on
another system, and even though the main executable might reference shared
libraries in ``/usr/lib``, we often don't want to load the versions on the
current computer.

Using the ``image list`` command will show us a list of all shared libraries
associated with the current target. As expected, we currently only have a
single binary:

.. code-block:: text

   (lldb) image list
   [  0] 73431214-6B76-3489-9557-5075F03E36B4 0x0000000100000000 /tmp/a.out
         /tmp/a.out.dSYM/Contents/Resources/DWARF/a.out

Now we can look up an address:

.. code-block:: text

   (lldb) image lookup --address 0x100000aa3
         Address: a.out[0x0000000100000aa3] (a.out.__TEXT.__text + 131)
         Summary: a.out`main + 67 at main.c:13

Since we haven't specified a slide or any load addresses for individual
sections in the binary, the address that we use here is a file address. A file
address refers to a virtual address as defined by each object file.

If we didn't use the ``--no-dependents`` option with ``target create``, we
would have loaded all dependent shared libraries:

.. code-block:: text

   (lldb) image list
   [  0] 73431214-6B76-3489-9557-5075F03E36B4 0x0000000100000000 /tmp/a.out
         /tmp/a.out.dSYM/Contents/Resources/DWARF/a.out
   [  1] 8CBCF9B9-EBB7-365E-A3FF-2F3850763C6B 0x0000000000000000 /usr/lib/system/libsystem_c.dylib
   [  2] 62AA0B84-188A-348B-8F9E-3E2DB08DB93C 0x0000000000000000 /usr/lib/system/libsystem_dnssd.dylib
   [  3] C0535565-35D1-31A7-A744-63D9F10F12A4 0x0000000000000000 /usr/lib/system/libsystem_kernel.dylib
   ...

Now if we do a lookup using a file address, this can result in multiple matches
since most shared libraries have a virtual address space that starts at zero:

.. code-block:: text

   (lldb) image lookup -a 0x1000
         Address: a.out[0x0000000000001000] (a.out.__PAGEZERO + 4096)

         Address: libsystem_c.dylib[0x0000000000001000] (libsystem_c.dylib.__TEXT.__text + 928)
         Summary: libsystem_c.dylib`mcount + 9

         Address: libsystem_dnssd.dylib[0x0000000000001000] (libsystem_dnssd.dylib.__TEXT.__text + 456)
         Summary: libsystem_dnssd.dylib`ConvertHeaderBytes + 38

         Address: libsystem_kernel.dylib[0x0000000000001000] (libsystem_kernel.dylib.__TEXT.__text + 1116)
         Summary: libsystem_kernel.dylib`clock_get_time + 102
   ...

To avoid getting multiple file address matches, you can specify the name of the
shared library to limit the search:

.. code-block:: text

   (lldb) image lookup -a 0x1000 a.out
         Address: a.out[0x0000000000001000] (a.out.__PAGEZERO + 4096)

Defining Load Addresses for Sections
------------------------------------

When symbolicating your crash logs, it can be tedious if you always have to
adjust your crashlog-addresses into file addresses. To avoid having to do any
conversion, you can set the load address for the sections of the modules in
your target. Once you set any section load address, lookups will switch to
using load addresses. You can slide all sections in the executable by the same
amount, or set the load address for individual sections. The ``target modules
load --slide`` command allows us to set the load address for all sections.

Below is an example of sliding all sections in a.out by adding 0x123000 to each
section's file address:

.. code-block:: text

   (lldb) target create --no-dependents --arch x86_64 /tmp/a.out
   (lldb) target modules load --file a.out --slide 0x123000


It is often much easier to specify the actual load location of each section by
name. Crash logs on macOS have a Binary Images section that specifies that
address of the __TEXT segment for each binary. Specifying a slide requires
requires that you first find the original (file) address for the __TEXT
segment, and subtract the two values. If you specify the address of the __TEXT
segment with ``target modules load section address``, you don't need to do any
calculations. To specify the load addresses of sections we can specify one or
more section name + address pairs in the ``target modules load`` command:

.. code-block:: text

   (lldb) target create --no-dependents --arch x86_64 /tmp/a.out
   (lldb) target modules load --file a.out __TEXT 0x100123000

We specified that the __TEXT section is loaded at 0x100123000. Now that we have
defined where sections have been loaded in our target, any lookups we do will
now use load addresses so we don't have to do any math on the addresses in the
crashlog backtraces, we can just use the raw addresses:

.. code-block:: text

   (lldb) image lookup --address 0x100123aa3
         Address: a.out[0x0000000100000aa3] (a.out.__TEXT.__text + 131)
         Summary: a.out`main + 67 at main.c:13

Loading Multiple Executables
----------------------------

You often have more than one executable involved when you need to symbolicate a
crash log. When this happens, you create a target for the main executable or
one of the shared libraries, then add more modules to the target using the
``target modules add`` command.

Lets say we have a Darwin crash log that contains the following images:

.. code-block:: text

   Binary Images:
      0x100000000 -    0x100000ff7 <A866975B-CA1E-3649-98D0-6C5FAA444ECF> /tmp/a.out
   0x7fff83f32000 - 0x7fff83ffefe7 <8CBCF9B9-EBB7-365E-A3FF-2F3850763C6B> /usr/lib/system/libsystem_c.dylib
   0x7fff883db000 - 0x7fff883e3ff7 <62AA0B84-188A-348B-8F9E-3E2DB08DB93C> /usr/lib/system/libsystem_dnssd.dylib
   0x7fff8c0dc000 - 0x7fff8c0f7ff7 <C0535565-35D1-31A7-A744-63D9F10F12A4> /usr/lib/system/libsystem_kernel.dylib

First we create the target using the main executable and then add any extra
shared libraries we want:

.. code-block:: text

   (lldb) target create --no-dependents --arch x86_64 /tmp/a.out
   (lldb) target modules add /usr/lib/system/libsystem_c.dylib
   (lldb) target modules add /usr/lib/system/libsystem_dnssd.dylib
   (lldb) target modules add /usr/lib/system/libsystem_kernel.dylib


If you have debug symbols in standalone files, such as dSYM files on macOS,
you can specify their paths using the --symfile option for the ``target create``
(recent LLDB releases only) and ``target modules add`` commands:

.. code-block:: text

   (lldb) target create --no-dependents --arch x86_64 /tmp/a.out --symfile /tmp/a.out.dSYM
   (lldb) target modules add /usr/lib/system/libsystem_c.dylib --symfile /build/server/a/libsystem_c.dylib.dSYM
   (lldb) target modules add /usr/lib/system/libsystem_dnssd.dylib --symfile /build/server/b/libsystem_dnssd.dylib.dSYM
   (lldb) target modules add /usr/lib/system/libsystem_kernel.dylib --symfile /build/server/c/libsystem_kernel.dylib.dSYM

Then we set the load addresses for each __TEXT section (note the colors of the
load addresses above and below) using the first address from the Binary Images
section for each image:

.. code-block:: text

   (lldb) target modules load --file a.out 0x100000000
   (lldb) target modules load --file libsystem_c.dylib 0x7fff83f32000
   (lldb) target modules load --file libsystem_dnssd.dylib 0x7fff883db000
   (lldb) target modules load --file libsystem_kernel.dylib 0x7fff8c0dc000


Now any stack backtraces that haven't been symbolicated can be symbolicated
using ``image lookup`` with the raw backtrace addresses.

Given the following raw backtrace:

.. code-block:: text

   Thread 0 Crashed:: Dispatch queue: com.apple.main-thread
   0   libsystem_kernel.dylib        	0x00007fff8a1e6d46 __kill + 10
   1   libsystem_c.dylib             	0x00007fff84597df0 abort + 177
   2   libsystem_c.dylib             	0x00007fff84598e2a __assert_rtn + 146
   3   a.out                         	0x0000000100000f46 main + 70
   4   libdyld.dylib                 	0x00007fff8c4197e1 start + 1

We can now symbolicate the load addresses:

.. code-block:: text

   (lldb) image lookup -a 0x00007fff8a1e6d46
   (lldb) image lookup -a 0x00007fff84597df0
   (lldb) image lookup -a 0x00007fff84598e2a
   (lldb) image lookup -a 0x0000000100000f46


Getting Variable Information
----------------------------

If you add the --verbose flag to the ``image lookup --address`` command, you
can get verbose information which can often include the locations of some of
your local variables:

.. code-block:: text

   (lldb) image lookup --address 0x100123aa3 --verbose
         Address: a.out[0x0000000100000aa3] (a.out.__TEXT.__text + 110)
         Summary: a.out`main + 50 at main.c:13
         Module: file = "/tmp/a.out", arch = "x86_64"
   CompileUnit: id = {0x00000000}, file = "/tmp/main.c", language = "ISO C:1999"
      Function: id = {0x0000004f}, name = "main", range = [0x0000000100000bc0-0x0000000100000dc9)
      FuncType: id = {0x0000004f}, decl = main.c:9, compiler_type = "int (int, const char **, const char **, const char **)"
        Blocks: id = {0x0000004f}, range = [0x100000bc0-0x100000dc9)
                id = {0x000000ae}, range = [0x100000bf2-0x100000dc4)
      LineEntry: [0x0000000100000bf2-0x0000000100000bfa): /tmp/main.c:13:23
        Symbol: id = {0x00000004}, range = [0x0000000100000bc0-0x0000000100000dc9), name="main"
      Variable: id = {0x000000bf}, name = "path", type= "char [1024]", location = DW_OP_fbreg(-1072), decl = main.c:28
      Variable: id = {0x00000072}, name = "argc", type= "int", location = r13, decl = main.c:8
      Variable: id = {0x00000081}, name = "argv", type= "const char **", location = r12, decl = main.c:8
      Variable: id = {0x00000090}, name = "envp", type= "const char **", location = r15, decl = main.c:8
      Variable: id = {0x0000009f}, name = "aapl", type= "const char **", location = rbx, decl = main.c:8


The interesting part is the variables that are listed. The variables are the
parameters and local variables that are in scope for the address that was
specified. These variable entries have locations which are shown in bold above.
Crash logs often have register information for the first frame in each stack,
and being able to reconstruct one or more local variables can often help you
decipher more information from a crash log than you normally would be able to.
Note that this is really only useful for the first frame, and only if your
crash logs have register information for your threads.

Using Python API to Symbolicate
-------------------------------

All of the commands above can be done through the python script bridge. The
code below will recreate the target and add the three shared libraries that we
added in the darwin crash log example above:

.. code-block:: python

   triple = "x86_64-apple-macosx"
   platform_name = None
   add_dependents = False
   target = lldb.debugger.CreateTarget("/tmp/a.out", triple, platform_name, add_dependents, lldb.SBError())
   if target:
         # Get the executable module
         module = target.GetModuleAtIndex(0)
         target.SetSectionLoadAddress(module.FindSection("__TEXT"), 0x100000000)
         module = target.AddModule ("/usr/lib/system/libsystem_c.dylib", triple, None, "/build/server/a/libsystem_c.dylib.dSYM")
         target.SetSectionLoadAddress(module.FindSection("__TEXT"), 0x7fff83f32000)
         module = target.AddModule ("/usr/lib/system/libsystem_dnssd.dylib", triple, None, "/build/server/b/libsystem_dnssd.dylib.dSYM")
         target.SetSectionLoadAddress(module.FindSection("__TEXT"), 0x7fff883db000)
         module = target.AddModule ("/usr/lib/system/libsystem_kernel.dylib", triple, None, "/build/server/c/libsystem_kernel.dylib.dSYM")
         target.SetSectionLoadAddress(module.FindSection("__TEXT"), 0x7fff8c0dc000)

         load_addr = 0x00007fff8a1e6d46
         # so_addr is a section offset address, or a lldb.SBAddress object
         so_addr = target.ResolveLoadAddress (load_addr)
         # Get a symbol context for the section offset address which includes
         # a module, compile unit, function, block, line entry, and symbol
         sym_ctx = so_addr.GetSymbolContext (lldb.eSymbolContextEverything)
         print sym_ctx


Use Builtin Python Module to Symbolicate
----------------------------------------

LLDB includes a module in the lldb package named lldb.utils.symbolication. This module contains a lot of symbolication functions that simplify the symbolication process by allowing you to create objects that represent symbolication class objects such as:

- lldb.utils.symbolication.Address
- lldb.utils.symbolication.Section
- lldb.utils.symbolication.Image
- lldb.utils.symbolication.Symbolicator


**lldb.utils.symbolication.Address**

This class represents an address that will be symbolicated. It will cache any
information that has been looked up: module, compile unit, function, block,
line entry, symbol. It does this by having a lldb.SBSymbolContext as a member
variable.

**lldb.utils.symbolication.Section**

This class represents a section that might get loaded in a
lldb.utils.symbolication.Image. It has helper functions that allow you to set
it from text that might have been extracted from a crash log file.

**lldb.utils.symbolication.Image**

This class represents a module that might get loaded into the target we use for
symbolication. This class contains the executable path, optional symbol file
path, the triple, and the list of sections that will need to be loaded if we
choose the ask the target to load this image. Many of these objects will never
be loaded into the target unless they are needed by symbolication. You often
have a crash log that has 100 to 200 different shared libraries loaded, but
your crash log stack backtraces only use a few of these shared libraries. Only
the images that contain stack backtrace addresses need to be loaded in the
target in order to symbolicate.

Subclasses of this class will want to override the
locate_module_and_debug_symbols method:

.. code-block:: text

   class CustomImage(lldb.utils.symbolication.Image):
      def locate_module_and_debug_symbols (self):
         # Locate the module and symbol given the info found in the crash log

Overriding this function allows clients to find the correct executable module
and symbol files as they might reside on a build server.

**lldb.utils.symbolication.Symbolicator**

This class coordinates the symbolication process by loading only the
lldb.utils.symbolication.Image instances that need to be loaded in order to
symbolicate an supplied address.

**lldb.macosx.crashlog**

lldb.macosx.crashlog is a package that is distributed on macOS builds that
subclasses the above classes. This module parses the information in the Darwin
crash logs and creates symbolication objects that represent the images, the
sections and the thread frames for the backtraces. It then uses the functions
in the lldb.utils.symbolication to symbolicate the crash logs.

This module installs a new ``crashlog`` command into the lldb command
interpreter so that you can use it to parse and symbolicate macOS crash
logs:

.. code-block:: text

   (lldb) command script import lldb.macosx.crashlog
   "crashlog" and "save_crashlog" command installed, use the "--help" option for detailed help
   (lldb) crashlog /tmp/crash.log
   ...

The command that is installed has built in help that shows the options that can
be used when symbolicating:

.. code-block:: text

   (lldb) crashlog --help
   Usage: crashlog [options]  [FILE ...]

Symbolicate one or more darwin crash log files to provide source file and line
information, inlined stack frames back to the concrete functions, and
disassemble the location of the crash for the first frame of the crashed
thread. If this script is imported into the LLDB command interpreter, a
``crashlog`` command will be added to the interpreter for use at the LLDB
command line. After a crash log has been parsed and symbolicated, a target will
have been created that has all of the shared libraries loaded at the load
addresses found in the crash log file. This allows you to explore the program
as if it were stopped at the locations described in the crash log and functions
can  be disassembled and lookups can be performed using the addresses found in
the crash log.

.. code-block:: text

   Options:
   -h, --help            show this help message and exit
   -v, --verbose         display verbose debug info
   -g, --debug           display verbose debug logging
   -a, --load-all        load all executable images, not just the images found
                           in the crashed stack frames
   --images              show image list
   --debug-delay=NSEC    pause for NSEC seconds for debugger
   -c, --crashed-only    only symbolicate the crashed thread
   -d DISASSEMBLE_DEPTH, --disasm-depth=DISASSEMBLE_DEPTH
                           set the depth in stack frames that should be
                           disassembled (default is 1)
   -D, --disasm-all      enabled disassembly of frames on all threads (not just
                           the crashed thread)
   -B DISASSEMBLE_BEFORE, --disasm-before=DISASSEMBLE_BEFORE
                           the number of instructions to disassemble before the
                           frame PC
   -A DISASSEMBLE_AFTER, --disasm-after=DISASSEMBLE_AFTER
                           the number of instructions to disassemble after the
                           frame PC
   -C NLINES, --source-context=NLINES
                           show NLINES source lines of source context (default =
                           4)
   --source-frames=NFRAMES
                           show source for NFRAMES (default = 4)
   --source-all          show source for all threads, not just the crashed
                           thread
   -i, --interactive     parse all crash logs and enter interactive mode


The source for the "symbolication" and "crashlog" modules are available in git.

