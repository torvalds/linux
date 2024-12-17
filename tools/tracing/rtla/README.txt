RTLA: Real-Time Linux Analysis tools

The rtla meta-tool includes a set of commands that aims to analyze
the real-time properties of Linux. Instead of testing Linux as a black box,
rtla leverages kernel tracing capabilities to provide precise information
about the properties and root causes of unexpected results.

Installing RTLA

RTLA depends on the following libraries and tools:

 - libtracefs
 - libtraceevent
 - libcpupower (optional, for --deepest-idle-state)

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
  $ cd $libcpupower_src
  $ make
  $ sudo make install
  $ cd $rtla_src
  $ make
  $ sudo make install

For further information, please refer to the rtla man page.
