Tracing with Intel Processor Trace
==================================

Intel PT is a technology available in modern Intel CPUs that allows efficient
tracing of all the instructions executed by a process.
LLDB can collect traces and dump them using its symbolication stack.
You can read more here
https://easyperf.net/blog/2019/08/23/Intel-Processor-Trace.

Prerequisites
-------------

Confirm that your CPU supports Intel PT
(see https://www.intel.com/content/www/us/en/support/articles/000056730/processors.html)
and that your operating system is Linux.

Check for the existence of this particular file on your Linux system

::

  $ cat /sys/bus/event_source/devices/intel_pt/type

The output should be a number. Otherwise, try upgrading your kernel.


Build Instructions
------------------

Clone and build the low level Intel PT
decoder library `LibIPT library <https://github.com/intel/libipt>`_.
::

  $ git clone git@github.com:intel/libipt.git
  $ mkdir libipt-build
  $ cmake -S libipt -B libipt-build
  $ cd libipt-build
  $ make

This will generate a few files in the ``<libipt-build>/lib``
and ``<libipt-build>/libipt/include`` directories.

Configure and build LLDB with Intel PT support

::

  $ cmake \
      -DLLDB_BUILD_INTEL_PT=ON \
      -DLIBIPT_INCLUDE_PATH="<libipt-build>/libipt/include" \
      -DLIBIPT_LIBRARY_PATH="<libipt-build>/lib" \
      ... other common configuration parameters

::

  $ cd <lldb-build> && ninja lldb lldb-server # if using Ninja


How to Use
----------

When you are debugging a process, you can turn on intel-pt tracing,
which will “record” all the instructions that the process will execute.
After turning it on, you can continue debugging, and at any breakpoint,
you can inspect the instruction list.

For example:

::

  lldb <target>
  > b main
  > run
  > process trace start # start tracing on all threads, including future ones
  # keep debugging until you hit a breakpoint

  > thread trace dump instructions
  # this should output something like

  thread #2: tid = 2861133, total instructions = 5305673
    libc.so.6`__GI___libc_read + 45 at read.c:25:1
      [4962255] 0x00007fffeb64c63d    subq   $0x10, %rsp
      [4962256] 0x00007fffeb64c641    movq   %rdi, -0x18(%rbp)
    libc.so.6`__GI___libc_read + 53 [inlined] __libc_read at read.c:26:10
      [4962257] 0x00007fffeb64c645    callq  0x7fffeb66b640            ; __libc_enable_asynccancel
    libc.so.6`__libc_enable_asynccancel
      [4962258] 0x00007fffeb66b640    movl   %fs:0x308, %eax
    libc.so.6`__libc_enable_asynccancel + 8
      [4962259] 0x00007fffeb66b648    movl   %eax, %r11d

  # you can keep pressing ENTER to see more and more instructions

The number between brackets is the instruction index,
and by default the current thread will be picked.

Configuring the trace size
--------------------------

The CPU stores the instruction list in a compressed format in a ring buffer,
which keeps the latest information.
By default, LLDB uses a buffer of 4KB per thread,
but you can change it by running.
The size must be a power of 2 and at least 4KB.

::

  thread trace start all -s <size_in_bytes>

For reference, a 1MB trace buffer can easily store around 5M instructions.

Printing more instructions
--------------------------

If you want to dump more instructions at a time, you can run

::

  thread trace dump instructions -c <count>

Printing the instructions of another thread
-------------------------------------------

By default the current thread will be picked when dumping instructions,
but you can do

::

  thread trace dump instructions <#thread index>
  #e.g.
  thread trace dump instructions 8

to select another thread.

Crash Analysis
--------------

What if you are debugging + tracing a process that crashes?
Then you can just do

::

  thread trace dump instructions

To inspect how it crashed! There's nothing special that you need to do.
For example

::

    * thread #1, name = 'a.out', stop reason = signal SIGFPE: integer divide by zero
        frame #0: 0x00000000004009f1 a.out`main at main.cpp:8:14
      6       int x;
      7       cin >> x;
   -> 8       cout << 12 / x << endl;
      9       return 0;
      10  }
    (lldb) thread trace dump instructions -c 5
    thread #1: tid = 604302, total instructions = 8388
      libstdc++.so.6`std::istream::operator>>(int&) + 181
        [8383] 0x00007ffff7b41665    popq   %rbp
        [8384] 0x00007ffff7b41666    retq
      a.out`main + 66 at main.cpp:8:14
        [8385] 0x00000000004009e8    movl   -0x4(%rbp), %ecx
        [8386] 0x00000000004009eb    movl   $0xc, %eax
        [8387] 0x00000000004009f0    cltd

.. note::
  At this moment, we are not including the failed instruction in the trace,
  but in the future we might do it for readability.


Offline Trace Analysis
----------------------

It's also possible to record a trace using a custom Intel PT collector
and decode + symbolicate the trace using LLDB.
For that, the command trace load is useful.
In order to use trace load, you need to first create a JSON file with
the definition of the trace session.
For example

::

  {
    "type": "intel-pt",
    "cpuInfo": {
      "vendor": "GenuineIntel",
      "family": 6,
      "model": 79,
      "stepping": 1
    },
    "processes": [
      {
        "pid": 815455,
        "triple": "x86_64-*-linux",
        "threads": [
          {
            "tid": 815455,
            "iptTrace": "trace.file" # raw thread-specific trace from the AUX buffer
          }
        ],
        "modules": [ # this are all the shared libraries + the main executable
          {
            "file": "a.out", # optional if it's the same as systemPath
            "systemPath": "a.out",
            "loadAddress": 4194304,
          },
          {
            "file": "libfoo.so",
            "systemPath": "/usr/lib/libfoo.so",
            "loadAddress": "0x00007ffff7bd9000",
          },
          {
            "systemPath": "libbar.so",
            "loadAddress": "0x00007ffff79d7000",
          }
        ]
      }
    ]
  }

You can see the full schema by typing

::

  trace schema intel-pt

The JSON file mainly contains all the shared libraries that
were part of the traced process, along with their memory load address.
If the analysis is done on the same computer where the traces were obtained,
it's enough to use the “systemPath” field.
If the analysis is done on a different machines, these files need to be
copied over and the “file” field should point to the
location of the file relative to the JSON file.
Once you have the JSON file and the module files in place, you can simple run

::

  lldb
  > trace load /path/to/json
  > thread trace dump instructions <optional thread index>

Then it's like in the live session case

References
----------

- Original RFC document_ for this feature.
- Some details about how Meta is using Intel Processor Trace can be found in this blog_ post.

.. _document: https://docs.google.com/document/d/1cOVTGp1sL_HBXjP9eB7qjVtDNr5xnuZvUUtv43G5eVI
.. _blog: https://engineering.fb.com/2021/04/27/developer-tools/reverse-debugging/
