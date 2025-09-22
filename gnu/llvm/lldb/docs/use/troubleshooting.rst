Troubleshooting
===============

File and Line Breakpoints Are Not Getting Hit
---------------------------------------------

First you must make sure that your source files were compiled with debug
information. Typically this means passing -g to the compiler when compiling
your source file.

When setting breakpoints in implementation source files (.c, cpp, cxx, .m, .mm,
etc), LLDB by default will only search for compile units whose filename
matches. If your code does tricky things like using #include to include source
files:

::

   $ cat foo.c
   #include "bar.c"
   #include "baz.c"
   ...

This will cause breakpoints in "bar.c" to be inlined into the compile unit for
"foo.c". If your code does this, or if your build system combines multiple
files in some way such that breakpoints from one implementation file will be
compiled into another implementation file, you will need to tell LLDB to always
search for inlined breakpoint locations by adding the following line to your
~/.lldbinit file:

::

   $ echo "settings set target.inline-breakpoint-strategy always" >> ~/.lldbinit

This tells LLDB to always look in all compile units and search for breakpoint
locations by file and line even if the implementation file doesn't match.
Setting breakpoints in header files always searches all compile units because
inline functions are commonly defined in header files and often cause multiple
breakpoints to have source line information that matches many header file
paths.

If you set a file and line breakpoint using a full path to the source file,
like Xcode does when setting a breakpoint in its GUI on macOS when you click
in the gutter of the source view, this path must match the full paths in the
debug information. If the paths mismatch, possibly due to passing in a resolved
source file path that doesn't match an unresolved path in the debug
information, this can cause breakpoints to not be resolved. Try setting
breakpoints using the file basename only.

If you are using an IDE and you move your project in your file system and build
again, sometimes doing a clean then build will solve the issue.This will fix
the issue if some .o files didn't get rebuilt after the move as the .o files in
the build folder might still contain stale debug information with the old
source locations.

How Do I Check If I Have Debug Symbols?
---------------------------------------

Checking if a module has any compile units (source files) is a good way to
check if there is debug information in a module:

::

   (lldb) file /tmp/a.out
   (lldb) image list
   [  0] 71E5A649-8FEF-3887-9CED-D3EF8FC2FD6E 0x0000000100000000 /tmp/a.out
         /tmp/a.out.dSYM/Contents/Resources/DWARF/a.out
   [  1] 6900F2BA-DB48-3B78-B668-58FC0CF6BCB8 0x00007fff5fc00000 /usr/lib/dyld
   ....
   (lldb) script lldb.target.module['/tmp/a.out'].GetNumCompileUnits()
   1
   (lldb) script lldb.target.module['/usr/lib/dyld'].GetNumCompileUnits()
   0

Above we can see that "/tmp/a.out" does have a compile unit, and
"/usr/lib/dyld" does not.

We can also list the full paths to all compile units for a module using python:

::

   (lldb) script
   Python Interactive Interpreter. To exit, type 'quit()', 'exit()' or Ctrl-D.
   >>> m = lldb.target.module['a.out']
   >>> for i in range(m.GetNumCompileUnits()):
   ...   cu = m.GetCompileUnitAtIndex(i).file.fullpath
   /tmp/main.c
   /tmp/foo.c
   /tmp/bar.c
   >>>

This can help to show the actual full path to the source files. Sometimes IDEs
will set breakpoints by full paths where the path doesn't match the full path
in the debug info and this can cause LLDB to not resolve breakpoints. You can
use the breakpoint list command with the --verbose option to see the full paths
for any source file and line breakpoints that the IDE set using:

::

   (lldb) breakpoint list --verbose
