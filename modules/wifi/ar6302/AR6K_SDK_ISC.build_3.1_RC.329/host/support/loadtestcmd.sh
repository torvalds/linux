#!/bin/sh
# Load Test cmd image into RAM.
Help() {
	echo "Usage: $0 [options]"
	echo
	echo "With NO options, loads athtcmd_ram.bin image into RAM"
	echo
	echo "To Disable ANI:"
	echo "$0 --noANI"
	echo "This load the image with ANI disabled"
	echo "To unload, use $WORKAREA/host/support/loadAR6000.sh unloadall"
	exit 0
}
DisableAni=0
param=0

if [ -z "$WORKAREA" ]
then
	echo "Please set your WORKAREA environment variable."
	exit
fi

if [ -z "$ATH_PLATFORM" ]
then
	echo "Please set your ATH_PLATFORM environment variable."
	exit
fi

if [ -z "$NETIF" ]
then
	NETIF=eth1
fi

LOADAR6000=${LOADAR6000:-$WORKAREA/host/support/loadAR6000.sh}
if [ ! -x "$LOADAR6000" ]; then
	echo "Loader application '$LOADAR6000' not found"
	exit
fi

BMILOADER=${BMILOADER:-$WORKAREA/host/.output/$ATH_PLATFORM/image/bmiloader}
if [ ! -x "$BMILOADER" ]; 
then
	echo "Loader application '$BMILOADER' not found"
	exit
fi

# Load the host side drivers alone to determine the target type and target
# version

echo "Loading Host drivers"
$LOADAR6000 --hostonly --test
sleep 1

if [ "$TARGET_TYPE" = "" ] # {
    then
        # Determine TARGET_TYPE
        eval export `$BMILOADER -i $NETIF --quiet --info | grep TARGET_TYPE`
fi # }

echo TARGET TYPE is $TARGET_TYPE

if [ "$TARGET_TYPE" = "AR6002" -o "$TARGET_TYPE" = "AR6003" ] # {
then
    if [ "$TARGET_VERSION" = "" ]
        then
            # Determine TARGET_VERSION
            eval export `$BMILOADER -i $NETIF --quiet --info | grep TARGET_VERSION`
    fi

    AR6002_VERSION_REV2=0x20000188
    AR6002_VERSION_REV4=0x300002ba
    AR6002_VERSION_REV4_2=0x30000384
    AR6002_VERSION_REV4_3=0x30000582

    echo TARGET VERSION is $TARGET_VERSION

    if [ "$TARGET_VERSION" = "$AR6002_VERSION_REV2" ]
    then
        export wlanapp=$WORKAREA/target/AR6002/hw2.0/bin/athtcmd_ram.bin
        LOCAL_SCRATCH_ADDRESS=0x180c0
    fi

    if [ "$TARGET_VERSION" = "$AR6002_VERSION_REV4" ]
    then
        export wlanapp=$WORKAREA/target/AR6003/hw1.0/bin/athtcmd_ram.bin
        LOCAL_SCRATCH_ADDRESS=0x180c0
    fi
    if [ "$TARGET_VERSION" = "$AR6002_VERSION_REV4_2" ]
    then
        export wlanapp=$WORKAREA/target/AR6003/hw2.0/bin/athtcmd_ram.bin
        LOCAL_SCRATCH_ADDRESS=0x180c0
    fi
    if [ "$TARGET_VERSION" = "$AR6002_VERSION_REV4_3" ]
    then
        export wlanapp=$WORKAREA/target/AR6003/hw2.1.1/bin/athtcmd_ram.bin
        LOCAL_SCRATCH_ADDRESS=0x180c0
    fi

fi  # }

if [ "$TARGET_TYPE" = "AR6001" ]
then
    TCMDIMAGE=${TCMDIMAGE:-$WORKAREA/target/AR6001/bin/athtcmd_ram.bin}
    LOCAL_SCRATCH_ADDRESS=0xac0140c0
    if [ ! -x "$TCMDIMAGE" ]; 
    then
        echo "TEST CMD '$TCMDIMAGE' not found"
        exit
    fi
fi

while [ "$#" -ne 0 ]
do
	case $1 in
		--noANI )
		DisableAni=1
		shift
		;;
	-h|--help )
		Help
		;;
       * )
      	echo "Unsupported argument"
            Help
			exit -1
		shift
	esac
done

echo "Loading target in test mode"
$LOADAR6000 --targonly --nostart
sleep 1

if [ "$TARGET_TYPE" = "AR6001" ]
then
    echo "Downloading the image $TCMDIMAGE into RAM"
    $BMILOADER -i $NETIF -w -a 0x80002000 -f $TCMDIMAGE
    sleep 1
    echo "Setting the start address to 0x80002000"
    $BMILOADER -i $NETIF -b -a 0x80002000
fi


if [ "$DisableAni" -eq "1" ]; then
       echo "Disabling ANI"
       $BMILOADER -i $NETIF --get --address $LOCAL_SCRATCH_ADDRESS
       sleep 1
       $BMILOADER -i $NETIF --set --address $LOCAL_SCRATCH_ADDRESS --param=0x20
       sleep 1
   	   $BMILOADER -i $NETIF --get --address $LOCAL_SCRATCH_ADDRESS
       sleep 1
fi

$BMILOADER -i $NETIF --done

