Remote Debugging
================

Remote debugging refers to the act of debugging a process which is running on a
different system, than the debugger itself. We shall refer to the system
running the debugger as the local system, while the system running the debugged
process will be the remote system.

To enable remote debugging, LLDB employs a client-server architecture. The
client part runs on the local system and the remote system runs the server. The
client and server communicate using the gdb-remote protocol, usually
transported over TCP/IP. More information on the protocol can be found here and
the LLDB-specific extensions are documented in docs/lldb-gdb-remote.txt file
inside LLDB source repository. Besides the gdb-remote stub, the server part of
LLDB also consists of a platform binary, which is responsible for performing
advanced debugging operations, like copying files from/to the remote system and
can be used to execute arbitrary shell commands on the remote system.

In order to reduce code complexity and improve remote debugging experience LLDB
on Linux and macOS uses the remote debugging stub even when debugging a process
locally. This is achieved by spawning a remote stub process locally and
communicating with it over the loopback interface. In the case of local
debugging this whole process is transparent to the user. The platform binary is
not used in this case, since no file transfers are needed.

Preparation for Remote Debugging
---------------------------------

While the process of actual debugging (stepping, backtraces, evaluating
expressions) is same as in the local case, in the case of remote debugging,
more preparation is needed as the required binaries cannot started on the
remote system automatically. Also, if the remote system runs a different OS or
architecture, the server component needs to be compiled separately.

Remote system
*************

On Linux and Android, all required remote functionality is contained in the
lldb-server binary. This binary combines the functionality of the platform and
gdb-remote stub. A single binary facilitates deployment and reduces code size,
since the two functions share a lot of code. The lldb-server binary is also
statically linked with the rest of LLDB (unlike lldb, which dynamically links
to liblldb.so by default), so it does not have any dependencies on the rest of
lldb. On macOS and iOS, the remote-gdb functionality is implemented by the
debugserver binary, which you will need to deploy alongside lldb-server.

The binaries mentioned above need to be present on the remote system to enable
remote debugging. You can either compile on the remote system directly or copy
them from the local machine. If compiling locally and the remote architecture
differs from the local one, you will need to cross-compile the correct version
of the binaries. More information on cross-compiling LLDB can be found on the
build page.

Once the binaries are in place, you just need to run the lldb-server in
platform mode and specify the port it should listen on. For example, the
command

::

   remote% lldb-server platform --listen "*:1234" --server

will start the LLDB platform and wait for incoming connections from any address
to port 1234. Specifying an address instead of * will only allow connections
originating from that address. Adding a --server parameter to the command line
will fork off a new process for every incoming connection, allowing multiple
parallel debug sessions.

Local system
************

On the local system, you need to let LLDB know that you intend to do remote
debugging. This is achieved through the platform command and its sub-commands.
As a first step you need to choose the correct platform plug-in for your remote
system. A list of available plug-ins can be obtained through platform list.

::

   local% lldb
   (lldb) platform list
   Available platforms:
   host: Local macOS user platform plug-in.
   remote-freebsd: Remote FreeBSD user platform plug-in.
   remote-linux: Remote Linux user platform plug-in.
   remote-netbsd: Remote NetBSD user platform plug-in.
   remote-windows: Remote Windows user platform plug-in.
   remote-android: Remote Android user platform plug-in.
   remote-ios: Remote iOS platform plug-in.
   remote-macosx: Remote macOS user platform plug-in.
   ios-simulator: iOS simulator platform plug-in.
   darwin-kernel: Darwin Kernel platform plug-in.
   tvos-simulator: Apple TV simulator platform plug-in.
   watchos-simulator: Apple Watch simulator platform plug-in.
   remote-tvos: Remote Apple TV platform plug-in.
   remote-watchos: Remote Apple Watch platform plug-in.
   remote-gdb-server: A platform that uses the GDB remote protocol as the communication transport.

The default platform is the platform host which is used for local debugging.
Apart from this, the list should contain a number of plug-ins, for debugging
different kinds of systems. The remote plug-ins are prefixed with "remote-".
For example, to debug a remote Linux application:

