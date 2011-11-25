Building the UMP Device Driver for Linux
----------------------------------------

Build the UMP Device Driver for Linux by running the following make command:

KDIR=<kdir_path> CONFIG=<your_config> BUILD=<build_option> make

where
    kdir_path: Path to your Linux Kernel directory
    your_config: Name of the sub-folder to find the required config.h file
                 ("arch-" will be prepended)
    build_option: debug or release. Debug is default.

The config.h contains following configuration parameters:

ARCH_UMP_BACKEND_DEFAULT
    0 specifies the dedicated memory allocator.
    1 specifies the OS memory allocator.
ARCH_UMP_MEMORY_ADDRESS_DEFAULT
    This is only required for the dedicated memory allocator, and specifies
    the physical start address of the memory block reserved for UMP.
ARCH_UMP_MEMORY_SIZE_DEFAULT
    This specified the size of the memory block reserved for UMP, or the
    maximum limit for allocations from the OS.

The result will be a ump.ko file, which can be loaded into the Linux kernel
by using the insmod command. The driver can also be built as a part of the
kernel itself.
