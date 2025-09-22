import os
import os.path
import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.gdbclientutils import *


class GDBRemoteTestBase(TestBase):
    """
    Base class for GDB client tests.

    This class will setup and start a mock GDB server for the test to use.
    It also provides assertPacketLogContains, which simplifies the checking
    of packets sent by the client.
    """

    NO_DEBUG_INFO_TESTCASE = True
    server = None
    server_socket_class = TCPServerSocket

    def setUp(self):
        TestBase.setUp(self)
        self.server = MockGDBServer(self.server_socket_class())
        self.server.start()

    def tearDown(self):
        # TestBase.tearDown will kill the process, but we need to kill it early
        # so its client connection closes and we can stop the server before
        # finally calling the base tearDown.
        if self.process() is not None:
            self.process().Kill()
        self.server.stop()
        TestBase.tearDown(self)

    def createTarget(self, yaml_path):
        """
        Create a target by auto-generating the object based on the given yaml
        instructions.

        This will track the generated object so it can be automatically removed
        during tearDown.
        """
        yaml_base, ext = os.path.splitext(yaml_path)
        obj_path = self.getBuildArtifact(yaml_base)
        self.yaml2obj(yaml_path, obj_path)
        return self.dbg.CreateTarget(obj_path)

    def connect(self, target):
        """
        Create a process by connecting to the mock GDB server.

        Includes assertions that the process was successfully created.
        """
        listener = self.dbg.GetListener()
        error = lldb.SBError()
        process = target.ConnectRemote(
            listener, self.server.get_connect_url(), "gdb-remote", error
        )
        self.assertTrue(error.Success(), error.description)
        self.assertTrue(process, PROCESS_IS_VALID)
        return process

    def assertPacketLogContains(self, packets, log=None):
        """
        Assert that the mock server's packet log contains the given packets.

        The packet log includes all packets sent by the client and received
        by the server.  This fuction makes it easy to verify that the client
        sent the expected packets to the server.

        The check does not require that the packets be consecutive, but does
        require that they are ordered in the log as they ordered in the arg.
        """
        if log is None:
            log = self.server.responder.packetLog
        i = 0
        j = 0

        while i < len(packets) and j < len(log):
            if log[j] == packets[i]:
                i += 1
            j += 1
        if i < len(packets):
            self.fail(
                "Did not receive: %s\nLast 10 packets:\n\t%s"
                % (packets[i], "\n\t".join(log))
            )


class GDBPlatformClientTestBase(GDBRemoteTestBase):
    """
    Base class for platform server clients.

    This class extends GDBRemoteTestBase by automatically connecting
    via "platform connect" in the setUp() method.
    """

    def setUp(self):
        super().setUp()
        self.runCmd("platform select remote-gdb-server")
        self.runCmd("platform connect " + self.server.get_connect_url())
        self.assertTrue(self.dbg.GetSelectedPlatform().IsConnected())

    def tearDown(self):
        self.dbg.GetSelectedPlatform().DisconnectRemote()
        super().tearDown()
