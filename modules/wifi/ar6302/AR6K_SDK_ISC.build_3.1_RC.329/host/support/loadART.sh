#!/bin/sh
# Load ART target image into RAM.
Help() {
	echo "Usage: $0 [options]"
	echo
	echo "With NO options, loads ART target image into RAM"
	echo
	echo "To unload, use $WORKAREA/host/support/loadAR6000.sh unloadall"
	exit 0
}

export NETIF=${NETIF:-eth1}
#export EEPROM=${EEPROM:-$WORKAREA/host/support/fakeBoardData_AR6002.bin}

if [ "$TARGET_TYPE" = "AR6003" ]; then
export slowbus=0
if [ $FPGA_FLAG = "1" ]; then
export slowbus=1
# for Venus FPGA. Have to uncomment this line in loadAR6000.sh
#export busclocksetting="DefaultOperClock=12500000"
fi
fi

XTALFREQ=${XTALFREQ:-26000000}

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

echo "Loading BMI only"
if [ "$TARGET_TYPE" = "AR6003" ]; then
$LOADAR6000 hostonly enableuartprint 
else
$LOADAR6000 hostonly
fi

if [ "$TARGET_TYPE" = "" ]
then
    # Determine TARGET_TYPE
    eval export `$BMILOADER -i $NETIF --quiet --info | grep TARGET_TYPE`
fi
echo TARGET TYPE is $TARGET_TYPE

if [ "$TARGET_VERSION" = "" ]
then
    # Determine TARGET_VERSION
    eval export `$BMILOADER -i $NETIF --quiet --info | grep TARGET_VERSION`
fi
AR6002_VERSION_REV2=0x20000188
AR6003_VERSION_REV1=0x300002ba
AR6003_VERSION_REV2=0x30000384
AR6003_VERSION_REV3=0x30000582
echo TARGET VERSION is $TARGET_VERSION

if [ "$TARGET_TYPE" = "AR6002" ]
then
    if [ "$TARGET_VERSION" = "$AR6002_VERSION_REV2" ]
    then
        export wlanapp=$WORKAREA/target/AR6002/hw2.0/bin/device.bin
    fi
fi
if [ "$TARGET_TYPE" = "AR6003" ]
then

    # didn't quite work on either FC3 or FC10. It could be due to the memory location change, pending further investigation.
    if [ "$TARGET_VERSION" = "$AR6003_VERSION_REV2" ]; then
        $BMILOADER -i $NETIF --write --address=0x544FB0 --file=$WORKAREA/target/AR6003/hw2.0/bin/memtest.bin
        $BMILOADER -i $NETIF --quiet --set --address=0x5451fc --param=0xffffffff
        $BMILOADER -i $NETIF --execute --address=0x945000 --param=0
        sleep 1
		    while true
		    do
		        result=`$BMILOADER -i $NETIF --quiet --get --address=0x5451fc`
		        if [ "$result" != "0xffffffff" ]
		        then
		           break;
		        fi
		    done
		   
		    if [ "$result" == "0x0" ]
		    then
		        echo "MEMORY TEST($PATTERN_MSG) PASSED"
		    else
		        echo "MEMORY TEST($PATTERN_MSG) FAILED AT ADDRESS $result"
		        echo "IF YOU THINK IT'S OK,PLEASE PRESS "ENTER",OR CTRL+C for quit program"
		        read -n 1
		        exit_value=-1
		    fi
    fi

    if [ "$TARGET_VERSION" = "$AR6003_VERSION_REV1" ]; then
        export wlanapp=$WORKAREA/target/AR6003/hw1.0/bin/device.bin
    else
    if [ "$TARGET_VERSION" = "$AR6003_VERSION_REV2" ]; then
        export wlanapp=$WORKAREA/target/AR6003/hw2.0/bin/device.bin
    else 
    if [ "$TARGET_VERSION" = "$AR6003_VERSION_REV3" ]; then
        export wlanapp=$WORKAREA/target/AR6003/hw2.1.1/bin/device.bin
    else # venus2.0 fpga
        export wlanapp=""
    fi
    fi
    fi
fi

while [ "$#" -ne 0 ]
do
        case $1 in
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

echo "Loading ART target"
if [ "$TARGET_TYPE" = "AR6003" ]; then
$LOADAR6000 targonly enableuartprint noresetok nostart bypasswmi 
else
$LOADAR6000 targonly noresetok nostart bypasswmi 
fi

if [ "$TARGET_TYPE" = "AR6002" ]
then
echo "Setting voltage scaling control"
$BMILOADER -i $NETIF --set --address=0x4110 --param=0xa1d

echo "Setting XTALFREQ $XTALFREQ"
$BMILOADER -i $NETIF --write --address=0x500478 --param=$XTALFREQ

# TBD: set hi_board_data_initialized to avoid loading eeprom.
$BMILOADER -i $NETIF --write --address=0x500458 --param=0x1
fi

if [ "$TARGET_TYPE" = "AR6003" ]; then
echo "Setting XTALFREQ $XTALFREQ"
host_intr_addr=0x540600
$BMILOADER -i $NETIF --quiet --set --address=$(($host_intr_addr + 0x78)) --param=$XTALFREQ

echo "Set hi_board_data_initialized to avoid loading eeprom."
#$BMILOADER -i $NETIF --write --address=0x540658 --param=0x1

# Disable ANI
echo "Disable ANI"
#$BMILOADER -i $NETIF --set --address=0x180c0 --or=32

fi


sleep 1

