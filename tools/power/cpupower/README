The cpupower package consists of the following elements:

requirements
------------

On x86 pciutils is needed at runtime (-lpci).
For compilation pciutils-devel (pci/pci.h) and a gcc version
providing cpuid.h is needed.
For both it's not explicitly checked for (yet).


libcpupower
----------

"libcpupower" is a library which offers a unified access method for userspace
tools and programs to the cpufreq core and drivers in the Linux kernel. This
allows for code reduction in userspace tools, a clean implementation of
the interaction to the cpufreq core, and support for both the sysfs and proc
interfaces [depending on configuration, see below].


compilation and installation
----------------------------

make
su
make install

should suffice on most systems. It builds libcpupower to put in
/usr/lib; cpupower, cpufreq-bench_plot.sh to put in /usr/bin; and
cpufreq-bench to put in /usr/sbin. If you want to set up the paths
differently and/or want to configure the package to your specific
needs, you need to open "Makefile" with an editor of your choice and
edit the block marked CONFIGURATION.


THANKS
------
Many thanks to Mattia Dongili who wrote the autotoolization and
libtoolization, the manpages and the italian language file for cpupower;
to Dave Jones for his feedback and his dump_psb tool; to Bruno Ducrot for his
powernow-k8-decode and intel_gsic tools as well as the french language file;
and to various others commenting on the previous (pre-)releases of 
cpupower.


        Dominik Brodowski
