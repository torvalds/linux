======================================
XRay Flight Data Recorder Trace Format
======================================

:Version: 1 as of 2017-07-20

.. contents::
   :local:


Introduction
============

When gathering XRay traces in Flight Data Recorder mode, each thread of an
application will claim buffers to fill with trace data, which at some point
is finalized and flushed.

A goal of the profiler is to minimize overhead, the flushed data directly
corresponds to the buffer.

This document describes the format of a trace file.


General
=======

Each trace file corresponds to a sequence of events in a particular thread.

The file has a header followed by a sequence of discriminated record types.

The endianness of byte fields matches the endianness of the platform which
produced the trace file.


Header Section
==============

A trace file begins with a 32 byte header.

+-------------------+-----------------+----------------------------------------+
| Field             | Size (bytes)    | Description                            |
+===================+=================+========================================+
| version           | ``2``           | Anticipates versioned  readers. This   |
|                   |                 | document describes the format when     |
|                   |                 | version == 1                           |
+-------------------+-----------------+----------------------------------------+
| type              | ``2``           | An enumeration encoding the type of    |
|                   |                 | trace. Flight Data Recorder mode       |
|                   |                 | traces have type == 1                  |
+-------------------+-----------------+----------------------------------------+
| bitfield          | ``4``           | Holds parameters that are not aligned  |
|                   |                 | to bytes. Further described below.     |
+-------------------+-----------------+----------------------------------------+
| cycle_frequency   | ``8``           | The frequency in hertz of the CPU      |
|                   |                 | oscillator used to measure duration of |
|                   |                 | events in ticks.                       |
+-------------------+-----------------+----------------------------------------+
| buffer_size       | ``8``           | The size in bytes of the data portion  |
|                   |                 | of the trace following the header.     |
+-------------------+-----------------+----------------------------------------+
| reserved          | ``8``           | Reserved for future use.               |
+-------------------+-----------------+----------------------------------------+

The bitfield parameter of the file header is composed of the following fields.

+-------------------+----------------+-----------------------------------------+
| Field             | Size (bits)    | Description                             |
+===================+================+=========================================+
| constant_tsc      | ``1``          | Whether the platform's timestamp        |
|                   |                | counter used to record ticks between    |
|                   |                | events ticks at a constant frequency    |
|                   |                | despite CPU frequency changes.          |
|                   |                | 0 == non-constant. 1 == constant.       |
+-------------------+----------------+-----------------------------------------+
| nonstop_tsc       | ``1``          | Whether the tsc continues to count      |
|                   |                | despite whether the CPU is in a low     |
|                   |                | power state. 0 == stop. 1 == non-stop.  |
+-------------------+----------------+-----------------------------------------+
| reserved          | ``30``         | Not meaningful.                         |
+-------------------+----------------+-----------------------------------------+


Data Section
============

Following the header in a trace is a data section with size matching the
buffer_size field in the header.

The data section is a stream of elements of different types.

There are a few categories of data in the sequence.

- ``Function Records``: Function Records contain the timing of entry into and
  exit from function execution. Function Records have 8 bytes each.

- ``Metadata Records``: Metadata records serve many purposes. Mostly, they
  capture information that may be too costly to record for each function, but
  that is required to contextualize the fine-grained timings. They also are used
  as markers for user-defined Event Data payloads. Metadata records have 16
  bytes each.

- ``Event Data``: Free form data may be associated with events that are traced
  by the binary and encode data defined by a handler function. Event data is
  always preceded with a marker record which indicates how large it is.

- ``Function Arguments``: The arguments to some functions are included in the
  trace. These are either pointer addresses or primitives that are read and
  logged independently of their types in a high level language. To the tracer,
  they are all numbers. Function Records that have attached arguments will
  indicate their presence on the function entry record. We only support logging
  contiguous function argument sequences starting with argument zero, which will
  be the "this" pointer for member function invocations. For example, we don't
  support logging the first and third argument.

A reader of the memory format must maintain a state machine. The format makes no
attempt to pad for alignment, and it is not seekable.


Function Records
----------------

Function Records have an 8 byte layout. This layout encodes information to
reconstruct a call stack of instrumented function and their durations.

