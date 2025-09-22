import sys
import os.path
import inspect


class NopLogger:
    def __init__(self):
        pass

    def write(self, data):
        pass

    def flush(self):
        pass

    def close(self):
        pass


class StdoutLogger:
    def __init__(self):
        pass

    def write(self, data):
        print(data)

    def flush(self):
        pass

    def close(self):
        pass


class FileLogger:
    def __init__(self, name):
        self.file = None
        try:
            name = os.path.abspath(name)
            self.file = open(name, "a")
        except:
            try:
                self.file = open("formatters.log", "a")
            except:
                pass

    def write(self, data):
        if self.file is not None:
            print(data, file=self.file)
        else:
            print(data)

    def flush(self):
        if self.file is not None:
            self.file.flush()

    def close(self):
        if self.file is not None:
            self.file.close()
            self.file = None


# to enable logging:
# define lldb.formatters.Logger._lldb_formatters_debug_level to any number greater than 0
# if you define it to any value greater than 1, the log will be automatically flushed after each write (slower but should make sure most of the stuff makes it to the log even if we crash)
# if you define it to any value greater than 2, the calling function's details will automatically be logged (even slower, but provides additional details)
# if you need the log to go to a file instead of on screen, define
# lldb.formatters.Logger._lldb_formatters_debug_filename to a valid
# filename


class Logger:
    def __init__(self, autoflush=False, logcaller=False):
        global _lldb_formatters_debug_level
        global _lldb_formatters_debug_filename
        self.autoflush = autoflush
        want_log = False
        try:
            want_log = _lldb_formatters_debug_level > 0
        except:
            pass
        if not (want_log):
            self.impl = NopLogger()
            return
        want_file = False
        try:
            want_file = (
                _lldb_formatters_debug_filename is not None
                and _lldb_formatters_debug_filename != ""
                and _lldb_formatters_debug_filename != 0
            )
        except:
            pass
        if want_file:
            self.impl = FileLogger(_lldb_formatters_debug_filename)
        else:
            self.impl = StdoutLogger()
        try:
            self.autoflush = _lldb_formatters_debug_level > 1
        except:
            self.autoflush = autoflush
        want_caller_info = False
        try:
            want_caller_info = _lldb_formatters_debug_level > 2
        except:
            pass
        if want_caller_info:
            self._log_caller()

    def _log_caller(self):
        caller = inspect.stack()[2]
        try:
            if caller is not None and len(caller) > 3:
                self.write("Logging from function " + str(caller))
            else:
                self.write(
                    "Caller info not available - Required caller logging not possible"
                )
        finally:
            del caller  # needed per Python docs to avoid keeping objects alive longer than we care

    def write(self, data):
        self.impl.write(data)
        if self.autoflush:
            self.flush()

    def __rshift__(self, data):
        self.write(data)

    def flush(self):
        self.impl.flush()

    def close(self):
        self.impl.close()
