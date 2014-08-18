#!/bin/bash
(perf record -e raw_syscalls:sys_exit $@ || \
 perf record -e syscalls:sys_exit $@) 2> /dev/null