+---------------+--------------+-----------------------------------------------+
| Field         | Size (bits)  | Description                                   |
+===============+==============+===============================================+
| discriminant  | ``1``        | Indicates whether a reader should read a      |
|               |              | Function or Metadata record. Set to ``0`` for |
|               |              | Function records.                             |
+---------------+--------------+-----------------------------------------------+
| action        | ``3``        | Specifies whether the function is being       |
|               |              | entered, exited, or is a non-standard entry   |
|               |              | or exit produced by optimizations.            |
+---------------+--------------+-----------------------------------------------+
| function_id   | ``28``       | A numeric ID for the function. Resolved to a  |
|               |              | name via the xray instrumentation map. The    |
|               |              | instrumentation map is built by xray at       |
|               |              | compile time into an object file and pairs    |
|               |              | the function ids to addresses. It is used for |
|               |              | patching and as a lookup into the binary's    |
|               |              | symbols to obtain names.                      |
+---------------+--------------+-----------------------------------------------+
| tsc_delta     | ``32``       | The number of ticks of the timestamp counter  |
|               |              | since a previous record recorded a delta or   |
|               |              | other TSC resetting event.                    |
+---------------+--------------+-----------------------------------------------+

On little-endian machines, the bitfields are ordered from least significant bit
bit to most significant bit. A reader can read an 8 bit value and apply the mask
``0x01`` for the discriminant. Similarly, they can read 32 bits and unsigned
shift right by ``0x04`` to obtain the function_id field.

On big-endian machine, the bitfields are written in order from most significant
bit to least significant bit. A reader would read an 8 bit value and unsigned
shift right by 7 bits for the discriminant. The function_id field could be
obtained by reading a 32 bit value and applying the mask ``0x0FFFFFFF``.

Function action types are as follows.

+---------------+--------------+-----------------------------------------------+
| Type          | Number       | Description                                   |
+===============+==============+===============================================+
| Entry         | ``0``        | Typical function entry.                       |
+---------------+--------------+-----------------------------------------------+
| Exit          | ``1``        | Typical function exit.                        |
+---------------+--------------+-----------------------------------------------+
| Tail_Exit     | ``2``        | An exit from a function due to tail call      |
|               |              | optimization.                                 |
+---------------+--------------+-----------------------------------------------+
| Entry_Args    | ``3``        | A function entry that records arguments.      |
+---------------+--------------+-----------------------------------------------+

Entry_Args records do not contain the arguments themselves. Instead, metadata
records for each of the logged args follow the function record in the stream.


Metadata Records
----------------

Interspersed throughout the buffer are 16 byte Metadata records. For typically
instrumented binaries, they will be sparser than Function records, and they
provide a fuller picture of the binary execution state.

Metadata record layout is partially record dependent, but they share a common
structure.

The same bit field rules described for function records apply to the first byte
of MetadataRecords. Within this byte, little endian machines use lsb to msb
ordering and big endian machines use msb to lsb ordering.

+---------------+--------------+-----------------------------------------------+
| Field         | Size         | Description                                   |
+===============+==============+===============================================+
| discriminant  | ``1 bit``    | Indicates whether a reader should read a      |
|               |              | Function or Metadata record. Set to ``1`` for |
|               |              | Metadata records.                             |
+---------------+--------------+-----------------------------------------------+
| record_kind   | ``7 bits``   | The type of Metadata record.                  |
+---------------+--------------+-----------------------------------------------+
| data          | ``15 bytes`` | A data field used differently for each record |
|               |              | type.                                         |
+---------------+--------------+-----------------------------------------------+

Here is a table of the enumerated record kinds.

+--------+---------------------------+
| Number | Type                      |
+========+===========================+
| 0      | NewBuffer                 |
+--------+---------------------------+
| 1      | EndOfBuffer               |
+--------+---------------------------+
| 2      | NewCPUId                  |
+--------+---------------------------+
| 3      | TSCWrap                   |
+--------+---------------------------+
| 4      | WallTimeMarker            |
+--------+---------------------------+
| 5      | CustomEventMarker         |
+--------+---------------------------+
| 6      | CallArgument              |
+--------+---------------------------+


NewBuffer Records
-----------------

Each buffer begins with a NewBuffer record immediately after the header.
It records the thread ID of the thread that the trace belongs to.

Its data segment is as follows.

+---------------+--------------+-----------------------------------------------+
| Field         | Size (bytes) | Description                                   |
+===============+==============+===============================================+
| thread_Id     | ``2``        | Thread ID for buffer.                         |
+---------------+--------------+-----------------------------------------------+
| reserved      | ``13``       | Unused.                                       |
+---------------+--------------+-----------------------------------------------+


WallClockTime Records
---------------------

Following the NewBuffer record, each buffer records an absolute time as a frame
of reference for the durations recorded by timestamp counter deltas.

Its data segment is as follows.

+---------------+--------------+-----------------------------------------------+
| Field         | Size (bytes) | Description                                   |
+===============+==============+===============================================+
| seconds       | ``8``        | Seconds on absolute timescale. The starting   |
|               |              | point is unspecified and depends on the       |
|               |              | implementation and platform configured by the |
|               |              | tracer.                                       |
+---------------+--------------+-----------------------------------------------+
| microseconds  | ``4``        | The microsecond component of the time.        |
+---------------+--------------+-----------------------------------------------+
| reserved      | ``3``        | Unused.                                       |
+---------------+--------------+-----------------------------------------------+


