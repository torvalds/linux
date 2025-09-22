# GDB Remote Protocol Extensions

LLDB has added new GDB server packets to better support multi-threaded and
remote debugging. These extend the
[protocol defined by GDB ](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Packets.html#Packets) (and [this page](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Host-I_002fO-Packets.html#Host-I_002fO-Packets) for `vFile` packets).

If a packet is restated here it is because LLDB's version has some behaviour
difference to GDB's version, or it provides some context for a following LLDB
extension packet.

Why did we add these? The most common reason is flexibility. Normally you need
to start the correct GDB and the correct GDB server when debugging. If you have
mismatch, then things go wrong very quickly. LLDB makes extensive use of the GDB
remote protocol and we wanted to make sure that the experience was a bit more
dynamic where we can discover information about a remote target without having
to know anything up front.

We also ran into performance issues with the existing GDB remote
protocol that can be overcome when using a reliable communications layer.

Some packets improve performance, others allow for remote process launching
(if you have an OS), and others allow us to dynamically figure out what
registers a thread might have. Again with GDB, both sides pre-agree on how the
registers will look (how many, their register number,name and offsets).

We prefer to be able to dynamically determine what kind of architecture, OS and
vendor we are debugging, as well as how things are laid out when it comes to
the thread register contexts.

## _M\<size\>,\<permissions\>

Allocate memory on the remote target with the specified size and
permissions.

The allocate memory packet starts with `_M<size>,<permissions>`. It returns a
raw big endian address value, or an empty response for unimplemented, or `EXX` for an error
code. The packet is formatted as:
```
char packet[256];
int packet_len;
packet_len = ::snprintf (
    packet,
    sizeof(packet),
    "_M%zx,%s%s%s",
    (size_t)size,
    permissions & lldb::ePermissionsReadable ? "r" : "",
    permissions & lldb::ePermissionsWritable ? "w" : "",
    permissions & lldb::ePermissionsExecutable ? "x" : "");
```

You request a size and give the permissions. This packet does NOT need to be
implemented if you don't want to support running JITed code. The return value
is just the address of the newly allocated memory as raw big endian hex bytes.

**Priority To Implement:** High if you want LLDB to be able to JIT code and run
that code. JIT code also needs data which is also allocated and tracked. Low if
you don't support running JIT'ed code.

## _m\<addr\>

Deallocate memory that was previously allocated using an allocate
memory pack.

The deallocate memory packet is `_m<addr>` where you pass in the address you
got back from a previous call to the allocate memory packet. It returns `OK`
if the memory was successfully deallocated, or `EXX`" for an error, or an
empty response if not supported.

**Priority To Implement:** High if you want LLDB to be able to JIT code and run
that code. JIT code also needs data which is also allocated and tracked. Low if
you don't support running JIT'ed code.

## "A" - launch args packet

Launch a program using the supplied arguments

We have added support for the "set program arguments" packet where we can
start a connection to a remote server and then later supply the path to the
executable and the arguments to use when executing:

GDB remote docs for this:
```
set program arguments(reserved) Aarglen,argnum,arg,...
```
Where A is followed by the length in bytes of the hex encoded argument,
followed by an argument integer, and followed by the ASCII characters
converted into hex bytes for each arg:
```
send packet: $A98,0,2f566f6c756d65732f776f726b2f67636c6179746f6e2f446f63756d656e74732f7372632f6174746163682f612e6f7574#00
read packet: $OK#00
```
The above packet helps when you have remote debugging abilities where you
could launch a process on a remote host, this isn't needed for bare board
debugging.

**Priority To Implement:** Low. Only needed if the remote target wants to launch
a target after making a connection to a GDB server that isn't already connected to
an inferior process.

## "D" - Detach and stay stopped

We extended the "D" packet to specify that the monitor should keep the
target suspended on detach.  The normal behavior is to resume execution
on detach.  We will send:
```
qSupportsDetachAndStayStopped:
```

to query whether the monitor supports the extended detach, and if it does,
when we want the monitor to detach but not resume the target, we will
send:
```
D1
```
In any case, if we want the normal detach behavior we will just send:
```
D
```

## jGetDyldProcessState

This packet fetches the process launch state, as reported by libdyld on
Darwin systems, most importantly to indicate when the system libraries
have initialized sufficiently to safely call utility functions.

```
LLDB SENDS: jGetDyldProcessState
STUB REPLIES: {"process_state_value":48,"process_state string":"dyld_process_state_libSystem_initialized"}
```

**Priority To Implement:** Low. This packet is needed to prevent lldb's utility
functions for scanning the Objective-C class list from running very early in
process startup.

## jGetLoadedDynamicLibrariesInfos

This packet asks the remote debug stub to send the details about libraries
being added/removed from the process as a performance optimization.

There are two ways this packet can be used.  Both return a dictionary of
binary images formatted the same way.

One requests information on all shared libraries:
```
jGetLoadedDynamicLibrariesInfos:{"fetch_all_solibs":true}
```
with an optional `"report_load_commands":false` which can be added, asking
that only the dyld SPI information (load addresses, filenames) be returned.
The default behavior is that debugserver scans the mach-o header and load
commands of each binary, and returns it in the JSON reply.

And the second requests information about a list of shared libraries, given their load addresses:
```
jGetLoadedDynamicLibrariesInfos:{"solib_addresses":[8382824135,3258302053,830202858503]}
```

The second call is both a performance optimization (instead of having lldb read the mach-o header/load commands
out of memory with generic read packets) but also adds additional information in the form of the
filename of the shared libraries (which is not available in the mach-o header/load commands.)

An example using the OS X 10.11 style call:
```
LLDB SENDS: jGetLoadedDynamicLibrariesInfos:{"image_count":1,"image_list_address":140734800075128}
STUB REPLIES: ${"images":[{"load_address":4294967296,"mod_date":0,"pathname":"/tmp/a.out","uuid":"02CF262C-ED6F-3965-9E14-63538B465CFF","mach_header":{"magic":4277009103,"cputype":16777223,"cpusubtype":18446744071562067971,"filetype":2},"segments":{"name":"__PAGEZERO","vmaddr":0,"vmsize":4294967296,"fileoff":0,"filesize":0,"maxprot":0},{"name":"__TEXT","vmaddr":4294967296,"vmsize":4096,"fileoff":0,"filesize":4096,"maxprot":7},{"name":"__LINKEDIT","vmaddr":4294971392,"vmsize":4096,"fileoff":4096,"filesize":152,"maxprot":7}}]}#00
```

Or pretty-printed:
```
STUB REPLIES: ${"images":
                [
                    {"load_address":4294967296,
                     "mod_date":0,
                     "pathname":"/tmp/a.out",
                     "uuid":"02CF262C-ED6F-3965-9E14-63538B465CFF",
                     "mach_header":
                        {"magic":4277009103,
                         "cputype":16777223,
                         "cpusubtype":18446744071562067971,
                         "filetype":2
                         },
                     "segments":
                      [
                        {"name":"__PAGEZERO",
                         "vmaddr":0,
                         "vmsize":4294967296,
                         "fileoff":0,
                         "filesize":0,
                         "maxprot":0
                        },
                        {"name":"__TEXT",
                         "vmaddr":4294967296,
                         "vmsize":4096,
                         "fileoff":0,
                         "filesize":4096,
                         "maxprot":7
                        },
                        {"name":"__LINKEDIT",
                         "vmaddr":4294971392,
                         "vmsize":4096,
                         "fileoff":4096,
                         "filesize":152,
                         "maxprot":7
                        }
                      ]
                    }
                ]
            }
```

This is similar to the `qXfer:libraries:read` packet, and it could
be argued that it should be merged into that packet.  A separate
packet was created primarily because lldb needs to specify the
number of images to be read and the address from which the initial
information is read.  Also the XML DTD would need to be extended
quite a bit to provide all the information that the `DynamicLoaderMacOSX`
would need to work correctly on this platform.

**Priority To Implement:**

On OS X 10.11, iOS 9, tvOS 9, watchOS 2 and older: Low.  If this packet is absent,
lldb will read the Mach-O headers/load commands out of memory.
On macOS 10.12, iOS 10, tvOS 10, watchOS 3 and newer: High.  If this packet is absent,
lldb will not know anything about shared libraries in the inferior, or where the main
executable loaded.

## jGetSharedCacheInfo

This packet asks the remote debug stub to send the details about the inferior's
shared cache. The shared cache is a collection of common libraries/frameworks that
are mapped into every process at the same address on Darwin systems, and can be
identified by a load address and UUID.

```
LLDB SENDS: jGetSharedCacheInfo:{}
STUB REPLIES: ${"shared_cache_base_address":140735683125248,"shared_cache_uuid":"DDB8D70C-C9A2-3561-B2C8-BE48A4F33F96","no_shared_cache":false,"shared_cache_private_cache":false]}#00
```

**Priority To Implement:** Low

When both lldb and the inferior process are running on the same computer, and lldb
and the inferior process have the same shared cache, lldb may (as an optimization) read
the shared cache out of its own memory instead of using gdb-remote read packets to read
them from the inferior process.

## jModulesInfo:[{"file":"...",triple:"..."}, ...]

Get information for a list of modules by given module path and
architecture.

The response is a JSON array of dictionaries containing the following keys:
* `uuid`
* `triple`
* `file_path`
* `file_offset`
* `file_size`

The meaning of the fields is the same as in the `qModuleInfo` packet. The server
signals the failure to retrieve the module info for a file by ommiting the
corresponding array entry from the response. The server may also
include entries the client did not ask for, if it has reason to
the modules will be interesting to the client.

**Priority To Implement:** Optional. If not implemented, `qModuleInfo` packet
will be used, which may be slower if the target contains a large number of modules
and the communication link has a non-negligible latency.

## jLLDBTraceGetBinaryData

Get binary data given a trace technology and a data identifier.
The input is specified as a JSON object and the response has the same format
as the "binary memory read" (aka "x") packet. In case of failures, an error
message is returned.

```
send packet: jLLDBTraceGetBinaryData:{"type":<type>,"kind":<query>,"tid":<tid>,"offset":<offset>,"size":<size>}]
read packet: <binary data>/E<error code>;AAAAAAAAA
```

### Schema

The schema for the input is:
```
{
 "type": <string>,
     Tracing technology name, e.g. intel-pt, arm-etm.
 "kind": <string>,
     Identifier for the data.
 "cpuId": <Optional decimal>,
     Core id in decimal if the data belongs to a CPU core.
 "tid"?: <Optional decimal>,
     Tid in decimal if the data belongs to a thread.
}
```

## jLLDBTraceGetState

Get the current state of the process and its threads being traced by
a given trace technology. The response is a JSON object with custom
information depending on the trace technology. In case of errors, an
error message is returned.

```
send packet: jLLDBTraceGetState:{"type":<type>}]
read packet: {...object}/E<error code>;AAAAAAAAA
```

### Input Schema

```
{
   "type": <string>
      Tracing technology name, e.g. intel-pt, arm-etm.
}
```

### Output Schema

```
{
  "tracedThreads": [{
    "tid": <decimal integer>,
    "binaryData": [
      {
        "kind": <string>,
            Identifier for some binary data related to this thread to
            fetch with the jLLDBTraceGetBinaryData packet.
        "size": <decimal integer>,
            Size in bytes of this thread data.
      },
    ]
  }],
  "processBinaryData": [
    {
      "kind": <string>,
          Identifier for some binary data related to this process to
          fetch with the jLLDBTraceGetBinaryData packet.
      "size": <decimal integer>,
          Size in bytes of this thread data.
    },
  ],
  "cpus"?: [
    "id": <decimal integer>,
        Identifier for this CPU logical core.
    "binaryData": [
      {
        "kind": <string>,
            Identifier for some binary data related to this thread to
            fetch with the jLLDBTraceGetBinaryData packet.
        "size": <decimal integer>,
            Size in bytes of this cpu core data.
      },
    ]
  ],
  "warnings"?: [<string>],
      Non-fatal messages useful for troubleshooting.

  ... other attributes specific to the given tracing technology
}
```

**Note:** `tracedThreads` includes all threads traced by both "process tracing"
and "thread tracing".

### Intel Pt

If per-cpu process tracing is enabled, "tracedThreads" will contain all
the threads of the process without any trace buffers. Besides that, the
"cpus" field will also be returned with per cpu core trace buffers.
A side effect of per-cpu tracing is that all the threads of unrelated
processes will also be traced, thus polluting the tracing data.

Binary data kinds:
  - iptTrace: trace buffer for a thread or a cpu.
  - perfContextSwitchTrace: context switch trace for a cpu generated by
                            perf_event_open.
  - procfsCpuInfo: contents of the /proc/cpuinfo file.

Additional attributes:
  * tscPerfZeroConversion
    * This field allows converting Intel processor's TSC values to nanoseconds.
      It is available through the Linux perf_event API when cap_user_time and cap_user_time_zero
      are set.
      See the documentation of time_zero in
      https://man7.org/linux/man-pages/man2/perf_event_open.2.html for more information about
      the calculation and the meaning of the values in the schema below.

      Schema for this field:
      ```
      "tscPerfZeroConversion": {
        "timeMult": <decimal integer>,
        "timeShift": <decimal integer>,
        "timeZero": <decimal integer>,
      }
      ```

## jLLDBTraceStart

Start tracing a process or its threads using a provided tracing technology.
The input and output are specified as JSON objects. In case of success, an OK
response is returned, or an error otherwise.

### Process Tracing

This traces existing and future threads of the current process. An error is
returned if the process is already being traced.

```
send packet: jLLDBTraceStart:{"type":<type>,...other params}]
read packet: OK/E<error code>;AAAAAAAAA
```

### Thread Tracing

This traces specific threads.

```
send packet: jLLDBTraceStart:{"type":<type>,"tids":<tids>,...other params}]
read packet: OK/E<error code>;AAAAAAAAA
```

### Input Schema

```
{
  "type": <string>,
      Tracing technology name, e.g. intel-pt, arm-etm.

  /* thread tracing only */
  "tids"?: [<decimal integer>],
      Individual threads to trace.

  ... other parameters specific to the provided tracing type
}
```

**Notes:**
- If "tids" is not provided, then the operation is "process tracing",
  otherwise it's "thread tracing".
- Each tracing technology can have different levels of support for "thread
  tracing" and "process tracing".

### Intel-Pt

intel-pt supports both "thread tracing" and "process tracing".

"Process tracing" is implemented in two different ways. If the
"perCpuTracing" option is false, then each thread is traced individually
but managed by the same "process trace" instance. This means that the
amount of trace buffers used is proportional to the number of running
threads. This is the recommended option unless the number of threads is
huge. If "perCpuTracing" is true, then each cpu core is traced invidually
instead of each thread, which uses a fixed number of trace buffers, but
might result in less data available for less frequent threads. See
"perCpuTracing" below for more information.

Each actual intel pt trace buffer, either from "process tracing" or "thread
tracing", is stored in an in-memory circular buffer, which keeps the most
recent data.

Additional params in the input schema:
```
 {
   "iptTraceSize": <decimal integer>,
       Size in bytes used by each individual per-thread or per-cpu trace
       buffer. It must be a power of 2 greater than or equal to 4096 (2^12)
       bytes.

   "enableTsc": <boolean>,
       Whether to enable TSC timestamps or not. This is supported on
       all devices that support intel-pt. A TSC timestamp is generated along
       with PSB (synchronization) packets, whose frequency can be configured
       with the "psbPeriod" parameter.

   "psbPeriod"?: <Optional decimal integer>,
       This value defines the period in which PSB packets will be generated.
       A PSB packet is a synchronization packet that contains a TSC
       timestamp and the current absolute instruction pointer.

       This parameter can only be used if

           /sys/bus/event_source/devices/intel_pt/caps/psb_cyc

       is 1. Otherwise, the PSB period will be defined by the processor.

       If supported, valid values for this period can be found in

           /sys/bus/event_source/devices/intel_pt/caps/psb_periods

       which contains a hexadecimal number, whose bits represent valid
       values e.g. if bit 2 is set, then value 2 is valid.

       The psb_period value is converted to the approximate number of
       raw trace bytes between PSB packets as:

           2 ^ (value + 11)

        e.g. value 3 means 16KiB between PSB packets. Defaults to
        0 if supported.

   /* process tracing only */
   "perCpuTracing": <boolean>
       Instead of having an individual trace buffer per thread, this option
       triggers the collection on a per cpu core basis. This effectively
       traces the entire activity on all cores. At decoding time, in order
       to correctly associate a decoded instruction with a thread, the
       context switch trace of each core is needed, as well as a record per
       cpu indicating which thread was running on each core when tracing
       started. These secondary traces are correlated with the intel-pt
       trace by comparing TSC timestamps.

       This option forces the capture of TSC timestamps (see "enableTsc").

       Note: This option can't be used simulatenously with any other trace
       sessions because of its system-wide nature.

   /* process tracing only */
   "processBufferSizeLimit": <decimal integer>,
       Maximum total buffer size per process in bytes.
       This limit applies to the sum of the sizes of all thread or cpu core
       buffers for the current process, excluding the ones started with
       "thread tracing".

       If "perCpuTracing" is false, whenever a thread is attempted to be
       traced due to "process tracing" and the limit would be reached, the
       process is stopped with a "tracing" reason along with a meaningful
       description, so that the user can retrace the process if needed.

       If "perCpuTracing" is true, then starting the system-wide trace
       session fails if all the individual per-cpu trace buffers require
       in total more memory that the limit impossed by this parameter.
 }
```

Notes:
 - Modifying the parameters of an existing trace is not supported. The user
   needs to stop the trace and start a new one.
 - If "process tracing" is attempted and there are individual threads
   already being traced with "thread tracing", these traces are left
   unaffected and the threads not traced twice.
 - If "thread tracing" is attempted on a thread already being traced with
   either "thread tracing" or "process tracing", it fails.

## jLLDBTraceStop

Stop tracing a process or its threads using a provided tracing technology.
The input and output are specified as JSON objects. In case of success, an OK
response is returned, or an error otherwise.

### Process Trace Stopping

Stopping a process trace stops the active traces initiated with
"thread tracing".

```
send packet: jLLDBTraceStop:{"type":<type>}]
read packet: OK/E<error code>;AAAAAAAAA
```

### Thread Trace Stopping

This is a best effort request, which tries to stop as many traces as
possible.

```
send packet: jLLDBTraceStop:{"type":<type>,"tids":<tids>}]
read packet: OK/E<error code>;AAAAAAAAA
```

### Input Schema

The schema for the input is
```
{
  "type": <string>
     Tracing technology name, e.g. intel-pt, arm-etm.

  /* thread trace stopping only */
  "tids":  [<decimal integer>]
     Individual thread traces to stop.
}
```

**Note:** If `tids` is not provided, then the operation is "process trace stopping".

### Intel Pt

Stopping a specific thread trace started with "process tracing" is allowed.

## jLLDBTraceSupported

Get the processor tracing type supported by the gdb-server for the current
inferior. Responses might be different depending on the architecture and
capabilities of the underlying OS.

```
send packet: jLLDBTraceSupported
read packet: {"name":<name>, "description":<description>}/E<error code>;AAAAAAAAA
```

### Output Schema

```
 {
   "name": <string>,
       Tracing technology name, e.g. intel-pt, arm-etm.
   "description": <string>,
       Description for this technology.
 }
```

If no tracing technology is supported for the inferior, or no process is
running, then an error message is returned.

**Note:** This packet is used by Trace plug-ins (see `lldb_private::Trace.h`) to
do live tracing. Specifically, the name of the plug-in should match the name
of the tracing technology returned by this packet.

## jThreadExtendedInfo

This packet, which takes its arguments as JSON and sends its reply as
JSON, allows the gdb remote stub to provide additional information
about a given thread.

This packet takes its arguments in [JSON](http://www.json.org).
At a minimum, a thread must be specified, for example:
```
jThreadExtendedInfo:{"thread":612910}
```

Because this is a JSON string, the thread number is provided in base 10.
Additional key-value pairs may be provided by lldb to the gdb remote
stub.  For instance, on some versions of macOS, lldb can read offset
information out of the system libraries.  Using those offsets, debugserver
is able to find the Thread Specific Address (TSD) for a thread and include
that in the return information.  So lldb will send these additional fields
like so:
```
jThreadExtendedInfo:{"plo_pthread_tsd_base_address_offset":0,"plo_pthread_tsd_base_offset":224,"plo_pthread_tsd_entry_size":8,"thread":612910}
```

There are no requirements for what is included in the response.  A simple
reply on a OS X Yosemite / iOS 8 may include the pthread_t value, the
Thread Specific Data (TSD) address, the dispatch_queue_t value if the thread
is associated with a GCD queue, and the requested Quality of Service (QoS)
information about that thread.  For instance, a reply may look like:
```
{"tsd_address":4371349728,"requested_qos":{"enum_value":33,"constant_name":"QOS_CLASS_USER_INTERACTIVE","printable_name":"User Interactive"},"pthread_t":4371349504,"dispatch_queue_t":140735087127872}
```

`tsd_address`, `pthread_t`, and `dispatch_queue_t` are all simple key-value pairs.
The JSON standard requires that numbers be expressed in base 10 - so all of
these are. `requested_qos` is a dictionary with three key-value pairs in it -
so the UI layer may choose the form most appropriate for displaying to the user.

Sending JSON over gdb-remote protocol introduces some problems.  We may be
sending strings with arbitrary contents in them, including the `#`, `$`, and `*`
characters that have special meaning in gdb-remote protocol and cannot occur
in the middle of the string. The standard solution for this would be to require
ascii-hex encoding of all strings, or ascii-hex encode the entire JSON payload.

Instead, the binary escaping convention is used for JSON data.  This convention
(e.g. used for the `X` packet) says that if `#`, `$`, `*`, or `}` are to occur in
the payload, the character `}` (`0x7d`) is emitted, then the metacharacter is emitted
xor'ed by `0x20`. The `}` character occurs in every JSON payload at least once, and
`} ^ 0x20` happens to be `]` so the raw packet characters for a request will look
like:
```
jThreadExtendedInfo:{"thread":612910}]
```

**Priority To Implement:** Low. This packet is only needed if the gdb remote stub
wants to provide interesting additional information about a thread for the user.

## jThreadsInfo

Ask for the server for thread stop information of all threads.

The data in this packet is very similar to the stop reply packets, but is packaged in
JSON and uses JSON arrays where applicable. The JSON output looks like:
```
    [
      { "tid":1580681,
        "metype":6,
        "medata":[2,0],
        "reason":"exception",
        "qaddr":140735118423168,
        "registers": {
          "0":"8000000000000000",
          "1":"0000000000000000",
          "2":"20fabf5fff7f0000",
          "3":"e8f8bf5fff7f0000",
          "4":"0100000000000000",
          "5":"d8f8bf5fff7f0000",
          "6":"b0f8bf5fff7f0000",
          "7":"20f4bf5fff7f0000",
          "8":"8000000000000000",
          "9":"61a8db78a61500db",
          "10":"3200000000000000",
          "11":"4602000000000000",
          "12":"0000000000000000",
          "13":"0000000000000000",
          "14":"0000000000000000",
          "15":"0000000000000000",
          "16":"960b000001000000",
          "17":"0202000000000000",
          "18":"2b00000000000000",
          "19":"0000000000000000",
          "20":"0000000000000000"
        },
        "memory":[
          {"address":140734799804592,"bytes":"c8f8bf5fff7f0000c9a59e8cff7f0000"},
          {"address":140734799804616,"bytes":"00000000000000000100000000000000"}
        ]
      }
    ]
```

It contains an array of dictionaries with all of the key value pairs that are
normally in the stop reply packet, including the expedited registers. The registers are
passed as hex-encoded JSON string in debuggee-endian byte order. Note that the register
numbers are decimal numbers, unlike the stop-reply packet, where they are written in
hex. The packet also contains expedited memory in the `memory` key.  This allows the
server to expedite memory that the client is likely to use (e.g., areas around the
stack pointer, which are needed for computing backtraces) and it reduces the packet
count.

On macOS with debugserver, we expedite the frame pointer backchain for a thread
(up to 256 entries) by reading 2 pointers worth of bytes at the frame pointer (for
the previous FP and PC), and follow the backchain. Most backtraces on macOS and
iOS now don't require us to read any memory!

**Priority To Implement:** Low

This is a performance optimization, which speeds up debugging by avoiding
multiple round-trips for retrieving thread information. The information from this
packet can be retrieved using a combination of `qThreadStopInfo` and `m` packets.

## QEnvironment:NAME=VALUE

Setup the environment up for a new child process that will soon be
launched using the "A" packet.

NB: key/value pairs are sent as-is so gdb-remote protocol meta characters
(e.g. `#` or `$`) are not acceptable.  If any non-printable or
metacharacters are present in the strings, `QEnvironmentHexEncoded`
should be used instead if it is available.  If you don't want to
scan the environment strings before sending, prefer
the `QEnvironmentHexEncoded` packet over `QEnvironment`, if it is
available.

Both GDB and LLDB support passing down environment variables. Is it ok to
respond with a `$#00` (unimplemented):
```
send packet: $QEnvironment:ACK_COLOR_FILENAME=bold yellow#00
read packet: $OK#00
```
This packet can be sent one or more times _prior_ to sending a "A" packet.

**Priority To Implement:** Low. Only needed if the remote target wants to launch
a target after making a connection to a GDB server that isn't already connected to
an inferior process.

## QEnvironmentHexEncoded:HEX-ENCODING(NAME=VALUE)

Setup the environment up for a new child process that will soon be
launched using the "A" packet.

The only difference between this packet and `QEnvironment` is that the
environment key-value pair is ascii hex encoded for transmission.
This allows values with gdb-remote metacharacters like `#` to be sent.

Both GDB and LLDB support passing down environment variables. Is it ok to
respond with a `$#00` (unimplemented):
```
send packet: $QEnvironment:41434b5f434f4c4f525f46494c454e414d453d626f6c642379656c6c6f77#00
read packet: $OK#00
```
This packet can be sent one or more times _prior_ to sending a "A" packet.

**Priority To Implement:** Low. Only needed if the remote target wants to launch
a target after making a connection to a GDB server that isn't already connected to
an inferior process.

## QEnableCompression

This packet enables compression of the packets that the debug stub sends to lldb.
If the debug stub can support compression, it indictes this in the reply of the
"qSupported" packet. For example:
```
LLDB SENDS:    qSupported:xmlRegisters=i386,arm,mips
STUB REPLIES:  qXfer:features:read+;SupportedCompressions=lzfse,zlib-deflate,lz4,lzma;
```

If lldb knows how to use any of these compression algorithms, it can ask that this
compression mode be enabled.
```
QEnableCompression:type:zlib-deflate;
```

The debug stub should reply with an uncompressed `OK` packet to indicate that the
request was accepted.  All further packets the stub sends will use this compression.

Packets are compressed as the last step before they are sent from the stub, and
decompressed as the first step after they are received.  The packet format in compressed
mode becomes one of two:
```
$N<uncompressed payload>#00

$C<size of uncompressed payload in base 10>:<compressed payload>#00
```

Where `#00` is the actual checksum value if noack mode is not enabled. The checksum
value is for the `N<uncompressed payload>` or
`C<size of uncompressed payload in base 10>:<compressed payload>` bytes in the packet.

The size of the uncompressed payload in base 10 is provided because it will simplify
decompression if the final buffer size needed is known ahead of time.

Compression on low-latency connections is unlikely to be an improvement. Particularly
when the debug stub and lldb are running on the same host. It should only be used
for slow connections, and likely only for larger packets.

Example compression algorithms that may be used include:
* `zlib-deflate` -
  The raw DEFLATE format as described in IETF RFC 1951.  With the ZLIB library, you
  can compress to this format with an initialization like
      deflateInit2 (&stream, 5, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY)
  and you can decompress with an initialization like
      inflateInit2 (&stream, -15).
* `lz4` -
  https://en.wikipedia.org/wiki/LZ4_(compression_algorithm)
  https://github.com/Cyan4973/lz4
  The libcompression APIs on darwin systems call this `COMPRESSION_LZ4_RAW`.
* `lzfse` -
  Compression algorithm added in macOS 10.11, with open source C reference
  implementation on github.
  https://en.wikipedia.org/wiki/LZFSE
  https://github.com/lzfse/lzfse
* `lzma` -
  libcompression implements "LZMA level 6", the default compression for the
  open source LZMA implementation.


## QEnableErrorStrings

This packet enables reporting of Error strings in remote packet
replies from the server to client. If the server supports this
feature, it should send an OK response.

```
send packet: $QEnableErrorStrings
read packet: $OK#00
```

The client can expect the following error replies if this feature is enabled in
the server:
```
EXX;AAAAAAAAA
```
where `AAAAAAAAA` will be a hex encoded ASCII string.
`XX`` is hex encoded byte number.

It must be noted that even if the client has enabled reporting
strings in error replies, it must not expect error strings to all
error replies.

**Priority To Implement:** Low. Only needed if the remote target wants to
provide strings that are human readable along with an error code.

## QLaunchArch

Set the architecture to use when launching a process for hosts that can run
multiple architecture slices that are contained in a single universal program
file.

```
send packet: $QLaunchArch:<architecture>
```

The response is `OK` if the value in `<architecture>` was recognised as valid
and will be used for the next launch request. `E63` if not.

**Priority To Implement:** Only required for hosts that support program files
that contain code for multiple architectures.

## QListThreadsInStopReply

Enable the `threads:` and `thread-pcs:` data in the question-mark packet
("T packet") responses when the stub reports that a program has
stopped executing.

```
send packet: QListThreadsInStopReply
read packet: OK
```

**Priority To Implement:** Performance.  This is a performance benefit to lldb
if the thread id's and thread pc values are provided to lldb in the T stop packet
-- if they are not provided to lldb, lldb will likely need to send one to
two packets per thread to fetch the data at every private stop.

## QRestoreRegisterState:\<save_id\> / QRestoreRegisterState:\<save_id\>;thread:XXXX;

The `QRestoreRegisterState` packet tells the remote debugserver to
restore all registers using the `save_id` which is an unsigned
integer that was returned from a previous call to
`QSaveRegisterState`. The restoration process can only be done once
as the data backing the register state will be freed upon the
completion of the `QRestoreRegisterState` command.

If thread suffixes are enabled the second form of this packet is
used, otherwise the first form is used.

The response is either:
* `OK` - if all registers were successfully restored
* `EXX` - for any errors

**Priority To Implement:** Low, this is mostly a convenience packet to avoid
having to send all registers with a `g` packet. It should only be implemented
if support for the `QSaveRegisterState` is added.

## QSaveRegisterState / QSaveRegisterState;thread:XXXX;

The `QSaveRegisterState` packet tells the remote debugserver to save
all registers and return a non-zero unique integer ID that
represents these save registers. If thread suffixes are enabled the
second form of this packet is used, otherwise the first form is
used. This packet is called prior to executing an expression, so
the remote GDB server should do anything it needs to in order to
ensure the registers that are saved are correct. On macOS this
involves calling `thread_abort_safely(mach_port_t thread)` to
ensure we get the correct registers for a thread in case it is
currently having code run on its behalf in the kernel.

The response is either:
* `<unsigned int>` - The save_id result is a non-zero unsigned integer value
                 that can be passed back to the GDB server using a
                 `QRestoreRegisterState` packet to restore the registers
                 one time.
* `EXX` - or an error code in the form of `EXX` where `XX` is a
          hex error code.

**Priority To Implement:** Low, this is mostly a convenience packet to avoid
having to send all registers with a `g` packet. It should only be implemented if
support for the `QRestoreRegisterState` is added.

## QSetDetachOnError

Sets what the server should do when the communication channel with LLDB
goes down. Either kill the inferior process (`0`) or remove breakpoints and
detach (`1`).

The data in this packet is a single a character, which should be `0` if the
inferior process should be killed, or `1` if the server should remove all
breakpoints and detach from the inferior.

**Priority To Implement:** Low. Only required if the target wants to keep the
inferior process alive when the communication channel goes down.

## QSetDisableASLR:\<bool\>

Enable or disable ASLR on the next "A" packet.

Or control if ASLR is enabled/disabled:
```
send packet: QSetDisableASLR:1
read packet: OK

send packet: QSetDisableASLR:0
read packet: OK
```
This packet must be sent  _prior_ to sending a "A" packet.

**Priority To Implement:** Low. Only needed if the remote target wants to launch
a target after making a connection to a GDB server that isn't already connected to
an inferior process and if the target supports disabling ASLR
(Address space layout randomization).

## QSetSTDIN:\<ascii-hex-path\> / QSetSTDOUT:\<ascii-hex-path\> / QSetSTDERR:\<ascii-hex-path\>

Setup where STDIN, STDOUT, and STDERR go prior to sending an "A"
packet.

When launching a program through the GDB remote protocol with the "A" packet,
you might also want to specify where stdin/out/err go:
```
QSetSTDIN:<ascii-hex-path>
QSetSTDOUT:<ascii-hex-path>
QSetSTDERR:<ascii-hex-path>
```
These packets must be sent  _prior_ to sending a "A" packet.

**Priority To Implement:** Low. Only needed if the remote target wants to launch
a target after making a connection to a GDB server that isn't already connected to
an inferior process.

## QSetWorkingDir:\<ascii-hex-path\>

Set the working directory prior to sending an "A" packet.

Or specify the working directory:
```
QSetWorkingDir:<ascii-hex-path>
```
This packet must be sent  _prior_ to sending a "A" packet.

**Priority To Implement:** Low. Only needed if the remote target wants to launch
a target after making a connection to a GDB server that isn't already connected to
an inferior process.

## QStartNoAckMode

Try to enable no ACK mode to skip sending ACKs and NACKs.

Having to send an ACK/NACK after every packet slows things down a bit, so we
have a way to disable ACK packets to minimize the traffic for reliable
communication interfaces (like sockets). Below GDB or LLDB will send this
packet to try and disable ACKs. All lines that start with "send packet: " are
from GDB/LLDB, and all lines that start with "read packet: " are from the GDB
remote server:
```
send packet: $QStartNoAckMode#b0
read packet: +
read packet: $OK#9a
send packet: +
```

**Priority To Implement:** High. Any GDB remote server that can implement this
should if the connection is reliable. This improves packet throughput and increases
the performance of the connection.

## QSupported

Query the GDB remote server for features it supports

QSupported is a standard GDB Remote Serial Protocol packet, but
there are several additions to the response that lldb can parse.
They are not all listed here.

An example exchange:
```
send packet: qSupported:xmlRegisters=i386,arm,mips,arc;multiprocess+;fork-events+;vfork-events+

read packet: qXfer:features:read+;PacketSize=20000;qEcho+;native-signals+;SupportedCompressions=lzfse,zlib-deflate,lz4,lzma;SupportedWatchpointTypes=aarch64-mask,aarch64-bas;
```

In the example above, three lldb extensions are shown:

  * `PacketSize=20000`
    * The base 16 maximum packet size that the stub can handle.
  * `SupportedCompressions=<item,item,...>`
    * A list of compression types that the stub can use to compress packets
    when the QEnableCompression packet is used to request one of them.
  * `SupportedWatchpointTypes=<item,item,...>`
    * A list of watchpoint types that this stub can manage. Currently defined 
      names are:
        * `x86_64` - 64-bit x86-64 watchpoints (1, 2, 4, 8 byte watchpoints
          aligned to those amounts)
        * `aarch64-bas`  AArch64 Byte Address Select watchpoints
                     (any number of contiguous bytes within a doubleword)
        * `aarch64-mask` AArch64 MASK watchpoints
                     (any power-of-2 region of memory from 8 to 2GB, aligned)

      If nothing is specified, lldb will default to sending power-of-2
      watchpoints, up to a pointer size, `sizeof(void*)`, a reasonable
      baseline assumption.

**Priority To Implement:** Optional

## QThreadSuffixSupported

Try to enable thread suffix support for the `g`, `G`, `p`, and `P` packets.

When reading thread registers, you currently need to set the current
thread, then read the registers. This is kind of cumbersome, so we added the
ability to query if the remote GDB server supports adding a `thread:<tid>;`
suffix to all packets that request information for a thread. To test if the
remote GDB server supports this feature:
```
send packet: $QThreadSuffixSupported#00
read packet: OK
```

If `OK` is returned, then the `g`, `G`, `p` and `P` packets can accept a
thread suffix. So to send a `g` packet (read all register values):
```
send packet: $g;thread:<tid>;#00
read packet: ....

send packet: $G;thread:<tid>;#00
read packet: ....

send packet: $p1a;thread:<tid>;#00
read packet: ....

send packet: $P1a=1234abcd;thread:<tid>;#00
read packet: ....
```

otherwise, without this you would need to always send two packets:
```
send packet: $Hg<tid>#00
read packet: ....
send packet: $g#00
read packet: ....
```

We also added support for allocating and deallocating memory. We use this to
allocate memory so we can run JITed code.

**Priority To Implement:** High

Adding a thread suffix allows us to read and write registers
more efficiently and stops us from having to select a thread with
one packet and then read registers with a second packet. It also
makes sure that no errors can occur where the debugger thinks it
already has a thread selected (see the `Hg` packet from the standard
GDB remote protocol documentation) yet the remote GDB server actually
has another thread selected.

## qAttachOrWaitSupported

This is a binary "is it supported" query. Return OK if you support
`vAttachOrWait`.

**Priority To Implement:** Low. This is required if you support `vAttachOrWait`,
otherwise no support is needed since the standard "I don't recognize this packet"
response will do the right thing.

## qFileLoadAddress:\<file_path\>

Get the load address of a memory mapped file.
The load address is defined as the address of the first memory
region what contains data mapped from the specified file.

The response is either:
* `<unsigned-hex64>` - Load address of the file in big endian encoding
* `E01` - the requested file isn't loaded
* `EXX` - for any other errors

**Priority To Implement:** Low, required if dynamic linker don't fill in the load
address of some object file in the rendezvous data structure.

## qfProcessInfo / qsProcessInfo (Platform Extension)

Get the first process info (`qfProcessInfo`) or subsequent process
info (`qsProcessInfo`) for one or more processes on the remote
platform. The first call gets the first match and subsequent calls
to `qsProcessInfo` gets the subsequent matches. Return an error `EXX`,
where `XX` are two hex digits, when no more matches are available.

The `qfProcessInfo` packet can be followed by a `:` and
some key value pairs. The key value pairs in the command are:
* `name` - `ascii-hex` -
  An ASCII hex string that contains the name of the process that will be matched.
* `name_match` - `enum` -
  One of:
    * `equals`
    * `starts_with`
    * `ends_with`
    * `contains`
    * `regex`
* `pid` - `integer`- A string value containing the decimal process ID
* `parent_pid` - `integer` - A string value containing the decimal parent process ID
* `uid` - `integer` - A string value containing the decimal user ID
* `gid` - `integer` - A string value containing the decimal group ID
* `euid` - `integer` - A string value containing the decimal effective user ID
* `egid` - `integer` - A string value containing the decimal effective group ID
* `all_users` - `bool` -
  A boolean value that specifies if processes should
  be listed for all users, not just the user that the
  platform is running as
* `triple` - `string` -
  An ASCII triple string (`x86_64`, `x86_64-apple-macosx`, `armv7-apple-ios`)
* `args` - `string` -
  A string value containing the process arguments separated by the character `-`,
  where each argument is hex-encoded. It includes `argv[0]`.

The response consists of key/value pairs where the key is separated from the
values with colons and each pair is terminated with a semi colon. For a list
of the key/value pairs in the response see the `qProcessInfoPID` packet
documentation.

Sample packet/response:
```
send packet: $qfProcessInfo#00
read packet: $pid:60001;ppid:59948;uid:7746;gid:11;euid:7746;egid:11;name:6c6c6462;triple:x86_64-apple-macosx;#00
send packet: $qsProcessInfo#00
read packet: $pid:59992;ppid:192;uid:7746;gid:11;euid:7746;egid:11;name:6d64776f726b6572;triple:x86_64-apple-macosx;#00
send packet: $qsProcessInfo#00
read packet: $E04#00
```

**Priority To Implement:** Required


## qGDBServerVersion

Get version information about this implementation of the gdb-remote
protocol.

The goal of this packet is to provide enough information about an
implementation of the gdb-remote-protocol server that lldb can
work around implementation problems that are discovered after the
version has been released/deployed.  The name and version number
should be sufficiently unique that lldb can unambiguously identify
the origin of the program (for instance, debugserver from lldb) and
the version/submission number/patch level of the program - whatever
is appropriate for your server implementation.

The packet follows the key-value pair model, semicolon separated.
```
send packet: $qGDBServerVersion#00
read packet: $name:debugserver;version:310.2;#00
```

Other clients may find other key-value pairs to be useful for identifying
a gdb stub.  Patch level, release name, build number may all be keys that
better describe your implementation's version.

Suggested key names:
* `name`: the name of your remote server - "debugserver" is the lldb standard
          implementation
* `version`: identifies the version number of this server
* `patch_level`: the patch level of this server
* `release_name`: the name of this release, if your project uses names
* `build_number`: if you use a build system with increasing build numbers,
                  this may be the right key name for your server
* `major_version`: major version number
* `minor_version`: minor version number

**Priority To Implement:** High. This packet is usually very easy to implement
and can help LLDB to work around bugs in a server's implementation when they
are found.

## qGetWorkingDir

Get the current working directory of the platform stub in
ASCII hex encoding.

```
receive: qGetWorkingDir
send:    2f4170706c65496e7465726e616c2f6c6c64622f73657474696e67732f342f5465737453657474696e67732e746573745f646973617373656d626c65725f73657474696e6773
```

## qHostInfo

Get information about the host we are remotely connected to.

LLDB supports a host info call that gets all sorts of details of the system
that is being debugged:
```
send packet: $qHostInfo#00
read packet: $cputype:16777223;cpusubtype:3;ostype:darwin;vendor:apple;endian:little;ptrsize:8;#00
```

Key value pairs are one of:
* `cputype`: is a number that is the mach-o CPU type that is being debugged (base 10)
* `cpusubtype`: is a number that is the mach-o CPU subtype type that is being debugged (base 10)
* `triple`: a string for the target triple (x86_64-apple-macosx) that can be used to specify arch + vendor + os in one entry
* `vendor`: a string for the vendor (apple), not needed if "triple" is specified
* `ostype`: a string for the OS being debugged (macosx, linux, freebsd, ios, watchos), not needed if "triple" is specified
* `endian`: is one of "little", "big", or "pdp"
* `ptrsize`: an unsigned number that represents how big pointers are in bytes on the debug target
* `hostname`: the hostname of the host that is running the GDB server if available
* `os_build`: a string for the OS build for the remote host as a string value
* `os_kernel`: a string describing the kernel version
* `os_version`: a version string that represents the current OS version (10.8.2)
* `watchpoint_exceptions_received`: one of "before" or "after" to specify if a watchpoint is triggered before or after the pc when it stops
* `default_packet_timeout`: an unsigned number that specifies the default timeout in seconds
* `distribution_id`: optional. For linux, specifies distribution id (e.g. ubuntu, fedora, etc.)
* `osmajor`: optional, specifies the major version number of the OS (e.g. for macOS 10.12.2, it would be 10)
* `osminor`: optional, specifies the minor version number of the OS (e.g. for macOS 10.12.2, it would be 12)
* `ospatch`: optional, specifies the patch level number of the OS (e.g. for macOS 10.12.2, it would be 2)
* `vm-page-size`: optional, specifies the target system VM page size, base 10.
  Needed for the "dirty-pages:" list in the qMemoryRegionInfo
  packet, where a list of dirty pages is sent from the remote
  stub.  This page size tells lldb how large each dirty page is.
* `addressing_bits`: optional, specifies how many bits in addresses are
	significant for addressing, base 10.  If bits 38..0
	in a 64-bit pointer are significant for addressing,
	then the value is 39.  This is needed on e.g. AArch64
	v8.3 ABIs that use pointer authentication, so lldb
	knows which bits to clear/set to get the actual
	addresses.
* `low_mem_addressing_bits`: optional, specifies how many bits in
  addresses in low memory are significant for addressing, base 10.
  AArch64 can have different page table setups for low and high
  memory, and therefore a different number of bits used for addressing.
* `high_mem_addressing_bits`: optional, specifies how many bits in
  addresses in high memory are significant for addressing, base 10.
  AArch64 can have different page table setups for low and high
  memory, and therefore a different number of bits used for addressing.

**Priority To Implement:** High. This packet is usually very easy to implement
and can help LLDB select the correct plug-ins for the job based on the target
triple information that is supplied.

## qKillSpawnedProcess (Platform Extension)

Kill a process running on the target system.

```
receive: qKillSpawnedProcess:1337
send:    OK
```
The request packet has the process ID in base 10.

## qLaunchGDBServer (Platform Extension)

Have the remote platform launch a GDB server.

The `qLaunchGDBServer` packet must be followed by a `:` and
some key value pairs. The key value pairs in the command are:
* `port` - `integer` -
  A string value containing the decimal port ID or zero if the port should be
  bound and returned
* `host` - `integer` -
  The host that connections should be limited to when the GDB server is connected to.

Sample packet/response:
```
send packet: $qLaunchGDBServer:port:0;host:lldb.apple.com;#00
read packet: $pid:60025;port:50776;#00
```

The `pid` key/value pair is only specified if the remote platform launched
a separate process for the GDB remote server and can be omitted if no
process was separately launched.

The `port` key/value pair in the response lets clients know what port number
to attach to in case zero was specified as the "port" in the sent command.

**Priority To Implement:** Required


## qLaunchSuccess

Check whether launching a process with the `A` packet succeeded.

Returns the status of the last attempt to launch a process.
Either `OK` if no error ocurred, or `E` followed by a string
describing the error.

**Priority To Implement:** High, launching processes is a key part of LLDB's
platform mode.

## qMemoryRegionInfo:\<addr\>

Get information about the address range that contains `<addr>`.

We added a way to get information for a memory region. The packet is:
```
qMemoryRegionInfo:<addr>
```

Where `<addr>` is a big endian hex address. The response is returned in a series
of tuples like the data returned in a stop reply packet. The currently valid
tuples to return are:
* `start:<start-addr>;` - `<start-addr>` is a big endian hex address that is
                          the start address of the range that contains `<addr>`
* `size:<size>;` - `<size>` is a big endian hex byte size of the address
                   of the range that contains `<addr>`
* `permissions:<permissions>;` - `<permissions>` is a string that contains one
                                 or more of the characters from `rwx`
* `name:<name>;` - `<name>` is a hex encoded string that contains the name of
                   the memory region mapped at the given address. In case of
                   regions backed by a file it have to be the absolute path of
                   the file while for anonymous regions it have to be the name
                   associated to the region if that is available.
* `flags:<flags-string>;` - where `<flags-string>` is a space separated string
                            of flag names. Currently the only supported flag
                            is `mt` for AArch64 memory tagging. lldb will
                            ignore any other flags in this field.
* `type:[<type>][,<type>];` - memory types that apply to this region, e.g.
                              `stack` for stack memory.
* `error:<ascii-byte-error-string>;` - where `<ascii-byte-error-string>` is
                                       a hex encoded string value that
                                       contains an error string
* `dirty-pages:[<hexaddr>][,<hexaddr];` -
  A list of memory pages within this
  region that are "dirty" -- they have been modified.
  Page addresses are in base 16. The size of a page can
  be found from the `qHostInfo`'s `page-size` key-value.
  
  If the stub supports identifying dirty pages within a
  memory region, this key should always be present for all
  `qMemoryRegionInfo` replies.  This key with no pages
  listed (`dirty-pages:;`) indicates no dirty pages in
  this memory region.  The *absence* of this key means
  that this stub cannot determine dirty pages.

If the address requested is not in a mapped region (e.g. we've jumped through
a NULL pointer and are at 0x0) currently lldb expects to get back the size
of the unmapped region -- that is, the distance to the next valid region.
For instance, with a macOS process which has nothing mapped in the first
4GB of its address space, if we're asking about address 0x2:
```
  qMemoryRegionInfo:2
  start:2;size:fffffffe;
```

The lack of `permissions:` indicates that none of read/write/execute are valid
for this region.

**Priority To Implement:** Medium

This is nice to have, but it isn't necessary. It helps LLDB
do stack unwinding when we branch into memory that isn't executable.
If we can detect that the code we are stopped in isn't executable,
then we can recover registers for stack frames above the current
frame. Otherwise we must assume we are in some JIT'ed code (not JIT
code that LLDB has made) and assume that no registers are available
in higher stack frames.

## qModuleInfo:\<module_path\>;\<arch triple\>

Get information for a module by given module path and architecture.

The response is either:
* `(uuid|md5):...;triple:...;file_offset:...;file_size...;`
* `EXX` - for any errors

**Priority To Implement:** Optional, required if dynamic loader cannot fetch
module's information like UUID directly from inferior's memory.

## qPathComplete (Platform Extension)

Get a list of matched disk files/directories by passing a boolean flag
and a partial path.

```
receive: qPathComplete:0,6d61696e
send:    M6d61696e2e637070
receive: qPathComplete:1,746573
send:    M746573742f,74657374732f
```

If the first argument is zero, the result should contain all
files (including directories) starting with the given path. If the
argument is one, the result should contain only directories.

The result should be a comma-separated list of hex-encoded paths.
Paths denoting a directory should end with a directory separator (`/` or `\`.


## qPlatform_mkdir

Creates a new directory on the connected remote machine.

Request: `qPlatform_mkdir:<hex-file-mode>,<ascii-hex-path>`

The request packet has the fields:
   1. mode bits in base 16
   2. file path in ascii-hex encoding

Reply: 
  * `F<mkdir-return-code>`
    (mkdir called successfully and returned with the given return code)
  * `Exx` (An error occurred)

**Priority To Implement:** Low

## qPlatform_shell

Run a command in a shell on the connected remote machine.

The request consists of the command to be executed encoded in ASCII characters
converted into hex bytes.

The response to this packet consists of the letter F followed by the return code,
followed by the signal number (or 0 if no signal was delivered), and escaped bytes
of captured program output.

Below is an example communication from a client sending an "ls -la" command:
```
send packet: $qPlatform_shell:6c73202d6c61,00000002#ec
read packet: $F,00000000,00000000,total 4736
drwxrwxr-x 16 username groupname    4096 Aug 15 21:36 .
drwxr-xr-x 17 username groupname    4096 Aug 10 16:39 ..
-rw-rw-r--  1 username groupname   73875 Aug 12 16:46 notes.txt
drwxrwxr-x  5 username groupname    4096 Aug 15 21:36 source.cpp
-rw-r--r--  1 username groupname    2792 Aug 12 16:46 a.out
-rw-r--r--  1 username groupname    3190 Aug 12 16:46 Makefile
```

**Priority To Implement:** High

## qProcessInfo

Get information about the process we are currently debugging.

**Priority To Implement:** Medium

On systems which can launch multiple different architecture processes,
the qHostInfo may not disambiguate sufficiently to know what kind of
process is being debugged.

For example on a 64-bit x86 Mac system both 32-bit and 64-bit user processes are possible,
and with Mach-O universal files, the executable file may contain both 32- and
64-bit slices so it may be impossible to know until you're attached to a real
process to know what you're working with.

All numeric fields return base 16 numbers without any "0x" prefix.

An i386 process:
```
send packet: $qProcessInfo#00
read packet: $pid:42a8;parent-pid:42bf;real-uid:ecf;real-gid:b;effective-uid:ecf;effective-gid:b;cputype:7;cpusubtype:3;ostype:macosx;vendor:apple;endian:little;ptrsize:4;#00
```

An x86_64 process:
```
send packet: $qProcessInfo#00
read packet: $pid:d22c;parent-pid:d34d;real-uid:ecf;real-gid:b;effective-uid:ecf;effective-gid:b;cputype:1000007;cpusubtype:3;ostype:macosx;vendor:apple;endian:little;ptrsize:8;#00
```

Key value pairs include:
* `pid`: the process id
* `parent-pid`: the process of the parent process (often debugserver will become the parent when attaching)
* `real-uid`: the real user id of the process
* `real-gid`: the real group id of the process
* `effective-uid`: the effective user id of the process
* `effective-gid`: the effective group id of the process
* `cputype`: the Mach-O CPU type of the process  (base 16)
* `cpusubtype`: the Mach-O CPU subtype of the process  (base 16)
* `ostype`: is a string the represents the OS being debugged (darwin, linux, freebsd)
* `vendor`: is a string that represents the vendor (apple)
* `endian`: is one of "little", "big", or "pdp"
* `ptrsize`: is a number that represents how big pointers are in bytes
* `main-binary-uuid`: is the UUID of a firmware type binary that the gdb stub knows about
* `main-binary-address`: is the load address of the firmware type binary
* `main-binary-slide`: is the slide of the firmware type binary, if address isn't known
* `binary-addresses`: A comma-separated list of binary load addresses base 16.
                      lldb will parse the binaries in memory to get UUIDs, then
                      try to find the binaries & debug info by UUID.  Intended for
                      use with a small number of firmware type binaries where the
                      search for binary/debug info may be expensive.

## qProcessInfoPID:PID (Platform Extension)

Have the remote platform get detailed information on a process by
ID. PID is specified as a decimal integer.

The response consists of key/value pairs where the key is separated from the
values with colons and each pair is terminated with a semi colon.

The key value pairs in the response are:
* `pid` - `integer` - Process ID as a decimal integer string
* `ppid` - `integer` - Parent process ID as a decimal integer string
* `uid` - `integer` - A string value containing the decimal user ID
* `gid` - `integer` - A string value containing the decimal group ID
* `euid` - `integer` - A string value containing the decimal effective user ID
* `egid` - `integer` - A string value containing the decimal effective group ID
* `name` - `ascii-hex` - An ASCII hex string that contains the name of the process
* `triple` - `string` - A target triple (`x86_64-apple-macosx`, `armv7-apple-ios`)

Sample packet/response:
```
send packet: $qProcessInfoPID:60050#00
read packet: $pid:60050;ppid:59948;uid:7746;gid:11;euid:7746;egid:11;name:6c6c6462;triple:x86_64-apple-macosx;#00
```

**Priority To Implement:** Optional

## qQueryGDBServer

Ask the platform for the list of gdbservers we have to connect

If the remote platform automatically started one or more gdbserver instance (without
lldb asking it) then it have to return the list of port number or socket name for
each of them what can be used by lldb to connect to those instances.

The data in this packet is a JSON array of JSON objects with the following keys:
* `port`: `<the port number to connect>` (optional)
* `socket_name`: `<the name of the socket to connect>` (optional)

Example packet:
```
[
    { "port": 1234 },
    { "port": 5432 },
    { "socket_name": "foo" }
]
```

**Priority To Implement:** Low

The packet is required to support connecting to gdbserver started
by the platform instance automatically.

## qRegisterInfo\<hex-reg-id\>

Discover register information from the remote GDB server.

With LLDB, for register information, remote GDB servers can add
support for the "qRegisterInfoN" packet where "N" is a zero based
base 16 register number that must start at zero and increase by one
for each register that is supported.  The response is done in typical
GDB remote fashion where a series of "KEY:VALUE;" pairs are returned.
An example for the x86_64 registers is included below:
```
send packet: $qRegisterInfo0#00
read packet: $name:rax;bitsize:64;offset:0;encoding:uint;format:hex;set:General Purpose Registers;gcc:0;dwarf:0;#00
send packet: $qRegisterInfo1#00
read packet: $name:rbx;bitsize:64;offset:8;encoding:uint;format:hex;set:General Purpose Registers;gcc:3;dwarf:3;#00
send packet: $qRegisterInfo2#00
read packet: $name:rcx;bitsize:64;offset:16;encoding:uint;format:hex;set:General Purpose Registers;gcc:2;dwarf:2;#00
send packet: $qRegisterInfo3#00
read packet: $name:rdx;bitsize:64;offset:24;encoding:uint;format:hex;set:General Purpose Registers;gcc:1;dwarf:1;#00
send packet: $qRegisterInfo4#00
read packet: $name:rdi;bitsize:64;offset:32;encoding:uint;format:hex;set:General Purpose Registers;gcc:5;dwarf:5;#00
send packet: $qRegisterInfo5#00
read packet: $name:rsi;bitsize:64;offset:40;encoding:uint;format:hex;set:General Purpose Registers;gcc:4;dwarf:4;#00
send packet: $qRegisterInfo6#00
read packet: $name:rbp;alt-name:fp;bitsize:64;offset:48;encoding:uint;format:hex;set:General Purpose Registers;gcc:6;dwarf:6;generic:fp;#00
send packet: $qRegisterInfo7#00
read packet: $name:rsp;alt-name:sp;bitsize:64;offset:56;encoding:uint;format:hex;set:General Purpose Registers;gcc:7;dwarf:7;generic:sp;#00
send packet: $qRegisterInfo8#00
read packet: $name:r8;bitsize:64;offset:64;encoding:uint;format:hex;set:General Purpose Registers;gcc:8;dwarf:8;#00
send packet: $qRegisterInfo9#00
read packet: $name:r9;bitsize:64;offset:72;encoding:uint;format:hex;set:General Purpose Registers;gcc:9;dwarf:9;#00
send packet: $qRegisterInfoa#00
read packet: $name:r10;bitsize:64;offset:80;encoding:uint;format:hex;set:General Purpose Registers;gcc:10;dwarf:10;#00
send packet: $qRegisterInfob#00
read packet: $name:r11;bitsize:64;offset:88;encoding:uint;format:hex;set:General Purpose Registers;gcc:11;dwarf:11;#00
send packet: $qRegisterInfoc#00
read packet: $name:r12;bitsize:64;offset:96;encoding:uint;format:hex;set:General Purpose Registers;gcc:12;dwarf:12;#00
send packet: $qRegisterInfod#00
read packet: $name:r13;bitsize:64;offset:104;encoding:uint;format:hex;set:General Purpose Registers;gcc:13;dwarf:13;#00
send packet: $qRegisterInfoe#00
read packet: $name:r14;bitsize:64;offset:112;encoding:uint;format:hex;set:General Purpose Registers;gcc:14;dwarf:14;#00
send packet: $qRegisterInfof#00
read packet: $name:r15;bitsize:64;offset:120;encoding:uint;format:hex;set:General Purpose Registers;gcc:15;dwarf:15;#00
send packet: $qRegisterInfo10#00
read packet: $name:rip;alt-name:pc;bitsize:64;offset:128;encoding:uint;format:hex;set:General Purpose Registers;gcc:16;dwarf:16;generic:pc;#00
send packet: $qRegisterInfo11#00
read packet: $name:rflags;alt-name:flags;bitsize:64;offset:136;encoding:uint;format:hex;set:General Purpose Registers;#00
send packet: $qRegisterInfo12#00
read packet: $name:cs;bitsize:64;offset:144;encoding:uint;format:hex;set:General Purpose Registers;#00
send packet: $qRegisterInfo13#00
read packet: $name:fs;bitsize:64;offset:152;encoding:uint;format:hex;set:General Purpose Registers;#00
send packet: $qRegisterInfo14#00
read packet: $name:gs;bitsize:64;offset:160;encoding:uint;format:hex;set:General Purpose Registers;#00
send packet: $qRegisterInfo15#00
read packet: $name:fctrl;bitsize:16;offset:176;encoding:uint;format:hex;set:Floating Point Registers;#00
send packet: $qRegisterInfo16#00
read packet: $name:fstat;bitsize:16;offset:178;encoding:uint;format:hex;set:Floating Point Registers;#00
send packet: $qRegisterInfo17#00
read packet: $name:ftag;bitsize:8;offset:180;encoding:uint;format:hex;set:Floating Point Registers;#00
send packet: $qRegisterInfo18#00
read packet: $name:fop;bitsize:16;offset:182;encoding:uint;format:hex;set:Floating Point Registers;#00
send packet: $qRegisterInfo19#00
read packet: $name:fioff;bitsize:32;offset:184;encoding:uint;format:hex;set:Floating Point Registers;#00
send packet: $qRegisterInfo1a#00
read packet: $name:fiseg;bitsize:16;offset:188;encoding:uint;format:hex;set:Floating Point Registers;#00
send packet: $qRegisterInfo1b#00
read packet: $name:fooff;bitsize:32;offset:192;encoding:uint;format:hex;set:Floating Point Registers;#00
send packet: $qRegisterInfo1c#00
read packet: $name:foseg;bitsize:16;offset:196;encoding:uint;format:hex;set:Floating Point Registers;#00
send packet: $qRegisterInfo1d#00
read packet: $name:mxcsr;bitsize:32;offset:200;encoding:uint;format:hex;set:Floating Point Registers;#00
send packet: $qRegisterInfo1e#00
read packet: $name:mxcsrmask;bitsize:32;offset:204;encoding:uint;format:hex;set:Floating Point Registers;#00
send packet: $qRegisterInfo1f#00
read packet: $name:stmm0;bitsize:80;offset:208;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:33;dwarf:33;#00
send packet: $qRegisterInfo20#00
read packet: $name:stmm1;bitsize:80;offset:224;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:34;dwarf:34;#00
send packet: $qRegisterInfo21#00
read packet: $name:stmm2;bitsize:80;offset:240;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:35;dwarf:35;#00
send packet: $qRegisterInfo22#00
read packet: $name:stmm3;bitsize:80;offset:256;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:36;dwarf:36;#00
send packet: $qRegisterInfo23#00
read packet: $name:stmm4;bitsize:80;offset:272;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:37;dwarf:37;#00
send packet: $qRegisterInfo24#00
read packet: $name:stmm5;bitsize:80;offset:288;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:38;dwarf:38;#00
send packet: $qRegisterInfo25#00
read packet: $name:stmm6;bitsize:80;offset:304;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:39;dwarf:39;#00
send packet: $qRegisterInfo26#00
read packet: $name:stmm7;bitsize:80;offset:320;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:40;dwarf:40;#00
send packet: $qRegisterInfo27#00
read packet: $name:xmm0;bitsize:128;offset:336;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:17;dwarf:17;#00
send packet: $qRegisterInfo28#00
read packet: $name:xmm1;bitsize:128;offset:352;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:18;dwarf:18;#00
send packet: $qRegisterInfo29#00
read packet: $name:xmm2;bitsize:128;offset:368;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:19;dwarf:19;#00
send packet: $qRegisterInfo2a#00
read packet: $name:xmm3;bitsize:128;offset:384;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:20;dwarf:20;#00
send packet: $qRegisterInfo2b#00
read packet: $name:xmm4;bitsize:128;offset:400;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:21;dwarf:21;#00
send packet: $qRegisterInfo2c#00
read packet: $name:xmm5;bitsize:128;offset:416;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:22;dwarf:22;#00
send packet: $qRegisterInfo2d#00
read packet: $name:xmm6;bitsize:128;offset:432;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:23;dwarf:23;#00
send packet: $qRegisterInfo2e#00
read packet: $name:xmm7;bitsize:128;offset:448;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:24;dwarf:24;#00
send packet: $qRegisterInfo2f#00
read packet: $name:xmm8;bitsize:128;offset:464;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:25;dwarf:25;#00
send packet: $qRegisterInfo30#00
read packet: $name:xmm9;bitsize:128;offset:480;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:26;dwarf:26;#00
send packet: $qRegisterInfo31#00
read packet: $name:xmm10;bitsize:128;offset:496;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:27;dwarf:27;#00
send packet: $qRegisterInfo32#00
read packet: $name:xmm11;bitsize:128;offset:512;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:28;dwarf:28;#00
send packet: $qRegisterInfo33#00
read packet: $name:xmm12;bitsize:128;offset:528;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:29;dwarf:29;#00
send packet: $qRegisterInfo34#00
read packet: $name:xmm13;bitsize:128;offset:544;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:30;dwarf:30;#00
send packet: $qRegisterInfo35#00
read packet: $name:xmm14;bitsize:128;offset:560;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:31;dwarf:31;#00
send packet: $qRegisterInfo36#00
read packet: $name:xmm15;bitsize:128;offset:576;encoding:vector;format:vector-uint8;set:Floating Point Registers;gcc:32;dwarf:32;#00
send packet: $qRegisterInfo37#00
read packet: $name:trapno;bitsize:32;offset:696;encoding:uint;format:hex;set:Exception State Registers;#00
send packet: $qRegisterInfo38#00
read packet: $name:err;bitsize:32;offset:700;encoding:uint;format:hex;set:Exception State Registers;#00
send packet: $qRegisterInfo39#00
read packet: $name:faultvaddr;bitsize:64;offset:704;encoding:uint;format:hex;set:Exception State Registers;#00
send packet: $qRegisterInfo3a#00
read packet: $E45#00
```

As we see above we keep making subsequent calls to the remote server to
discover all registers by increasing the number appended to `qRegisterInfo` and
we get a response back that is a series of `key=value;` strings.

The `offset:` fields should not leave a gap anywhere in the g/G packet -- the
register values should be appended one after another.  For instance, if the
register context for a thread looks like:
```
struct rctx {
    uint32_t gpr1;  // offset 0
    uint32_t gpr2;  // offset 4
    uint32_t gpr3;  // offset 8
    uint64_t fp1;   // offset 16
};
```

You may end up with a 4-byte gap between gpr3 and fp1 on architectures
that align values like this.  The correct offset: value for fp1 is 12 -
in the g/G packet fp1 will immediately follow gpr3, even though the
in-memory thread structure has an empty 4 bytes for alignment between
these two registers.

The keys and values are detailed below:

* `name` -
  The primary register name as a string ("rbp" for example)
* `alt-name` -
  An alternate name for a register as a string ("fp" for example
  for the above "rbp")
* `bitsize` - Size in bits of a register (32, 64, etc).  Base 10.
* `offset` -
  The offset within the "g" and "G" packet of the register data for
  this register.  This is the byte offset once the data has been
  transformed into binary, not the character offset into the g/G
  packet.  Base 10.
* `encoding` -
  The encoding type of the register which must be one of:
  * `uint` (unsigned integer)
  * `sint` (signed integer)
  * `ieee754` (IEEE 754 float)
  * `vector` (vector register)
* format -
  The preferred format for display of this register. The value must be one of:
  * `binary`
  * `decimal`
  * `hex`
  * `float`
  * `vector-sint8`
  * `vector-uint8`
  * `vector-sint16`
  * `vector-uint16`
  * `vector-sint32`
  * `vector-uint32`
  * `vector-float32`
  * `vector-uint128`
* `set`-
  The register set name as a string that this register belongs to.
* `gcc` -
  The GCC compiler registers number for this register (used for
  EH frame and other compiler information that is encoded in the
  executable files). The supplied number will be decoded like a
  string passed to strtoul() with a base of zero, so the number
  can be decimal, or hex if it is prefixed with "0x".

  **Note:** If the compiler doesn't have a register number for this
  register, this key/value pair should be omitted.
* `dwarf` -
  The DWARF register number for this register that is used for this
  register in the debug information. The supplied number will be decoded
  like a string passed to strtoul() with a base of zero, so the number
  can be decimal, or hex if it is prefixed with "0x".

  **Note:** If the compiler doesn't have a register number for this
  register, this key/value pair should be omitted.
* `generic` -
  If the register is a generic register that most CPUs have, classify
  it correctly so the debugger knows. Valid values are one of:
  * `pc` (a program counter register. for example `name=eip;` (i386),
    `name=rip;` (x86_64), `name=r15;` (32 bit arm) would
    include a `generic=pc;` key value pair)
  * `sp` (a stack pointer register. for example `name=esp;` (i386),
    `name=rsp;` (x86_64), `name=r13;` (32 bit arm) would
    include a `generic=sp;` key value pair)
  * `fp` (a frame pointer register. for example `name=ebp;` (i386),
    `name=rbp;` (x86_64), `name=r7;` (32 bit arm with macosx
    ABI) would include a `generic=fp;` key value pair)
  * `ra` (a return address register. for example `name=lr;` (32 bit ARM)
    would include a `generic=ra;` key value pair)
  * `flags` (a CPU flags register. for example `name=eflags;` (i386),
    `name=rflags;` (x86_64), `name=cpsr;` (32 bit ARM)
    would include a `generic=flags;` key value pair)
  * `arg1` - `arg8` (specified for registers that contain function
    arguments when the argument fits into a register)
* `container-regs` -
  The value for this key is a comma separated list of raw hex (optional
  leading "0x") register numbers.

  This specifies that this register is contained in other concrete
  register values. For example "eax" is in the lower 32 bits of the
  "rax" register value for x86_64, so "eax" could specify that it is
  contained in "rax" by specifying the register number for "rax" (whose
  register number is 0x00):
  ```
  container-regs:00;
  ```
  If a register is comprised of one or more registers, like "d0" is ARM
  which is a 64 bit register, it might be made up of "s0" and "s1". If
  the register number for "s0" is 0x20, and the register number of "s1"
  is "0x21", the "container-regs" key/value pair would be:
  ```
  container-regs:20,21;
  ```
  This is handy for defining what GDB used to call "pseudo" registers.
  These registers are never requested by LLDB via the register read
  or write packets, the container registers will be requested on behalf
  of this register.
* `invalidate-regs` -
  The value for this key is a comma separated list of raw hex (optional
  leading "0x") register numbers.

  This specifies which register values should be invalidated when this
  register is modified. For example if modifying "eax" would cause "rax",
  "eax", "ax", "ah", and "al" to be modified where rax is 0x0, eax is 0x15,
  ax is 0x25, ah is 0x35, and al is 0x39, the "invalidate-regs" key/value
  pair would be:
  ```
  invalidate-regs:0,15,25,35,39;
  ```
  If there is a single register that gets invalidated, then omit the comma
  and just list a single register:
  ```
  invalidate-regs:0;
  ```
  This is handy when modifying a specific register can cause other
  register values to change. For example, when debugging an ARM target,
  modifying the CPSR register can cause the r8 - r14 and cpsr value to
  change depending on if the mode has changed.

**Priority To Implement:** High. Any target that can self describe its registers,
should do so. This means if new registers are ever added to a remote target, they
will get picked up automatically, and allows registers to change
depending on the actual CPU type that is used.

**Note:** `qRegisterInfo` is deprecated in favor of the standard gdb remote
serial protocol register description method, `qXfer:features:read:target.xml`.
If `qXfer:features:read:target.xml` is supported, `qRegisterInfo` does
not need to be implemented.  The target.xml format is used by most
gdb RSP stubs whereas `qRegisterInfo` was an lldb-only design.
`qRegisterInfo` requires one packet per register and can have undesirable
performance costs at the start of a debug session, whereas target.xml
may be able to describe all registers in a single packet.

## qShlibInfoAddr

Get an address where the dynamic linker stores information about
where shared libraries are loaded.

LLDB and GDB both support the `qShlibInfoAddr` packet which is a hint to each
debugger as to where to find the dynamic loader information. For darwin
binaries that run in user land this is the address of the `all_image_infos`
structure in the `/usr/lib/dyld` executable, or the result of a `TASK_DYLD_INFO`
call. The result is returned as big endian hex bytes that are the address
value:
```
send packet: $qShlibInfoAddr#00
read packet: $7fff5fc40040#00
```

**Priority To Implement:** High

If you have a dynamic loader plug-in in LLDB for your target
triple (see the "qHostInfo" packet) that can use this information.
Many times address load randomization can make it hard to detect
where the dynamic loader binary and data structures are located and
some platforms know, or can find out where this information is.

Low if you have a debug target where all object and symbol files
contain static load addresses.

## qSpeedTest

Test the maximum speed at which packets can be sent and received.

```
send packet: qSpeedTest:response_size:<response size>;
read packet: data:<response data>
```

`<response size>` is a hex encoded unsigned number up to 64 bits in size.
The remote will respond with `data:` followed by a block of `a` characters
whose size should match `<response size>`, if the connection is stable.

If there is an error parsing the packet, the response is `E79`.

This packet is used by LLDB to discover how reliable the connection is by
varying the amount of data requested by `<response size>` and checking whether
the expected amount and values were received.

**Priority to Implemment:** Not required for debugging on the same host, otherwise
low unless you know your connection quality is variable.

## qSymbol

Notify the remote that LLDB is ready to do symbol lookups on behalf of the
debug server. The response is the symbol name the debug server wants to know the
value of, or `OK` if the debug server does not need to know any more symbol values.

The exchange always begins with:
```
send packet: qSymbol::
```

The `::` are delimiters for fields that may be filled in future responses. These
delimiters must be included even in the first packet sent.

The debug server can reply one of two ways. If it doesn't need any symbol values:
```
read packet: OK
```

If it does need a symbol value, it includes the ASCII hex encoded name of the
symbol:
```
read packet: qSymbol:6578616D706C65
```

This should be looked up by LLDB then sent back to the server. Include the name
again, with the vaue as a hex number:
```
read packet: qSymbol:6578616D706C65:CAFEF00D
```

If LLDB cannot find the value, it should respond with only the name. Note that
the second `:` is not included here, whereas it is in the initial packet.
```
read packet: qSymbol:6578616D706C65
```

If LLDB is asked for any symbols that it cannot find, it should send the
initial `qSymbol::` again at any point where new libraries are loaded. In case
the symbol can now be resolved.

If the debug server has requested all the symbols it wants, the final response
will be `OK` (whether they were all found or not).

If LLDB did find all the symbols and recieves an `OK` it does not need to send
`qSymbol::` again during the debug session.

**Priority To Implement:** Low, this is rarely used.

## qThreadStopInfo\<tid\>

Get information about why a thread, whose ID is `<tid>`, is stopped.

LLDB tries to use the `qThreadStopInfo` packet which is formatted as
`qThreadStopInfo%x` where `%x` is the hex thread ID. This requests information
about why a thread is stopped. The response is the same as the stop reply
packets and tells us what happened to the other threads. The standard GDB
remote packets love to think that there is only _one_ reason that _one_ thread
stops at a time. This allows us to see why all threads stopped and allows us
to implement better multi-threaded debugging support.

**Priority To Implement:** High

If you need to support multi-threaded or multi-core debugging.
Many times one thread will hit a breakpoint and while the debugger
is in the process of suspending the other threads, other threads
will also hit a breakpoint. This packet allows LLDB to know why all
threads (live system debug) / cores (JTAG) in your program have
stopped and allows LLDB to display and control your program
correctly.

## Stop reply packet extensions

This section describes some of the additional information you can
specify in stop reply packets that help LLDB to know more detailed
information about your threads.

Standard GDB remote stop reply packets are reply packets sent in
response to a packet  that made the program run. They come in the
following forms:

* `SAA` -
  `S` means signal and `AA` is a hex signal number that describes why
  the thread or stopped. It doesn't specify which thread, so the `T`
  packet is recommended to use instead of the `S` packet.

* `TAAkey1:value1;key2:value2;...` -
  `T` means a thread stopped due to a unix signal where `AA` is a hex
  signal number that describes why the program stopped. This is
  followed by a series of key/value pairs:
    * If key is a hex number, it is a register number and value is
      the hex value of the register in debuggee endian byte order.
    * If key == "thread", then the value is the big endian hex
      thread-id of the stopped thread.
    * If key == "core", then value is a hex number of the core on
      which the stop was detected.
    * If key == "watch" or key == "rwatch" or key == "awatch", then
      value is the data address in big endian hex
    * If key == "library", then value is ignore and "qXfer:libraries:read"
      packets should be used to detect any newly loaded shared libraries

* `WAA` - `W` means the process exited and `AA` is the exit status.

* `XAA` - `X` means the process exited and `AA` is signal that caused the program
  to exit.

* `O<ascii-hex-string>` - `O` means `STDOUT` has data that was written to its
  console and is being delivered to the debugger. This packet happens asynchronously
  and the debugger is expected to continue to wait for another stop reply
  packet.

### Lldb Extensions

We have extended the `T` packet to be able to also understand the
following keys and values:

* `metype` - `unsigned` -
  mach exception type (the value of the `EXC_XXX` enumerations)
  as an unsigned integer. For targets with mach
  kernels only.
* `mecount` - `unsigned` -
  mach exception data count as an unsigned integer
  For targets with mach kernels only.
* `medata` - `unsigned` -
  There should be `mecount` of these and it is the data
  that goes along with a mach exception (as an unsigned
  integer). For targets with mach kernels only.
* `name` - `string` -
  The name of the thread as a plain string. The string
  must not contain an special packet characters or
  contain a `:` or a `;`. Use `hexname` if the thread
  name has special characters.
* `hexname` - `ascii-hex` -  An ASCII hex string that contains the name of the thread
* `qaddr` - `hex` -
  Big endian hex value that contains the `libdispatch`
  queue address for the queue of the thread.
* `reason` - `enum` - The enumeration must be one of:
  * `trace` -
    the program stopped after a single instruction
    was executed on a core. Usually done when single
    stepping past a breakpoint
  * `breakpoint` - a breakpoint set using a `z` packet was hit.
  * `trap` - stopped due to user interruption
  * `signal` -
    stopped due to an actual unix signal, not
    just the debugger using a unix signal to keep
    the GDB remote client happy.
  * `watchpoint` - Can be used with of the `watch`/`rwatch`/`awatch` key value
    pairs. Or can be used *instead* of those keys, with the specially formatted
    `description` field.
  * `exception` - an exception stop reason. Use with
    the `description` key/value pair to describe the
    exceptional event the user should see as the stop
    reason.
  * `description` -
    An ASCII hex string that contains a more descriptive
    reason that the thread stopped. This is only needed
    if none of the key/value pairs are enough to
    describe why something stopped.

    For `reason:watchpoint`, `description` is an ascii-hex
    encoded string with between one and three base 10 numbers,
    space separated.  The three numbers are:
      1. Watchpoint address. This address should always be within
         a memory region lldb has a watchpoint on.
         On architectures where the actual reported hit address may
         be outside the watchpoint that was triggered, the remote
         stub should determine which watchpoint was triggered and
         report an address from within its range.
      2. Wwatchpoint hardware register index number.
      3. Actual watchpoint trap address, which may be outside
         the range of any watched region of memory. On MIPS, an addr
         outside a watched range means lldb should disable the wp,
         step, re-enable the wp and continue silently.

    On MIPS, the low 3 bits are masked so if a watchpoint is on
    0x1004, a 2-byte write to 0x1000 will trigger the watchpoint
    (a false positive hit), and lldb needs to disable the
    watchpoint at 0x1004, inst-step, then re-enable the watchpoint
    and not make this a user visible event. The description here
    would be "0x1004 0 0x1000". lldb needs a known watchpoint address
    in the first field, so it can disable it and step.

    On AArch64 we have a related issue, where you watch 4 bytes at
    0x1004, an instruction does an 8-byte write starting at
    0x1000 (a true watchpoint hit) and the hardware may report the
    trap address as 0x1000 - before the watched memory region -
    with the write extending into the watched region.  This can
    be reported as "0x1004 0 0x1000".  lldb will use 0x1004 to
    identify which Watchpoint was triggered, and can report 0x1000
    to the user.  The behavior of silently stepping over the
    watchpoint, with an 3rd field addr outside the range, is
    restricted to MIPS.

    There may be false-positive watchpoint hits on AArch64 as well,
    in the SVE Streaming Mode, but that is less common (see ESR
    register flag "WPF", "Watchpoint might be False-Positive") and
    not currently handled by lldb.
* `threads` - `comma-sep-base16` -
  A list of thread ids for all threads (including
  the thread that we're reporting as stopped) that
  are live in the process right now.  lldb may
  request that this be included in the T packet via
  the QListThreadsInStopReply packet earlier in
  the debug session.

  Example:
  ```
  threads:63387,633b2,63424,63462,63486;
  ```
* `thread-pcs` - `comma-sep-base16` -
  A list of pc values for all threads that currently
  exist in the process, including the thread that
  this `T` packet is reporting as stopped.
  This key-value pair will only be emitted when the
  `threads` key is already included in the `T` packet.
  The pc values correspond to the threads reported
  in the `threads` list.  The number of pcs in the
  `thread-pcs` list will be the same as the number of
  threads in the `threads` list.
  lldb may request that this be included in the `T`
  packet via the `QListThreadsInStopReply` packet
  earlier in the debug session.

  Example:
  ```
  thread-pcs:dec14,2cf872b0,2cf8681c,2d02d68c,2cf716a8;
  ```
* `addressing_bits` - `unsigned` (optional) -
  Specifies how many bits in addresses are significant for addressing, base
  10.  If bits 38..0 in a 64-bit pointer are significant for addressing, then the
  value is 39. This is needed on e.g. AArch64 v8.3 ABIs that use pointer
  authentication in the high bits. This value is normally sent in the `qHostInfo`
  packet response, and if the value cannot change during the process lifetime,
  it does not need to be duplicated here in the stop packet. For a firmware
  environment with early start code that may be changing the page table setup,
  a dynamically set value may be needed.
* `low_mem_addressing_bits` - `unsigned` (optional) -
  Specifies how many bits in addresses in low memory are significant for
  addressing, base 10.  AArch64 can have different page table setups for low
  and high memory, and therefore a different number of bits used for addressing.
* `high_mem_addressing_bits` - `unsigned` (optional) -
  Specifies how many bits in addresses in high memory are significant for
  addressing, base 10.  AArch64 can have different page table setups for low and
  high memory, and therefore a different number of bits used for addressing.

### Best Practices

Since register values can be supplied with this packet, it is often useful
to return the PC, SP, FP, LR (if any), and FLAGS registers so that separate
packets don't need to be sent to read each of these registers from each
thread.

If a thread is stopped for no reason (like just because another thread
stopped, or because when one core stops all cores should stop), use a
`T` packet with `00` as the signal number and fill in as many key values
and registers as possible.

LLDB likes to know why a thread stopped since many thread control
operations like stepping over a source line, actually are implemented
by running the process multiple times. If a breakpoint is hit while
trying to step over a source line and LLDB finds out that a breakpoint
is hit in the "reason", we will know to stop trying to do the step
over because something happened that should stop us from trying to
do the step. If we are at a breakpoint and we disable the breakpoint
at the current PC and do an instruction single step, knowing that
we stopped due to a "trace" helps us know that we can continue
running versus stopping due to a "breakpoint" (if we have two
breakpoint instruction on consecutive instructions). So the more info
we can get about the reason a thread stops, the better job LLDB can
do when controlling your process. A typical GDB server behavior is
to send a SIGTRAP for breakpoints _and_ also when instruction single
stepping, in this case the debugger doesn't really know why we
stopped and it can make it hard for the debugger to control your
program correctly. What if a real SIGTRAP was delivered to a thread
while we were trying to single step? We wouldn't know the difference
with a standard GDB remote server and we could do the wrong thing.

**Priority To Implement:** High. Having the extra information in your stop reply
packets makes your debug session more reliable and informative.

## vAttachName

Same as `vAttach`, except instead of a `pid` you send a process name.

**Priority To Implement:** Low. Only needed for `process attach -n`. If the
packet isn't supported then `process attach -n` will fail gracefully. So you need
only to support it if attaching to a process by name makes sense for your environment.

## vAttachOrWait

Same as `vAttachWait`, except that the stub will attach to a process
by name if it exists, and if it does not, it will wait for a process
of that name to appear and attach to it.

**Priority To Implement:** Low

Only needed to implement `process attach -w -i false -n`.  If
you don't implement it but do implement `-n` AND lldb can somehow get
a process list from your device, it will fall back on scanning the
process list, and sending `vAttach` or `vAttachWait` depending on
whether the requested process exists already.  This is racy,
however, so if you want to support this behavior it is better to
support this packet.

## vAttachWait

Same as `vAttachName`, except that the stub should wait for the next instance
of a process by that name to be launched and attach to that.

**Priority To Implement:** Low. Only needed to support `process attach -w -n`
which will fail gracefully if the packet is not supported.

## vFile Packets

Though some of these may match the ones described in GDB's protocol
documentation, we include our own expectations here in case of
mismatches or extensions.

### vFile:chmod / qPlatform_chmod

Change the permissions of a file on the connected remote machine.

Request: `qPlatform_chmod:<hex-file-mode>,<ascii-hex-path>`

Reply:
* `F<chmod-return-code>`
  (chmod called successfully and returned with the given return code)
* `Exx` (An error occurred)

### vFile:close

Close a previously opened file descriptor.

```
receive: vFile:close:7
send:    F0
```

File descriptor is in base 16. `F-1,errno` with the errno if an error occurs,
errno is base 16.

### vFile:exists

Check whether the file at the given path exists.

```
receive: vFile:exists:2f746d702f61
send         (exists): F,1
send (does not exist): F,0
```

Request packet contains the ASCII hex encoded filename.

The response is a return code where 1 means the file exists
and 0 means it does not.

**Priority To Implement:** Low

### vFile:MD5

Generate an MD5 hash of the file at the given path.

```
receive: vFile:MD5:2f746d702f61
send (success): F,00000000000000001111111111111111
send (failure): F,x
```

Request packet contains the ASCII hex encoded filename.

If the hash succeeded, the response is `F,` followed by the low 64
bits of the result, and finally the high 64 bits of the result. Both are in
hex format without a prefix.

The response is `F,`, followed by `x` if the file did not exist
or failed to hash.

### vFile:mode

Get the mode bits of a file on the target system, filename in ASCII hex.

```
receive: vFile:mode:2f746d702f61
send:    F1ed
```

response is `F` followed by the mode bits in base 16, this `0x1ed` would
correspond to `0755` in octal.
`F-1,errno` with the errno if an error occurs, base 16.

### vFile:open

Open a file on the remote system and return the file descriptor of it.

```
receive: vFile:open:2f746d702f61,00000001,00000180
send:    F8
```

request packet has the fields:
   1. ASCII hex encoded filename
   2. Flags passed to the open call, base 16.
      Note that these are not the `oflags` that `open(2)` takes, but
      are the constant values in `enum OpenOptions` from LLDB's
      [`File.h`](https://github.com/llvm/llvm-project/blob/main/lldb/include/lldb/Host/File.h).
   3. Mode bits, base 16

response is `F` followed by the opened file descriptor in base 16.
`F-1,errno` with the errno if an error occurs, base 16.

### vFile:pread

Read data from an opened file descriptor.

```
receive: vFile:pread:7,1024,0
send:    F4;a'b\00
```

Request packet has the fields:
   1. File descriptor, base 16
   2. Number of bytes to be read, base 16
   3. Offset into file to start from, base 16

Response is `F`, followed by the number of bytes read (base 16), a
semicolon, followed by the data in the binary-escaped-data encoding.

### vFile:pwrite

Write data to a previously opened file descriptor.

```
receive: vFile:pwrite:8,0,\cf\fa\ed\fe\0c\00\00
send:    F1024
```

Request packet has the fields:
   1. File descriptor, base 16
   2. Offset into file to start from, base 16
   3. binary-escaped-data to be written

Response is `F`, followed by the number of bytes written (base 16).

### vFile:size

Get the size of a file on the target system, filename in ASCII hex.

```
receive: vFile:size:2f746d702f61
send:    Fc008
```

response is `F` followed by the file size in base 16.
`F-1,errno` with the errno if an error occurs, base 16.

### vFile:symlink

Create a symbolic link (symlink, soft-link) on the target system.

```
receive: vFile:symlink:<SRC-FILE>,<DST-NAME>
send:    F0,0
```

Argument file paths are in ascii-hex encoding.
Response is `F` plus the return value of `symlink()`, base 16 encoding,
optionally followed by the value of errno if it failed, also base 16.

### vFile:unlink

Remove a file on the target system.

```
receive: vFile:unlink:2f746d702f61
send:    F0
```

Argument is a file path in ascii-hex encoding.
Response is `F` plus the return value of `unlink()`, base 16 encoding.
Return value may optionally be followed by a comma and the base16
value of errno if unlink failed.

## "x" - Binary memory read

Like the `m` (read) and `M` (write) packets, this is a partner to the
`X` (write binary data) packet, `x`.

It is called like
```
xADDRESS,LENGTH
```

where both `ADDRESS` and `LENGTH` are big-endian base 16 values.

To test if this packet is available, send a addr/len of 0:
```
x0,0
```
You will get an `OK` response if it is supported.

The reply will be the data requested in 8-bit binary data format.
The standard quoting is applied to the payload. Characters `}  #  $  *`
will all be escaped with `}` (`0x7d`) character and then XOR'ed with `0x20`.

A typical use to read 512 bytes at 0x1000 would look like:
```
x0x1000,0x200
```
The `0x` prefixes are optional - like most of the gdb-remote packets,
omitting them will work fine; these numbers are always base 16.

The length of the payload is not provided.  A reliable, 8-bit clean,
transport layer is assumed.