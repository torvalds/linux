from abc import ABCMeta, abstractmethod

import lldb
import json, struct, signal


class ScriptedProcess(metaclass=ABCMeta):

    """
    The base class for a scripted process.

    Most of the base class methods are `@abstractmethod` that need to be
    overwritten by the inheriting class.
    """

    capabilities = None
    memory_regions = None
    loaded_images = None
    threads = None
    metadata = None

    @abstractmethod
    def __init__(self, exe_ctx, args):
        """Construct a scripted process.

        Args:
            exe_ctx (lldb.SBExecutionContext): The execution context for the scripted process.
            args (lldb.SBStructuredData): A Dictionary holding arbitrary
                key/value pairs used by the scripted process.
        """
        target = None
        self.target = None
        self.args = None
        self.arch = None
        if isinstance(exe_ctx, lldb.SBExecutionContext):
            target = exe_ctx.target
        if isinstance(target, lldb.SBTarget) and target.IsValid():
            self.target = target
            triple = self.target.triple
            if triple:
                self.arch = triple.split("-")[0]
            self.dbg = target.GetDebugger()
        if isinstance(args, lldb.SBStructuredData) and args.IsValid():
            self.args = args
        self.threads = {}
        self.loaded_images = []
        self.metadata = {}
        self.capabilities = {}
        self.pid = 42

    def get_capabilities(self):
        """Get a dictionary containing the process capabilities.

        Returns:
            Dict[str:bool]: The dictionary of capability, with the capability
            name as the key and a boolean flag as the value.
            The dictionary can be empty.
        """
        return self.capabilities

    def get_memory_region_containing_address(self, addr):
        """Get the memory region for the scripted process, containing a
            specific address.

        Args:
            addr (int): Address to look for in the scripted process memory
                regions.

        Returns:
            lldb.SBMemoryRegionInfo: The memory region containing the address.
                None if out of bounds.
        """
        return None

    def get_threads_info(self):
        """Get the dictionary describing the process' Scripted Threads.

        Returns:
            Dict: The dictionary of threads, with the thread ID as the key and
            a Scripted Thread instance as the value.
            The dictionary can be empty.
        """
        return self.threads

    @abstractmethod
    def read_memory_at_address(self, addr, size, error):
        """Get a memory buffer from the scripted process at a certain address,
            of a certain size.

        Args:
            addr (int): Address from which we should start reading.
            size (int): Size of the memory to read.
            error (lldb.SBError): Error object.

        Returns:
            lldb.SBData: An `lldb.SBData` buffer with the target byte size and
                byte order storing the memory read.
        """
        pass

    def write_memory_at_address(self, addr, data, error):
        """Write a buffer to the scripted process memory.

        Args:
            addr (int): Address from which we should start reading.
            data (lldb.SBData): An `lldb.SBData` buffer to write to the process
            memory.
            error (lldb.SBError): Error object.

        Returns:
            size (int): Size of the memory to read.
        """
        error.SetErrorString(
            "%s doesn't support memory writes." % self.__class__.__name__
        )
        return 0

    def get_loaded_images(self):
        """Get the list of loaded images for the scripted process.

        .. code-block:: python

            scripted_image = {
                uuid = "c6ea2b64-f77c-3d27-9528-74f507b9078b",
                path = "/usr/lib/dyld"
                load_addr = 0xbadc0ffee
            }

        Returns:
            List[scripted_image]: A list of `scripted_image` dictionaries
                containing for each entry the library UUID or its file path
                and its load address.
                None if the list is empty.
        """
        return self.loaded_images

    def get_process_id(self):
        """Get the scripted process identifier.

        Returns:
            int: The scripted process identifier.
        """
        return self.pid

    def launch(self):
        """Simulate the scripted process launch.

        Returns:
            lldb.SBError: An `lldb.SBError` with error code 0.
        """
        return lldb.SBError()

    def attach(self, attach_info):
        """Simulate the scripted process attach.

        Args:
            attach_info (lldb.SBAttachInfo): The information related to the
            process we're attaching to.

        Returns:
            lldb.SBError: An `lldb.SBError` with error code 0.
        """
        return lldb.SBError()

    def resume(self, should_stop=True):
        """Simulate the scripted process resume.

        Args:
            should_stop (bool): If True, resume will also force the process
            state to stopped after running it.

        Returns:
            lldb.SBError: An `lldb.SBError` with error code 0.
        """
        process = self.target.GetProcess()
        if not process:
            error = lldb.SBError()
            error.SetErrorString("Invalid process.")
            return error

        process.ForceScriptedState(lldb.eStateRunning)
        if should_stop:
            process.ForceScriptedState(lldb.eStateStopped)
        return lldb.SBError()

    @abstractmethod
    def is_alive(self):
        """Check if the scripted process is alive.

        Returns:
            bool: True if scripted process is alive. False otherwise.
        """
        pass

    @abstractmethod
    def get_scripted_thread_plugin(self):
        """Get scripted thread plugin name.

        Returns:
            str: Name of the scripted thread plugin.
        """
        return None

    def get_process_metadata(self):
        """Get some metadata for the scripted process.

        Returns:
            Dict: A dictionary containing metadata for the scripted process.
                  None if the process as no metadata.
        """
        return self.metadata

    def create_breakpoint(self, addr, error):
        """Create a breakpoint in the scripted process from an address.
            This is mainly used with interactive scripted process debugging.

        Args:
            addr (int): Address at which the breakpoint should be set.
            error (lldb.SBError): Error object.

        Returns:
            SBBreakpoint: A valid breakpoint object that was created a the specified
                          address. None if the breakpoint creation failed.
        """
        error.SetErrorString(
            "%s doesn't support creating breakpoints." % self.__class__.__name__
        )
        return False