NewCpuId Records
----------------

Each function entry invokes a routine to determine what CPU is executing.
Typically, this is done with readtscp, which reads the timestamp counter at the
same time.

If the tracing detects that the execution has switched CPUs or if this is the
first instrumented entry point, the tracer will output a NewCpuId record.

Its data segment is as follows.

+---------------+--------------+-----------------------------------------------+
| Field         | Size (bytes) | Description                                   |
+===============+==============+===============================================+
| cpu_id        | ``2``        | CPU Id.                                       |
+---------------+--------------+-----------------------------------------------+
| absolute_tsc  | ``8``        | The absolute value of the timestamp counter.  |
+---------------+--------------+-----------------------------------------------+
| reserved      | ``5``        | Unused.                                       |
+---------------+--------------+-----------------------------------------------+


TSCWrap Records
---------------

Since each function record uses a 32 bit value to represent the number of ticks
of the timestamp counter since the last reference, it is possible for this value
to overflow, particularly for sparsely instrumented binaries.

When this delta would not fit into a 32 bit representation, a reference absolute
timestamp counter record is written in the form of a TSCWrap record.

Its data segment is as follows.

+---------------+--------------+-----------------------------------------------+
| Field         | Size (bytes) | Description                                   |
+===============+==============+===============================================+
| absolute_tsc  | ``8``        | Timestamp counter value.                      |
+---------------+--------------+-----------------------------------------------+
| reserved      | ``7``        | Unused.                                       |
+---------------+--------------+-----------------------------------------------+


CallArgument Records
--------------------

Immediately following an Entry_Args type function record, there may be one or
more CallArgument records that contain the traced function's parameter values.

The order of the CallArgument Record sequency corresponds one to one with the
order of the function parameters.

CallArgument data segment:

+---------------+--------------+-----------------------------------------------+
| Field         | Size (bytes) | Description                                   |
+===============+==============+===============================================+
| argument      | ``8``        | Numeric argument (may be pointer address).    |
+---------------+--------------+-----------------------------------------------+
| reserved      | ``7``        | Unused.                                       |
+---------------+--------------+-----------------------------------------------+


CustomEventMarker Records
-------------------------

XRay provides the feature of logging custom events. This may be leveraged to
record tracing info for RPCs or similarly trace data that is application
specific.

Custom Events themselves are an unstructured (application defined) segment of
memory with arbitrary size within the buffer. They are preceded by
CustomEventMarkers to indicate their presence and size.

CustomEventMarker data segment:

+---------------+--------------+-----------------------------------------------+
| Field         | Size (bytes) | Description                                   |
+===============+==============+===============================================+
| event_size    | ``4``        | Size of preceded event.                       |
+---------------+--------------+-----------------------------------------------+
| absolute_tsc  | ``8``        | A timestamp counter of the event.             |
+---------------+--------------+-----------------------------------------------+
| reserved      | ``3``        | Unused.                                       |
+---------------+--------------+-----------------------------------------------+


EndOfBuffer Records
-------------------

An EndOfBuffer record type indicates that there is no more trace data in this
buffer. The reader is expected to seek past the remaining buffer_size expressed
before the start of buffer and look for either another header or EOF.


Format Grammar and Invariants
=============================

Not all sequences of Metadata records and Function records are valid data. A
sequence should be parsed as a state machine. The expectations for a valid
format can be expressed as a context free grammar.

This is an attempt to explain the format with statements in EBNF format.

- Format := Header ThreadBuffer* EOF

- ThreadBuffer := NewBuffer WallClockTime NewCPUId BodySequence* End

- BodySequence := NewCPUId | TSCWrap | Function | CustomEvent

- Function := (Function_Entry_Args CallArgument*) | Function_Other_Type

- CustomEvent := CustomEventMarker CustomEventUnstructuredMemory

- End := EndOfBuffer RemainingBufferSizeToSkip


Function Record Order
---------------------

There are a few clarifications that may help understand what is expected of
Function records.

- Functions with an Exit are expected to have a corresponding Entry or
  Entry_Args function record precede them in the trace.

- Tail_Exit Function records record the Function ID of the function whose return
  address the program counter will take. In other words, the final function that
  would be popped off of the call stack if tail call optimization was not used.

- Not all functions marked for instrumentation are necessarily in the trace. The
  tracer uses heuristics to preserve the trace for non-trivial functions.

- Not every entry must have a traced Exit or Tail Exit. The buffer may run out
  of space or the program may request for the tracer to finalize toreturn the
  buffer before an instrumented function exits.
