from lldbsuite.test.lldbtest import *
import os
import time
import json

ADDRESS_REGEX = "0x[0-9a-fA-F]*"


# Decorator that runs a test with both modes of USE_SB_API.
# It assumes that no tests can be executed in parallel.
def testSBAPIAndCommands(func):
    def wrapper(*args, **kwargs):
        TraceIntelPTTestCaseBase.USE_SB_API = True
        func(*args, **kwargs)
        TraceIntelPTTestCaseBase.USE_SB_API = False
        func(*args, **kwargs)

    return wrapper


# Class that should be used by all python Intel PT tests.
#
# It has a handy check that skips the test if the intel-pt plugin is not enabled.
#
# It also contains many functions that can test both the SB API or the command line version
# of the most important tracing actions.
class TraceIntelPTTestCaseBase(TestBase):
    NO_DEBUG_INFO_TESTCASE = True

    # If True, the trace test methods will use the SB API, otherwise they'll use raw commands.
    USE_SB_API = False

    def setUp(self):
        TestBase.setUp(self)
        if "intel-pt" not in configuration.enabled_plugins:
            self.skipTest("The intel-pt test plugin is not enabled")

    def skipIfPerCpuTracingIsNotSupported(self):
        def is_supported():
            try:
                with open("/proc/sys/kernel/perf_event_paranoid", "r") as permissions:
                    value = int(permissions.readlines()[0])
                    if value <= 0:
                        return True
            except:
                return False

        if not is_supported():
            self.skipTest(
                "Per cpu tracing is not supported. You need "
                "/proc/sys/kernel/perf_event_paranoid to be 0 or -1. "
                "You can use `sudo sysctl -w kernel.perf_event_paranoid=-1` for that."
            )

    def getTraceOrCreate(self):
        if not self.target().GetTrace().IsValid():
            error = lldb.SBError()
            self.target().CreateTrace(error)
        return self.target().GetTrace()

    def assertSBError(self, sberror, error=False):
        if error:
            self.assertTrue(sberror.Fail())
        else:
            self.assertSuccess(sberror)

    def createConfiguration(
        self,
        iptTraceSize=None,
        processBufferSizeLimit=None,
        enableTsc=False,
        psbPeriod=None,
        perCpuTracing=False,
    ):
        obj = {}
        if processBufferSizeLimit is not None:
            obj["processBufferSizeLimit"] = processBufferSizeLimit
        if iptTraceSize is not None:
            obj["iptTraceSize"] = iptTraceSize
        if psbPeriod is not None:
            obj["psbPeriod"] = psbPeriod
        obj["enableTsc"] = enableTsc
        obj["perCpuTracing"] = perCpuTracing

        configuration = lldb.SBStructuredData()
        configuration.SetFromJSON(json.dumps(obj))
        return configuration

    def traceStartThread(
        self,
        thread=None,
        error=False,
        substrs=None,
        iptTraceSize=None,
        enableTsc=False,
        psbPeriod=None,
    ):
        if self.USE_SB_API:
            trace = self.getTraceOrCreate()
            thread = thread if thread is not None else self.thread()
            configuration = self.createConfiguration(
                iptTraceSize=iptTraceSize, enableTsc=enableTsc, psbPeriod=psbPeriod
            )
            self.assertSBError(trace.Start(thread, configuration), error)
        else:
            command = "thread trace start"
            if thread is not None:
                command += " " + str(thread.GetIndexID())
            if iptTraceSize is not None:
                command += " -s " + str(iptTraceSize)
            if enableTsc:
                command += " --tsc"
            if psbPeriod is not None:
                command += " --psb-period " + str(psbPeriod)
            self.expect(command, error=error, substrs=substrs)

    def traceStartProcess(
        self,
        processBufferSizeLimit=None,
        error=False,
        substrs=None,
        enableTsc=False,
        psbPeriod=None,
        perCpuTracing=False,
    ):
        if self.USE_SB_API:
            trace = self.getTraceOrCreate()
            configuration = self.createConfiguration(
                processBufferSizeLimit=processBufferSizeLimit,
                enableTsc=enableTsc,
                psbPeriod=psbPeriod,
                perCpuTracing=perCpuTracing,
            )
            self.assertSBError(trace.Start(configuration), error=error)
        else:
            command = "process trace start"
            if processBufferSizeLimit is not None:
                command += " -l " + str(processBufferSizeLimit)
            if enableTsc:
                command += " --tsc"
            if psbPeriod is not None:
                command += " --psb-period " + str(psbPeriod)
            if perCpuTracing:
                command += " --per-cpu-tracing"
            self.expect(command, error=error, substrs=substrs)

    def traceStopProcess(self):
        if self.USE_SB_API:
            self.assertSuccess(self.target().GetTrace().Stop())
        else:
            self.expect("process trace stop")

    def traceStopThread(self, thread=None, error=False, substrs=None):
        if self.USE_SB_API:
            thread = thread if thread is not None else self.thread()
            self.assertSBError(self.target().GetTrace().Stop(thread), error)

        else:
            command = "thread trace stop"
            if thread is not None:
                command += " " + str(thread.GetIndexID())
            self.expect(command, error=error, substrs=substrs)

    def traceLoad(self, traceDescriptionFilePath, error=False, substrs=None):
        if self.USE_SB_API:
            traceDescriptionFile = lldb.SBFileSpec(traceDescriptionFilePath, True)
            loadTraceError = lldb.SBError()
            self.dbg.LoadTraceFromFile(loadTraceError, traceDescriptionFile)
            self.assertSBError(loadTraceError, error)
        else:
            command = f"trace load -v {traceDescriptionFilePath}"
            self.expect(command, error=error, substrs=substrs)

    def traceSave(self, traceBundleDir, compact=False, error=False, substrs=None):
        if self.USE_SB_API:
            save_error = lldb.SBError()
            self.target().GetTrace().SaveToDisk(
                save_error, lldb.SBFileSpec(traceBundleDir), compact
            )
            self.assertSBError(save_error, error)
        else:
            command = f"trace save {traceBundleDir}"
            if compact:
                command += " -c"
            self.expect(command, error=error, substrs=substrs)
