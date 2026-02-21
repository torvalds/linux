# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
from metric import (d_ratio, Event, Metric, MetricGroup)


def Cycles() -> MetricGroup:
    cyc_k = Event("cpu\\-cycles:kHh")  # exclude user and guest
    cyc_g = Event("cpu\\-cycles:G")   # exclude host
    cyc_u = Event("cpu\\-cycles:uH")  # exclude kernel, hypervisor and guest
    cyc = cyc_k + cyc_g + cyc_u

    return MetricGroup("lpm_cycles", [
        Metric("lpm_cycles_total", "Total number of cycles", cyc, "cycles"),
        Metric("lpm_cycles_user", "User cycles as a percentage of all cycles",
               d_ratio(cyc_u, cyc), "100%"),
        Metric("lpm_cycles_kernel", "Kernel cycles as a percentage of all cycles",
               d_ratio(cyc_k, cyc), "100%"),
        Metric("lpm_cycles_guest", "Hypervisor guest cycles as a percentage of all cycles",
               d_ratio(cyc_g, cyc), "100%"),
    ], description="cycles breakdown per privilege level (users, kernel, guest)")
