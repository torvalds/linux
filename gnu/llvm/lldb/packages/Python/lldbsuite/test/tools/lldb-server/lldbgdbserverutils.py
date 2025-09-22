"""Module for supporting unit testing of the lldb-server debug monitor exe.
"""

import binascii
import os
import os.path
import platform
import re
import socket
import subprocess
from lldbsuite.support import seven
from lldbsuite.test.lldbtest import *
from lldbsuite.test import configuration
from textwrap import dedent
import shutil
import select
import random

def _get_support_exe(basename):
    support_dir = lldb.SBHostOS.GetLLDBPath(lldb.ePathTypeSupportExecutableDir)

    return shutil.which(basename, path=support_dir.GetDirectory())


def get_lldb_server_exe():
    """Return the lldb-server exe path.

    Returns:
        A path to the lldb-server exe if it is found to exist; otherwise,
        returns None.
    """

    return _get_support_exe("lldb-server")


def get_debugserver_exe():
    """Return the debugserver exe path.

    Returns:
        A path to the debugserver exe if it is found to exist; otherwise,
        returns None.
    """
    if (
        configuration.arch
        and configuration.arch == "x86_64"
        and platform.machine().startswith("arm64")
    ):
        return "/Library/Apple/usr/libexec/oah/debugserver"

    return _get_support_exe("debugserver")


_LOG_LINE_REGEX = re.compile(
    r"^(lldb-server|debugserver)\s+<\s*(\d+)>\s+(read|send)\s+packet:\s+(.+)$"
)


def _is_packet_lldb_gdbserver_input(packet_type, llgs_input_is_read):
    """Return whether a given packet is input for lldb-gdbserver.

    Args:
        packet_type: a string indicating 'send' or 'receive', from a
            gdbremote packet protocol log.

        llgs_input_is_read: true if lldb-gdbserver input (content sent to
            lldb-gdbserver) is listed as 'read' or 'send' in the packet
            log entry.

    Returns:
        True if the packet should be considered input for lldb-gdbserver; False
        otherwise.
    """
    if packet_type == "read":
        # when llgs is the read side, then a read packet is meant for
        # input to llgs (when captured from the llgs/debugserver exe).
        return llgs_input_is_read
    elif packet_type == "send":
        # when llgs is the send side, then a send packet is meant to
        # be input to llgs (when captured from the lldb exe).
        return not llgs_input_is_read
    else:
        # don't understand what type of packet this is
        raise "Unknown packet type: {}".format(packet_type)


_STRIP_CHECKSUM_REGEX = re.compile(r"#[0-9a-fA-F]{2}$")
_STRIP_COMMAND_PREFIX_REGEX = re.compile(r"^\$")
_STRIP_COMMAND_PREFIX_M_REGEX = re.compile(r"^\$m")


def assert_packets_equal(asserter, actual_packet, expected_packet):
    # strip off the checksum digits of the packet.  When we're in
    # no-ack mode, the # checksum is ignored, and should not be cause
    # for a mismatched packet.
    actual_stripped = _STRIP_CHECKSUM_REGEX.sub("", actual_packet)
    expected_stripped = _STRIP_CHECKSUM_REGEX.sub("", expected_packet)
    asserter.assertEqual(actual_stripped, expected_stripped)


