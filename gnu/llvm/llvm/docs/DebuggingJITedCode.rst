=====================
Debugging JIT-ed Code
=====================

Background
==========

Without special runtime support, debugging dynamically generated code can be
quite painful.  Debuggers generally read debug information from object files on
disk, but for JITed code there is no such file to look for.

In order to hand over the necessary debug info, `GDB established an
interface <https://sourceware.org/gdb/onlinedocs/gdb/JIT-Interface.html>`_
for registering JITed code with debuggers. LLDB implements it in the
JITLoaderGDB plugin.  On the JIT side, LLVM MCJIT does implement the interface
for ELF object files.

At a high level, whenever MCJIT generates new machine code, it does so in an
in-memory object file that contains the debug information in DWARF format.
MCJIT then adds this in-memory object file to a global list of dynamically
generated object files and calls a special function
``__jit_debug_register_code`` that the debugger knows about. When the debugger
attaches to a process, it puts a breakpoint in this function and associates a
special handler with it.  Once MCJIT calls the registration function, the
debugger catches the breakpoint signal, loads the new object file from the
inferior's memory and resumes execution.  This way it can obtain debug
information for pure in-memory object files.


GDB Version
===========

In order to debug code JIT-ed by LLVM, you need GDB 7.0 or newer, which is
available on most modern distributions of Linux.  The version of GDB that
Apple ships with Xcode has been frozen at 6.3 for a while.


LLDB Version
============

Due to a regression in release 6.0, LLDB didn't support JITed code debugging for
a while.  The bug was fixed in mainline recently, so that debugging JITed ELF
objects should be possible again from the upcoming release 12.0 on. On macOS the
feature must be enabled explicitly using the ``plugin.jit-loader.gdb.enable``
setting.


Debugging MCJIT-ed code
=======================

The emerging MCJIT component of LLVM allows full debugging of JIT-ed code with
GDB.  This is due to MCJIT's ability to use the MC emitter to provide full
DWARF debugging information to GDB.

Note that lli has to be passed the ``--jit-kind=mcjit`` flag to JIT the code
with MCJIT instead of the newer ORC JIT.

Example
-------

Consider the following C code (with line numbers added to make the example
easier to follow):

..
   FIXME:
   Sphinx has the ability to automatically number these lines by adding
   :linenos: on the line immediately following the `.. code-block:: c`, but
   it looks like garbage; the line numbers don't even line up with the
   lines. Is this a Sphinx bug, or is it a CSS problem?

.. code-block:: c

   1   int compute_factorial(int n)
   2   {
   3       if (n <= 1)
   4           return 1;
   5
   6       int f = n;
   7       while (--n > 1)
   8           f *= n;
   9       return f;
   10  }
   11
   12
   13  int main(int argc, char** argv)
   14  {
   15      if (argc < 2)
   16          return -1;
   17      char firstletter = argv[1][0];
   18      int result = compute_factorial(firstletter - '0');
   19
   20      // Returned result is clipped at 255...
   21      return result;
   22  }

Here is a sample command line session that shows how to build and run this
code via ``lli`` inside LLDB:

