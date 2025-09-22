""" Does a step-over then prints the local variables or only the ones passed in """
import lldb


class StepAndPrint:
    def __init__(self, debugger, unused):
        return

    def __call__(self, debugger, command, exe_ctx, result):
        # Set the command to synchronous so the step will complete
        # before we try to run the frame variable.
        old_async = debugger.GetAsync()
        debugger.SetAsync(False)

        debugger.HandleCommand("thread step-over")
        print("---------- Values: -------------------\n")
        debugger.HandleCommand("frame variable %s" % (command))

        debugger.SetAsync(old_async)

    def get_short_help(self):
        return (
            "Does a step-over then runs frame variable passing the command args to it\n"
        )


def __lldb_init_module(debugger, unused):
    debugger.HandleCommand("command script add -o -c step_and_print.StepAndPrint sap")