def expect_lldb_gdbserver_replay(
    asserter, server, test_sequence, timeout_seconds, logger=None
):
    """Replay socket communication with lldb-gdbserver and verify responses.

    Args:
        asserter: the object providing assertEqual(first, second, msg=None), e.g. TestCase instance.

        test_sequence: a GdbRemoteTestSequence instance that describes
            the messages sent to the gdb remote and the responses
            expected from it.

        timeout_seconds: any response taking more than this number of
           seconds will cause an exception to be raised.

        logger: a Python logger instance.

    Returns:
        The context dictionary from running the given gdbremote
        protocol sequence.  This will contain any of the capture
        elements specified to any GdbRemoteEntry instances in
        test_sequence.

        The context will also contain an entry, context["O_content"]
        which contains the text from the inferior received via $O
        packets.  $O packets should not attempt to be matched
        directly since they are not entirely deterministic as to
        how many arrive and how much text is in each one.

        context["O_count"] will contain an integer of the number of
        O packets received.
    """

    # Ensure we have some work to do.
    if len(test_sequence.entries) < 1:
        return {}

    context = {"O_count": 0, "O_content": ""}

    # Grab the first sequence entry.
    sequence_entry = test_sequence.entries.pop(0)

    # While we have an active sequence entry, send messages
    # destined for the stub and collect/match/process responses
    # expected from the stub.
    while sequence_entry:
        if sequence_entry.is_send_to_remote():
            # This is an entry to send to the remote debug monitor.
            send_packet = sequence_entry.get_send_packet()
            if logger:
                if len(send_packet) == 1 and send_packet[0] == chr(3):
                    packet_desc = "^C"
                else:
                    packet_desc = send_packet
                logger.info("sending packet to remote: {}".format(packet_desc))
            server.send_raw(send_packet.encode())
        else:
            # This is an entry expecting to receive content from the remote
            # debug monitor.

            # We'll pull from (and wait on) the queue appropriate for the type of matcher.
            # We keep separate queues for process output (coming from non-deterministic
            # $O packet division) and for all other packets.
            try:
                if sequence_entry.is_output_matcher():
                    # Grab next entry from the output queue.
                    content = server.get_raw_output_packet()
                else:
                    content = server.get_raw_normal_packet()
                content = seven.bitcast_to_string(content)
            except socket.timeout:
                asserter.fail(
                    "timed out while waiting for '{}':\n{}".format(
                        sequence_entry, server
                    )
                )

            # Give the sequence entry the opportunity to match the content.
            # Output matchers might match or pass after more output accumulates.
            # Other packet types generally must match.
            asserter.assertIsNotNone(content)
            context = sequence_entry.assert_match(asserter, content, context=context)

        # Move on to next sequence entry as needed.  Some sequence entries support executing multiple
        # times in different states (for looping over query/response
        # packets).
        if sequence_entry.is_consumed():
            if len(test_sequence.entries) > 0:
                sequence_entry = test_sequence.entries.pop(0)
            else:
                sequence_entry = None

    # Fill in the O_content entries.
    context["O_count"] = 1
    context["O_content"] = server.consume_accumulated_output()

    return context


def gdbremote_hex_encode_string(str):
    output = ""
    for c in str:
        output += "{0:02x}".format(ord(c))
    return output


def gdbremote_hex_decode_string(str):
    return str.decode("hex")


def gdbremote_packet_encode_string(str):
    checksum = 0
    for c in str:
        checksum += ord(c)
    return "$" + str + "#{0:02x}".format(checksum % 256)


def build_gdbremote_A_packet(args_list):
    """Given a list of args, create a properly-formed $A packet containing each arg."""
    payload = "A"

    # build the arg content
    arg_index = 0
    for arg in args_list:
        # Comma-separate the args.
        if arg_index > 0:
            payload += ","

        # Hex-encode the arg.
        hex_arg = gdbremote_hex_encode_string(arg)

        # Build the A entry.
        payload += "{},{},{}".format(len(hex_arg), arg_index, hex_arg)

        # Next arg index, please.
        arg_index += 1

    # return the packetized payload
    return gdbremote_packet_encode_string(payload)


def parse_reg_info_response(response_packet):
    if not response_packet:
        raise Exception("response_packet cannot be None")

    # Strip off prefix $ and suffix #xx if present.
    response_packet = _STRIP_COMMAND_PREFIX_REGEX.sub("", response_packet)
    response_packet = _STRIP_CHECKSUM_REGEX.sub("", response_packet)

    # Build keyval pairs
    values = {}
    for kv in response_packet.split(";"):
        if len(kv) < 1:
            continue
        (key, val) = kv.split(":")
        values[key] = val

    return values


def parse_threadinfo_response(response_packet):
    if not response_packet:
        raise Exception("response_packet cannot be None")

    # Strip off prefix $ and suffix #xx if present.
    response_packet = _STRIP_COMMAND_PREFIX_M_REGEX.sub("", response_packet)
    response_packet = _STRIP_CHECKSUM_REGEX.sub("", response_packet)

    for tid in response_packet.split(","):
        if not tid:
            continue
        if tid.startswith("p"):
            pid, _, tid = tid.partition(".")
            yield (int(pid[1:], 16), int(tid, 16))
        else:
            yield int(tid, 16)


def unpack_endian_binary_string(endian, value_string):
    """Unpack a gdb-remote binary (post-unescaped, i.e. not escaped) response to an unsigned int given endianness of the inferior."""
    if not endian:
        raise Exception("endian cannot be None")
    if not value_string or len(value_string) < 1:
        raise Exception("value_string cannot be None or empty")

    if endian == "little":
        value = 0
        i = 0
        while len(value_string) > 0:
            value += ord(value_string[0]) << i
            value_string = value_string[1:]
            i += 8
        return value
    elif endian == "big":
        value = 0
        while len(value_string) > 0:
            value = (value << 8) + ord(value_string[0])
            value_string = value_string[1:]
        return value
    else:
        # pdp is valid but need to add parse code once needed.
        raise Exception("unsupported endian:{}".format(endian))


