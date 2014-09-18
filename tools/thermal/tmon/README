TMON - A Monitoring and Testing Tool for Linux kernel thermal subsystem

Why TMON?
==========
Increasingly, Linux is running on thermally constrained devices. The simple
thermal relationship between processor and fan has become past for modern
computers.

As hardware vendors cope with the thermal constraints on their products, more
and more sensors are added, new cooling capabilities are introduced. The
complexity of the thermal relationship can grow exponentially among cooling
devices, zones, sensors, and trip points. They can also change dynamically.

To expose such relationship to the userspace, Linux generic thermal layer
introduced sysfs entry at /sys/class/thermal with a matrix of symbolic
links, trip point bindings, and device instances. To traverse such
matrix by hand is not a trivial task. Testing is also difficult in that
thermal conditions are often exception cases that hard to reach in
normal operations.

TMON is conceived as a tool to help visualize, tune, and test the
complex thermal subsystem.

Files
=====
	tmon.c : main function for set up and configurations.
	tui.c : handles ncurses based user interface
	sysfs.c : access to the generic thermal sysfs
	pid.c : a proportional-integral-derivative (PID) controller
	that can be used for thermal relationship training.

Requirements
============
Depends on ncurses

Build
=========
$ make
$ sudo ./tmon -h
Usage: tmon [OPTION...]
  -c, --control         cooling device in control
  -d, --daemon          run as daemon, no TUI
  -l, --log             log data to /var/tmp/tmon.log
  -h, --help            show this help message
  -t, --time-interval   set time interval for sampling
  -v, --version         show version
  -g, --debug           debug message in syslog

1. For monitoring only:
$ sudo ./tmon