class ScriptedThread(metaclass=ABCMeta):

    """
    The base class for a scripted thread.

    Most of the base class methods are `@abstractmethod` that need to be
    overwritten by the inheriting class.
    """

    @abstractmethod
    def __init__(self, process, args):
        """Construct a scripted thread.

        Args:
            process (ScriptedProcess/lldb.SBProcess): The process owning this thread.
            args (lldb.SBStructuredData): A Dictionary holding arbitrary
                key/value pairs used by the scripted thread.
        """
        self.target = None
        self.originating_process = None
        self.process = None
        self.args = None
        self.idx = 0
        self.tid = 0
        self.idx = None
        self.name = None
        self.queue = None
        self.state = None
        self.stop_reason = None
        self.register_info = None
        self.register_ctx = {}
        self.frames = []
        self.extended_info = []

        if (
            isinstance(process, ScriptedProcess)
            or isinstance(process, lldb.SBProcess)
            and process.IsValid()
        ):
            self.target = process.target
            self.originating_process = process
            self.process = self.target.GetProcess()
            self.get_register_info()

    def get_thread_idx(self):
        """Get the scripted thread index.

        Returns:
            int: The index of the scripted thread in the scripted process.
        """
        return self.idx

    def get_thread_id(self):
        """Get the scripted thread identifier.

        Returns:
            int: The identifier of the scripted thread.
        """
        return self.tid

    def get_name(self):
        """Get the scripted thread name.

        Returns:
            str: The name of the scripted thread.
        """
        return self.name

    def get_state(self):
        """Get the scripted thread state type.

        .. code-block:: python

            eStateStopped,   ///< Process or thread is stopped and can be examined.
            eStateRunning,   ///< Process or thread is running and can't be examined.
            eStateStepping,  ///< Process or thread is in the process of stepping and
                             /// can not be examined.
            eStateCrashed,   ///< Process or thread has crashed and can be examined.

        Returns:
            int: The state type of the scripted thread.
                 Returns lldb.eStateStopped by default.
        """
        return lldb.eStateStopped

    def get_queue(self):
        """Get the scripted thread associated queue name.
            This method is optional.

        Returns:
            str: The queue name associated with the scripted thread.
        """
        return self.queue

    @abstractmethod
    def get_stop_reason(self):
        """Get the dictionary describing the stop reason type with some data.
            This method is optional.

        Returns:
            Dict: The dictionary holding the stop reason type and the possibly
            the stop reason data.
        """
        pass

    def get_stackframes(self):
        """Get the list of stack frames for the scripted thread.

        .. code-block:: python

            scripted_frame = {
                idx = 0,
                pc = 0xbadc0ffee
            }

        Returns:
            List[scripted_frame]: A list of `scripted_frame` dictionaries
                containing at least for each entry, the frame index and
                the program counter value for that frame.
                The list can be empty.
        """
        return self.frames

    def get_register_info(self):
        if self.register_info is None:
            self.register_info = dict()
            if self.originating_process.arch == "x86_64":
                self.register_info["sets"] = ["General Purpose Registers"]
                self.register_info["registers"] = INTEL64_GPR
            elif "arm64" in self.originating_process.arch:
                self.register_info["sets"] = ["General Purpose Registers"]
                self.register_info["registers"] = ARM64_GPR
            else:
                raise ValueError("Unknown architecture", self.originating_process.arch)
        return self.register_info

    @abstractmethod
    def get_register_context(self):
        """Get the scripted thread register context

        Returns:
            str: A byte representing all register's value.
        """
        pass

    def get_extended_info(self):
        """Get scripted thread extended information.

        Returns:
            List: A list containing the extended information for the scripted process.
                  None if the thread as no extended information.
        """
        return self.extended_info


