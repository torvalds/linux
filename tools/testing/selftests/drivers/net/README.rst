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

NETIF
~~~~~

Name of the netdevice against which the test should be executed.
When empty or not set software devices will be used.
