import os, json, struct, signal, uuid, tempfile

from typing import Any, Dict

import lldb
from lldb.plugins.scripted_process import ScriptedProcess
from lldb.plugins.scripted_process import ScriptedThread

from lldb.macosx.crashlog import CrashLog, CrashLogParser


class CrashLogScriptedProcess(ScriptedProcess):
    def set_crashlog(self, crashlog):
        self.crashlog = crashlog
        if self.crashlog.process_id:
            if type(self.crashlog.process_id) is int:
                self.pid = self.crashlog.process_id
            elif type(self.crashlog.process_id) is str:
                self.pid = int(self.crashlog.process_id, 0)
            else:
                self.pid = super().get_process_id()
        self.addr_mask = self.crashlog.addr_mask
        self.crashed_thread_idx = self.crashlog.crashed_thread_idx
        self.loaded_images = []
        self.exception = self.crashlog.exception
        self.app_specific_thread = None
        if hasattr(self.crashlog, "asi"):
            self.metadata["asi"] = self.crashlog.asi
        if hasattr(self.crashlog, "asb"):
            self.extended_thread_info = self.crashlog.asb

        crashlog.load_images(self.options, self.loaded_images)

        for thread in self.crashlog.threads:
            if (
                hasattr(thread, "app_specific_backtrace")
                and thread.app_specific_backtrace
            ):
                # We don't want to include the Application Specific Backtrace
                # Thread into the Scripted Process' Thread list.
                # Instead, we will try to extract the stackframe pcs from the
                # backtrace and inject that as the extended thread info.
                self.app_specific_thread = thread
                continue

            self.threads[thread.index] = CrashLogScriptedThread(self, None, thread)

        if self.app_specific_thread:
            self.extended_thread_info = CrashLogScriptedThread.resolve_stackframes(
                self.app_specific_thread, self.addr_mask, self.target
            )

    class CrashLogOptions:
        load_all_images = False
        crashed_only = True
        no_parallel_image_loading = False

    def __init__(self, exe_ctx: lldb.SBExecutionContext, args: lldb.SBStructuredData):
        super().__init__(exe_ctx, args)

        if not self.target or not self.target.IsValid():
            # Return error
            return

        self.crashlog_path = None

        crashlog_path = args.GetValueForKey("file_path")
        if crashlog_path and crashlog_path.IsValid():
            if crashlog_path.GetType() == lldb.eStructuredDataTypeString:
                self.crashlog_path = crashlog_path.GetStringValue(4096)

        if not self.crashlog_path:
            # Return error
            return

        self.options = self.CrashLogOptions()

        load_all_images = args.GetValueForKey("load_all_images")
        if load_all_images and load_all_images.IsValid():
            if load_all_images.GetType() == lldb.eStructuredDataTypeBoolean:
                self.options.load_all_images = load_all_images.GetBooleanValue()

        crashed_only = args.GetValueForKey("crashed_only")
        if crashed_only and crashed_only.IsValid():
            if crashed_only.GetType() == lldb.eStructuredDataTypeBoolean:
                self.options.crashed_only = crashed_only.GetBooleanValue()

        no_parallel_image_loading = args.GetValueForKey("no_parallel_image_loading")
        if no_parallel_image_loading and no_parallel_image_loading.IsValid():
            if no_parallel_image_loading.GetType() == lldb.eStructuredDataTypeBoolean:
                self.options.no_parallel_image_loading = (
                    no_parallel_image_loading.GetBooleanValue()
                )

        self.pid = super().get_process_id()
        self.crashed_thread_idx = 0
        self.exception = None
        self.extended_thread_info = None

    def read_memory_at_address(
        self, addr: int, size: int, error: lldb.SBError
    ) -> lldb.SBData:
        # NOTE: CrashLogs don't contain any memory.
        return lldb.SBData()

    def get_loaded_images(self):
        # TODO: Iterate over corefile_target modules and build a data structure
        # from it.
        return self.loaded_images

    def should_stop(self) -> bool:
        return True

    def is_alive(self) -> bool:
        return True

    def get_scripted_thread_plugin(self):
        return CrashLogScriptedThread.__module__ + "." + CrashLogScriptedThread.__name__

    def get_process_metadata(self):
        return self.metadata


class CrashLogScriptedThread(ScriptedThread):
    def create_register_ctx(self):
        if not self.has_crashed:
            return dict.fromkeys(
                [*map(lambda reg: reg["name"], self.register_info["registers"])], 0
            )

        if not self.backing_thread or not len(self.backing_thread.registers):
            return dict.fromkeys(
                [*map(lambda reg: reg["name"], self.register_info["registers"])], 0
            )

        for reg in self.register_info["registers"]:
            reg_name = reg["name"]
            if reg_name in self.backing_thread.registers:
                self.register_ctx[reg_name] = self.backing_thread.registers[reg_name]
            else:
                self.register_ctx[reg_name] = 0

        return self.register_ctx

    def resolve_stackframes(thread, addr_mask, target):
        frames = []
        for frame in thread.frames:
            frame_pc = frame.pc & addr_mask
            pc = frame_pc if frame.index == 0 or frame_pc == 0 else frame_pc - 1
            sym_addr = lldb.SBAddress()
            sym_addr.SetLoadAddress(pc, target)
            if not sym_addr.IsValid():
                continue
            frames.append({"idx": frame.index, "pc": pc})
        return frames

    def create_stackframes(self):
        if not (self.originating_process.options.load_all_images or self.has_crashed):
            return None

        if not self.backing_thread or not len(self.backing_thread.frames):
            return None

        self.frames = CrashLogScriptedThread.resolve_stackframes(
            self.backing_thread, self.originating_process.addr_mask, self.target
        )

        return self.frames

    def __init__(self, process, args, crashlog_thread):
        super().__init__(process, args)

        self.backing_thread = crashlog_thread
        self.idx = self.backing_thread.index
        self.tid = self.backing_thread.id
        self.name = self.backing_thread.name
        self.queue = self.backing_thread.queue
        self.has_crashed = self.originating_process.crashed_thread_idx == self.idx
        self.create_stackframes()

    def get_state(self):
        if not self.has_crashed:
            return lldb.eStateStopped
        return lldb.eStateCrashed

    def get_stop_reason(self) -> Dict[str, Any]:
        if not self.has_crashed:
            return {"type": lldb.eStopReasonNone}
        # TODO: Investigate what stop reason should be reported when crashed
        stop_reason = {"type": lldb.eStopReasonException, "data": {}}
        if self.originating_process.exception:
            stop_reason["data"]["mach_exception"] = self.originating_process.exception
        return stop_reason

    def get_register_context(self) -> str:
        if not self.register_ctx:
            self.register_ctx = self.create_register_ctx()

        return struct.pack(
            "{}Q".format(len(self.register_ctx)), *self.register_ctx.values()
        )

    def get_extended_info(self):
        if self.has_crashed:
            self.extended_info = self.originating_process.extended_thread_info
        return self.extended_info
