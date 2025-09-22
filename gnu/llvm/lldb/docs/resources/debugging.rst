Debugging
=========

This page details various ways to debug LLDB itself and other LLDB tools. If
you want to know how to use LLDB in general, please refer to
:doc:`/use/tutorial`.

As LLDB is generally split into 2 tools, ``lldb`` and ``lldb-server``
(``debugserver`` on Mac OS), the techniques shown here will not always apply to
both. With some knowledge of them all, you can mix and match as needed.

In this document we refer to the initial ``lldb`` as the "debugger" and the
program being debugged as the "inferior".

Building For Debugging
----------------------

To build LLDB with debugging information add the following to your CMake
configuration:

::

  -DCMAKE_BUILD_TYPE=Debug \
  -DLLDB_EXPORT_ALL_SYMBOLS=ON

Note that the ``lldb`` you will use to do the debugging does not itself need to
have debug information.

Then build as you normally would according to :doc:`/resources/build`.

If you are going to debug in a way that doesn't need debug info (printf, strace,
etc.) we recommend adding ``LLVM_ENABLE_ASSERTIONS=ON`` to Release build
configurations. This will make LLDB fail earlier instead of continuing with
invalid state (assertions are enabled by default for Debug builds).

Debugging ``lldb``
------------------

The simplest scenario is where we want to debug a local execution of ``lldb``
like this one:

::

  ./bin/lldb test_program

LLDB is like any other program, so you can use the same approach.

::

  ./bin/lldb -- ./bin/lldb /tmp/test.o

That's it. At least, that's the minimum. There's nothing special about LLDB
being a debugger that means you can't attach another debugger to it like any
other program.

What can be an issue is that both debuggers have command line interfaces which
makes it very confusing which one is which:

::

  (the debugger)
  (lldb) run
  Process 1741640 launched: '<...>/bin/lldb' (aarch64)
  Process 1741640 stopped and restarted: thread 1 received signal: SIGCHLD

  (the inferior)
  (lldb) target create "/tmp/test.o"
  Current executable set to '/tmp/test.o' (aarch64).

Another issue is that when you resume the inferior, it will not print the
``(lldb)`` prompt because as far as it knows it hasn't changed state. A quick
way around that is to type something that is clearly not a command and hit
enter.

::

  (lldb) Process 1742266 stopped and restarted: thread 1 received signal: SIGCHLD
  Process 1742266 stopped
  * thread #1, name = 'lldb', stop reason = signal SIGSTOP
      frame #0: 0x0000ffffed5bfbf0 libc.so.6`__GI___libc_read at read.c:26:10
  (lldb) c
  Process 1742266 resuming
  notacommand
  error: 'notacommand' is not a valid command.
  (lldb)

You could just remember whether you are in the debugger or the inferior but
it's more for you to remember, and for interrupt based events you simply may not
be able to know.

Here are some better approaches. First, you could use another debugger like GDB
to debug LLDB. Perhaps an IDE like Xcode or Visual Studio Code. Something which
runs LLDB under the hood so you don't have to type in commands to the debugger
yourself.

Or you could change the prompt text for the debugger and/or inferior.

::

  $ ./bin/lldb -o "settings set prompt \"(lldb debugger) \"" -- \
    ./bin/lldb -o "settings set prompt \"(lldb inferior) \"" /tmp/test.o
  <...>
  (lldb) settings set prompt "(lldb debugger) "
  (lldb debugger) run
  <...>
  (lldb) settings set prompt "(lldb inferior) "
  (lldb inferior)

If you want spacial separation you can run the inferior in one terminal then
attach to it in another. Remember that while paused in the debugger, the inferior
will not respond to input so you will have to ``continue`` in the debugger
first.

::

  (in terminal A)
  $ ./bin/lldb /tmp/test.o

  (in terminal B)
  $ ./bin/lldb ./bin/lldb --attach-pid $(pidof lldb)

Placing Breakpoints
*******************

Generally you will want to hit some breakpoint in the inferior ``lldb``. To place
that breakpoint you must first stop the inferior.

If you're debugging from another window this is done with ``process interrupt``.
The inferior will stop, you place the breakpoint and then ``continue``. Go back
to the inferior and input the command that should trigger the breakpoint.

If you are running debugger and inferior in the same window, input ``ctrl+c``
instead of ``process interrupt`` and then folllow the rest of the steps.

