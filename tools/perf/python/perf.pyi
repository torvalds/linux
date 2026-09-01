"""Type stubs for the perf Python module."""
from typing import Callable, Dict, List, Optional, Any, Iterator, Union

def config_get(name: str) -> Optional[str]:
    """Get a configuration value from perf config.

    Args:
        name: The configuration variable name (e.g., 'colors.top').

    Returns:
        The configuration value as a string, or None if not set.
    """
    ...

def metrics() -> List[Dict[str, Union[str, List[str]]]]:
    """Get a list of available metrics.

    Returns:
        A list of dictionaries, each describing a metric.
    """
    ...

def syscall_name(id: int, *, elf_machine: Optional[int] = None) -> str:
    """Convert a syscall number to its name.

    Args:
        sc_id: The syscall number.
        elf_machine: Optional ELF machine type.

    Returns:
        The name of the syscall.
    """
    ...

def syscall_id(name: str, *, elf_machine: Optional[int] = None) -> int:
    """Convert a syscall name to its number.

    Args:
        name: The syscall name.
        elf_machine: Optional ELF machine type.

    Returns:
        The number of the syscall.
    """
    ...

def parse_events(
    event_string: str,
    cpus: Optional[cpu_map] = None,
    threads: Optional['thread_map'] = None
) -> 'evlist':
    """Parse an event string and return an evlist.

    Args:
        event_string: The event string (e.g., 'cycles,instructions').
        cpus: Optional CPU map to bind events to.
        threads: Optional thread map to bind events to.

    Returns:
        An evlist containing the parsed events.
    """
    ...

def parse_metrics(
    metrics_string: str,
    pmu: Optional[str] = None,
    cpus: Optional[cpu_map] = None,
    threads: Optional['thread_map'] = None
) -> 'evlist':
    """Parse a string of metrics or metric groups and return an evlist."""
    ...

def tracepoint(sys: str, name: str) -> int:
    """Returns the tracepoint ID for a given system and name."""
    ...

def pmus() -> Iterator[Any]:
    """Returns a sequence of pmus."""
    ...

class data:
    """Represents a perf data file."""
    def __init__(self, path: str = ..., fd: int = ...) -> None: ...

class thread:
    """Represents a thread in the system."""
    def comm(self) -> str:
        """Get the command name of the thread."""
        ...
    pid: int
    tid: int
    ppid: int
    cpu: int

class counts_values:
    """Raw counter values."""
    id: int
    val: int
    ena: int
    run: int
    lost: int
    values: List[int]

class thread_map:
    """Map of threads being monitored."""
    def __init__(self, pid: int = -1, tid: int = -1) -> None:
        """Initialize a thread map.

        Args:
            pid: Process ID to monitor (-1 for all).
            tid: Thread ID to monitor (-1 for all).
        """
        ...
    def __len__(self) -> int: ...
    def __getitem__(self, index: int) -> int: ...
    def __iter__(self) -> Iterator[int]: ...

class evsel:
    """Event selector, represents a single event being monitored."""
    def __init__(
        self,
        type: int = ...,
        config: int = ...,
        sample_freq: int = ...,
        sample_period: int = ...,
        sample_type: int = ...,
        read_format: int = ...,
        disabled: bool = ...,
        inherit: bool = ...,
        pinned: bool = ...,
        exclusive: bool = ...,
        exclude_user: bool = ...,
        exclude_kernel: bool = ...,
        exclude_hv: bool = ...,
        exclude_idle: bool = ...,
        mmap: bool = ...,
        context_switch: bool = ...,
        comm: bool = ...,
        freq: bool = ...,
        inherit_stat: bool = ...,
        enable_on_exec: bool = ...,
        task: bool = ...,
        watermark: int = ...,
        precise_ip: int = ...,
        mmap_data: bool = ...,
        sample_id_all: bool = ...,
        wakeup_events: int = ...,
        bp_type: int = ...,
        bp_addr: int = ...,
        bp_len: int = ...,
        idx: int = ...,
    ) -> None: ...
    def __str__(self) -> str:
        """Return string representation of the event."""
        ...
    def open(self) -> None:
        """Open the event selector file descriptor table."""
        ...
    def read(self, cpu: int, thread: int) -> counts_values:
        """Read counter values for a specific CPU and thread."""
        ...
    ids: List[int]
    def cpus(self) -> cpu_map:
        """Get CPU map for this event."""
        ...
    def threads(self) -> thread_map:
        """Get thread map for this event."""
        ...
    tracking: bool
    config: int
    read_format: int
    sample_period: int
    sample_type: int
    size: int
    type: int
    wakeup_events: int


