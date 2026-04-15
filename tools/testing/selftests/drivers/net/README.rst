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

The current support for bash tests restricts the use of the same interface name
on the local system and the remote one and will bail if this case is
encountered.

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

Please note that the config parser is very simple, if there are
any non-alphanumeric characters in the value it needs to be in
double quotes.

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

LOCAL_PREFIX_V6
~~~~~~~~~~~~~~~

Local IP prefix/subnet which can be used to allocate extra IP addresses (for
network name spaces behind macvlan, veth, netkit devices). DUT must be
reachable using these addresses from the endpoint.

LOCAL_PREFIX_V6 must NOT match LOCAL_V6.

Example:
  NETIF           = "eth0"
  LOCAL_V6        = "2001:db8:1::1"
  REMOTE_V6       = "2001:db8:1::2"
  LOCAL_PREFIX_V6 = "2001:db8:2::0/64"

          +-----------------------------+        +------------------------------+
  dst     | INIT NS                     |        | TEST NS                      |
  2001:   | +---------------+           |        |                              |
  db8:2::2| | NETIF         |           |  bpf   |                              |
      +---|>| 2001:db8:1::1 |           |redirect| +-------------------------+  |
      |   | |               |-----------|--------|>| Netkit                  |  |
      |   | +---------------+           | _peer  | | nk_guest                |  |
      |   | +-------------+ Netkit pair |        | | fe80::2/64              |  |
      |   | | Netkit      |.............|........|>| 2001:db8:2::2/64        |  |
      |   | | nk_host     |             |        | +-------------------------+  |
      |   | | fe80::1/64  |             |        |                              |
      |   | +-------------+             |        | route:                       |
      |   |                             |        |   default                    |
      |   | route:                      |        |     via fe80::1 dev nk_guest |
      |   |   2001:db8:2::2/128         |        +------------------------------+
      |   |     via fe80::2 dev nk_host |
      |   +-----------------------------+
      |
      |   +---------------+
      |   | REMOTE        |
      +---| 2001:db8:1::2 |
          +---------------+

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
  # KTAP version 1
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
  KTAP version 1
  1..3
  ok 1 ping.test_v4
  ok 2 ping.test_v6 # SKIP Test requires IPv6 connectivity
  ok 3 ping.test_tcp
  # Totals: pass:2 fail:0 xfail:0 xpass:0 skip:1 error:0

Dependencies
~~~~~~~~~~~~

The tests have a handful of dependencies. For Fedora / CentOS::

  dnf -y install netsniff-ng python-yaml socat iperf3

Guidance for test authors
=========================

This section mostly applies to Python tests but some of the guidance
may be more broadly applicable.

Kernel config
~~~~~~~~~~~~~

Each test directory has a ``config`` file listing which kernel
configuration options the tests depend on. This file must be kept
up to date, the CIs build minimal kernels for each test group.

Adding checks inside the tests to validate that the necessary kernel
configs are enabled is discouraged. The test author may include such
checks, but standalone patches to make tests compatible e.g. with
distro kernel configs are unlikely to be accepted.

Avoid libraries and frameworks
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Test files should be relatively self contained. The libraries should
only include very core or non-trivial code.
It may be tempting to "factor out" the common code, but fight that urge.
Library code increases the barrier of entry, and complexity in general.

Avoid mixing test code and boilerplate
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

In Python, try to avoid adding code in the ``main()`` function which
instantiates ``NetDrvEnv()`` and calls ``ksft_run()``. It's okay to
set up global resources (e.g. open an RtNetlink socket used by multiple
tests), but any complex logic, test-specific environment configuration
and validation should be done in the tests (even if it means it has to
be repeated).

Local host is the DUT
~~~~~~~~~~~~~~~~~~~~~

Dual-host tests (tests with an endpoint) should be written from the DUT
perspective. IOW the local machine should be the one tested, remote is
just for traffic generation.

Avoid modifying remote
~~~~~~~~~~~~~~~~~~~~~~

Avoid making configuration changes to the remote system as much as possible.
Remote system may be used concurrently by multiple DUTs.

defer()
~~~~~~~

The env must be clean after test exits. Register a ``defer()`` for any
action that needs an "undo" as soon as possible. If you need to run
the cancel action as part of the test - ``defer()`` returns an object
you can ``.exec()``-ute.

ksft_pr()
~~~~~~~~~

Use ``ksft_pr()`` instead of ``print()`` to avoid breaking TAP format.

ksft_disruptive
~~~~~~~~~~~~~~~

By default the tests are expected to be able to run on
single-interface systems. All tests which may disconnect ``NETIF``
must be annotated with ``@ksft_disruptive``.

ksft_variants
~~~~~~~~~~~~~

Use the ``@ksft_variants`` decorator to run a test with multiple sets
of inputs as separate test cases. This avoids duplicating test functions
that only differ in parameters.

Parameters can be a single value, a tuple, or a ``KsftNamedVariant``
(which gives an explicit name to the sub-case). The argument to the
decorator can be a list or a generator.

Example::

  @ksft_variants([
      KsftNamedVariant("main", False),
      KsftNamedVariant("ctx", True),
  ])
  def resize_periodic(cfg, create_context):
      # test body receives (cfg, create_context) where create_context
      # is False for the "main" variant and True for "ctx"
      pass

or::

  def _gro_variants():
      for mode in ["sw", "hw"]:
          for protocol in ["tcp4", "tcp6"]:
              yield (mode, protocol)

  @ksft_variants(_gro_variants())
  def test(cfg, mode, protocol):
      pass

Running tests CI-style
======================

See https://github.com/linux-netdev/nipa/wiki for instructions on how
to easily run the tests using ``virtme-ng``.
