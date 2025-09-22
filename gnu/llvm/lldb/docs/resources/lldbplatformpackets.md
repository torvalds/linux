# LLDB Platform Packets

This is a list of the packets that an lldb platform server
needs to implement for the lldb testsuite to be run on a remote
target device/system.

These are almost all lldb extensions to the gdb-remote serial
protocol. Many of the `vFile:` packets are also described in the "Host
I/O Packets" detailed in the gdb-remote protocol documentation,
although the lldb platform extensions include packets that are not
defined there (`vFile:size:`, `vFile:mode:`, `vFile:symlink`, `vFile:chmod:`).

Most importantly, the flags that LLDB passes to `vFile:open:` are
incompatible with the flags that GDB specifies.

* [QSetWorkingDir](./lldbgdbremote.md#qsetworkingdir-ascii-hex-path)
* [QStartNoAckMode](./lldbgdbremote.md#qstartnoackmode)
* [qGetWorkingDir](./lldbgdbremote.md#qgetworkingdir)
* [qHostInfo](./lldbgdbremote.md#qhostinfo)
* [qKillSpawnedProcess](./lldbgdbremote.md#qkillspawnedprocess-platform-extension)
* [qLaunchGDBServer](./lldbgdbremote.md#qlaunchgdbserver-platform-extension)
* [qModuleInfo](./lldbgdbremote.md#qmoduleinfo-module-path-arch-triple)
* [qPathComplete](./lldbgdbremote.md#qpathcomplete-platform-extension)
* [qPlatform_mkdir](./lldbgdbremote.md#qplatform-mkdir)
* [qPlatform_shell](./lldbgdbremote.md#qplatform-shell)
* [qProcessInfo](./lldbgdbremote.md#qprocessinfo)
  * The lldb test suite currently only uses `name_match:equals` and the no-criteria mode to list every process.
* [qProcessInfoPID](./lldbgdbremote.md#qprocessinfopid-pid-platform-extension)
  * It is likely that you only need to support the `pid` and `name` fields.
* [vFile:chmod](./lldbgdbremote.md#vfile-chmod-qplatform-chmod)
* [vFile:close](./lldbgdbremote.md#vfile-close)
* [vFile:mode](./lldbgdbremote.md#vfile-mode)
* [vFile:open](./lldbgdbremote.md#vfile-open)
* [vFile:pread](./lldbgdbremote.md#vfile-pread)
* [vFile:pwrite](./lldbgdbremote.md#vfile-pwrite)
* [vFile:size](./lldbgdbremote.md#vfile-size)
* [vFile:symlink](./lldbgdbremote.md#vfile-symlink)
* [vFile:unlink](./lldbgdbremote.md#vfile-unlink)

The remote platform must be able to launch processes so that debugserver
can attach to them. This requires the following packets in addition to the
previous list:
* [A](./lldbgdbremote.md#a-launch-args-packet)
* [QEnvironment](./lldbgdbremote.md#qenvironment-name-value)
* [QEnvironmentHexEncoded](./lldbgdbremote.md#qenvironmenthexencoded-hex-encoding-name-value)
* [QSetDetatchOnError](./lldbgdbremote.md#qsetdetachonerror)
* [QSetDisableASLR](./lldbgdbremote.md#qsetdisableaslr-bool)
* [QSetSTDIN / QSetSTDOUT / QSetSTDERR](./lldbgdbremote.md#qsetstdin-ascii-hex-path-qsetstdout-ascii-hex-path-qsetstderr-ascii-hex-path) (all 3)
* [qLaunchSuccess](./lldbgdbremote.md#qlaunchsuccess)