class _sample_members:
    sample_pid: int
    sample_tid: int
    sample_time: int
    sample_id: int
    sample_stream_id: int
    sample_period: int
    sample_cpu: int

class sample_event(_sample_members):
    """Represents a sample event from perf."""
    evsel: evsel
    sample_ip: int
    sample_addr: int
    sample_phys_addr: int
    sample_weight: int
    sample_data_src: int
    sample_insn_count: int
    sample_cyc_count: int
    type: int
    raw_buf: bytes
    dso: str
    dso_long_name: str
    dso_bid: Optional[bytes]
    map_start: int
    map_end: int
    map_pgoff: int
    symbol: str
    sym_start: int
    sym_end: int
    brstack: Optional['branch_stack']
    callchain: Optional['callchain']
    def srccode(self) -> str: ...
    def insn(self) -> str: ...
    def __getattr__(self, name: str) -> Any: ...

class mmap_event(_sample_members):
    """Represents a mmap event from perf."""
    type: int
    misc: int
    pid: int
    tid: int
    start: int
    len: int
    pgoff: int
    filename: str
    evsel: Optional['evsel']

class mmap2_event(_sample_members):
    """Represents a mmap2 event from perf."""
    type: int
    misc: int
    pid: int
    tid: int
    start: int
    len: int
    pgoff: int
    prot: int
    flags: int
    filename: str
    maj: Optional[int]
    min: Optional[int]
    ino: Optional[int]
    ino_generation: Optional[int]
    build_id: Optional[bytes]
    evsel: Optional['evsel']

class lost_event(_sample_members):
    """Represents a lost events record."""
    type: int
    id: int
    lost: int
    evsel: Optional['evsel']

class comm_event(_sample_members):
    """Represents a COMM record."""
    type: int
    pid: int
    tid: int
    comm: str
    evsel: Optional['evsel']

class task_event(_sample_members):
    """Represents an EXIT or FORK record."""
    type: int
    pid: int
    ppid: int
    tid: int
    ptid: int
    time: int
    evsel: Optional['evsel']

class throttle_event(_sample_members):
    """Represents a THROTTLE or UNTHROTTLE record."""
    type: int
    time: int
    id: int
    stream_id: int
    evsel: Optional['evsel']

class read_event(_sample_members):
    """Represents a READ record."""
    type: int
    pid: int
    tid: int
    evsel: Optional['evsel']

class switch_event(_sample_members):
    """Represents a SWITCH or SWITCH_CPU_WIDE record."""
    type: int
    next_prev_pid: int
    next_prev_tid: int
    evsel: Optional['evsel']

class branch_entry:
    """Represents a branch entry in the branch stack.

    Attributes:
        from_ip: Source address of the branch (corresponds to 'from' keyword in C).
        to_ip: Destination address of the branch.
        mispred: True if the branch was mispredicted.
        predicted: True if the branch was predicted.
        in_tx: True if the branch was in a transaction.
        abort: True if the branch was an abort.
        cycles: Number of cycles since the last branch.
        type: Type of branch.
    """
    from_ip: int
    to_ip: int
    mispred: bool
    predicted: bool
    in_tx: bool
    abort: bool
    cycles: int
    type: int

class branch_stack:
    """Sequence of branch entries in the branch stack."""
    def __len__(self) -> int: ...
    def __getitem__(self, index: int) -> branch_entry: ...

class callchain_node:
    """Represents a frame in the callchain."""
    ip: int
    symbol: Optional[str]
    dso: Optional[str]

class callchain:
    """Sequence of callchain frames."""
    def __len__(self) -> int: ...
    def __getitem__(self, index: int) -> callchain_node: ...

class stat_event(_sample_members):
    """Represents a stat event from perf."""
    type: int
    id: int
    cpu: int
    thread: int
    val: int
    ena: int
    run: int
    evsel: Optional['evsel']

class stat_round_event(_sample_members):
    """Represents a stat round event from perf."""
    type: int
    time: int
    stat_round_type: int
    evsel: Optional['evsel']

class cpu_map:
    """Map of CPUs being monitored."""
    def __init__(self, cpustr: Optional[str] = None) -> None: ...
    def __len__(self) -> int: ...
    def __getitem__(self, index: int) -> int: ...
    def __iter__(self) -> Iterator[int]: ...


