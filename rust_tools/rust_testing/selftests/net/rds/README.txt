RDS self-tests
==============

These scripts provide a coverage test for RDS-TCP by creating two
network namespaces and running rds packets between them. A loopback
network is provisioned with optional probability of packet loss or
corruption. A workload of 50000 hashes, each 64 characters in size,
are passed over an RDS socket on this test network. A passing test means
the RDS-TCP stack was able to recover properly.  The provided config.sh
can be used to compile the kernel with the necessary gcov options.  The
kernel may optionally be configured to omit the coverage report as well.

USAGE:
	run.sh [-d logdir] [-l packet_loss] [-c packet_corruption]
	       [-u packet_duplcate]

OPTIONS:
	-d	Log directory.  Defaults to tools/testing/selftests/net/rds/rds_logs

	-l	Simulates a percentage of packet loss

	-c	Simulates a percentage of packet corruption

	-u	Simulates a percentage of packet duplication.

EXAMPLE:

    # Create a suitable gcov enabled .config
    tools/testing/selftests/net/rds/config.sh -g

    # Alternatly create a gcov disabled .config
    tools/testing/selftests/net/rds/config.sh

    # build the kernel
    vng --build  --config tools/testing/selftests/net/config

    # launch the tests in a VM
    vng -v --rwdir ./ --run . --user root --cpus 4 -- \
        "export PYTHONPATH=tools/testing/selftests/net/; tools/testing/selftests/net/rds/run.sh"

An HTML coverage report will be output in tools/testing/selftests/net/rds/rds_logs/coverage/.
