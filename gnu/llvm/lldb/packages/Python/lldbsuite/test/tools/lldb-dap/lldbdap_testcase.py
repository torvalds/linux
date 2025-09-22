import os
import time

import dap_server
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbplatformutil
import lldbgdbserverutils


class DAPTestCaseBase(TestBase):
    # set timeout based on whether ASAN was enabled or not. Increase
    # timeout by a factor of 10 if ASAN is enabled.
    timeoutval = 10 * (10 if ('ASAN_OPTIONS' in os.environ) else 1)
    NO_DEBUG_INFO_TESTCASE = True

    def create_debug_adaptor(self, lldbDAPEnv=None):
        """Create the Visual Studio Code debug adaptor"""
        self.assertTrue(
            is_exe(self.lldbDAPExec), "lldb-dap must exist and be executable"
        )
        log_file_path = self.getBuildArtifact("dap.txt")
        self.dap_server = dap_server.DebugAdaptorServer(
            executable=self.lldbDAPExec,
            init_commands=self.setUpCommands(),
            log_file=log_file_path,
            env=lldbDAPEnv,
        )

    def build_and_create_debug_adaptor(self, lldbDAPEnv=None):
        self.build()
        self.create_debug_adaptor(lldbDAPEnv)

    def set_source_breakpoints(self, source_path, lines, data=None):
        """Sets source breakpoints and returns an array of strings containing
        the breakpoint IDs ("1", "2") for each breakpoint that was set.
        Parameter data is array of data objects for breakpoints.
        Each object in data is 1:1 mapping with the entry in lines.
        It contains optional location/hitCondition/logMessage parameters.
        """
        response = self.dap_server.request_setBreakpoints(source_path, lines, data)
        if response is None:
            return []
        breakpoints = response["body"]["breakpoints"]
        breakpoint_ids = []
        for breakpoint in breakpoints:
            breakpoint_ids.append("%i" % (breakpoint["id"]))
        return breakpoint_ids

    def set_function_breakpoints(self, functions, condition=None, hitCondition=None):
        """Sets breakpoints by function name given an array of function names
        and returns an array of strings containing the breakpoint IDs
        ("1", "2") for each breakpoint that was set.
        """
        response = self.dap_server.request_setFunctionBreakpoints(
            functions, condition=condition, hitCondition=hitCondition
        )
        if response is None:
            return []
        breakpoints = response["body"]["breakpoints"]
        breakpoint_ids = []
        for breakpoint in breakpoints:
            breakpoint_ids.append("%i" % (breakpoint["id"]))
        return breakpoint_ids

    def waitUntil(self, condition_callback):
        for _ in range(20):
            if condition_callback():
                return True
            time.sleep(0.5)
        return False

    def verify_breakpoint_hit(self, breakpoint_ids):
        """Wait for the process we are debugging to stop, and verify we hit
        any breakpoint location in the "breakpoint_ids" array.
        "breakpoint_ids" should be a list of breakpoint ID strings
        (["1", "2"]). The return value from self.set_source_breakpoints()
        or self.set_function_breakpoints() can be passed to this function"""
        stopped_events = self.dap_server.wait_for_stopped()
        for stopped_event in stopped_events:
            if "body" in stopped_event:
                body = stopped_event["body"]
                if "reason" not in body:
                    continue
                if body["reason"] != "breakpoint":
                    continue
                if "description" not in body:
                    continue
                # Descriptions for breakpoints will be in the form
                # "breakpoint 1.1", so look for any description that matches
                # ("breakpoint 1.") in the description field as verification
                # that one of the breakpoint locations was hit. DAP doesn't
                # allow breakpoints to have multiple locations, but LLDB does.
                # So when looking at the description we just want to make sure
                # the right breakpoint matches and not worry about the actual
                # location.
                description = body["description"]
                for breakpoint_id in breakpoint_ids:
                    match_desc = "breakpoint %s." % (breakpoint_id)
                    if match_desc in description:
                        return
        self.assertTrue(False, "breakpoint not hit")

    def verify_stop_exception_info(self, expected_description):
        """Wait for the process we are debugging to stop, and verify the stop
        reason is 'exception' and that the description matches
        'expected_description'
        """
        stopped_events = self.dap_server.wait_for_stopped()
        for stopped_event in stopped_events:
            if "body" in stopped_event:
                body = stopped_event["body"]
                if "reason" not in body:
                    continue
                if body["reason"] != "exception":
                    continue
                if "description" not in body:
                    continue
                description = body["description"]
                if expected_description == description:
                    return True
        return False

    def verify_commands(self, flavor, output, commands):
        self.assertTrue(output and len(output) > 0, "expect console output")
        lines = output.splitlines()
        prefix = "(lldb) "
        for cmd in commands:
            found = False
            for line in lines:
                if len(cmd) > 0 and (cmd[0] == "!" or cmd[0] == "?"):
                    cmd = cmd[1:]
                if line.startswith(prefix) and cmd in line:
                    found = True
                    break
            self.assertTrue(
                found, "verify '%s' found in console output for '%s'" % (cmd, flavor)
            )

    def get_dict_value(self, d, key_path):
        """Verify each key in the key_path array is in contained in each
        dictionary within "d". Assert if any key isn't in the
        corresponding dictionary. This is handy for grabbing values from VS
        Code response dictionary like getting
        response['body']['stackFrames']
        """
        value = d
        for key in key_path:
            if key in value:
                value = value[key]
            else:
                self.assertTrue(
                    key in value,
                    'key "%s" from key_path "%s" not in "%s"' % (key, key_path, d),
                )
        return value

    def get_stackFrames_and_totalFramesCount(
        self, threadId=None, startFrame=None, levels=None, dump=False
    ):
        response = self.dap_server.request_stackTrace(
            threadId=threadId, startFrame=startFrame, levels=levels, dump=dump
        )
        if response:
            stackFrames = self.get_dict_value(response, ["body", "stackFrames"])
            totalFrames = self.get_dict_value(response, ["body", "totalFrames"])
            self.assertTrue(
                totalFrames > 0,
                "verify totalFrames count is provided by extension that supports "
                "async frames loading",
            )
            return (stackFrames, totalFrames)
        return (None, 0)

    def get_stackFrames(self, threadId=None, startFrame=None, levels=None, dump=False):
        (stackFrames, totalFrames) = self.get_stackFrames_and_totalFramesCount(
            threadId=threadId, startFrame=startFrame, levels=levels, dump=dump
        )
        return stackFrames

    def get_source_and_line(self, threadId=None, frameIndex=0):
        stackFrames = self.get_stackFrames(
            threadId=threadId, startFrame=frameIndex, levels=1
        )
        if stackFrames is not None:
            stackFrame = stackFrames[0]
            ["source", "path"]
            if "source" in stackFrame:
                source = stackFrame["source"]
                if "path" in source:
                    if "line" in stackFrame:
                        return (source["path"], stackFrame["line"])
        return ("", 0)

    def get_stdout(self, timeout=0.0):
        return self.dap_server.get_output("stdout", timeout=timeout)

    def get_console(self, timeout=0.0):
        return self.dap_server.get_output("console", timeout=timeout)

    def collect_console(self, timeout_secs, pattern=None):
        return self.dap_server.collect_output(
            "console", timeout_secs=timeout_secs, pattern=pattern
        )

    def get_local_as_int(self, name, threadId=None):
        value = self.dap_server.get_local_variable_value(name, threadId=threadId)
        # 'value' may have the variable value and summary.
        # Extract the variable value since summary can have nonnumeric characters.
        value = value.split(" ")[0]
        if value.startswith("0x"):
            return int(value, 16)
        elif value.startswith("0"):
            return int(value, 8)
        else:
            return int(value)

    def set_local(self, name, value, id=None):
        """Set a top level local variable only."""
        return self.dap_server.request_setVariable(1, name, str(value), id=id)

    def set_global(self, name, value, id=None):
        """Set a top level global variable only."""
        return self.dap_server.request_setVariable(2, name, str(value), id=id)

    def stepIn(self, threadId=None, targetId=None, waitForStop=True):
        self.dap_server.request_stepIn(threadId=threadId, targetId=targetId)
        if waitForStop:
            return self.dap_server.wait_for_stopped()
        return None

    def stepOver(self, threadId=None, waitForStop=True):
        self.dap_server.request_next(threadId=threadId)
        if waitForStop:
            return self.dap_server.wait_for_stopped()
        return None

    def stepOut(self, threadId=None, waitForStop=True):
        self.dap_server.request_stepOut(threadId=threadId)
        if waitForStop:
            return self.dap_server.wait_for_stopped()
        return None

    def continue_to_next_stop(self):
        self.dap_server.request_continue()
        return self.dap_server.wait_for_stopped()

    def continue_to_breakpoints(self, breakpoint_ids):
        self.dap_server.request_continue()
        self.verify_breakpoint_hit(breakpoint_ids)

    def continue_to_exception_breakpoint(self, filter_label):
        self.dap_server.request_continue()
        self.assertTrue(
            self.verify_stop_exception_info(filter_label),
            'verify we got "%s"' % (filter_label),
        )

    def continue_to_exit(self, exitCode=0):
        self.dap_server.request_continue()
        stopped_events = self.dap_server.wait_for_stopped()
        self.assertEqual(
            len(stopped_events), 1, "stopped_events = {}".format(stopped_events)
        )
        self.assertEqual(
            stopped_events[0]["event"], "exited", "make sure program ran to completion"
        )
        self.assertEqual(
            stopped_events[0]["body"]["exitCode"],
            exitCode,
            "exitCode == %i" % (exitCode),
        )

    def disassemble(self, threadId=None, frameIndex=None):
        stackFrames = self.get_stackFrames(
            threadId=threadId, startFrame=frameIndex, levels=1
        )
        self.assertIsNotNone(stackFrames)
        memoryReference = stackFrames[0]["instructionPointerReference"]
        self.assertIsNotNone(memoryReference)

        if memoryReference not in self.dap_server.disassembled_instructions:
            self.dap_server.request_disassemble(memoryReference=memoryReference)

        return self.dap_server.disassembled_instructions[memoryReference]

    def attach(
        self,
        program=None,
        pid=None,
        waitFor=None,
        trace=None,
        initCommands=None,
        preRunCommands=None,
        stopCommands=None,
        exitCommands=None,
        attachCommands=None,
        coreFile=None,
        disconnectAutomatically=True,
        terminateCommands=None,
        postRunCommands=None,
        sourceMap=None,
        sourceInitFile=False,
        expectFailure=False,
        gdbRemotePort=None,
        gdbRemoteHostname=None,
    ):
        """Build the default Makefile target, create the DAP debug adaptor,
        and attach to the process.
        """

        # Make sure we disconnect and terminate the DAP debug adaptor even
        # if we throw an exception during the test case.
        def cleanup():
            if disconnectAutomatically:
                self.dap_server.request_disconnect(terminateDebuggee=True)
            self.dap_server.terminate()

        # Execute the cleanup function during test case tear down.
        self.addTearDownHook(cleanup)
        # Initialize and launch the program
        self.dap_server.request_initialize(sourceInitFile)
        response = self.dap_server.request_attach(
            program=program,
            pid=pid,
            waitFor=waitFor,
            trace=trace,
            initCommands=initCommands,
            preRunCommands=preRunCommands,
            stopCommands=stopCommands,
            exitCommands=exitCommands,
            attachCommands=attachCommands,
            terminateCommands=terminateCommands,
            coreFile=coreFile,
            postRunCommands=postRunCommands,
            sourceMap=sourceMap,
            gdbRemotePort=gdbRemotePort,
            gdbRemoteHostname=gdbRemoteHostname,
        )
        if expectFailure:
            return response
        if not (response and response["success"]):
            self.assertTrue(
                response["success"], "attach failed (%s)" % (response["message"])
            )

    def launch(
        self,
        program=None,
        args=None,
        cwd=None,
        env=None,
        stopOnEntry=False,
        disableASLR=True,
        disableSTDIO=False,
        shellExpandArguments=False,
        trace=False,
        initCommands=None,
        preRunCommands=None,
        stopCommands=None,
        exitCommands=None,
        terminateCommands=None,
        sourcePath=None,
        debuggerRoot=None,
        sourceInitFile=False,
        launchCommands=None,
        sourceMap=None,
        disconnectAutomatically=True,
        runInTerminal=False,
        expectFailure=False,
        postRunCommands=None,
        enableAutoVariableSummaries=False,
        enableSyntheticChildDebugging=False,
        commandEscapePrefix=None,
        customFrameFormat=None,
        customThreadFormat=None,
    ):
        """Sending launch request to dap"""

        # Make sure we disconnect and terminate the DAP debug adapter,
        # if we throw an exception during the test case
        def cleanup():
            if disconnectAutomatically:
                self.dap_server.request_disconnect(terminateDebuggee=True)
            self.dap_server.terminate()

        # Execute the cleanup function during test case tear down.
        self.addTearDownHook(cleanup)

        # Initialize and launch the program
        self.dap_server.request_initialize(sourceInitFile)
        response = self.dap_server.request_launch(
            program,
            args=args,
            cwd=cwd,
            env=env,
            stopOnEntry=stopOnEntry,
            disableASLR=disableASLR,
            disableSTDIO=disableSTDIO,
            shellExpandArguments=shellExpandArguments,
            trace=trace,
            initCommands=initCommands,
            preRunCommands=preRunCommands,
            stopCommands=stopCommands,
            exitCommands=exitCommands,
            terminateCommands=terminateCommands,
            sourcePath=sourcePath,
            debuggerRoot=debuggerRoot,
            launchCommands=launchCommands,
            sourceMap=sourceMap,
            runInTerminal=runInTerminal,
            postRunCommands=postRunCommands,
            enableAutoVariableSummaries=enableAutoVariableSummaries,
            enableSyntheticChildDebugging=enableSyntheticChildDebugging,
            commandEscapePrefix=commandEscapePrefix,
            customFrameFormat=customFrameFormat,
            customThreadFormat=customThreadFormat,
        )

        if expectFailure:
            return response

        if not (response and response["success"]):
            self.assertTrue(
                response["success"], "launch failed (%s)" % (response["message"])
            )
        return response

    def build_and_launch(
        self,
        program,
        args=None,
        cwd=None,
        env=None,
        stopOnEntry=False,
        disableASLR=True,
        disableSTDIO=False,
        shellExpandArguments=False,
        trace=False,
        initCommands=None,
        preRunCommands=None,
        stopCommands=None,
        exitCommands=None,
        terminateCommands=None,
        sourcePath=None,
        debuggerRoot=None,
        sourceInitFile=False,
        runInTerminal=False,
        disconnectAutomatically=True,
        postRunCommands=None,
        lldbDAPEnv=None,
        enableAutoVariableSummaries=False,
        enableSyntheticChildDebugging=False,
        commandEscapePrefix=None,
        customFrameFormat=None,
        customThreadFormat=None,
        launchCommands=None,
        expectFailure=False,
    ):
        """Build the default Makefile target, create the DAP debug adaptor,
        and launch the process.
        """
        self.build_and_create_debug_adaptor(lldbDAPEnv)
        self.assertTrue(os.path.exists(program), "executable must exist")

        return self.launch(
            program,
            args,
            cwd,
            env,
            stopOnEntry,
            disableASLR,
            disableSTDIO,
            shellExpandArguments,
            trace,
            initCommands,
            preRunCommands,
            stopCommands,
            exitCommands,
            terminateCommands,
            sourcePath,
            debuggerRoot,
            sourceInitFile,
            runInTerminal=runInTerminal,
            disconnectAutomatically=disconnectAutomatically,
            postRunCommands=postRunCommands,
            enableAutoVariableSummaries=enableAutoVariableSummaries,
            enableSyntheticChildDebugging=enableSyntheticChildDebugging,
            commandEscapePrefix=commandEscapePrefix,
            customFrameFormat=customFrameFormat,
            customThreadFormat=customThreadFormat,
            launchCommands=launchCommands,
            expectFailure=expectFailure,
        )

    def getBuiltinDebugServerTool(self):
        # Tries to find simulation/lldb-server/gdbserver tool path.
        server_tool = None
        if lldbplatformutil.getPlatform() == "linux":
            server_tool = lldbgdbserverutils.get_lldb_server_exe()
            if server_tool is None:
                self.dap_server.request_disconnect(terminateDebuggee=True)
                self.assertIsNotNone(server_tool, "lldb-server not found.")
        elif lldbplatformutil.getPlatform() == "macosx":
            server_tool = lldbgdbserverutils.get_debugserver_exe()
            if server_tool is None:
                self.dap_server.request_disconnect(terminateDebuggee=True)
                self.assertIsNotNone(server_tool, "debugserver not found.")
        return server_tool