class PassthroughScriptedProcess(ScriptedProcess):
    driving_target = None
    driving_process = None

    def __init__(self, exe_ctx, args, launched_driving_process=True):
        super().__init__(exe_ctx, args)

        self.driving_target = None
        self.driving_process = None

        self.driving_target_idx = args.GetValueForKey("driving_target_idx")
        if self.driving_target_idx and self.driving_target_idx.IsValid():
            idx = self.driving_target_idx.GetUnsignedIntegerValue(42)
            self.driving_target = self.target.GetDebugger().GetTargetAtIndex(idx)

            if launched_driving_process:
                self.driving_process = self.driving_target.GetProcess()
                for driving_thread in self.driving_process:
                    structured_data = lldb.SBStructuredData()
                    structured_data.SetFromJSON(
                        json.dumps(
                            {
                                "driving_target_idx": idx,
                                "thread_idx": driving_thread.GetIndexID(),
                            }
                        )
                    )

                    self.threads[
                        driving_thread.GetThreadID()
                    ] = PassthroughScriptedThread(self, structured_data)

                for module in self.driving_target.modules:
                    path = module.file.fullpath
                    load_addr = module.GetObjectFileHeaderAddress().GetLoadAddress(
                        self.driving_target
                    )
                    self.loaded_images.append({"path": path, "load_addr": load_addr})

    def get_memory_region_containing_address(self, addr):
        mem_region = lldb.SBMemoryRegionInfo()
        error = self.driving_process.GetMemoryRegionInfo(addr, mem_region)
        if error.Fail():
            return None
        return mem_region

    def read_memory_at_address(self, addr, size, error):
        data = lldb.SBData()
        bytes_read = self.driving_process.ReadMemory(addr, size, error)

        if error.Fail():
            return data

        data.SetDataWithOwnership(
            error,
            bytes_read,
            self.driving_target.GetByteOrder(),
            self.driving_target.GetAddressByteSize(),
        )

        return data

    def write_memory_at_address(self, addr, data, error):
        return self.driving_process.WriteMemory(
            addr, bytearray(data.uint8.all()), error
        )

    def get_process_id(self):
        return self.driving_process.GetProcessID()

    def is_alive(self):
        return True

    def get_scripted_thread_plugin(self):
        return f"{PassthroughScriptedThread.__module__}.{PassthroughScriptedThread.__name__}"