If you are doing this with ``lldb-server`` and find your breakpoint is never
hit, check that you are breaking in code that is actually run by
``lldb-server``. There are cases where code only used by ``lldb`` ends up
linked into ``lldb-server``, so the debugger can break there but the breakpoint
will never be hit.

Debugging ``lldb-server``
-------------------------

Note: If you are on MacOS you are likely using ``debugserver`` instead of
``lldb-server``. The spirit of these instructions applies but the specifics will
be different.

We suggest you read :doc:`/use/remote` before attempting to debug ``lldb-server``
as working out exactly what you want to debug requires that you understand its
various modes and behaviour. While you may not be literally debugging on a
remote target, think of your host machine as the "remote" in this scenario.

The ``lldb-server`` options for your situation will depend on what part of it
or mode you are interested in. To work out what those are, recreate the scenario
first without any extra debugging layers. Let's say we want to debug
``lldb-server`` during the following command:

::

  $ ./bin/lldb /tmp/test.o

We can treat ``lldb-server`` as we treated ``lldb`` before, running it under
``lldb``. The equivalent to having ``lldb`` launch the ``lldb-server`` for us is
to start ``lldb-server`` in the ``gdbserver`` mode.

The following commands recreate that, while debugging ``lldb-server``:

::

  $ ./bin/lldb -- ./bin/lldb-server gdbserver :1234 /tmp/test.o
  (lldb) target create "./bin/lldb-server"
  Current executable set to '<...>/bin/lldb-server' (aarch64).
  <...>
  Process 1742485 launched: '<...>/bin/lldb-server' (aarch64)
  Launched '/tmp/test.o' as process 1742586...

  (in another terminal)
  $ ./bin/lldb /tmp/test.o -o "gdb-remote 1234"

Note that the first ``lldb`` is the one debugging ``lldb-server``. The second
``lldb`` is debugging ``/tmp/test.o`` and is only used to trigger the
interesting code path in ``lldb-server``.

This is another case where you may want to layout your terminals in a
predictable way, or change the prompt of one or both copies of ``lldb``.

If you are debugging a scenario where the ``lldb-server`` starts in ``platform``
mode, but you want to debug the ``gdbserver`` mode you'll have to work out what
subprocess it's starting for the ``gdbserver`` part. One way is to look at the
list of runninng processes and take the command line from there.

In theory it should be possible to use LLDB's
``target.process.follow-fork-mode`` or GDB's ``follow-fork-mode`` to
automatically debug the ``gdbserver`` process as it's created. However this
author has not been able to get either to work in this scenario so we suggest
making a more specific command wherever possible instead.

Another option is to let ``lldb-server`` start up, then attach to the process
that's interesting to you. It's less automated and won't work if the bug occurs
during startup. However it is a good way to know you've found the right one,
then you can take its command line and run that directly.

Output From ``lldb-server``
***************************

As ``lldb-server`` often launches subprocesses, output messages may be hidden
if they are emitted from the child processes.

You can tell it to enable logging using the ``--log-channels`` option. For
example ``--log-channels "posix ptrace"``. However that is not passed on to the
child processes.

The same goes for ``printf``. If it's called in a child process you won't see
the output.

In these cases consider interactive debugging ``lldb-server`` or
working out a more specific command such that it does not have to spawn a
subprocess. For example if you start with ``platform`` mode, work out what
``gdbserver`` mode process it spawns and run that command instead.

Another option if you have ``strace`` available is to trace the whole process
tree and inspect the logs after the session has ended. ::

  $ strace -ff -o log -p $(pidof lldb-server)

This will log all syscalls made by ``lldb-server`` and processes that it forks.
``-ff`` tells ``strace`` to trace child processes and write the results to a
separate file for each process, named using the prefix given by ``-o``.

