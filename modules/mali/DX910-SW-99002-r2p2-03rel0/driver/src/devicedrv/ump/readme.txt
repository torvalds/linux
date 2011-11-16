Building the UMP Device Driver for Linux
----------------------------------------

Build the UMP Device Driver for Linux by running the following make command:

KDIR=<kdir_path> CONFIG=<your_config> make

where
    kdir_path: Path to your Linux Kernel directory
    your_config: Name of the sub-folder to find the required config.h file
                 ("arch-" will be prepended)

The config.h file contains the configuration parameters needed, like the
memory backend to use, and the amount of memory.

The result will be a ump.ko file, which can be loaded into the Linux kernel
by using the insmod command.
