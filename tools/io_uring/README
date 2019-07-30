This directory includes a few programs that demonstrate how to use io_uring
in an application. The examples are:

io_uring-cp
	A very basic io_uring implementation of cp(1). It takes two
	arguments, copies the first argument to the second. This example
	is part of liburing, and hence uses the simplified liburing API
	for setting up an io_uring instance, submitting IO, completing IO,
	etc. The support functions in queue.c and setup.c are straight
	out of liburing.

io_uring-bench
	Benchmark program that does random reads on a number of files. This
	app demonstrates the various features of io_uring, like fixed files,
	fixed buffers, and polled IO. There are options in the program to
	control which features to use. Arguments is the file (or files) that
	io_uring-bench should operate on. This uses the raw io_uring
	interface.

liburing can be cloned with git here:

	git://git.kernel.dk/liburing

and contains a number of unit tests as well for testing io_uring. It also
comes with man pages for the three system calls.

Fio includes an io_uring engine, you can clone fio here:

	git://git.kernel.dk/fio
