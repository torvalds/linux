# SPDX-License-Identifier: GPL-2.0

export RE_NUMBER="[0-9\.]+"
# Number
# Examples:
#    123.456


export RE_NUMBER_HEX="[0-9A-Fa-f]+"
# Hexadecimal number
# Examples:
#    1234
#    a58d
#    aBcD
#    deadbeef


export RE_DATE_YYYYMMDD="[0-9]{4}-(?:(?:01|03|05|07|08|10|12)-(?:[0-2][0-9]|3[0-1])|02-[0-2][0-9]|(?:(?:04|06|09|11)-(?:[0-2][0-9]|30)))"
# Date in YYYY-MM-DD form
# Examples:
#    1990-02-29
#    0015-07-31
#    2456-12-31
#!   2012-13-01
#!   1963-09-31


export RE_TIME="(?:[0-1][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]"
# Time
# Examples:
#    15:12:27
#    23:59:59
#!   24:00:00
#!   11:25:60
#!   17:60:15


export RE_DATE_TIME="\w+\s+\w+\s+$RE_NUMBER\s+$RE_TIME\s+$RE_NUMBER"
# Time and date
# Examples:
#    Wed Feb 12 10:46:26 2020
#    Mon Mar  2 13:27:06 2020
#!   St úno 12 10:57:21 CET 2020
#!   Po úno 14 15:17:32 2010


export RE_ADDRESS="0x$RE_NUMBER_HEX"
# Memory address
# Examples:
#    0x123abc
#    0xffffffff9abe8ae8
#    0x0


export RE_ADDRESS_NOT_NULL="0x[0-9A-Fa-f]*[1-9A-Fa-f]+[0-9A-Fa-f]*"
# Memory address (not NULL)
# Examples:
#    0xffffffff9abe8ae8
#!   0x0
#!   0x0000000000000000

export RE_PROCESS_PID="[^\/]+\/\d+"
# A process with PID
# Example:
#    sleep/4102
#    test_overhead./866185
#    in:imjournal/1096
#    random#$& test/866607

export RE_EVENT_ANY="[\w\-\:\/_=,]+"
# Name of any event (universal)
# Examples:
#    cpu-cycles
#    cpu/event=12,umask=34/
#    r41e1
#    nfs:nfs_getattr_enter


export RE_EVENT="[\w\-:_]+"
# Name of an usual event
# Examples:
#    cpu-cycles


export RE_EVENT_RAW="r$RE_NUMBER_HEX"
# Specification of a raw event
# Examples:
#    r41e1
#    r1a


export RE_EVENT_CPU="cpu/(\w+=$RE_NUMBER_HEX,?)+/p*"
# Specification of a CPU event
# Examples:
#    cpu/event=12,umask=34/pp


export RE_EVENT_UNCORE="uncore/[\w_]+/"
# Specification of an uncore event
# Examples:
#    uncore/qhl_request_local_reads/


export RE_EVENT_SUBSYSTEM="[\w\-]+:[\w\-]+"
# Name of an event from subsystem
# Examples:
#    ext4:ext4_ordered_write_end
#    sched:sched_switch


export RE_FILE_NAME="[\w\+\.-]+"
# A filename
# Examples:
#    libstdc++.so.6
#!   some/path


export RE_PATH_ABSOLUTE="(?:\/$RE_FILE_NAME)+"
# A full filepath
# Examples:
#    /usr/lib64/somelib.so.5.4.0
#    /lib/modules/4.3.0-rc5/kernel/fs/xfs/xfs.ko
#    /usr/bin/mv
#!   some/relative/path
#!   ./some/relative/path


export RE_PATH="(?:$RE_FILE_NAME)?$RE_PATH_ABSOLUTE"
# A filepath
# Examples:
#    /usr/lib64/somelib.so.5.4.0
#    /lib/modules/4.3.0-rc5/kernel/fs/xfs/xfs.ko
#    ./.emacs
#    src/fs/file.c