class PassthroughScriptedThread(ScriptedThread):
    def __init__(self, process, args):
        super().__init__(process, args)
        driving_target_idx = args.GetValueForKey("driving_target_idx")
        thread_idx = args.GetValueForKey("thread_idx")

        # TODO: Change to Walrus operator (:=) with oneline if assignment
        # Requires python 3.8
        val = thread_idx.GetUnsignedIntegerValue()
        if val is not None:
            self.idx = val

        self.driving_target = None
        self.driving_process = None
        self.driving_thread = None

        # TODO: Change to Walrus operator (:=) with oneline if assignment
        # Requires python 3.8
        val = driving_target_idx.GetUnsignedIntegerValue()
        if val is not None:
            self.driving_target = self.target.GetDebugger().GetTargetAtIndex(val)
            self.driving_process = self.driving_target.GetProcess()
            self.driving_thread = self.driving_process.GetThreadByIndexID(self.idx)

        if self.driving_thread:
            self.id = self.driving_thread.GetThreadID()

    def get_thread_id(self):
        return self.id

    def get_name(self):
        return f"{PassthroughScriptedThread.__name__}.thread-{self.idx}"

    def get_stop_reason(self):
        stop_reason = {"type": lldb.eStopReasonInvalid, "data": {}}

        if (
            self.driving_thread
            and self.driving_thread.IsValid()
            and self.get_thread_id() == self.driving_thread.GetThreadID()
        ):
            stop_reason["type"] = lldb.eStopReasonNone

            # TODO: Passthrough stop reason from driving process
            if self.driving_thread.GetStopReason() != lldb.eStopReasonNone:
                if "arm64" in self.originating_process.arch:
                    stop_reason["type"] = lldb.eStopReasonException
                    stop_reason["data"][
                        "desc"
                    ] = self.driving_thread.GetStopDescription(100)
                elif self.originating_process.arch == "x86_64":
                    stop_reason["type"] = lldb.eStopReasonSignal
                    stop_reason["data"]["signal"] = signal.SIGTRAP
                else:
                    stop_reason["type"] = self.driving_thread.GetStopReason()

        return stop_reason

    def get_register_context(self):
        if not self.driving_thread or self.driving_thread.GetNumFrames() == 0:
            return None
        frame = self.driving_thread.GetFrameAtIndex(0)

        GPRs = None
        registerSet = frame.registers  # Returns an SBValueList.
        for regs in registerSet:
            if "general purpose" in regs.name.lower():
                GPRs = regs
                break

        if not GPRs:
            return None

        for reg in GPRs:
            self.register_ctx[reg.name] = int(reg.value, base=16)

        return struct.pack(f"{len(self.register_ctx)}Q", *self.register_ctx.values())


