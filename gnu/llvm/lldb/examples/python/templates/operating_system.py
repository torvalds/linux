from abc import abstractmethod

import lldb
import struct

from lldb.plugins.scripted_process import ScriptedThread


class OperatingSystem(ScriptedThread):
    """
    Class that provides data for an instance of a LLDB 'OperatingSystemPython' plug-in class.

    .. code-block:: python

        thread_info = {
            "tid": tid,
            "name": "four",
            "queue": "queue4",
            "state": "stopped",
            "stop_reason": "none",
            "core" : 2
        }

    - tid : thread ID (mandatory)
    - name : thread name (optional key/value pair)
    - queue : thread dispatch queue name (optional key/value pair)
    - state : thread state (mandatory, set to 'stopped' for now)
    - core : the index of the core (lldb) thread that this OS Thread should shadow
    - stop_reason : thread stop reason. (mandatory, usually set to 'none')
        Possible values include:
        - 'breakpoint': thread is stopped at a breakpoint
        - 'none': thread is stopped because the process is stopped
        - 'trace': thread is stopped after single stepping
        The usual value for this while threads are in memory is 'none'
    - register_data_addr : the address of the register data in memory (optional key/value pair)
        Specifying this key/value pair for a thread will avoid a call to get_register_data()
        and can be used when your registers are in a thread context structure that is contiguous
        in memory. Don't specify this if your register layout in memory doesn't match the layout
        described by the dictionary returned from a call to the get_register_info() method.
    """

    def __init__(self, process):
        """Initialization needs a valid lldb.SBProcess object. This plug-in
        will get created after a live process is valid and has stopped for the
        first time.

        Args:
            process (lldb.SBProcess): The process owning this thread.
        """
        self.registers = None
        super().__init__(process, None)
        self.registers = self.register_info
        self.threads = []

    def create_thread(self, tid, context):
        """Lazily create an operating system thread using a thread information
        dictionary and an optional operating system thread context address.
        This method is called manually, using the SBAPI
        `lldb.SBProcess.CreateOSPluginThread` affordance.

        Args:
            tid (int): Thread ID to get `thread_info` dictionary for.
            context (int): Address of the operating system thread struct.

        Returns:
            Dict: The `thread_info` dictionary containing the various information
            for lldb to create a Thread object and add it to the process thread list.
        """
        return None

    @abstractmethod
    def get_thread_info(self):
        """Get the list of operating system threads. This method gets called
        automatically every time the process stops and it needs to update its
        thread list.

        Returns:
            List[thread_info]: A list of `os_thread` dictionaries
                containing at least for each entry, the thread id, it's name,
                queue, state, stop reason. It can also contain a
                `register_data_addr`. The list can be empty.
        """
        pass

    @abstractmethod
    def get_register_data(self, tid):
        """Get the operating system thread register context for given a thread
        id. This method is called when unwinding the stack of one of the
        operating system threads.

        Args:
            tid (int): Thread ID to get register context for.

        Returns:
            str: A byte representing all register's value.
        """
        pass

    def get_register_context(self):
        pass

    def get_stop_reason(self):
        pass
