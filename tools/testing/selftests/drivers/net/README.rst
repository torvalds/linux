Running tests
=============

Tests are executed within kselftest framework like any other tests.
By default tests execute against software drivers such as netdevsim.
All tests must support running against a real device (SW-only tests
should instead be placed in net/ or drivers/net/netdevsim, HW-only
tests in drivers/net/hw).

Set appropriate variables to point the tests at a real device.

Variables
=========

Variables can be set in the environment or by creating a net.config
file in the same directory as this README file. Example::

  $ NETIF=eth0 ./some_test.sh

or::

  $ cat tools/testing/selftests/drivers/net/net.config
  # Variable set in a file
  NETIF=eth0

Please note that the config parser is very simple, if there are
any non-alphanumeric characters in the value it needs to be in
double quotes.

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