ARM64_GPR = [
    {
        "name": "x0",
        "bitsize": 64,
        "offset": 0,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 0,
        "dwarf": 0,
        "generic": "arg0",
        "alt-name": "arg0",
    },
    {
        "name": "x1",
        "bitsize": 64,
        "offset": 8,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 1,
        "dwarf": 1,
        "generic": "arg1",
        "alt-name": "arg1",
    },
    {
        "name": "x2",
        "bitsize": 64,
        "offset": 16,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 2,
        "dwarf": 2,
        "generic": "arg2",
        "alt-name": "arg2",
    },
    {
        "name": "x3",
        "bitsize": 64,
        "offset": 24,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 3,
        "dwarf": 3,
        "generic": "arg3",
        "alt-name": "arg3",
    },
    {
        "name": "x4",
        "bitsize": 64,
        "offset": 32,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 4,
        "dwarf": 4,
        "generic": "arg4",
        "alt-name": "arg4",
    },
    {
        "name": "x5",
        "bitsize": 64,
        "offset": 40,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 5,
        "dwarf": 5,
        "generic": "arg5",
        "alt-name": "arg5",
    },
    {
        "name": "x6",
        "bitsize": 64,
        "offset": 48,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 6,
        "dwarf": 6,
        "generic": "arg6",
        "alt-name": "arg6",
    },
    {
        "name": "x7",
        "bitsize": 64,
        "offset": 56,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 7,
        "dwarf": 7,
        "generic": "arg7",
        "alt-name": "arg7",
    },
    {
        "name": "x8",
        "bitsize": 64,
        "offset": 64,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 8,
        "dwarf": 8,
    },
    {
        "name": "x9",
        "bitsize": 64,
        "offset": 72,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 9,
        "dwarf": 9,
    },
    {
        "name": "x10",
        "bitsize": 64,
        "offset": 80,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 10,
        "dwarf": 10,
    },
    {
        "name": "x11",
        "bitsize": 64,
        "offset": 88,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 11,
        "dwarf": 11,
    },
    {
        "name": "x12",
        "bitsize": 64,
        "offset": 96,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 12,
        "dwarf": 12,
    },
    {
        "name": "x13",
        "bitsize": 64,
        "offset": 104,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 13,
        "dwarf": 13,
    },
    {
        "name": "x14",
        "bitsize": 64,
        "offset": 112,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 14,
        "dwarf": 14,
    },
    {
        "name": "x15",
        "bitsize": 64,
        "offset": 120,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 15,
        "dwarf": 15,
    },
    {
        "name": "x16",
        "bitsize": 64,
        "offset": 128,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 16,
        "dwarf": 16,
    },
    {
        "name": "x17",
        "bitsize": 64,
        "offset": 136,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 17,
        "dwarf": 17,
    },
    {
        "name": "x18",
        "bitsize": 64,
        "offset": 144,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 18,
        "dwarf": 18,
    },
    {
        "name": "x19",
        "bitsize": 64,
        "offset": 152,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 19,
        "dwarf": 19,
    },
    {
        "name": "x20",
        "bitsize": 64,
        "offset": 160,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 20,
        "dwarf": 20,
    },
    {
        "name": "x21",
        "bitsize": 64,
        "offset": 168,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 21,
        "dwarf": 21,
    },
    {
        "name": "x22",
        "bitsize": 64,
        "offset": 176,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 22,
        "dwarf": 22,
    },
    {
        "name": "x23",
        "bitsize": 64,
        "offset": 184,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 23,
        "dwarf": 23,
    },
    {
        "name": "x24",
        "bitsize": 64,
        "offset": 192,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 24,
        "dwarf": 24,
    },
    {
        "name": "x25",
        "bitsize": 64,
        "offset": 200,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 25,
        "dwarf": 25,
    },
    {
        "name": "x26",
        "bitsize": 64,
        "offset": 208,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 26,
        "dwarf": 26,
    },
    {
        "name": "x27",
        "bitsize": 64,
        "offset": 216,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 27,
        "dwarf": 27,
    },
    {
        "name": "x28",
        "bitsize": 64,
        "offset": 224,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 28,
        "dwarf": 28,
    },
    {
        "name": "x29",
        "bitsize": 64,
        "offset": 232,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 29,
        "dwarf": 29,
        "generic": "fp",
        "alt-name": "fp",
    },
    {
        "name": "x30",
        "bitsize": 64,
        "offset": 240,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 30,
        "dwarf": 30,
        "generic": "lr",
        "alt-name": "lr",
    },
    {
        "name": "sp",
        "bitsize": 64,
        "offset": 248,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 31,
        "dwarf": 31,
        "generic": "sp",
        "alt-name": "sp",
    },
    {
        "name": "pc",
        "bitsize": 64,
        "offset": 256,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 32,
        "dwarf": 32,
        "generic": "pc",
        "alt-name": "pc",
    },
    {
        "name": "cpsr",
        "bitsize": 32,
        "offset": 264,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 33,
        "dwarf": 33,
    },
]