export RE_DSO="(?:$RE_PATH_ABSOLUTE(?: \(deleted\))?|\[kernel\.kallsyms\]|\[unknown\]|\[vdso\]|\[kernel\.vmlinux\][\.\w]*)"
# A DSO name in various result tables
# Examples:
#    /usr/lib64/somelib.so.5.4.0
#    /usr/bin/somebinart (deleted)
#    /lib/modules/4.3.0-rc5/kernel/fs/xfs/xfs.ko
#    [kernel.kallsyms]
#    [kernel.vmlinux]
#    [vdso]
#    [unknown]


export RE_LINE_COMMENT="^#.*"
# A comment line
# Examples:
#    # Started on Thu Sep 10 11:43:00 2015


export RE_LINE_EMPTY="^\s*$"
# An empty line with possible whitespaces
# Examples:
#


export RE_LINE_RECORD1="^\[\s+perf\s+record:\s+Woken up $RE_NUMBER times? to write data\s+\].*$"
# The first line of perf-record "OK" output
# Examples:
#    [ perf record: Woken up 1 times to write data ]


export RE_LINE_RECORD2="^\[\s+perf\s+record:\s+Captured and wrote $RE_NUMBER\s*MB\s+(?:[\w\+\.-]*(?:$RE_PATH)?\/)?perf\.data(?:\.\d+)?\s*\(~?$RE_NUMBER samples\)\s+\].*$"
# The second line of perf-record "OK" output
# Examples:
#    [ perf record: Captured and wrote 0.405 MB perf.data (109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB perf.data (~109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB /some/temp/dir/perf.data (109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB ./perf.data (109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB ./perf.data.3 (109 samples) ]


export RE_LINE_RECORD2_TOLERANT="^\[\s+perf\s+record:\s+Captured and wrote $RE_NUMBER\s*MB\s+(?:[\w\+\.-]*(?:$RE_PATH)?\/)?perf\.data(?:\.\d+)?\s*(?:\(~?$RE_NUMBER samples\))?\s+\].*$"
# The second line of perf-record "OK" output, even no samples is OK here
# Examples:
#    [ perf record: Captured and wrote 0.405 MB perf.data (109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB perf.data (~109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB /some/temp/dir/perf.data (109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB ./perf.data (109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB ./perf.data.3 (109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB perf.data ]


export RE_LINE_RECORD2_TOLERANT_FILENAME="^\[\s+perf\s+record:\s+Captured and wrote $RE_NUMBER\s*MB\s+(?:[\w\+\.-]*(?:$RE_PATH)?\/)?perf\w*\.data(?:\.\d+)?\s*\(~?$RE_NUMBER samples\)\s+\].*$"
# The second line of perf-record "OK" output
# Examples:
#    [ perf record: Captured and wrote 0.405 MB perf.data (109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB perf_ls.data (~109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB perf_aNyCaSe.data (109 samples) ]
#    [ perf record: Captured and wrote 0.405 MB ./perfdata.data.3 (109 samples) ]
#!    [ perf record: Captured and wrote 0.405 MB /some/temp/dir/my_own.data (109 samples) ]
#!    [ perf record: Captured and wrote 0.405 MB ./UPPERCASE.data (109 samples) ]
#!    [ perf record: Captured and wrote 0.405 MB ./aNyKiNDoF.data.3 (109 samples) ]
#!    [ perf record: Captured and wrote 0.405 MB perf.data ]


export RE_LINE_TRACE_FULL="^\s*$RE_NUMBER\s*\(\s*$RE_NUMBER\s*ms\s*\):\s*$RE_PROCESS_PID\s+.*\)\s+=\s+(:?\-?$RE_NUMBER|0x$RE_NUMBER_HEX).*$"
# A line of perf-trace output
# Examples:
#    0.115 ( 0.005 ms): sleep/4102 open(filename: 0xd09e2ab2, flags: CLOEXEC                             ) = 3
#    0.157 ( 0.005 ms): sleep/4102 mmap(len: 3932736, prot: EXEC|READ, flags: PRIVATE|DENYWRITE, fd: 3   ) = 0x7f89d0605000
#!    0.115 ( 0.005 ms): sleep/4102 open(filename: 0xd09e2ab2, flags: CLOEXEC                             ) =

