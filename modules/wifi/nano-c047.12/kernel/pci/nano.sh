#!/bin/sh
module="nano_pci_cdev"
device="nano"


start() {
    #
    # Load the module
    #
    sync
    /sbin/insmod ./nano_if.ko || exit 1
    /sbin/insmod ./nano_pci.ko $* || exit 1
}


stop() {
    rmmod nano_pci
    rmmod nano_if
}


case "$1" in
    start)
	start $2
	;;

    stop)
	stop
	;;

    *)
	echo "Usage: nano.sh {start|stop}"
	;;
esac