INTEL64_GPR = [
    {
        "name": "rax",
        "bitsize": 64,
        "offset": 0,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 0,
        "dwarf": 0,
    },
    {
        "name": "rbx",
        "bitsize": 64,
        "offset": 8,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 3,
        "dwarf": 3,
    },
    {
        "name": "rcx",
        "bitsize": 64,
        "offset": 16,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 2,
        "dwarf": 2,
        "generic": "arg4",
        "alt-name": "arg4",
    },
    {
        "name": "rdx",
        "bitsize": 64,
        "offset": 24,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 1,
        "dwarf": 1,
        "generic": "arg3",
        "alt-name": "arg3",
    },
    {
        "name": "rdi",
        "bitsize": 64,
        "offset": 32,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 5,
        "dwarf": 5,
        "generic": "arg1",
        "alt-name": "arg1",
    },
    {
        "name": "rsi",
        "bitsize": 64,
        "offset": 40,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 4,
        "dwarf": 4,
        "generic": "arg2",
        "alt-name": "arg2",
    },
    {
        "name": "rbp",
        "bitsize": 64,
        "offset": 48,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 6,
        "dwarf": 6,
        "generic": "fp",
        "alt-name": "fp",
    },
    {
        "name": "rsp",
        "bitsize": 64,
        "offset": 56,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 7,
        "dwarf": 7,
        "generic": "sp",
        "alt-name": "sp",
    },
    {
        "name": "r8",
        "bitsize": 64,
        "offset": 64,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 8,
        "dwarf": 8,
        "generic": "arg5",
        "alt-name": "arg5",
    },
    {
        "name": "r9",
        "bitsize": 64,
        "offset": 72,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 9,
        "dwarf": 9,
        "generic": "arg6",
        "alt-name": "arg6",
    },
    {
        "name": "r10",
        "bitsize": 64,
        "offset": 80,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 10,
        "dwarf": 10,
    },
    {
        "name": "r11",
        "bitsize": 64,
        "offset": 88,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 11,
        "dwarf": 11,
    },
    {
        "name": "r12",
        "bitsize": 64,
        "offset": 96,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 12,
        "dwarf": 12,
    },
    {
        "name": "r13",
        "bitsize": 64,
        "offset": 104,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 13,
        "dwarf": 13,
    },
    {
        "name": "r14",
        "bitsize": 64,
        "offset": 112,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 14,
        "dwarf": 14,
    },
    {
        "name": "r15",
        "bitsize": 64,
        "offset": 120,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 15,
        "dwarf": 15,
    },
    {
        "name": "rip",
        "bitsize": 64,
        "offset": 128,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 16,
        "dwarf": 16,
        "generic": "pc",
        "alt-name": "pc",
    },
    {
        "name": "rflags",
        "bitsize": 64,
        "offset": 136,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "generic": "flags",
        "alt-name": "flags",
    },
    {
        "name": "cs",
        "bitsize": 64,
        "offset": 144,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
    },
    {
        "name": "fs",
        "bitsize": 64,
        "offset": 152,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
    },
    {
        "name": "gs",
        "bitsize": 64,
        "offset": 160,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
    },
]

