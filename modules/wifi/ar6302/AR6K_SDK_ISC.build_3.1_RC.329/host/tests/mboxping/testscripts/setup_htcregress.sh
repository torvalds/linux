#
# 
#!/bin/sh

BINDIR=$WORKAREA/host/.output/$ATH_PLATFORM/image
          

AR6003_VERSION_REV1=0x300002ba
AR6003_VERSION_REV2=0x30000384
    
setuphtctest()
{
	
    eval export `$BINDIR/bmiloader -i $NETIF --quiet --info | grep TARGET_TYPE`
	eval export `$BINDIR/bmiloader -i $NETIF --quiet --info | grep TARGET_VERSION`
	
	echo $TARGET_TYPE
	echo $TARGET_VERSION
	
	if [ "$TARGET_TYPE" = "AR6003" ]; then
		if [ "$TARGET_VERSION" = "$AR6003_VERSION_REV1" ]; then
			V_MBOX_FW=$WORKAREA/target/AR6003/hw1.0/bin/endpointping.bin
        else      
         	if [ "$TARGET_VERSION" = "$AR6003_VERSION_REV2" ]; then
				V_MBOX_FW=$WORKAREA/target/AR6003/hw2.0/bin/endpointping.bin
            else
            	# 6003 FPGA
                V_MBOX_FW=$WORKAREA/target/AR6003/fpga/bin/endpointping.bin
            fi
        fi	
	fi
	
	if [ "$TARGET_TYPE" = "MCKINLEY" ]; then
         V_MBOX_FW=$WORKAREA/target/AR6002/fpga.6/bin/endpointping.bin
         TARGET_TYPE=AR6003
	fi
	
case $TARGET_TYPE in
	AR6002)
	MBOX_FW=$WORKAREA/target/AR6002/hw2.0/bin/endpointping.bin
	if [ ! -e "$MBOX_FW" ]; then	
	echo "$MBOX_FW not found!"
	exit 1
	fi
	# Run at 40/44MHz
	$BINDIR/bmiloader -i $NETIF --set --address=0x4020 --param=0
	$BINDIR/bmiloader -i $NETIF --set --address=0x180dc --param=$1
	$WORKAREA/host/support/download.ram.sh $MBOX_FW
	;;
	AR6003)
	if [ ! -e "$V_MBOX_FW" ]; then	
	echo "$V_MBOX_FW not found!"
	exit 1
	fi
	# Run at 80/88MHz
	
	if [ "$TARGET_VERSION" = "$AR6003_VERSION_REV1" -o "$TARGET_VERSION" = "$AR6003_VERSION_REV2" ]; then
        # Run at 80/88MHz by default.
        $BINDIR/bmiloader -i $NETIF --set --address=0x4020 --param=1
        dbguart_tx=${dbguart_tx:-8}
        $BINDIR/bmiloader -i $NETIF --set --address=0x540680 --param=$dbguart_tx
    else 
        # Run at 40/44MHz by default for the FPGA
        $BINDIR/bmiloader -i $NETIF --set --address=0x4020 --param=0
    fi
		
	$BINDIR/bmiloader -i $NETIF --set --address=0x180dc --param=$1
	$BINDIR/bmiloader -i $NETIF --set --address=0x180dc --or=$2
    $WORKAREA/host/support/download.ram.sh $V_MBOX_FW

	# optionally set HCI UART baud rate scale value
	#$BINDIR/bmiloader -i $NETIF --set --address=0x540690 --param=6
	# optionally set HCI UART baud rate step value
	#$BINDIR/bmiloader -i $NETIF --set --address=0x540694 --param=32768
	# optionally set debug flags
	#$BINDIR/bmiloader -i $NETIF --set --address=0x5406a0 --param=3
	;;
	AR6001)
	if [ ! -e "$AR6001_FW" ]; then	
	echo "$AR6001_FW not found!"
	exit 1
	fi
	if [ ! -x "$AR6001_DOWNLOAD" ]; then	
	echo "$AR6001_DOWNLOAD not found or executable!"
	exit 1
	fi		
    $AR6001_DOWNLOAD $AR6001_FW
	;;
	*)
	echo "TARGET_TYPE not determined!"
	exit
esac

	$BINDIR/bmiloader -i $NETIF --done
	sleep 1	
	ifconfig $NETIF up
    HCI_IF=`/usr/sbin/hciconfig | grep -P "hci\d" | cut -d : -f 1`
    if [ -n "$HCI_IF" ]; then
        /usr/sbin/hciconfig $HCI_IF up
    fi
}


sleep=0
hci=0
hciargs="setuphci"
dounload=""

while [ "$#" -ne 0 ]
do
case $1 in
    allowsleep)
    sleep=1
	shift
	;;
	hcibridge)
    hci=2
    # in this mode we are running real HCI bridging */
    hciargs="$hciargs setupbtdev"
	shift
	;;
	hcibridge-loop)
    hci=6
	shift
	;;
	unloadall)
    dounload="yes"
	shift
	;;
	*)
	echo "Unrecognized argument!"
	exit
	shift
esac
done

if [ "$dounload" != "yes" ]; then
# load drivers 
$WORKAREA/host/support/loadAR6000.sh bmi bypasswmi enableuartprint $hciargs
sleep 1
# download and configure firmware
setuphtctest $sleep $hci
else
$WORKAREA/host/support/loadAR6000.sh unloadall
fi