.. code-block:: bash

   > export BINPATH=/workspaces/llvm-project/build/bin
   > $BINPATH/clang -g -S -emit-llvm --target=x86_64-unknown-unknown-elf showdebug.c
   > lldb $BINPATH/lli
   (lldb) target create "/workspaces/llvm-project/build/bin/lli"
   Current executable set to '/workspaces/llvm-project/build/bin/lli' (x86_64).
   (lldb) settings set plugin.jit-loader.gdb.enable on
   (lldb) b compute_factorial
   Breakpoint 1: no locations (pending).
   WARNING:  Unable to resolve breakpoint to any actual locations.
   (lldb) run --jit-kind=mcjit showdebug.ll 5
   1 location added to breakpoint 1
   Process 21340 stopped
   * thread #1, name = 'lli', stop reason = breakpoint 1.1
      frame #0: 0x00007ffff7fd0007 JIT(0x45c2cb0)`compute_factorial(n=5) at showdebug.c:3:11
      1    int compute_factorial(int n)
      2    {
   -> 3        if (n <= 1)
      4            return 1;
      5        int f = n;
      6        while (--n > 1)
      7            f *= n;
   (lldb) p n
   (int) $0 = 5
   (lldb) b showdebug.c:9
   Breakpoint 2: where = JIT(0x45c2cb0)`compute_factorial + 60 at showdebug.c:9:1, address = 0x00007ffff7fd003c
   (lldb) c
   Process 21340 resuming
   Process 21340 stopped
   * thread #1, name = 'lli', stop reason = breakpoint 2.1
      frame #0: 0x00007ffff7fd003c JIT(0x45c2cb0)`compute_factorial(n=1) at showdebug.c:9:1
      6        while (--n > 1)
      7            f *= n;
      8        return f;
   -> 9    }
      10
      11   int main(int argc, char** argv)
      12   {
   (lldb) p f
   (int) $1 = 120
   (lldb) bt
   * thread #1, name = 'lli', stop reason = breakpoint 2.1
   * frame #0: 0x00007ffff7fd003c JIT(0x45c2cb0)`compute_factorial(n=1) at showdebug.c:9:1
      frame #1: 0x00007ffff7fd0095 JIT(0x45c2cb0)`main(argc=2, argv=0x00000000046122f0) at showdebug.c:16:18
      frame #2: 0x0000000002a8306e lli`llvm::MCJIT::runFunction(this=0x000000000458ed10, F=0x0000000004589ff8, ArgValues=ArrayRef<llvm::GenericValue> @ 0x00007fffffffc798) at MCJIT.cpp:554:31
      frame #3: 0x00000000029bdb45 lli`llvm::ExecutionEngine::runFunctionAsMain(this=0x000000000458ed10, Fn=0x0000000004589ff8, argv=size=0, envp=0x00007fffffffe140) at ExecutionEngine.cpp:467:10
      frame #4: 0x0000000001f2fc2f lli`main(argc=4, argv=0x00007fffffffe118, envp=0x00007fffffffe140) at lli.cpp:643:18
      frame #5: 0x00007ffff788c09b libc.so.6`__libc_start_main(main=(lli`main at lli.cpp:387), argc=4, argv=0x00007fffffffe118, init=<unavailable>, fini=<unavailable>, rtld_fini=<unavailable>, stack_end=0x00007fffffffe108) at libc-start.c:308:16
      frame #6: 0x0000000001f2dc7a lli`_start + 42
   (lldb) finish
   Process 21340 stopped
   * thread #1, name = 'lli', stop reason = step out
   Return value: (int) $2 = 120

      frame #0: 0x00007ffff7fd0095 JIT(0x45c2cb0)`main(argc=2, argv=0x00000000046122f0) at showdebug.c:16:9
      13       if (argc < 2)
      14           return -1;
      15       char firstletter = argv[1][0];
   -> 16       int result = compute_factorial(firstletter - '0');
      17
      18       // Returned result is clipped at 255...
      19       return result;
   (lldb) p result
   (int) $3 = 73670648
   (lldb) n
   Process 21340 stopped
   * thread #1, name = 'lli', stop reason = step over
      frame #0: 0x00007ffff7fd0098 JIT(0x45c2cb0)`main(argc=2, argv=0x00000000046122f0) at showdebug.c:19:12
      16       int result = compute_factorial(firstletter - '0');
      17
      18       // Returned result is clipped at 255...
   -> 19       return result;
      20   }
   (lldb) p result
   (int) $4 = 120
   (lldb) expr result=42
   (int) $5 = 42
   (lldb) p result
   (int) $6 = 42
   (lldb) c
   Process 21340 resuming
   Process 21340 exited with status = 42 (0x0000002a)
   (lldb) exit