ARM64_GPR = [
    {
        "name": "x0",
        "bitsize": 64,
        "offset": 0,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 0,
        "dwarf": 0,
        "generic": "arg0",
        "alt-name": "arg0",
    },
    {
        "name": "x1",
        "bitsize": 64,
        "offset": 8,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 1,
        "dwarf": 1,
        "generic": "arg1",
        "alt-name": "arg1",
    },
    {
        "name": "x2",
        "bitsize": 64,
        "offset": 16,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 2,
        "dwarf": 2,
        "generic": "arg2",
        "alt-name": "arg2",
    },
    {
        "name": "x3",
        "bitsize": 64,
        "offset": 24,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 3,
        "dwarf": 3,
        "generic": "arg3",
        "alt-name": "arg3",
    },
    {
        "name": "x4",
        "bitsize": 64,
        "offset": 32,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 4,
        "dwarf": 4,
        "generic": "arg4",
        "alt-name": "arg4",
    },
    {
        "name": "x5",
        "bitsize": 64,
        "offset": 40,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 5,
        "dwarf": 5,
        "generic": "arg5",
        "alt-name": "arg5",
    },
    {
        "name": "x6",
        "bitsize": 64,
        "offset": 48,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 6,
        "dwarf": 6,
        "generic": "arg6",
        "alt-name": "arg6",
    },
    {
        "name": "x7",
        "bitsize": 64,
        "offset": 56,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 7,
        "dwarf": 7,
        "generic": "arg7",
        "alt-name": "arg7",
    },
    {
        "name": "x8",
        "bitsize": 64,
        "offset": 64,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 8,
        "dwarf": 8,
    },
    {
        "name": "x9",
        "bitsize": 64,
        "offset": 72,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 9,
        "dwarf": 9,
    },
    {
        "name": "x10",
        "bitsize": 64,
        "offset": 80,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 10,
        "dwarf": 10,
    },
    {
        "name": "x11",
        "bitsize": 64,
        "offset": 88,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 11,
        "dwarf": 11,
    },
    {
        "name": "x12",
        "bitsize": 64,
        "offset": 96,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 12,
        "dwarf": 12,
    },
    {
        "name": "x13",
        "bitsize": 64,
        "offset": 104,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 13,
        "dwarf": 13,
    },
    {
        "name": "x14",
        "bitsize": 64,
        "offset": 112,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 14,
        "dwarf": 14,
    },
    {
        "name": "x15",
        "bitsize": 64,
        "offset": 120,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 15,
        "dwarf": 15,
    },
    {
        "name": "x16",
        "bitsize": 64,
        "offset": 128,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 16,
        "dwarf": 16,
    },
    {
        "name": "x17",
        "bitsize": 64,
        "offset": 136,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 17,
        "dwarf": 17,
    },
    {
        "name": "x18",
        "bitsize": 64,
        "offset": 144,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 18,
        "dwarf": 18,
    },
    {
        "name": "x19",
        "bitsize": 64,
        "offset": 152,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 19,
        "dwarf": 19,
    },
    {
        "name": "x20",
        "bitsize": 64,
        "offset": 160,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 20,
        "dwarf": 20,
    },
    {
        "name": "x21",
        "bitsize": 64,
        "offset": 168,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 21,
        "dwarf": 21,
    },
    {
        "name": "x22",
        "bitsize": 64,
        "offset": 176,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 22,
        "dwarf": 22,
    },
    {
        "name": "x23",
        "bitsize": 64,
        "offset": 184,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 23,
        "dwarf": 23,
    },
    {
        "name": "x24",
        "bitsize": 64,
        "offset": 192,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 24,
        "dwarf": 24,
    },
    {
        "name": "x25",
        "bitsize": 64,
        "offset": 200,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 25,
        "dwarf": 25,
    },
    {
        "name": "x26",
        "bitsize": 64,
        "offset": 208,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 26,
        "dwarf": 26,
    },
    {
        "name": "x27",
        "bitsize": 64,
        "offset": 216,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 27,
        "dwarf": 27,
    },
    {
        "name": "x28",
        "bitsize": 64,
        "offset": 224,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 28,
        "dwarf": 28,
    },
    {
        "name": "x29",
        "bitsize": 64,
        "offset": 232,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 29,
        "dwarf": 29,
        "generic": "fp",
        "alt-name": "fp",
    },
    {
        "name": "x30",
        "bitsize": 64,
        "offset": 240,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 30,
        "dwarf": 30,
        "generic": "lr",
        "alt-name": "lr",
    },
    {
        "name": "sp",
        "bitsize": 64,
        "offset": 248,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 31,
        "dwarf": 31,
        "generic": "sp",
        "alt-name": "sp",
    },
    {
        "name": "pc",
        "bitsize": 64,
        "offset": 256,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 32,
        "dwarf": 32,
        "generic": "pc",
        "alt-name": "pc",
    },
    {
        "name": "cpsr",
        "bitsize": 32,
        "offset": 264,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 33,
        "dwarf": 33,
    },
]

