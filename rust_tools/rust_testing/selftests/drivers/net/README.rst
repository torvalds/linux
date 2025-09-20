.. SPDX-License-Identifier: GPL-2.0

Running driver tests
====================

Networking driver tests are executed within kselftest framework like any
other tests. They support testing both real device drivers and emulated /
software drivers (latter mostly to test the core parts of the stack).

SW mode
~~~~~~~

By default, when no extra parameters are set or exported, tests execute
against software drivers such as netdevsim. No extra preparation is required
the software devices are created and destroyed as part of the test.
In this mode the tests are indistinguishable from other selftests and
(for example) can be run under ``virtme-ng`` like the core networking selftests.

HW mode
~~~~~~~

Executing tests against a real device requires external preparation.
The netdevice against which tests will be run must exist, be running
(in UP state) and be configured with an IP address.

Refer to list of :ref:`Variables` later in this file to set up running
the tests against a real device.

Both modes required
~~~~~~~~~~~~~~~~~~~

All tests in drivers/net must support running both against a software device
and a real device. SW-only tests should instead be placed in net/ or
drivers/net/netdevsim, HW-only tests in drivers/net/hw.

Variables
=========

The variables can be set in the environment or by creating a net.config
file in the same directory as this README file. Example::

  $ NETIF=eth0 ./some_test.sh

or::

  $ cat tools/testing/selftests/drivers/net/net.config
  # Variable set in a file
  NETIF=eth0

Local test (which don't require endpoint for sending / receiving traffic)
need only the ``NETIF`` variable. Remaining variables define the endpoint
and communication method.

NETIF
~~~~~

Name of the netdevice against which the test should be executed.
When empty or not set software devices will be used.

LOCAL_V4, LOCAL_V6, REMOTE_V4, REMOTE_V6
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Local and remote endpoint IP addresses.

REMOTE_TYPE
~~~~~~~~~~~

Communication method used to run commands on the remote endpoint.
Test framework has built-in support for ``netns`` and ``ssh`` channels.
``netns`` assumes the "remote" interface is part of the same
host, just moved to the specified netns.
``ssh`` communicates with remote endpoint over ``ssh`` and ``scp``.
Using persistent SSH connections is strongly encouraged to avoid
the latency of SSH connection setup on every command.

Communication methods are defined by classes in ``lib/py/remote_{name}.py``.
It should be possible to add a new method without modifying any of
the framework, by simply adding an appropriately named file to ``lib/py``.

REMOTE_ARGS
~~~~~~~~~~~

Arguments used to construct the communication channel.
Communication channel dependent::

  for netns - name of the "remote" namespace
  for ssh - name/address of the remote host

Example
=======

Build the selftests::

  # make -C tools/testing/selftests/ TARGETS="drivers/net drivers/net/hw"

"Install" the tests and copy them over to the target machine::

  # make -C tools/testing/selftests/ TARGETS="drivers/net drivers/net/hw" \
     install INSTALL_PATH=/tmp/ksft-net-drv

  # rsync -ra --delete /tmp/ksft-net-drv root@192.168.1.1:/root/

On the target machine, running the tests will use netdevsim by default::

  [/root] # ./ksft-net-drv/run_kselftest.sh -t drivers/net:ping.py
  TAP version 13
  1..1
  # timeout set to 45
  # selftests: drivers/net: ping.py
  # TAP version 13
  # 1..3
  # ok 1 ping.test_v4
  # ok 2 ping.test_v6
  # ok 3 ping.test_tcp
  # # Totals: pass:3 fail:0 xfail:0 xpass:0 skip:0 error:0
  ok 1 selftests: drivers/net: ping.py

Create a config with remote info::

  [/root] # cat > ./ksft-net-drv/drivers/net/net.config <<EOF
  NETIF=eth0
  LOCAL_V4=192.168.1.1
  REMOTE_V4=192.168.1.2
  REMOTE_TYPE=ssh
  REMOTE_ARGS=root@192.168.1.2
  EOF

Run the test::

  [/root] # ./ksft-net-drv/drivers/net/ping.py
  TAP version 13
  1..3
  ok 1 ping.test_v4
  ok 2 ping.test_v6 # SKIP Test requires IPv6 connectivity
  ok 3 ping.test_tcp
  # Totals: pass:2 fail:0 xfail:0 xpass:0 skip:1 error:0