class evlist:
    def __init__(self, cpus: cpu_map, threads: thread_map) -> None: ...
    def open(self) -> None:
        """Open the events in the list."""
        ...
    def close(self) -> None:
        """Close the events in the list."""
        ...
    def mmap(self) -> None:
        """Memory map the event buffers."""
        ...
    def poll(self, timeout: int) -> int:
        """Poll for events.

        Args:
            timeout: Timeout in milliseconds.

        Returns:
            Number of events ready.
        """
        ...
    def read_on_cpu(self, cpu: int) -> Optional[Any]:
        """Read a sample event from a specific CPU.

        Args:
            cpu: The CPU number.

        Returns:
            A sample_event or other event type if available, or None.
        """
        ...
    def all_cpus(self) -> cpu_map:
        """Get a cpu_map of all CPUs in the system."""
        ...
    def metrics(self) -> List[str]:
        """Get a list of metric names within the evlist."""
        ...
    def compute_metric(self, metric: str, cpu: int, thread: int) -> float:
        """Compute metric for given name, cpu and thread.

        Args:
            metric: The metric name.
            cpu: The CPU number.
            thread: The thread ID.

        Returns:
            The computed metric value.
        """
        ...
    def config(self) -> None:
        """Configure the events in the list."""
        ...
    def disable(self) -> None:
        """Disable all events in the list."""
        ...
    def enable(self) -> None:
        """Enable all events in the list."""
        ...
    def get_pollfd(self) -> List[int]:
        """Get a list of file descriptors for polling."""
        ...
    def add(self, evsel: evsel) -> int:
        """Add an event to the list."""
        ...
    def __iter__(self) -> Iterator[evsel]:
        """Iterate over the events (evsel) in the list."""
        ...


class session:
    def __init__(
        self,
        data: data,
        sample: Optional[Callable[[sample_event], None]] = None,
        stat: Optional[Callable[[Any, Optional[str]], None]] = None
    ) -> None:
        """Initialize a perf session.

        Args:
            data: The perf data file to read.
            sample: Callback for sample events.
            stat: Callback for stat events.
        """
        ...
    def process_events(self) -> None:
        """Process all events in the session."""
        ...
    def find_thread(self, pid: int) -> thread:
        """Returns the thread associated with a pid."""
        ...

# Event Types
TYPE_HARDWARE: int
"""Hardware event."""

TYPE_SOFTWARE: int
"""Software event."""

TYPE_TRACEPOINT: int
"""Tracepoint event."""

TYPE_HW_CACHE: int
"""Hardware cache event."""

TYPE_RAW: int
"""Raw hardware event."""

TYPE_BREAKPOINT: int
"""Breakpoint event."""


# Hardware Counters
COUNT_HW_CPU_CYCLES: int
"""Total cycles. Be wary of what happens during CPU frequency scaling."""

COUNT_HW_INSTRUCTIONS: int
"""Retired instructions. Be careful, these can be affected by various issues,
most notably hardware interrupt counts."""

COUNT_HW_CACHE_REFERENCES: int
"""Cache accesses. Usually this indicates Last Level Cache accesses but this
may vary depending on your CPU."""

COUNT_HW_CACHE_MISSES: int
"""Cache misses. Usually this indicates Last Level Cache misses."""

COUNT_HW_BRANCH_INSTRUCTIONS: int
"""Retired branch instructions."""

COUNT_HW_BRANCH_MISSES: int
"""Mispredicted branch instructions."""

COUNT_HW_BUS_CYCLES: int
"""Bus cycles, which can be different from total cycles."""

COUNT_HW_STALLED_CYCLES_FRONTEND: int
"""Stalled cycles during issue [This event is an alias of idle-cycles-frontend]."""

COUNT_HW_STALLED_CYCLES_BACKEND: int
"""Stalled cycles during retirement [This event is an alias of idle-cycles-backend]."""

COUNT_HW_REF_CPU_CYCLES: int
"""Total cycles; not affected by CPU frequency scaling."""


# Cache Counters
COUNT_HW_CACHE_L1D: int
"""Level 1 data cache."""

COUNT_HW_CACHE_L1I: int
"""Level 1 instruction cache."""

COUNT_HW_CACHE_LL: int
"""Last Level Cache."""

COUNT_HW_CACHE_DTLB: int
"""Data TLB."""