::

   (lldb) platform select remote-linux

After selecting the platform plug-in, you should receive a prompt which
confirms the selected platform, and states that you are not connected. This is
because remote plug-ins need to be connected to their remote platform
counterpart to operate. This is achieved using the platform connect command.
This command takes a number of arguments (as always, use the help command to
find out more), but normally you only need to specify the address to connect
to, e.g.:

::

   (lldb) platform connect connect://remote:1234
     Platform: remote-linux
       Triple: x86_64-gnu-linux
     Hostname: remote
    Connected: yes
   WorkingDir: /tmp

Note that the platform has a working directory of /tmp. This directory will be
used as the directory that executables will be uploaded to by default when
launching a process from local.

After this, you should be able to debug normally. You can use the process
attach to attach to an existing remote process or target create, process launch
to start a new one. The platform plugin will transparently take care of
uploading or downloading the executable in order to be able to debug. If your
application needs additional files, you can transfer them using the platform
commands: get-file, put-file, mkdir, etc. The environment can be prepared
further using the platform shell command.

When using the "remote-android" platform, the client LLDB forwards two ports, one
for connecting to the platform, and another for connecting to the gdbserver.
The client ports are configurable through the environment variables
ANDROID_PLATFORM_LOCAL_PORT and ANDROID_PLATFORM_LOCAL_GDB_PORT, respectively.

Launching a locally built process on the remote machine
-------------------------------------------------------

Install and run in the platform working directory
*************************************************

To launch a locally built process on the remote system in the platform working
directory:

::

   (lldb) file a.out
   (lldb) run

This will cause LLDB to create a target with the "a.out" executable that you
cross built. The "run" command will cause LLDB to upload "a.out" to the
platform's current working directory only if the file has changed. The platform
connection allows us to transfer files, but also allows us to get the MD5
checksum of the file on the other end and only upload the file if it has
changed. LLDB will automatically launch a lldb-server in gdbremote mode to
allow you to debug this executable, connect to it and start your debug session
for you.

Changing the platform working directory
***************************************

You can change the platform working directory while connected to the platform
with:

::

   (lldb) platform settings -w /usr/local/bin

And you can verify it worked using "platform status":

::

   (lldb) platform status
     Platform: remote-linux
       Triple: x86_64-gnu-linux
     Hostname: remote
    Connected: yes
   WorkingDir: /usr/local/bin

If we run again, the program will be installed into ``/usr/local/bin``.

Install and run by specifying a remote install path
***************************************************

If you want the "a.out" executable to be installed into "/bin/a.out" instead of
the platform's current working directory, we can set the platform file
specification using python:

::

   (lldb) file a.out
   (lldb) script lldb.target.module['a.out'].SetPlatformFileSpec("/bin/a.out")
   (lldb) run

Now when you run your program, the program will be uploaded to "/bin/a.out"
instead of the platform current working directory. Only the main executable is
uploaded to the remote system by default when launching the application. If you
have shared libraries that should also be uploaded, then you can add the
locally build shared library to the current target and set its platform file
specification:

::

   (lldb) file a.out
   (lldb) target module add /local/build/libfoo.so
   (lldb) target module add /local/build/libbar.so
   (lldb) script lldb.target.module['libfoo.so'].SetPlatformFileSpec("/usr/lib/libfoo.so")
   (lldb) script lldb.target.module['libbar.so'].SetPlatformFileSpec("/usr/local/lib/libbar.so")
   (lldb) run

Attaching to a remote process
*****************************

If you want to attach to a remote process, you can first list the processes on
the remote system:

::

   (lldb) platform process list
   223 matching processes were found on "remote-linux"
   PID    PARENT USER       TRIPLE                   NAME
   ====== ====== ========== ======================== ============================
   68639  90652             x86_64-apple-macosx      lldb
   ...

Then attaching is as simple as specifying the remote process ID:

::

   (lldb) attach 68639
