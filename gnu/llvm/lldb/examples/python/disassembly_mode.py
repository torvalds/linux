""" Adds the 'toggle-disassembly' command to switch you into a disassembly only mode """
import lldb


class DisassemblyMode:
    def __init__(self, debugger, unused):
        self.dbg = debugger
        self.interp = debugger.GetCommandInterpreter()
        self.store_state()
        self.mode_off = True

    def store_state(self):
        self.dis_count = self.get_string_value("stop-disassembly-count")
        self.dis_display = self.get_string_value("stop-disassembly-display")
        self.before_count = self.get_string_value("stop-line-count-before")
        self.after_count = self.get_string_value("stop-line-count-after")

    def get_string_value(self, setting):
        result = lldb.SBCommandReturnObject()
        self.interp.HandleCommand("settings show " + setting, result)
        value = result.GetOutput().split(" = ")[1].rstrip("\n")
        return value

    def set_value(self, setting, value):
        result = lldb.SBCommandReturnObject()
        self.interp.HandleCommand("settings set " + setting + " " + value, result)

    def __call__(self, debugger, command, exe_ctx, result):
        if self.mode_off:
            self.mode_off = False
            self.store_state()
            self.set_value("stop-disassembly-display", "always")
            self.set_value("stop-disassembly-count", "8")
            self.set_value("stop-line-count-before", "0")
            self.set_value("stop-line-count-after", "0")
            result.AppendMessage("Disassembly mode on.")
        else:
            self.mode_off = True
            self.set_value("stop-disassembly-display", self.dis_display)
            self.set_value("stop-disassembly-count", self.dis_count)
            self.set_value("stop-line-count-before", self.before_count)
            self.set_value("stop-line-count-after", self.after_count)
            result.AppendMessage("Disassembly mode off.")

    def get_short_help(self):
        return "Toggles between a disassembly only mode and normal source mode\n"


def __lldb_init_module(debugger, unused):
    debugger.HandleCommand(
        "command script add -o -c disassembly_mode.DisassemblyMode toggle-disassembly"
    )
