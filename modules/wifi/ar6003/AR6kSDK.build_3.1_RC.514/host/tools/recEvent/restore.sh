#!/bin/sh

if [ -z "$WORKAREA" ]
then
        echo "Please set your WORKAREA environment variable."
        exit
fi

if [ -z "$ATH_PLATFORM" ]
then
        echo "Please set your ATH_PLATFORM environment variable."
        exit 1
fi

if [ -z "$NETIF" ]
then
        NETIF=eth1
fi

export IMAGEPATH=${IMAGEPATH:-$WORKAREA/host/.output/$ATH_PLATFORM/image}

/sbin/ifconfig $NETIF down
rmmod ar6000
sleep 2
case $ATH_PLATFORM in
        LOCAL_i686-SDIO|LOCAL_i686-CF|SANDGATEII_ARM-SDIO|SANDGATEII_ARM-CF)
        /sbin/insmod $IMAGEPATH/ar6000.ko
        ;;
        *)
        /sbin/insmod $IMAGEPATH/ar6000.o
esac
