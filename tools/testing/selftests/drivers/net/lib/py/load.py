# SPDX-License-Identifier: GPL-2.0

import time

from lib.py import ksft_pr, cmd, ip, rand_port, wait_port_listen

class GenerateTraffic:
    def __init__(self, env, port=None):
        env.require_cmd("iperf3", remote=True)

        self.env = env

        if port is None:
            port = rand_port()
        self._iperf_server = cmd(f"iperf3 -s -1 -p {port}", background=True)
        wait_port_listen(port)
        time.sleep(0.1)
        self._iperf_client = cmd(f"iperf3 -c {env.addr} -P 16 -p {port} -t 86400",
                                 background=True, host=env.remote)

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