def unpack_register_hex_unsigned(endian, value_string):
    """Unpack a gdb-remote $p-style response to an unsigned int given endianness of inferior."""
    if not endian:
        raise Exception("endian cannot be None")
    if not value_string or len(value_string) < 1:
        raise Exception("value_string cannot be None or empty")

    if endian == "little":
        value = 0
        i = 0
        while len(value_string) > 0:
            value += int(value_string[0:2], 16) << i
            value_string = value_string[2:]
            i += 8
        return value
    elif endian == "big":
        return int(value_string, 16)
    else:
        # pdp is valid but need to add parse code once needed.
        raise Exception("unsupported endian:{}".format(endian))


def pack_register_hex(endian, value, byte_size=None):
    """Unpack a gdb-remote $p-style response to an unsigned int given endianness of inferior."""
    if not endian:
        raise Exception("endian cannot be None")

    if endian == "little":
        # Create the litt-endian return value.
        retval = ""
        while value != 0:
            retval = retval + "{:02x}".format(value & 0xFF)
            value = value >> 8
        if byte_size:
            # Add zero-fill to the right/end (MSB side) of the value.
            retval += "00" * (byte_size - len(retval) // 2)
        return retval

    elif endian == "big":
        retval = ""
        while value != 0:
            retval = "{:02x}".format(value & 0xFF) + retval
            value = value >> 8
        if byte_size:
            # Add zero-fill to the left/front (MSB side) of the value.
            retval = ("00" * (byte_size - len(retval) // 2)) + retval
        return retval

    else:
        # pdp is valid but need to add parse code once needed.
        raise Exception("unsupported endian:{}".format(endian))


class GdbRemoteEntryBase(object):
    def is_output_matcher(self):
        return False


class GdbRemoteEntry(GdbRemoteEntryBase):
    def __init__(
        self, is_send_to_remote=True, exact_payload=None, regex=None, capture=None
    ):
        """Create an entry representing one piece of the I/O to/from a gdb remote debug monitor.

        Args:

            is_send_to_remote: True if this entry is a message to be
                sent to the gdbremote debug monitor; False if this
                entry represents text to be matched against the reply
                from the gdbremote debug monitor.

            exact_payload: if not None, then this packet is an exact
                send (when sending to the remote) or an exact match of
                the response from the gdbremote. The checksums are
                ignored on exact match requests since negotiation of
                no-ack makes the checksum content essentially
                undefined.

            regex: currently only valid for receives from gdbremote.  When
                specified (and only if exact_payload is None), indicates the
                gdbremote response must match the given regex. Match groups in
                the regex can be used for the matching portion (see capture
                arg). It is perfectly valid to have just a regex arg without a
                capture arg. This arg only makes sense if exact_payload is not
                specified.

            capture: if specified, is a dictionary of regex match
                group indices (should start with 1) to variable names
                that will store the capture group indicated by the
                index. For example, {1:"thread_id"} will store capture
                group 1's content in the context dictionary where
                "thread_id" is the key and the match group value is
                the value. This arg only makes sense when regex is specified.
        """
        self._is_send_to_remote = is_send_to_remote
        self.exact_payload = exact_payload
        self.regex = regex
        self.capture = capture

    def is_send_to_remote(self):
        return self._is_send_to_remote

    def is_consumed(self):
        # For now, all packets are consumed after first use.
        return True

    def get_send_packet(self):
        if not self.is_send_to_remote():
            raise Exception(
                "get_send_packet() called on GdbRemoteEntry that is not a send-to-remote packet"
            )
        if not self.exact_payload:
            raise Exception(
                "get_send_packet() called on GdbRemoteEntry but it doesn't have an exact payload"
            )
        return self.exact_payload

    def _assert_exact_payload_match(self, asserter, actual_packet):
        assert_packets_equal(asserter, actual_packet, self.exact_payload)
        return None

    def _assert_regex_match(self, asserter, actual_packet, context):
        # Ensure the actual packet matches from the start of the actual packet.
        match = self.regex.match(actual_packet)
        if not match:
            asserter.fail(
                "regex '{}' failed to match against content '{}'".format(
                    self.regex.pattern, actual_packet
                )
            )

        if self.capture:
            # Handle captures.
            for group_index, var_name in list(self.capture.items()):
                capture_text = match.group(group_index)
                # It is okay for capture text to be None - which it will be if it is a group that can match nothing.
                # The user must be okay with it since the regex itself matched
                # above.
                context[var_name] = capture_text

        return context

    def assert_match(self, asserter, actual_packet, context=None):
        # This only makes sense for matching lines coming from the
        # remote debug monitor.
        if self.is_send_to_remote():
            raise Exception(
                "Attempted to match a packet being sent to the remote debug monitor, doesn't make sense."
            )

        # Create a new context if needed.
        if not context:
            context = {}

        # If this is an exact payload, ensure they match exactly,
        # ignoring the packet checksum which is optional for no-ack
        # mode.
        if self.exact_payload:
            self._assert_exact_payload_match(asserter, actual_packet)
            return context
        elif self.regex:
            return self._assert_regex_match(asserter, actual_packet, context)
        else:
            raise Exception(
                "Don't know how to match a remote-sent packet when exact_payload isn't specified."
            )


class MultiResponseGdbRemoteEntry(GdbRemoteEntryBase):
    """Represents a query/response style packet.

    Assumes the first item is sent to the gdb remote.
    An end sequence regex indicates the end of the query/response
    packet sequence.  All responses up through (but not including) the
    end response are stored in a context variable.

    Settings accepted from params:

        next_query or query: required.  The typical query packet without the $ prefix or #xx suffix.
            If there is a special first packet to start the iteration query, see the
            first_query key.

        first_query: optional. If the first query requires a special query command, specify
            it with this key.  Do not specify the $ prefix or #xx suffix.

        append_iteration_suffix: defaults to False.  Specify True if the 0-based iteration
            index should be appended as a suffix to the command.  e.g. qRegisterInfo with
            this key set true will generate query packets of qRegisterInfo0, qRegisterInfo1,
            etc.

        end_regex: required. Specifies a compiled regex object that will match the full text
            of any response that signals an end to the iteration.  It must include the
            initial $ and ending #xx and must match the whole packet.

        save_key: required.  Specifies the key within the context where an array will be stored.
            Each packet received from the gdb remote that does not match the end_regex will get
            appended to the array stored within the context at that key.

        runaway_response_count: optional. Defaults to 10000. If this many responses are retrieved,
            assume there is something wrong with either the response collection or the ending
            detection regex and throw an exception.
    """

    def __init__(self, params):
        self._next_query = params.get("next_query", params.get("query"))
        if not self._next_query:
            raise "either next_query or query key must be specified for MultiResponseGdbRemoteEntry"

        self._first_query = params.get("first_query", self._next_query)
        self._append_iteration_suffix = params.get("append_iteration_suffix", False)
        self._iteration = 0
        self._end_regex = params["end_regex"]
        self._save_key = params["save_key"]
        self._runaway_response_count = params.get("runaway_response_count", 10000)
        self._is_send_to_remote = True
        self._end_matched = False

    def is_send_to_remote(self):
        return self._is_send_to_remote

    def get_send_packet(self):
        if not self.is_send_to_remote():
            raise Exception(
                "get_send_packet() called on MultiResponseGdbRemoteEntry that is not in the send state"
            )
        if self._end_matched:
            raise Exception(
                "get_send_packet() called on MultiResponseGdbRemoteEntry but end of query/response sequence has already been seen."
            )

        # Choose the first or next query for the base payload.
        if self._iteration == 0 and self._first_query:
            payload = self._first_query
        else:
            payload = self._next_query

        # Append the suffix as needed.
        if self._append_iteration_suffix:
            payload += "%x" % self._iteration

        # Keep track of the iteration.
        self._iteration += 1

        # Now that we've given the query packet, flip the mode to
        # receive/match.
        self._is_send_to_remote = False

        # Return the result, converted to packet form.
        return gdbremote_packet_encode_string(payload)

    def is_consumed(self):
        return self._end_matched

    def assert_match(self, asserter, actual_packet, context=None):
        # This only makes sense for matching lines coming from the remote debug
        # monitor.
        if self.is_send_to_remote():
            raise Exception(
                "assert_match() called on MultiResponseGdbRemoteEntry but state is set to send a query packet."
            )

        if self._end_matched:
            raise Exception(
                "assert_match() called on MultiResponseGdbRemoteEntry but end of query/response sequence has already been seen."
            )

        # Set up a context as needed.
        if not context:
            context = {}

        # Check if the packet matches the end condition.
        match = self._end_regex.match(actual_packet)
        if match:
            # We're done iterating.
            self._end_matched = True
            return context

        # Not done iterating - save the packet.
        context[self._save_key] = context.get(self._save_key, [])
        context[self._save_key].append(actual_packet)

        # Check for a runaway response cycle.
        if len(context[self._save_key]) >= self._runaway_response_count:
            raise Exception(
                "runaway query/response cycle detected: %d responses captured so far. Last response: %s"
                % (len(context[self._save_key]), context[self._save_key][-1])
            )

        # Flip the mode to send for generating the query.
        self._is_send_to_remote = True
        return context


class MatchRemoteOutputEntry(GdbRemoteEntryBase):
    """Waits for output from the debug monitor to match a regex or time out.

    This entry type tries to match each time new gdb remote output is accumulated
    using a provided regex.  If the output does not match the regex within the
    given timeframe, the command fails the playback session.  If the regex does
    match, any capture fields are recorded in the context.

    Settings accepted from params:

        regex: required. Specifies a compiled regex object that must either succeed
            with re.match or re.search (see regex_mode below) within the given timeout
            (see timeout_seconds below) or cause the playback to fail.

        regex_mode: optional. Available values: "match" or "search". If "match", the entire
            stub output as collected so far must match the regex.  If search, then the regex
            must match starting somewhere within the output text accumulated thus far.
            Default: "match" (i.e. the regex must match the entirety of the accumulated output
            buffer, so unexpected text will generally fail the match).

        capture: optional.  If specified, is a dictionary of regex match group indices (should start
            with 1) to variable names that will store the capture group indicated by the
            index. For example, {1:"thread_id"} will store capture group 1's content in the
            context dictionary where "thread_id" is the key and the match group value is
            the value. This arg only makes sense when regex is specified.
    """

    def __init__(self, regex=None, regex_mode="match", capture=None):
        self._regex = regex
        self._regex_mode = regex_mode
        self._capture = capture
        self._matched = False

        if not self._regex:
            raise Exception("regex cannot be None")

        if not self._regex_mode in ["match", "search"]:
            raise Exception(
                'unsupported regex mode "{}": must be "match" or "search"'.format(
                    self._regex_mode
                )
            )

    def is_output_matcher(self):
        return True

    def is_send_to_remote(self):
        # This is always a "wait for remote" command.
        return False

    def is_consumed(self):
        return self._matched

    def assert_match(self, asserter, accumulated_output, context):
        # Validate args.
        if not accumulated_output:
            raise Exception("accumulated_output cannot be none")
        if not context:
            raise Exception("context cannot be none")

        # Validate that we haven't already matched.
        if self._matched:
            raise Exception(
                "invalid state - already matched, attempting to match again"
            )

        # If we don't have any content yet, we don't match.
        if len(accumulated_output) < 1:
            return context

        # Check if we match
        if self._regex_mode == "match":
            match = self._regex.match(accumulated_output)
        elif self._regex_mode == "search":
            match = self._regex.search(accumulated_output)
        else:
            raise Exception("Unexpected regex mode: {}".format(self._regex_mode))

        # If we don't match, wait to try again after next $O content, or time
        # out.
        if not match:
            # print("re pattern \"{}\" did not match against \"{}\"".format(self._regex.pattern, accumulated_output))
            return context

        # We do match.
        self._matched = True
        # print("re pattern \"{}\" matched against \"{}\"".format(self._regex.pattern, accumulated_output))

        # Collect up any captures into the context.
        if self._capture:
            # Handle captures.
            for group_index, var_name in list(self._capture.items()):
                capture_text = match.group(group_index)
                if not capture_text:
                    raise Exception("No content for group index {}".format(group_index))
                context[var_name] = capture_text

        return context


class GdbRemoteTestSequence(object):
    _LOG_LINE_REGEX = re.compile(r"^.*(read|send)\s+packet:\s+(.+)$")

    def __init__(self, logger):
        self.entries = []
        self.logger = logger

    def __len__(self):
        return len(self.entries)

    def add_log_lines(self, log_lines, remote_input_is_read):
        for line in log_lines:
            if isinstance(line, str):
                # Handle log line import
                # if self.logger:
                #     self.logger.debug("processing log line: {}".format(line))
                match = self._LOG_LINE_REGEX.match(line)
                if match:
                    playback_packet = match.group(2)
                    direction = match.group(1)
                    if _is_packet_lldb_gdbserver_input(direction, remote_input_is_read):
                        # Handle as something to send to the remote debug monitor.
                        # if self.logger:
                        #     self.logger.info("processed packet to send to remote: {}".format(playback_packet))
                        self.entries.append(
                            GdbRemoteEntry(
                                is_send_to_remote=True, exact_payload=playback_packet
                            )
                        )
                    else:
                        # Log line represents content to be expected from the remote debug monitor.
                        # if self.logger:
                        #     self.logger.info("receiving packet from llgs, should match: {}".format(playback_packet))
                        self.entries.append(
                            GdbRemoteEntry(
                                is_send_to_remote=False, exact_payload=playback_packet
                            )
                        )
                else:
                    raise Exception("failed to interpret log line: {}".format(line))
            elif isinstance(line, dict):
                entry_type = line.get("type", "regex_capture")
                if entry_type == "regex_capture":
                    # Handle more explicit control over details via dictionary.
                    direction = line.get("direction", None)
                    regex = line.get("regex", None)
                    capture = line.get("capture", None)

                    # Compile the regex.
                    if regex and (isinstance(regex, str)):
                        regex = re.compile(regex, re.DOTALL)

                    if _is_packet_lldb_gdbserver_input(direction, remote_input_is_read):
                        # Handle as something to send to the remote debug monitor.
                        # if self.logger:
                        #     self.logger.info("processed dict sequence to send to remote")
                        self.entries.append(
                            GdbRemoteEntry(
                                is_send_to_remote=True, regex=regex, capture=capture
                            )
                        )
                    else:
                        # Log line represents content to be expected from the remote debug monitor.
                        # if self.logger:
                        #     self.logger.info("processed dict sequence to match receiving from remote")
                        self.entries.append(
                            GdbRemoteEntry(
                                is_send_to_remote=False, regex=regex, capture=capture
                            )
                        )
                elif entry_type == "multi_response":
                    self.entries.append(MultiResponseGdbRemoteEntry(line))
                elif entry_type == "output_match":
                    regex = line.get("regex", None)
                    # Compile the regex.
                    if regex and (isinstance(regex, str)):
                        regex = re.compile(regex, re.DOTALL)

                    regex_mode = line.get("regex_mode", "match")
                    capture = line.get("capture", None)
                    self.entries.append(
                        MatchRemoteOutputEntry(
                            regex=regex, regex_mode=regex_mode, capture=capture
                        )
                    )
                else:
                    raise Exception('unknown entry type "%s"' % entry_type)


def process_is_running(pid, unknown_value=True):
    """If possible, validate that the given pid represents a running process on the local system.

    Args:

        pid: an OS-specific representation of a process id.  Should be an integral value.

        unknown_value: value used when we cannot determine how to check running local
        processes on the OS.

    Returns:

        If we can figure out how to check running process ids on the given OS:
        return True if the process is running, or False otherwise.

        If we don't know how to check running process ids on the given OS:
        return the value provided by the unknown_value arg.
    """
    if not isinstance(pid, int):
        raise Exception(
            "pid must be an integral type (actual type: %s)" % str(type(pid))
        )

    process_ids = []

    if lldb.remote_platform:
        # Don't know how to get list of running process IDs on a remote
        # platform
        return unknown_value
    elif platform.system() in ["Darwin", "Linux", "FreeBSD", "NetBSD"]:
        # Build the list of running process ids
        output = subprocess.check_output(
            "ps ax | awk '{ print $1; }'", shell=True
        ).decode("utf-8")
        text_process_ids = output.split("\n")[1:]
        # Convert text pids to ints
        process_ids = [int(text_pid) for text_pid in text_process_ids if text_pid != ""]
    elif platform.system() == "Windows":
        output = subprocess.check_output(
            'for /f "tokens=2 delims=," %F in (\'tasklist /nh /fi "PID ne 0" /fo csv\') do @echo %~F',
            shell=True,
        ).decode("utf-8")
        text_process_ids = output.split("\n")[1:]
        process_ids = [int(text_pid) for text_pid in text_process_ids if text_pid != ""]
    # elif {your_platform_here}:
    #   fill in process_ids as a list of int type process IDs running on
    #   the local system.
    else:
        # Don't know how to get list of running process IDs on this
        # OS, so return the "don't know" value.
        return unknown_value

    # Check if the pid is in the process_ids
    return pid in process_ids


def _handle_output_packet_string(packet_contents):
    # Warning: in non-stop mode, we currently handle only the first output
    # packet since we'd need to inject vStdio packets
    if not packet_contents.startswith((b"$O", b"%Stdio:O")):
        return None
    elif packet_contents == b"$OK":
        return None
    else:
        return binascii.unhexlify(packet_contents.partition(b"O")[2])


class Server(object):
    _GDB_REMOTE_PACKET_REGEX = re.compile(rb"^([\$%][^\#]*)#[0-9a-fA-F]{2}")

    class ChecksumMismatch(Exception):
        pass

    def __init__(self, sock, proc=None):
        self._accumulated_output = b""
        self._receive_buffer = b""
        self._normal_queue = []
        self._output_queue = []
        self._sock = sock
        self._proc = proc

    def send_raw(self, frame):
        self._sock.sendall(frame)

    def send_ack(self):
        self.send_raw(b"+")

    def send_packet(self, packet):
        self.send_raw(b"$%s#%02x" % (packet, self._checksum(packet)))

    @staticmethod
    def _checksum(packet):
        checksum = 0
        for c in iter(packet):
            checksum += c
        return checksum % 256

    def _read(self, q):
        while not q:
            new_bytes = self._sock.recv(4096)
            self._process_new_bytes(new_bytes)
        return q.pop(0)

    def _process_new_bytes(self, new_bytes):
        # Add new bytes to our accumulated unprocessed packet bytes.
        self._receive_buffer += new_bytes

        # Parse fully-formed packets into individual packets.
        has_more = len(self._receive_buffer) > 0
        while has_more:
            if len(self._receive_buffer) <= 0:
                has_more = False
            # handle '+' ack
            elif self._receive_buffer[0:1] == b"+":
                self._normal_queue += [b"+"]
                self._receive_buffer = self._receive_buffer[1:]
            else:
                packet_match = self._GDB_REMOTE_PACKET_REGEX.match(self._receive_buffer)
                if packet_match:
                    # Our receive buffer matches a packet at the
                    # start of the receive buffer.
                    new_output_content = _handle_output_packet_string(
                        packet_match.group(1)
                    )
                    if new_output_content:
                        # This was an $O packet with new content.
                        self._accumulated_output += new_output_content
                        self._output_queue += [self._accumulated_output]
                    else:
                        # Any packet other than $O.
                        self._normal_queue += [packet_match.group(0)]

                    # Remove the parsed packet from the receive
                    # buffer.
                    self._receive_buffer = self._receive_buffer[
                        len(packet_match.group(0)) :
                    ]
                else:
                    # We don't have enough in the receive bufferto make a full
                    # packet. Stop trying until we read more.
                    has_more = False

    def get_raw_output_packet(self):
        return self._read(self._output_queue)

    def get_raw_normal_packet(self):
        return self._read(self._normal_queue)

    @staticmethod
    def _get_payload(frame):
        payload = frame[1:-3]
        checksum = int(frame[-2:], 16)
        if checksum != Server._checksum(payload):
            raise ChecksumMismatch
        return payload

    def get_normal_packet(self):
        frame = self.get_raw_normal_packet()
        if frame == b"+":
            return frame
        return self._get_payload(frame)

    def get_accumulated_output(self):
        return self._accumulated_output

    def consume_accumulated_output(self):
        output = self._accumulated_output
        self._accumulated_output = b""
        return output

    def __str__(self):
        return dedent(
            """\
            server '{}' on '{}'
            _receive_buffer: {}
            _normal_queue: {}
            _output_queue: {}
            _accumulated_output: {}
            """
        ).format(
            self._proc,
            self._sock,
            self._receive_buffer,
            self._normal_queue,
            self._output_queue,
            self._accumulated_output,
        )


# A class representing a pipe for communicating with debug server.
# This class includes menthods to open the pipe and read the port number from it.
if lldbplatformutil.getHostPlatform() == "windows":
    import ctypes
    import ctypes.wintypes
    from ctypes.wintypes import BOOL, DWORD, HANDLE, LPCWSTR, LPDWORD, LPVOID

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    PIPE_ACCESS_INBOUND = 1
    FILE_FLAG_FIRST_PIPE_INSTANCE = 0x00080000
    FILE_FLAG_OVERLAPPED = 0x40000000
    PIPE_TYPE_BYTE = 0
    PIPE_REJECT_REMOTE_CLIENTS = 8
    INVALID_HANDLE_VALUE = -1
    ERROR_ACCESS_DENIED = 5
    ERROR_IO_PENDING = 997

    class OVERLAPPED(ctypes.Structure):
        _fields_ = [
            ("Internal", LPVOID),
            ("InternalHigh", LPVOID),
            ("Offset", DWORD),
            ("OffsetHigh", DWORD),
            ("hEvent", HANDLE),
        ]

        def __init__(self):
            super(OVERLAPPED, self).__init__(
                Internal=0, InternalHigh=0, Offset=0, OffsetHigh=0, hEvent=None
            )

    LPOVERLAPPED = ctypes.POINTER(OVERLAPPED)

    CreateNamedPipe = kernel32.CreateNamedPipeW
    CreateNamedPipe.restype = HANDLE
    CreateNamedPipe.argtypes = (
        LPCWSTR,
        DWORD,
        DWORD,
        DWORD,
        DWORD,
        DWORD,
        DWORD,
        LPVOID,
    )

    ConnectNamedPipe = kernel32.ConnectNamedPipe
    ConnectNamedPipe.restype = BOOL
    ConnectNamedPipe.argtypes = (HANDLE, LPOVERLAPPED)

    CreateEvent = kernel32.CreateEventW
    CreateEvent.restype = HANDLE
    CreateEvent.argtypes = (LPVOID, BOOL, BOOL, LPCWSTR)

    GetOverlappedResultEx = kernel32.GetOverlappedResultEx
    GetOverlappedResultEx.restype = BOOL
    GetOverlappedResultEx.argtypes = (HANDLE, LPOVERLAPPED, LPDWORD, DWORD, BOOL)

    ReadFile = kernel32.ReadFile
    ReadFile.restype = BOOL
    ReadFile.argtypes = (HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED)

    CloseHandle = kernel32.CloseHandle
    CloseHandle.restype = BOOL
    CloseHandle.argtypes = (HANDLE,)

    class Pipe(object):
        def __init__(self, prefix):
            while True:
                self.name = "lldb-" + str(random.randrange(10**10))
                full_name = "\\\\.\\pipe\\" + self.name
                self._handle = CreateNamedPipe(
                    full_name,
                    PIPE_ACCESS_INBOUND
                    | FILE_FLAG_FIRST_PIPE_INSTANCE
                    | FILE_FLAG_OVERLAPPED,
                    PIPE_TYPE_BYTE | PIPE_REJECT_REMOTE_CLIENTS,
                    1,
                    4096,
                    4096,
                    0,
                    None,
                )
                if self._handle != INVALID_HANDLE_VALUE:
                    break
                if ctypes.get_last_error() != ERROR_ACCESS_DENIED:
                    raise ctypes.WinError(ctypes.get_last_error())

            self._overlapped = OVERLAPPED()
            self._overlapped.hEvent = CreateEvent(None, True, False, None)
            result = ConnectNamedPipe(self._handle, self._overlapped)
            assert result == 0
            if ctypes.get_last_error() != ERROR_IO_PENDING:
                raise ctypes.WinError(ctypes.get_last_error())

        def finish_connection(self, timeout):
            if not GetOverlappedResultEx(
                self._handle,
                self._overlapped,
                ctypes.byref(DWORD(0)),
                timeout * 1000,
                True,
            ):
                raise ctypes.WinError(ctypes.get_last_error())

        def read(self, size, timeout):
            buf = ctypes.create_string_buffer(size)
            if not ReadFile(
                self._handle, ctypes.byref(buf), size, None, self._overlapped
            ):
                if ctypes.get_last_error() != ERROR_IO_PENDING:
                    raise ctypes.WinError(ctypes.get_last_error())
            read = DWORD(0)
            if not GetOverlappedResultEx(
                self._handle, self._overlapped, ctypes.byref(read), timeout * 1000, True
            ):
                raise ctypes.WinError(ctypes.get_last_error())
            return buf.raw[0 : read.value]

        def close(self):
            CloseHandle(self._overlapped.hEvent)
            CloseHandle(self._handle)

else:

    class Pipe(object):
        def __init__(self, prefix):
            self.name = os.path.join(prefix, "stub_port_number")
            os.mkfifo(self.name)
            self._fd = os.open(self.name, os.O_RDONLY | os.O_NONBLOCK)

        def finish_connection(self, timeout):
            pass

        def read(self, size, timeout):
            (readers, _, _) = select.select([self._fd], [], [], timeout)
            if self._fd not in readers:
                raise TimeoutError
            return os.read(self._fd, size)

        def close(self):
            os.close(self._fd)
