RV: Runtime Verification

Runtime Verification (RV) is a lightweight (yet rigorous) method that
complements classical exhaustive verification techniques (such as model
checking and theorem proving) with a more practical approach for
complex systems.

The rv tool is the interface for a collection of monitors that aim
analysing the logical and timing behavior of Linux.

Installing RV

RV depends on the following libraries and tools:

 - libtracefs
 - libtraceevent

It also depends on python3-docutils to compile man pages.

For development, we suggest the following steps for compiling rtla:

  $ git clone git://git.kernel.org/pub/scm/libs/libtrace/libtraceevent.git
  $ cd libtraceevent/
  $ make
  $ sudo make install
  $ cd ..
  $ git clone git://git.kernel.org/pub/scm/libs/libtrace/libtracefs.git
  $ cd libtracefs/
  $ make
  $ sudo make install
  $ cd ..
  $ cd $rv_src
  $ make
  $ sudo make install

For further information, please see rv manpage and the kernel documentation:
  Runtime Verification:
    Documentation/trace/rv/runtime-verification.rst
