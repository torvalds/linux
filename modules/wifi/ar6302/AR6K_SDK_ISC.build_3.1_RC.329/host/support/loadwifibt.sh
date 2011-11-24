#!/bin/sh
#Applicable only for AR6003 and 3001.
# 

Help() {
    echo "Usage: $0 [options]"
    echo
    echo "With NO options, equivalent to single antenna no hcioversdio with colocated bt=atherosbt"
    echo
    echo " For co-located BT 3001 - use $0 atherosbt"
    echo " For co-located BT qcom  - use $0 qcombt "
    echo " For co-located BT qcom  - use $0 csrbt "
    echo " Whenever bt is specified default 3001 configuration is used"

    echo "Load wifi and bluetooth with dual antenna no coex"
    echo "   $0 danocoex"
    echo 
    echo "Load wifi and bluetooth with dual antenna and coex"
    echo "   $0 dawithcoex"
    echo 
    echo "Load wifi and bluetooth with single antenna"
    echo "   $0 sa"
    echo 
    echo "Load wifi and bluetooth over sdio (hciOverSDIO)"
    echo "   $0 hcioversdio"
    echo 
    echo "To unload all of the drivers:"
    echo "   $0 unloadall"
    echo 
    echo "To enable WLAN degug UART print:"
    echo "   $0 [options] enableuartprint"
    exit 0
}

NETIF=${NETIF:-eth1}
ATH_BTSERIAL_TTY=${ATH_BTSERIAL_TTY:-/dev/ttyS4}
dounload=""
startbtfilter="yes"
singleantenna="yes"
colocatedbt=3
args=""
btoversdio="no"

if [ -z "$WORKAREA" ]
then
    echo "Please set your WORKAREA environment variable."
    exit -1
fi


if [ -z "$ATH_PLATFORM" ]
then
    echo "Please set your ATH_PLATFORM environment variable."
    exit -1
fi


while [ "$#" -ne 0 ]
do
case $1 in
        sa)
        startbtfilter="yes"
        singleantenna="yes"
        shift
        ;;
        danocoex)
        startbtfilter="no"
        singleantenna="no"
        shift
        ;;
        dawithcoex)
        startbtfilter="yes"
        singleantenna="no"
        shift
        ;;
	    hcioversdio)
	    btoversdio="yes"
        args="$args setuphci setupbtdev"
	    echo "args is $args"
	    shift
	    ;;
        --mode)
        args="$args $1"
        shift
        args="$args $1"
        echo "args is $args"
        shift
        ;;
        setuphcipal)
        ar6000args="$ar6000args setuphcipal=1"
        shift
        ;;
        unloadall)
        dounload="yes"
        shift
        ;;
        enableuartprint)
        args="$args enableuartprint=1"
        shift
        ;;
        qcombt)
        colocatedbt=1
        shift
        ;;
        csrbt)
        colocatedbt=2
        shift
        ;;
        atherosbt)
        colocatedbt=3
        shift
        ;;
        * )
            echo "Unsupported argument"
            exit -1
        shift
    esac
done



if [ "$dounload" = "yes" ]; then
     lsmod | grep hci_uart > /dev/null
    if [ $? -eq 0 ]; then
        $WORKAREA_BT/scripts/load.sh unloadall
    fi
    sleep 2	
    pkill abtfilt
    sleep 2
    $WORKAREA/host/support/loadAR6000.sh unloadall
    exit 0
fi

if [ "$btoversdio" = "no" ]; then
    if [ -z "$WORKAREA_BT" ]; then
        echo "Please set your WORKAREA_BT environment variable."
        exit -1
    fi
fi
if [ "$startbtfilter" = "yes" ]; then
    if [ -z "$WORKAREA/host/.output/$ATH_PLATFORM/image/abtfilt" ]
    then
      echo "Unable to find btfilter @ $WORKAREA/host/.output/$ATH_PLATFORM/image/abtfilt"
    exit -1
    fi
fi
if [ "$singleantenna" = "yes" ]; then
    echo "copy bluetooth PST with acl set to LowPri file to /lib/firmware/ar3k"
    cp -r $WORKAREA/host/miscdrv/ar3k/30000coex/PS_ASIC_aclLowPri.pst $WORKAREA/host/miscdrv/ar3k/30000coex/PS_ASIC.pst
    cp -r $WORKAREA/host/miscdrv/ar3k/30101coex/PS_ASIC_aclLowPri.pst $WORKAREA/host/miscdrv/ar3k/30101coex/PS_ASIC.pst
    cp -rf $WORKAREA/host/miscdrv/ar3k/  /lib/firmware/.
 else    
    echo "copy bluetooth PST with acl set to HighPri file to /lib/firmware/ar3k"
    cp -r $WORKAREA/host/miscdrv/ar3k/30000coex/PS_ASIC_aclHighPri.pst $WORKAREA/host/miscdrv/ar3k/30000coex/PS_ASIC.pst
    cp -r $WORKAREA/host/miscdrv/ar3k/30101coex/PS_ASIC_aclHighPri.pst $WORKAREA/host/miscdrv/ar3k/30101coex/PS_ASIC.pst
    cp -rf $WORKAREA/host/miscdrv/ar3k/  /lib/firmware/.
fi

$WORKAREA/host/support/loadAR6000.sh $args
sleep 2


if [ "$btoversdio" = "no" ]; then
$WORKAREA_BT/scripts/load.sh -i $ATH_BTSERIAL_TTY  -s 3000000 
sleep 5
fi

if [ "$startbtfilter" = "yes" ]; then
    echo "Start Btfilter"
    if [ "$singleantenna" = "yes" ]; then
        echo "Set front end antenna conf to Single Ant"
        $WORKAREA/host/.output/$ATH_PLATFORM/image/abtfilt -a -d -v -s
    else        
        echo "Set front end antenna conf to Dual Ant"
        $WORKAREA/host/.output/$ATH_PLATFORM/image/abtfilt -a -d -v
    fi
fi