Search the log files for specific terms to find the process you're interested
in. For example, to find a process that acted as a ``gdbserver`` instance::

  $ grep "gdbserver" log.*
  log.<N>:execve("<...>/lldb-server", [<...> "gdbserver", <...>) = 0

Remote Debugging
----------------

If you want to debug part of LLDB running on a remote machine, the principals
are the same but we will have to start debug servers, then attach debuggers to
those servers.

In the example below we're debugging an ``lldb-server`` ``gdbserver`` mode
command running on a remote machine.

For simplicity we'll use the same ``lldb-server`` as the debug server
and the inferior, but it doesn't need to be that way. You can use ``gdbserver``
(as in, GDB's debug server program) or a system installed ``lldb-server`` if you
suspect your local copy is not stable. As is the case in many of these
scenarios.

::

  $ <...>/bin/lldb-server gdbserver 0.0.0.0:54322 -- \
    <...>/bin/lldb-server gdbserver 0.0.0.0:54321 -- /tmp/test.o

Now we have a debug server listening on port 54322 of our remote (``0.0.0.0``
means it's listening for external connections). This is where we will connect
``lldb`` to, to debug the second ``lldb-server``.

To trigger behaviour in the second ``lldb-server``, we will connect a second
``lldb`` to port 54321 of the remote.

This is the final configuration:

::

  Host                                        | Remote
  --------------------------------------------|--------------------
  lldb A debugs lldb-server on port 54322 ->  | lldb-server A
                                              |  (which runs)
  lldb B debugs /tmp/test.o on port 54321 ->  |    lldb-server B
                                              |      (which runs)
                                              |        /tmp/test.o

You would use ``lldb A`` to place a breakpoint in the code you're interested in,
then ``lldb B`` to trigger ``lldb-server B`` to go into that code and hit the
breakpoint. ``lldb-server A`` is only here to let us debug ``lldb-server B``
remotely.

Debugging The Remote Protocol
-----------------------------

LLDB mostly follows the `GDB Remote Protocol <https://sourceware.org/gdb/onlinedocs/gdb/Remote-Protocol.html>`_
. Where there are differences it tries to handle both LLDB and GDB behaviour.

LLDB does have extensions to the protocol which are documented in
`lldb-gdb-remote.txt <https://github.com/llvm/llvm-project/blob/main/lldb/docs/lldb-gdb-remote.txt>`_
and `lldb/docs/lldb-platform-packets.txt <https://github.com/llvm/llvm-project/blob/main/lldb/docs/lldb-platform-packets.txt>`_.

Logging Packets
***************

If you just want to observe packets, you can enable the ``gdb-remote packets``
log channel.

::

  (lldb) log enable gdb-remote packets
  (lldb) run
  lldb             <   1> send packet: +
  lldb             history[1] tid=0x264bfd <   1> send packet: +
  lldb             <  19> send packet: $QStartNoAckMode#b0
  lldb             <   1> read packet: +

You can do this on the ``lldb-server`` end as well by passing the option
``--log-channels "gdb-remote packets"``. Then you'll see both sides of the
connection.

Some packets may be printed in a nicer way than others. For example XML packets
will print the literal XML, some binary packets may be decoded. Others will just
be printed unmodified. So do check what format you expect, a common one is hex
encoded bytes.

You can enable this logging even when you are connecting to an ``lldb-server``
in platform mode, this protocol is used for that too.

Debugging Packet Exchanges
**************************

Say you want to make ``lldb`` send a packet to ``lldb-server``, then debug
how the latter builds its response. Maybe even see how ``lldb`` handles it once
it's sent back.

That all takes time, so LLDB will likely time out and think the remote has gone
away. You can change the ``plugin.process.gdb-remote.packet-timeout`` setting
to prevent this.

Here's an example, first we'll start an ``lldb-server`` being debugged by
``lldb``. Placing a breakpoint on a packet handler we know will be hit once
another ``lldb`` connects.

::

  $ lldb -- lldb-server gdbserver :1234 -- /tmp/test.o
  <...>
  (lldb) b GDBRemoteCommunicationServerCommon::Handle_qSupported
  Breakpoint 1: where = <...>
  (lldb) run
  <...>

Next we connect another ``lldb`` to this, with a timeout of 5 minutes:

::

  $ lldb /tmp/test.o
  <...>
  (lldb) settings set plugin.process.gdb-remote.packet-timeout 300
  (lldb) gdb-remote 1234

Doing so triggers the breakpoint in ``lldb-server``, bringing us back into
``lldb``. Now we've got 5 minutes to do whatever we need before LLDB decides
the connection has failed.

::

  * thread #1, name = 'lldb-server', stop reason = breakpoint 1.1
      frame #0: 0x0000aaaaaacc6848 lldb-server<...>
  lldb-server`lldb_private::process_gdb_remote::GDBRemoteCommunicationServerCommon::Handle_qSupported:
  ->  0xaaaaaacc6848 <+0>:  sub    sp, sp, #0xc0
  <...>
  (lldb)

Once you're done simply ``continue`` the ``lldb-server``. Back in the other
``lldb``, the connection process will continue as normal.

::

  Process 2510266 stopped
  * thread #1, name = 'test.o', stop reason = signal SIGSTOP
      frame #0: 0x0000fffff7fcd100 ld-2.31.so`_start
  ld-2.31.so`_start:
  ->  0xfffff7fcd100 <+0>: mov    x0, sp
  <...>
  (lldb)

Reducing Bugs
-------------

This section covers reducing a bug that happens in LLDB itself, or where you
suspect that LLDB causes something else to behave abnormally.

Since bugs vary wildly, the advice here is general and incomplete. Let your
instincts guide you and don't feel the need to try everything before reporting
an issue or asking for help. This is simply inspiration.

Reduction
*********

The first step is to reduce uneeded compexity where it is cheap to do so. If
something is easily removed or frozen to a cerain value, do so. The goal is to
keep the failure mode the same, with fewer dependencies.

This includes, but is not limited to:

* Removing test cases that don't crash.
* Replacing dynamic lookups with constant values.
* Replace supporting functions with stubs that do nothing.
* Moving the test case to less unqiue system. If your machine has an exotic
  extension, try it on a readily available commodity machine.
* Removing irrelevant parts of the test program.
* Reproducing the issue without using the LLDB test runner.
* Converting a remote debuging scenario into a local one.

Now we hopefully have a smaller reproducer than we started with. Next we need to
find out what components of the software stack might be failing.

Some examples are listed below with suggestions for how to investigate them.

* Debugger

  * Use a `released version of LLDB <https://github.com/llvm/llvm-project/releases>`_.

  * If on MacOS, try the system ``lldb``.

  * Try GDB or any other system debugger you might have e.g. Microsoft Visual
    Studio.

* Kernel

  * Start a virtual machine running a different version. ``qemu-system`` is
    useful here.

  * Try a different physical system running a different version.

  * Remember that for most kernels, userspace crashing the kernel is always a
    kernel bug. Even if the userspace program is doing something unconventional.
    So it could be a bug in the application and the kernel.

* Compiler and compiler options

  * Try other versions of the same compiler or your system compiler.

  * Emit older versions of DWARF info, particularly DWARFv4 to v5, some tools
    did/do not understand the new constructs.

  * Reduce optimisation options as much as possible.

  * Try all the language modes e.g. C++17/20 for C++.

  * Link against LLVM's libcxx if you suspect a bug involving the system C++
    library.

  * For languages other than C/C++ e.g. Rust, try making an equivalent program
    in C/C++. LLDB tends to try to fit other languages into a C/C++ mould, so
    porting the program can make triage and reporting much easier.

* Operating system

  * Use docker to try various versions of Linux.

  * Use ``qemu-system`` to emulate other operating systems e.g. FreeBSD.

* Architecture

  * Use `QEMU user space emulation <https://www.qemu.org/docs/master/user/main.html>`_
    to quickly test other architectures. Note that ``lldb-server`` cannot be used
    with this as the ptrace APIs are not emulated.

  * If you need to test a big endian system use QEMU to emulate s390x (user
    space emulation for just ``lldb``, ``qemu-system`` for testing
    ``lldb-server``).

.. note:: When using QEMU you may need to use the built in GDB stub, instead of
          ``lldb-server``. For example if you wanted to debug ``lldb`` running
          inside ``qemu-user-s390x`` you would connect to the GDB stub provided
          by QEMU.

          The same applies if you want to see how ``lldb`` would debug a test
          program that is running on s390x. It's not totally accurate because
          you're not using ``lldb-server``, but this is fine for features that
          are mostly implemented in ``lldb``.

          If you are running a full system using ``qemu-system``, you likely
          want to connect to the ``lldb-server`` running within the userspace
          of that system.

          If your test program is bare metal (meaning it requires no supporting
          operating system) then connect to the built in GDB stub. This can be
          useful when testing embedded systems or kernel debugging.

Reducing Ptrace Related Bugs
****************************

This section is written Linux specific but the same can likely be done on
other Unix or Unix like operating systems.

Sometimes you will find ``lldb-server`` doing something with ptrace that causes
a problem. Your reproducer involves running ``lldb`` as well, this is not going
to go over well with kernel and is generally more difficult to explain if you
want to get help with it.

If you think you can get your point across without this, no need. If you're
pretty sure you have for example found a Linux Kernel bug, doing this greatly
increases the chances it'll get fixed.

We'll remove the LLDB dependency by making a smaller standalone program that
does the same actions. Starting with a skeleton program that forks and debugs
the inferior process.

The program presented `here <https://eli.thegreenplace.net/2011/01/23/how-debuggers-work-part-1>`_
(`source <https://github.com/eliben/code-for-blog/blob/master/2011/simple_tracer.c>`_)
is a great starting point. There is also an AArch64 specific example in
`the LLDB examples folder <https://github.com/llvm/llvm-project/tree/main/lldb/examples/ptrace_example.c>`_.

For either, you'll need to modify that to fit your architecture. A tip for this
is to take any constants used in it, find in which function(s) they are used in
LLDB and then you'll find the equivalent constants in the same LLDB functions
for your architecture.

Once that is running as expected we can convert ``lldb-server``'s into calls in
this program. To get a log of those, run ``lldb-server`` with
``--log-channels "posix ptrace"``. You'll see output like:

::

  $ lldb-server gdbserver :1234 --log-channels "posix ptrace" -- /tmp/test.o
  1694099878.829990864 <...> ptrace(16896, 2659963, 0x0000000000000000, 0x000000000000007E, 0)=0x0
  1694099878.830722332 <...> ptrace(16900, 2659963, 0x0000FFFFD14BF7CC, 0x0000FFFFD14BF7D0, 16)=0x0
  1694099878.831967115 <...> ptrace(16900, 2659963, 0x0000FFFFD14BF66C, 0x0000FFFFD14BF630, 16)=0xffffffffffffffff
  1694099878.831982136 <...> ptrace() failed: Invalid argument
  Launched '/tmp/test.o' as process 2659963...

Each call is logged with its parameters and its result as the ``=`` on the end.

From here you will need to use a combination of the `ptrace documentation <https://man7.org/linux/man-pages/man2/ptrace.2.html>`_
and Linux Kernel headers (``uapi/linux/ptrace.h`` mainly) to figure out what
the calls are.

The most important parameter is the first, which is the request number. In the
example above ``16896``, which is hex ``0x4200``, is ``PTRACE_SETOPTIONS``.

Luckily, you don't usually have to figure out all those early calls. Our
skeleton program will be doing all that, successfully we hope.

What you should do is record just the interesting bit to you. Let's say
something odd is happening when you read the ``tpidr`` register (this is an
AArch64 register, just for example purposes).

First, go to the ``lldb-server`` terminal and press enter a few times to put
some blank lines after the last logging output.

Then go to your ``lldb`` and:

::

  (lldb) register read tpidr
  tpidr = 0x0000fffff7fef320

You'll see this from ``lldb-server``:

::

  <...> ptrace(16900, 2659963, 0x0000FFFFD14BF6CC, 0x0000FFFFD14BF710, 8)=0x0

If you don't see that, it may be because ``lldb`` has cached it. The easiest way
to clear that cache is to step. Remember that some registers are read every
step, so you'll have to adjust depending on the situation.

Assuming you've got that line, you would look up what ``116900`` is. This is
``0x4204`` in hex, which is ``PTRACE_GETREGSET``. As we expected.

The following parameters are not as we might expect because what we log is a bit
different from the literal ptrace call. See your platform's definition of
``PtraceWrapper`` for the exact form.

The point of all this is that by doing a single action you can get a few
isolated ptrace calls and you can then fill in the blanks and write
equivalent calls in the skeleton program.

The final piece of this is likely breakpoints. Assuming your bug does not
require a hardware breakpoint, you can get software breakpoints by inserting
a break instruction into the inferior's code at compile time. Usually by using
an architecture specific assembly statement, as you will need to know exactly
how many instructions to overwrite later.

Doing it this way instead of exactly copying what LLDB does will save a few
ptrace calls. The AArch64 example program shows how to do this.

* The inferior contains ``BRK #0`` then ``NOP``.
* 2 4 byte instructins means 8 bytes of data to replace, which matches the
  minimum size you can write with ``PTRACE_POKETEXT``.
* The inferior runs to the ``BRK``, which brings us into the debugger.
* The debugger reads ``PC`` and writes ``NOP`` then ``NOP`` to the location
  pointed to by ``PC``.
* The debugger then single steps the inferior to the next instruction
  (this is not required in this specific scenario, you could just continue but
  it is included because this more cloesly matches what ``lldb`` does).
* The debugger then continues the inferior.
* The inferior exits, and the whole program exits.

Using this technique you can emulate the usual "run to main, do a thing" type
reproduction steps.

Finally, that "thing" is the ptrace calls you got from the ``lldb-server`` logs.
Add those to the debugger function and you now have a reproducer that doesn't
need any part of LLDB.

Debugging Tests
---------------

See :doc:`/resources/test`.