coredump selftest
=================

Background context
------------------

`coredump` is a feature which dumps a process's memory space when the process terminates
unexpectedly (e.g. due to segmentation fault), which can be useful for debugging. By default,
`coredump` dumps the memory to the file named `core`, but this behavior can be changed by writing a
different file name to `/proc/sys/kernel/core_pattern`. Furthermore, `coredump` can be piped to a
user-space program by writing the pipe symbol (`|`) followed by the command to be executed to
`/proc/sys/kernel/core_pattern`. For the full description, see `man 5 core`.

The piped user program may be interested in reading the stack pointers of the crashed process. The
crashed process's stack pointers can be read from `procfs`: it is the `kstkesp` field in
`/proc/$PID/stat`. See `man 5 proc` for all the details.

The problem
-----------
While a thread is active, the stack pointer is unsafe to read and therefore the `kstkesp` field
reads zero. But when the thread is dead (e.g. during a coredump), this field should have valid
value.

However, this was broken in the past and `kstkesp` was zero even during coredump:

* commit 0a1eb2d474ed ("fs/proc: Stop reporting eip and esp in /proc/PID/stat") changed kstkesp to
  always be zero

* commit fd7d56270b52 ("fs/proc: Report eip/esp in /prod/PID/stat for coredumping") fixed it for the
  coredumping thread. However, other threads in a coredumping process still had the problem.

* commit cb8f381f1613 ("fs/proc/array.c: allow reporting eip/esp for all coredumping threads") fixed
  for all threads in a coredumping process.

* commit 92307383082d ("coredump:  Don't perform any cleanups before dumping core") broke it again
  for the other threads in a coredumping process.

The problem has been fixed now, but considering the history, it may appear again in the future.

The goal of this test
---------------------
This test detects problem with reading `kstkesp` during coredump by doing the following:

#. Tell the kernel to execute the "stackdump" script when a coredump happens. This script
   reads the stack pointers of all threads of crashed processes.

#. Spawn a child process who creates some threads and then crashes.

#. Read the output from the "stackdump" script, and make sure all stack pointer values are
   non-zero.