INTEL64_GPR = [
    {
        "name": "rax",
        "bitsize": 64,
        "offset": 0,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 0,
        "dwarf": 0,
    },
    {
        "name": "rbx",
        "bitsize": 64,
        "offset": 8,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 3,
        "dwarf": 3,
    },
    {
        "name": "rcx",
        "bitsize": 64,
        "offset": 16,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 2,
        "dwarf": 2,
        "generic": "arg4",
        "alt-name": "arg4",
    },
    {
        "name": "rdx",
        "bitsize": 64,
        "offset": 24,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 1,
        "dwarf": 1,
        "generic": "arg3",
        "alt-name": "arg3",
    },
    {
        "name": "rdi",
        "bitsize": 64,
        "offset": 32,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 5,
        "dwarf": 5,
        "generic": "arg1",
        "alt-name": "arg1",
    },
    {
        "name": "rsi",
        "bitsize": 64,
        "offset": 40,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 4,
        "dwarf": 4,
        "generic": "arg2",
        "alt-name": "arg2",
    },
    {
        "name": "rbp",
        "bitsize": 64,
        "offset": 48,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 6,
        "dwarf": 6,
        "generic": "fp",
        "alt-name": "fp",
    },
    {
        "name": "rsp",
        "bitsize": 64,
        "offset": 56,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 7,
        "dwarf": 7,
        "generic": "sp",
        "alt-name": "sp",
    },
    {
        "name": "r8",
        "bitsize": 64,
        "offset": 64,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 8,
        "dwarf": 8,
        "generic": "arg5",
        "alt-name": "arg5",
    },
    {
        "name": "r9",
        "bitsize": 64,
        "offset": 72,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 9,
        "dwarf": 9,
        "generic": "arg6",
        "alt-name": "arg6",
    },
    {
        "name": "r10",
        "bitsize": 64,
        "offset": 80,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 10,
        "dwarf": 10,
    },
    {
        "name": "r11",
        "bitsize": 64,
        "offset": 88,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 11,
        "dwarf": 11,
    },
    {
        "name": "r12",
        "bitsize": 64,
        "offset": 96,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 12,
        "dwarf": 12,
    },
    {
        "name": "r13",
        "bitsize": 64,
        "offset": 104,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 13,
        "dwarf": 13,
    },
    {
        "name": "r14",
        "bitsize": 64,
        "offset": 112,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 14,
        "dwarf": 14,
    },
    {
        "name": "r15",
        "bitsize": 64,
        "offset": 120,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 15,
        "dwarf": 15,
    },
    {
        "name": "rip",
        "bitsize": 64,
        "offset": 128,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "gcc": 16,
        "dwarf": 16,
        "generic": "pc",
        "alt-name": "pc",
    },
    {
        "name": "rflags",
        "bitsize": 64,
        "offset": 136,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
        "generic": "flags",
        "alt-name": "flags",
    },
    {
        "name": "cs",
        "bitsize": 64,
        "offset": 144,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
    },
    {
        "name": "fs",
        "bitsize": 64,
        "offset": 152,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
    },
    {
        "name": "gs",
        "bitsize": 64,
        "offset": 160,
        "encoding": "uint",
        "format": "hex",
        "set": 0,
    },
]