export RE_LINE_TRACE_ONE_PROC="^\s*$RE_NUMBER\s*\(\s*$RE_NUMBER\s*ms\s*\):\s*\w+\(.*\)\s+=\s+(?:\-?$RE_NUMBER|0x$RE_NUMBER_HEX).*$"
# A line of perf-trace output
# Examples:
#    0.115 ( 0.005 ms): open(filename: 0xd09e2ab2, flags: CLOEXEC                             ) = 3
#    0.157 ( 0.005 ms): mmap(len: 3932736, prot: EXEC|READ, flags: PRIVATE|DENYWRITE, fd: 3   ) = 0x7f89d0605000
#!    0.115 ( 0.005 ms): open(filename: 0xd09e2ab2, flags: CLOEXEC                             ) =

export RE_LINE_TRACE_CONTINUED="^\s*(:?$RE_NUMBER|\?)\s*\(\s*($RE_NUMBER\s*ms\s*)?\):\s*($RE_PROCESS_PID\s*)?\.\.\.\s*\[continued\]:\s+\w+\(\).*\s+=\s+(?:\-?$RE_NUMBER|0x$RE_NUMBER_HEX).*$"
# A line of perf-trace output
# Examples:
#    0.000 ( 0.000 ms):  ... [continued]: nanosleep()) = 0
#    0.000 ( 0.000 ms):  ... [continued]: nanosleep()) = 0x00000000
#    ? (         ): packagekitd/94838  ... [continued]: poll())                                             = 0 (Timeout)
#!    0.000 ( 0.000 ms):  ... [continued]: nanosleep()) =

export RE_LINE_TRACE_UNFINISHED="^\s*$RE_NUMBER\s*\(\s*\):\s*$RE_PROCESS_PID\s+.*\)\s+\.\.\.\s*$"
# A line of perf-trace output
# Examples:
#    901.040 (         ): in:imjournal/1096 ppoll(ufds: 0x7f701a5adb70, nfds: 1, tsp: 0x7f701a5adaf0, sigsetsize: 8) ...
#    613.727 (         ): gmain/1099 poll(ufds: 0x56248f6b64b0, nfds: 2, timeout_msecs: 3996)           ...

export RE_LINE_TRACE_SUMMARY_HEADER="\s*syscall\s+calls\s+(?:errors\s+)?total\s+min\s+avg\s+max\s+stddev"
# A header of a perf-trace summary table
# Example:
#    syscall            calls    total       min       avg       max      stddev
#    syscall            calls  errors  total       min       avg       max       stddev


export RE_LINE_TRACE_SUMMARY_CONTENT="^\s*\w+\s+(?:$RE_NUMBER\s+){5,6}$RE_NUMBER%"
# A line of a perf-trace summary table
# Example:
#    open                   3     0.017     0.005     0.006     0.007     10.90%
#    openat                 2      0     0.017     0.008     0.009     0.010     12.29%


export RE_LINE_REPORT_CONTENT="^\s+$RE_NUMBER%\s+\w+\s+\S+\s+\S+\s+\S+" # FIXME
# A line from typicap perf report --stdio output
# Example:
#     100.00%  sleep    [kernel.vmlinux]  [k] syscall_return_slowpath


export RE_TASK="\s+[\w~\/ \.\+:#-]+(?:\[-1(?:\/\d+)?\]|\[\d+(?:\/\d+)?\])"
# A name of a task used for perf sched timehist -s
# Example:
#     sleep[62755]
#     runtest.sh[62762]
#     gmain[705/682]
#     xfsaild/dm-0[495]
#     kworker/u8:1-ev[62714]
#     :-1[-1/62756]
#     :-1[-1]
#     :-1[62756]


export RE_SEGFAULT=".*(?:Segmentation\sfault|SIGSEGV|\score\s|dumped|segfault).*"
# Possible variations of the segfault message
# Example:
#     /bin/bash: line 1:    32 Segmentation fault      timeout 15s
#     Segmentation fault (core dumped)
#     Program terminated with signal SIGSEGV
#!     WARNING: 12323431 isn't a 'cpu_core', please use a CPU list in the 'cpu_core' range (0-15)