COUNT_HW_CACHE_ITLB: int
"""Instruction TLB."""

COUNT_HW_CACHE_BPU: int
"""Branch Processing Unit."""

COUNT_HW_CACHE_OP_READ: int
"""Read accesses."""

COUNT_HW_CACHE_OP_WRITE: int
"""Write accesses."""

COUNT_HW_CACHE_OP_PREFETCH: int
"""Prefetch accesses."""

COUNT_HW_CACHE_RESULT_ACCESS: int
"""Accesses."""

COUNT_HW_CACHE_RESULT_MISS: int
"""Misses."""


# Software Counters
COUNT_SW_CPU_CLOCK: int
"""CPU clock event."""

COUNT_SW_TASK_CLOCK: int
"""Task clock event."""

COUNT_SW_PAGE_FAULTS: int
"""Page faults."""

COUNT_SW_CONTEXT_SWITCHES: int
"""Context switches."""

COUNT_SW_CPU_MIGRATIONS: int
"""CPU migrations."""

COUNT_SW_PAGE_FAULTS_MIN: int
"""Minor page faults."""

COUNT_SW_PAGE_FAULTS_MAJ: int
"""Major page faults."""

COUNT_SW_ALIGNMENT_FAULTS: int
"""Alignment faults."""

COUNT_SW_EMULATION_FAULTS: int
"""Emulation faults."""

COUNT_SW_DUMMY: int
"""Dummy event."""


# Sample Fields
SAMPLE_IP: int
"""Instruction pointer."""

SAMPLE_TID: int
"""Process and thread ID."""

SAMPLE_TIME: int
"""Timestamp."""

SAMPLE_ADDR: int
"""Sampled address."""

SAMPLE_READ: int
"""Read barcode."""

SAMPLE_CALLCHAIN: int
"""Call chain."""

SAMPLE_ID: int
"""Unique ID."""

SAMPLE_CPU: int
"""CPU number."""

SAMPLE_PERIOD: int
"""Sample period."""

SAMPLE_STREAM_ID: int
"""Stream ID."""

SAMPLE_RAW: int
"""Raw sample."""


# Format Fields
FORMAT_TOTAL_TIME_ENABLED: int
"""Total time enabled."""

FORMAT_TOTAL_TIME_RUNNING: int
"""Total time running."""

FORMAT_ID: int
"""Event ID."""

FORMAT_GROUP: int
"""Event group."""


# Record Types
RECORD_MMAP: int
"""MMAP record. Contains header, pid, tid, addr, len, pgoff, filename, and sample_id."""

RECORD_LOST: int
"""Lost events record. Contains header, id, lost count, and sample_id."""

RECORD_COMM: int
"""COMM record. Contains header, pid, tid, comm, and sample_id."""

RECORD_EXIT: int
"""EXIT record. Contains header, pid, ppid, tid, ptid, time, and sample_id."""

RECORD_THROTTLE: int
"""THROTTLE record. Contains header, time, id, stream_id, and sample_id."""

RECORD_UNTHROTTLE: int
"""UNTHROTTLE record. Contains header, time, id, stream_id, and sample_id."""

RECORD_FORK: int
"""FORK record. Contains header, pid, ppid, tid, ptid, time, and sample_id."""

RECORD_READ: int
"""READ record. Contains header, and read values."""

RECORD_SAMPLE: int
"""SAMPLE record. Contains header, and sample data requested by sample_type."""

RECORD_MMAP2: int
"""MMAP2 record. Contains header, pid, tid, addr, len, pgoff, maj, min, ino,
ino_generation, prot, flags, filename, and sample_id."""

RECORD_AUX: int
"""AUX record. Contains header, aux_offset, aux_size, flags, and sample_id."""

RECORD_ITRACE_START: int
"""ITRACE_START record. Contains header, pid, tid, and sample_id."""

RECORD_LOST_SAMPLES: int
"""LOST_SAMPLES record. Contains header, lost count, and sample_id."""

RECORD_SWITCH: int
"""SWITCH record. Contains header, and sample_id."""

RECORD_SWITCH_CPU_WIDE: int
"""SWITCH_CPU_WIDE record. Contains header, and sample_id."""

RECORD_STAT: int
"""STAT record."""

RECORD_STAT_ROUND: int
"""STAT_ROUND record."""

RECORD_MISC_SWITCH_OUT: int
"""MISC_SWITCH_OUT record."""
