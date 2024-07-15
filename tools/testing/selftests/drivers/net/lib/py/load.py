# SPDX-License-Identifier: GPL-2.0

import time

from lib.py import ksft_pr, cmd, ip, rand_port, wait_port_listen

class GenerateTraffic:
    def __init__(self, env):
        env.require_cmd("iperf3", remote=True)

        self.env = env

        port = rand_port()
        self._iperf_server = cmd(f"iperf3 -s -p {port}", background=True)
        wait_port_listen(port)
        time.sleep(0.1)
        self._iperf_client = cmd(f"iperf3 -c {env.addr} -P 16 -p {port} -t 86400",
                                 background=True, host=env.remote)

        # Wait for traffic to ramp up
        pkt = ip("-s link show dev " + env.ifname, json=True)[0]["stats64"]["rx"]["packets"]
        for _ in range(50):
            time.sleep(0.1)
            now = ip("-s link show dev " + env.ifname, json=True)[0]["stats64"]["rx"]["packets"]
            if now - pkt > 1000:
                return
            pkt = now
        self.stop(verbose=True)
        raise Exception("iperf3 traffic did not ramp up")

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
