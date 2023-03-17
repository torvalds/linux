Using the mtty vfio-mdev sample code
====================================

mtty is a sample vfio-mdev driver that demonstrates how to use the mediated
device framework.

The sample driver creates an mdev device that simulates a serial port over a PCI
card.

1. Build and load the mtty.ko module.

   This step creates a dummy device, /sys/devices/virtual/mtty/mtty/

   Files in this device directory in sysfs are similar to the following::

     # tree /sys/devices/virtual/mtty/mtty/
        /sys/devices/virtual/mtty/mtty/
        |-- mdev_supported_types
        |   |-- mtty-1
        |   |   |-- available_instances
        |   |   |-- create
        |   |   |-- device_api
        |   |   |-- devices
        |   |   `-- name
        |   `-- mtty-2
        |       |-- available_instances
        |       |-- create
        |       |-- device_api
        |       |-- devices
        |       `-- name
        |-- mtty_dev
        |   `-- sample_mtty_dev
        |-- power
        |   |-- autosuspend_delay_ms
        |   |-- control
        |   |-- runtime_active_time
        |   |-- runtime_status
        |   `-- runtime_suspended_time
        |-- subsystem -> ../../../../class/mtty
        `-- uevent

2. Create a mediated device by using the dummy device that you created in the
   previous step::

     # echo "83b8f4f2-509f-382f-3c1e-e6bfe0fa1001" >	\
              /sys/devices/virtual/mtty/mtty/mdev_supported_types/mtty-2/create

3. Add parameters to qemu-kvm::

     -device vfio-pci,\
      sysfsdev=/sys/bus/mdev/devices/83b8f4f2-509f-382f-3c1e-e6bfe0fa1001

4. Boot the VM.

   In the Linux guest VM, with no hardware on the host, the device appears
   as  follows::

     # lspci -s 00:05.0 -xxvv
     00:05.0 Serial controller: Device 4348:3253 (rev 10) (prog-if 02 [16550])
             Subsystem: Device 4348:3253
             Physical Slot: 5
             Control: I/O+ Mem- BusMaster- SpecCycle- MemWINV- VGASnoop- ParErr-
     Stepping- SERR- FastB2B- DisINTx-
             Status: Cap- 66MHz- UDF- FastB2B- ParErr- DEVSEL=medium >TAbort-
     <TAbort- <MAbort- >SERR- <PERR- INTx-
             Interrupt: pin A routed to IRQ 10
             Region 0: I/O ports at c150 [size=8]
             Region 1: I/O ports at c158 [size=8]
             Kernel driver in use: serial
     00: 48 43 53 32 01 00 00 02 10 02 00 07 00 00 00 00
     10: 51 c1 00 00 59 c1 00 00 00 00 00 00 00 00 00 00
     20: 00 00 00 00 00 00 00 00 00 00 00 00 48 43 53 32
     30: 00 00 00 00 00 00 00 00 00 00 00 00 0a 01 00 00

     In the Linux guest VM, dmesg output for the device is as follows:

     serial 0000:00:05.0: PCI INT A -> Link[LNKA] -> GSI 10 (level, high) -> IRQ 10
     0000:00:05.0: ttyS1 at I/O 0xc150 (irq = 10) is a 16550A
     0000:00:05.0: ttyS2 at I/O 0xc158 (irq = 10) is a 16550A


5. In the Linux guest VM, check the serial ports::

     # setserial -g /dev/ttyS*
     /dev/ttyS0, UART: 16550A, Port: 0x03f8, IRQ: 4
     /dev/ttyS1, UART: 16550A, Port: 0xc150, IRQ: 10
     /dev/ttyS2, UART: 16550A, Port: 0xc158, IRQ: 10

6. Using minicom or any terminal emulation program, open port /dev/ttyS1 or
   /dev/ttyS2 with hardware flow control disabled.

7. Type data on the minicom terminal or send data to the terminal emulation
   program and read the data.

   Data is loop backed from hosts mtty driver.

8. Destroy the mediated device that you created::

     # echo 1 > /sys/bus/mdev/devices/83b8f4f2-509f-382f-3c1e-e6bfe0fa1001/remove

