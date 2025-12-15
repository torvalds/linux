# SPDX-License-Identifier: GPL-2.0

import re
import time
import json

from lib.py import ksft_pr, cmd, ip, rand_port, wait_port_listen


class Iperf3Runner:
    """
    Sets up and runs iperf3 traffic.
    """
    def __init__(self, env, port=None, server_ip=None, client_ip=None):
        env.require_cmd("iperf3", local=True, remote=True)
        self.env = env
        self.port = rand_port() if port is None else port
        self.server_ip = server_ip
        self.client_ip = client_ip

    def _build_server(self):
        cmdline = f"iperf3 -s -1 -p {self.port}"
        if self.server_ip:
            cmdline += f" -B {self.server_ip}"
        return cmdline

    def _build_client(self, streams, duration, reverse):
        host = self.env.addr if self.server_ip is None else self.server_ip
        cmdline = f"iperf3 -c {host} -p {self.port} -P {streams} -t {duration} -J"
        if self.client_ip:
            cmdline += f" -B {self.client_ip}"
        if reverse:
            cmdline += " --reverse"
        return cmdline

    def start_server(self):
        """
        Starts an iperf3 server with optional bind IP.
        """
        cmdline = self._build_server()
        proc = cmd(cmdline, background=True)
        wait_port_listen(self.port)
        time.sleep(0.1)
        return proc

    def start_client(self, background=False, streams=1, duration=10, reverse=False):
        """
        Starts the iperf3 client with the configured options.
        """
        cmdline = self._build_client(streams, duration, reverse)
        return cmd(cmdline, background=background, host=self.env.remote)

    def measure_bandwidth(self, reverse=False):
        """
        Runs an iperf3 measurement and returns the average bandwidth (Gbps).
        Discards the first and last few reporting intervals and uses only the
        middle part of the run where throughput is typically stable.
        """
        self.start_server()
        result = self.start_client(duration=10, reverse=reverse)

        if result.ret != 0:
            raise RuntimeError("iperf3 failed to run successfully")
        try:
            out = json.loads(result.stdout)
        except json.JSONDecodeError as exc:
            raise ValueError("Failed to parse iperf3 JSON output") from exc

        intervals = out.get("intervals", [])
        samples = [i["sum"]["bits_per_second"] / 1e9 for i in intervals]
        if len(samples) < 10:
            raise ValueError(f"iperf3 returned too few intervals: {len(samples)}")
        # Discard potentially unstable first and last 3 seconds.
        stable = samples[3:-3]

        avg = sum(stable) / len(stable)

        return avg


class GenerateTraffic:
    def __init__(self, env, port=None):
        self.env = env
        self.runner = Iperf3Runner(env, port)

        self._iperf_server = self.runner.start_server()
        self._iperf_client = self.runner.start_client(background=True, streams=16, duration=86400)

        # Wait for traffic to ramp up
        if not self._wait_pkts(pps=1000):
            self.stop(verbose=True)
            raise Exception("iperf3 traffic did not ramp up")

    def _wait_pkts(self, pkt_cnt=None, pps=None):
        """
        Wait until we've seen pkt_cnt or until traffic ramps up to pps.
        Only one of pkt_cnt or pss can be specified.
        """
        pkt_start = ip("-s link show dev " + self.env.ifname, json=True)[0]["stats64"]["rx"]["packets"]
        for _ in range(50):
            time.sleep(0.1)
            pkt_now = ip("-s link show dev " + self.env.ifname, json=True)[0]["stats64"]["rx"]["packets"]
            if pps:
                if pkt_now - pkt_start > pps / 10:
                    return True
                pkt_start = pkt_now
            elif pkt_cnt:
                if pkt_now - pkt_start > pkt_cnt:
                    return True
        return False

    def wait_pkts_and_stop(self, pkt_cnt):
        failed = not self._wait_pkts(pkt_cnt=pkt_cnt)
        self.stop(verbose=failed)

    def stop(self, verbose=None):
        self._iperf_client.process(terminate=True)
        if verbose:
            ksft_pr(">> Client:")
            ksft_pr(self._iperf_client.stdout)
            ksft_pr(self._iperf_client.stderr)
        self._iperf_server.process(terminate=True)
        if verbose:
            ksft_pr(">> Server:")
            ksft_pr(self._iperf_server.stdout)
            ksft_pr(self._iperf_server.stderr)
        self._wait_client_stopped()

    def _wait_client_stopped(self, sleep=0.005, timeout=5):
        end = time.monotonic() + timeout

        live_port_pattern = re.compile(fr":{self.runner.port:04X} 0[^6] ")

        while time.monotonic() < end:
            data = cmd("cat /proc/net/tcp*", host=self.env.remote).stdout
            if not live_port_pattern.search(data):
                return
            time.sleep(sleep)
        raise Exception(f"Waiting for client to stop timed out after {timeout}s")
