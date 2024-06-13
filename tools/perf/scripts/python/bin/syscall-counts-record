#!/bin/bash
(perf record -e raw_syscalls:sys_enter $@ || \
 perf record -e syscalls:sys_enter $@) 2> /dev/null
