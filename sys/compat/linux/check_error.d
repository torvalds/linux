#!/usr/sbin/dtrace -qs

/*-
 * Copyright (c) 2008-2012 Alexander Leidinger <netchild@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Report error conditions:
 *  - emulation errors (unsupportet stuff, unknown stuff, ...)
 *  - kernel errors (resource shortage, ...)
 *  - programming errors (errors which can happen, but should not happen)
 */

linuxulator*:dummy::not_implemented,
linuxulator*:emul:linux_thread_detach:child_clear_tid_error,
linuxulator*:emul:linux_thread_detach:futex_failed,
linuxulator*:emul:linux_schedtail:copyout_error,
linuxulator*:futex:futex_get:error,
linuxulator*:futex:futex_sleep:requeue_error,
linuxulator*:futex:futex_sleep:sleep_error,
linuxulator*:futex:futex_wait:copyin_error,
linuxulator*:futex:futex_wait:itimerfix_error,
linuxulator*:futex:futex_wait:sleep_error,
linuxulator*:futex:futex_atomic_op:missing_access_check,
linuxulator*:futex:futex_atomic_op:unimplemented_op,
linuxulator*:futex:futex_atomic_op:unimplemented_cmp,
linuxulator*:futex:linux_sys_futex:unimplemented_clockswitch,
linuxulator*:futex:linux_sys_futex:copyin_error,
linuxulator*:futex:linux_sys_futex:unhandled_efault,
linuxulator*:futex:linux_sys_futex:unimplemented_lock_pi,
linuxulator*:futex:linux_sys_futex:unimplemented_unlock_pi,
linuxulator*:futex:linux_sys_futex:unimplemented_trylock_pi,
linuxulator*:futex:linux_sys_futex:unimplemented_wait_requeue_pi,
linuxulator*:futex:linux_sys_futex:unimplemented_cmp_requeue_pi,
linuxulator*:futex:linux_sys_futex:unknown_operation,
linuxulator*:futex:linux_get_robust_list:copyout_error,
linuxulator*:futex:handle_futex_death:copyin_error,
linuxulator*:futex:fetch_robust_entry:copyin_error,
linuxulator*:futex:release_futexes:copyin_error,
linuxulator*:time:linux_clock_gettime:conversion_error,
linuxulator*:time:linux_clock_gettime:gettime_error,
linuxulator*:time:linux_clock_gettime:copyout_error,
linuxulator*:time:linux_clock_settime:conversion_error,
linuxulator*:time:linux_clock_settime:settime_error,
linuxulator*:time:linux_clock_settime:copyin_error,
linuxulator*:time:linux_clock_getres:conversion_error,
linuxulator*:time:linux_clock_getres:getres_error,
linuxulator*:time:linux_clock_getres:copyout_error,
linuxulator*:time:linux_nanosleep:conversion_error,
linuxulator*:time:linux_nanosleep:nanosleep_error,
linuxulator*:time:linux_nanosleep:copyout_error,
linuxulator*:time:linux_nanosleep:copyin_error,
linuxulator*:time:linux_clock_nanosleep:copyin_error,
linuxulator*:time:linux_clock_nanosleep:conversion_error,
linuxulator*:time:linux_clock_nanosleep:copyout_error,
linuxulator*:time:linux_clock_nanosleep:nanosleep_error,
linuxulator*:sysctl:handle_string:copyout_error,
linuxulator*:sysctl:linux_sysctl:copyin_error,
linuxulator*:mib:linux_sysctl_osname:sysctl_string_error,
linuxulator*:mib:linux_sysctl_osrelease:sysctl_string_error,
linuxulator*:mib:linux_sysctl_oss_version:sysctl_string_error,
linuxulator*:mib:linux_prison_create:vfs_copyopt_error,
linuxulator*:mib:linux_prison_check:vfs_copyopt_error,
linuxulator*:mib:linux_prison_check:vfs_getopt_error,
linuxulator*:mib:linux_prison_set:vfs_copyopt_error,
linuxulator*:mib:linux_prison_set:vfs_getopt_error,
linuxulator*:mib:linux_prison_get:vfs_setopt_error,
linuxulator*:mib:linux_prison_get:vfs_setopts_error
{
	printf("ERROR: %s in %s:%s:%s\n", probename, probeprov, probemod, probefunc);
	stack();
	ustack();
}

linuxulator*:util:linux_driver_get_name_dev:nullcall,
linuxulator*:util:linux_driver_get_major_minor:nullcall,
linuxulator*:futex:linux_sys_futex:invalid_cmp_requeue_use,
linuxulator*:futex:linux_sys_futex:deprecated_requeue,
linuxulator*:futex:linux_set_robust_list:size_error,
linuxulator*:time:linux_clock_getres:nullcall
{
	printf("WARNING: %s:%s:%s:%s in application %s, maybe an application error?\n", probename, probeprov, probemod, probefunc, execname);
	stack();
	ustack();
}

linuxulator*:util:linux_driver_get_major_minor:notfound
{
	printf("WARNING: Application %s failed to find %s in %s:%s:%s, this may or may not be a problem.\n", execname, stringof(args[0]), probename, probeprov, probemod);
	stack();
	ustack();
}

linuxulator*:time:linux_to_native_clockid:unknown_clockid
{
	printf("INFO: Application %s tried to use unknown clockid %d. Please report this to freebsd-emulation@FreeBSD.org.\n", execname, arg0);
}

linuxulator*:time:linux_to_native_clockid:unsupported_clockid,
linuxulator*:time:linux_clock_nanosleep:unsupported_clockid
{
	printf("WARNING: Application %s tried to use unsupported clockid (%d), this may or may not be a problem for the application.\nPatches to support this clockid are welcome on the freebsd-emulation@FreeBSD.org mailinglist.\n", execname, arg0);
}

linuxulator*:time:linux_clock_nanosleep:unsupported_flags
{
	printf("WARNING: Application %s tried to use unsupported flags (%d), this may or may not be a problem for the application.\nPatches to support those flags are welcome on the freebsd-emulation@FreeBSD.org mailinglist.\n", execname, arg0);
}

linuxulator*:sysctl:linux_sysctl:wrong_length
{
	printf("ERROR: Application %s issued a sysctl which failed the length restrictions.\nThe length passed is %d, the min length supported is 1 and the max length supported is %d.\n", execname, arg0, arg1);
	stack();
	ustack();
}

linuxulator*:sysctl:linux_sysctl:unsupported_sysctl
{
	printf("ERROR: Application %s issued an unsupported sysctl (%s).\nPatches to support this sysctl are welcome on the freebsd-emulation@FreeBSD.org mailinglist.\n", execname, stringof(args[0]));
}
