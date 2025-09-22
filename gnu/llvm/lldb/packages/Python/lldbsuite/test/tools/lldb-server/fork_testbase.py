import gdbremote_testcase


class GdbRemoteForkTestBase(gdbremote_testcase.GdbRemoteTestCaseBase):
    fork_regex = (
        "[$]T[0-9a-fA-F]{{2}}thread:p([0-9a-f]+)[.]([0-9a-f]+);.*"
        "{}:p([0-9a-f]+)[.]([0-9a-f]+).*"
    )
    fork_regex_nonstop = (
        "%Stop:T[0-9a-fA-F]{{2}}"
        "thread:p([0-9a-f]+)[.]([0-9a-f]+);.*"
        "{}:p([0-9a-f]+)[.]([0-9a-f]+).*"
    )
    fork_capture = {1: "parent_pid", 2: "parent_tid", 3: "child_pid", 4: "child_tid"}
    stop_regex_base = "T[0-9a-fA-F]{{2}}thread:p{}.{};.*reason:signal.*"
    stop_regex = "^[$]" + stop_regex_base

    def start_fork_test(self, args, variant="fork", nonstop=False):
        self.build()
        self.prep_debug_monitor_and_inferior(inferior_args=args)
        self.add_qSupported_packets(["multiprocess+", "{}-events+".format(variant)])
        ret = self.expect_gdbremote_sequence()
        self.assertIn("{}-events+".format(variant), ret["qSupported_response"])
        self.reset_test_sequence()

        # continue and expect fork
        if nonstop:
            self.test_sequence.add_log_lines(
                [
                    "read packet: $QNonStop:1#00",
                    "send packet: $OK#00",
                    "read packet: $c#00",
                    "send packet: $OK#00",
                    {
                        "direction": "send",
                        "regex": self.fork_regex_nonstop.format(variant),
                        "capture": self.fork_capture,
                    },
                    "read packet: $vStopped#00",
                    "send packet: $OK#00",
                ],
                True,
            )
        else:
            self.test_sequence.add_log_lines(
                [
                    "read packet: $c#00",
                    {
                        "direction": "send",
                        "regex": self.fork_regex.format(variant),
                        "capture": self.fork_capture,
                    },
                ],
                True,
            )
        ret = self.expect_gdbremote_sequence()
        self.reset_test_sequence()

        return tuple(
            ret[x] for x in ("parent_pid", "parent_tid", "child_pid", "child_tid")
        )

    def fork_and_detach_test(self, variant, nonstop=False):
        parent_pid, parent_tid, child_pid, child_tid = self.start_fork_test(
            [variant], variant, nonstop=nonstop
        )

        # detach the forked child
        self.test_sequence.add_log_lines(
            [
                "read packet: $D;{}#00".format(child_pid),
                "send packet: $OK#00",
                # verify that the current process is correct
                "read packet: $qC#00",
                "send packet: $QCp{}.{}#00".format(parent_pid, parent_tid),
                # verify that the correct processes are detached/available
                "read packet: $Hgp{}.{}#00".format(child_pid, child_tid),
                "send packet: $Eff#00",
                "read packet: $Hgp{}.{}#00".format(parent_pid, parent_tid),
                "send packet: $OK#00",
            ],
            True,
        )
        self.expect_gdbremote_sequence()
        self.reset_test_sequence()
        return parent_pid, parent_tid

    def fork_and_follow_test(self, variant, nonstop=False):
        parent_pid, parent_tid, child_pid, child_tid = self.start_fork_test(
            [variant], variant, nonstop=nonstop
        )

        # switch to the forked child
        self.test_sequence.add_log_lines(
            [
                "read packet: $Hgp{}.{}#00".format(child_pid, child_tid),
                "send packet: $OK#00",
                "read packet: $Hcp{}.{}#00".format(child_pid, child_tid),
                "send packet: $OK#00",
                # detach the parent
                "read packet: $D;{}#00".format(parent_pid),
                "send packet: $OK#00",
                # verify that the correct processes are detached/available
                "read packet: $Hgp{}.{}#00".format(parent_pid, parent_tid),
                "send packet: $Eff#00",
                "read packet: $Hgp{}.{}#00".format(child_pid, child_tid),
                "send packet: $OK#00",
                # then resume the child
                "read packet: $c#00",
            ],
            True,
        )

        if nonstop:
            self.test_sequence.add_log_lines(
                [
                    "send packet: $OK#00",
                    "send packet: %Stop:W00;process:{}#00".format(child_pid),
                    "read packet: $vStopped#00",
                    "send packet: $OK#00",
                ],
                True,
            )
        else:
            self.test_sequence.add_log_lines(
                [
                    "send packet: $W00;process:{}#00".format(child_pid),
                ],
                True,
            )
        self.expect_gdbremote_sequence()

    def detach_all_test(self, nonstop=False):
        parent_pid, parent_tid, child_pid, child_tid = self.start_fork_test(
            ["fork"], nonstop=nonstop
        )

        self.test_sequence.add_log_lines(
            [
                # double-check our PIDs
                "read packet: $Hgp{}.{}#00".format(parent_pid, parent_tid),
                "send packet: $OK#00",
                "read packet: $Hgp{}.{}#00".format(child_pid, child_tid),
                "send packet: $OK#00",
                # detach all processes
                "read packet: $D#00",
                "send packet: $OK#00",
                # verify that both PIDs are invalid now
                "read packet: $Hgp{}.{}#00".format(parent_pid, parent_tid),
                "send packet: $Eff#00",
                "read packet: $Hgp{}.{}#00".format(child_pid, child_tid),
                "send packet: $Eff#00",
            ],
            True,
        )
        self.expect_gdbremote_sequence()

    def vkill_test(self, kill_parent=False, kill_child=False, nonstop=False):
        assert kill_parent or kill_child
        parent_pid, parent_tid, child_pid, child_tid = self.start_fork_test(
            ["fork"], nonstop=nonstop
        )

        if kill_parent:
            self.test_sequence.add_log_lines(
                [
                    # kill the process
                    "read packet: $vKill;{}#00".format(parent_pid),
                    "send packet: $OK#00",
                ],
                True,
            )
        if kill_child:
            self.test_sequence.add_log_lines(
                [
                    # kill the process
                    "read packet: $vKill;{}#00".format(child_pid),
                    "send packet: $OK#00",
                ],
                True,
            )
        self.test_sequence.add_log_lines(
            [
                # check child PID/TID
                "read packet: $Hgp{}.{}#00".format(child_pid, child_tid),
                "send packet: ${}#00".format("Eff" if kill_child else "OK"),
                # check parent PID/TID
                "read packet: $Hgp{}.{}#00".format(parent_pid, parent_tid),
                "send packet: ${}#00".format("Eff" if kill_parent else "OK"),
            ],
            True,
        )
        self.expect_gdbremote_sequence()

    def resume_one_test(self, run_order, use_vCont=False, nonstop=False):
        parent_pid, parent_tid, child_pid, child_tid = self.start_fork_test(
            ["fork", "stop"], nonstop=nonstop
        )

        parent_expect = [
            self.stop_regex_base.format(parent_pid, parent_tid),
            "W00;process:{}#.*".format(parent_pid),
        ]
        child_expect = [
            self.stop_regex_base.format(child_pid, child_tid),
            "W00;process:{}#.*".format(child_pid),
        ]

        for x in run_order:
            if x == "parent":
                pidtid = (parent_pid, parent_tid)
                expect = parent_expect.pop(0)
            elif x == "child":
                pidtid = (child_pid, child_tid)
                expect = child_expect.pop(0)
            else:
                assert False, "unexpected x={}".format(x)

            if use_vCont:
                self.test_sequence.add_log_lines(
                    [
                        # continue the selected process
                        "read packet: $vCont;c:p{}.{}#00".format(*pidtid),
                    ],
                    True,
                )
            else:
                self.test_sequence.add_log_lines(
                    [
                        # continue the selected process
                        "read packet: $Hcp{}.{}#00".format(*pidtid),
                        "send packet: $OK#00",
                        "read packet: $c#00",
                    ],
                    True,
                )
            if nonstop:
                self.test_sequence.add_log_lines(
                    [
                        "send packet: $OK#00",
                        {"direction": "send", "regex": "%Stop:" + expect},
                        "read packet: $vStopped#00",
                        "send packet: $OK#00",
                    ],
                    True,
                )
            else:
                self.test_sequence.add_log_lines(
                    [
                        {"direction": "send", "regex": "[$]" + expect},
                    ],
                    True,
                )
            # if at least one process remained, check both PIDs
            if parent_expect or child_expect:
                self.test_sequence.add_log_lines(
                    [
                        "read packet: $Hgp{}.{}#00".format(parent_pid, parent_tid),
                        "send packet: ${}#00".format("OK" if parent_expect else "Eff"),
                        "read packet: $Hgp{}.{}#00".format(child_pid, child_tid),
                        "send packet: ${}#00".format("OK" if child_expect else "Eff"),
                    ],
                    True,
                )
        self.expect_gdbremote_sequence()
