#!/bin/sh
#
# Platform-Dependent load script for x86 systems using Atheros SDIO stack
# 
#

ATH_RESTORE_NATIVEMMC=${ATH_RESTORE_NATIVEMMC:-yes}

ATH_BTHCI_ARGS=${ATH_BTHCI_ARGS:-"ar3khcibaud=3000000 hciuartscale=1 hciuartstep=8937"}

NATIVE_MMC_STACK_STD_HOST_AVAIL="no"
NATIVE_SDHCI_PCI_AVAIL="no"
NATIVE_MMC_STACK_STD_HOST_LOADED="no"
NATIVE_SDHCI_PCI_LOADED="no"
BT_HCI_SDIO_LOADED="no"
HCI_IF=""
CFG80211_AVAIL="no"
CFG80211_LOADED="no"

modprobe -l sdhci_pci | grep sdhci > /dev/null
if [ $? -eq 0 ]; then
# native PCI based STD host kernel module is available
NATIVE_SDHCI_PCI_AVAIL="yes"	
fi

modprobe -l sdhci | grep sdhci > /dev/null
if [ $? -eq 0 ]; then
# native STD host kernel module is available
NATIVE_MMC_STACK_STD_HOST_AVAIL="yes"	
fi

lsmod | grep sdhci_pci > /dev/null   
if [ $? -eq 0 ]; then
# native PCI based STD host kernel module is also loaded
NATIVE_SDHCI_PCI_LOADED="yes"	
fi

lsmod | grep sdhci > /dev/null   
if [ $? -eq 0 ]; then
# native STD host kernel module is also loaded
NATIVE_MMC_STACK_STD_HOST_LOADED="yes"	
fi

lsmod | grep bt_hci_sdio > /dev/null   
if [ $? -eq 0 ]; then
# BT HCI SDIO kernel module is also loaded
BT_HCI_SDIO_LOADED="yes"	
fi

echo $AR6K_MODULE_ARGS | grep setuphci > /dev/null
if [ $? -eq 0 ]; then
ENABLE_BT_HCI="yes"
fi

modprobe -l cfg80211 | grep cfg80211 > /dev/null
if [ $? -eq 0 ]; then
# cfg80211 kernel module is available
CFG80211_AVAIL="yes"	
fi

lsmod | grep cfg80211 > /dev/null   
if [ $? -eq 0 ]; then
# cfg80211 kernel module is loaded
CFG80211_LOADED="yes"	
fi

if [ -f /usr/sbin/hciconfig ]; then
# check if HCI inteface is up
HCI_IF=`/usr/sbin/hciconfig | grep -P "hci\d" | cut -d : -f 1`
fi

if [ "$ATH_AR6K_HCI_PAL" = "yes" ]; then
# check if HCI inteface is up
HCI1_IF=`/usr/sbin/hciconfig | grep -P "hci\d" | tail -n 1 | cut -d : -f 1`
fi

case $1 in
	loadbus)
    if [ "$ENABLE_BT_HCI" = "yes" ]; then
        # only if BlueZ is installed 
        if [ -f /usr/sbin/bluetoothd ]; then
            service bluetooth restart
            sleep 0.5
        fi
    fi
	if [ "$NATIVE_SDHCI_PCI_LOADED" = "yes" ]; then
		echo "Linux native PCI based stack STD host detected...attempting to remove kernel module"
		modprobe -r sdhci_pci
		if [ $? -ne 0 ]; then
			echo "failed to remove sdhci_pci"
			exit -1
		fi
	else
    	if [ "$NATIVE_MMC_STACK_STD_HOST_LOADED" = "yes" ]; then
    		echo "Linux native stack STD host detected...attempting to remove kernel module"
            ### TODO: need to revisit why this won't remove sdhci_pci 
    		modprobe -r sdhci	
    		if [ $? -ne 0 ]; then
    			echo "failed to remove native stack std host"
    			exit -1
    		fi
    	fi
    fi
	echo "sdiostack loading"
	/sbin/insmod $IMAGEPATH/sdio_lib.ko
	/sbin/insmod $IMAGEPATH/sdio_busdriver.ko debuglevel=7 $ATH_SDIO_STACK_PARAMS
	/sbin/insmod $IMAGEPATH/sdio_pcistd_hcd.ko debuglevel=7 CommonBufferDMASize=32768
	if [ $? -ne 0 ]; then
		echo "*** Failed to install SDIO stack"
		exit -1
	fi
	;;
	unloadbus)
	echo "sdio stack unloading ..."	
    /sbin/rmmod -w sdio_pcistd_hcd.ko
	/sbin/rmmod -w sdio_busdriver.ko
	/sbin/rmmod -w sdio_lib.ko	
	if [ "$ATH_RESTORE_NATIVEMMC" = "yes" ]; then
		if [ "$NATIVE_SDHCI_PCI_AVAIL" = "yes" ]; then
		 	echo "restoring linux native pci based stack std host"
			modprobe -q sdhci_pci
		else	
		    if [ "$NATIVE_MMC_STACK_STD_HOST_AVAIL" = "yes" ]; then
		     	echo "restoring linux native stack std host"
		    	modprobe -q sdhci	
		    fi	
        fi
	fi
	;;
	loadAR6K)
	if [ "$CFG80211_AVAIL" = "yes" ]; then
   	   if [ "$CFG80211_LOADED" = "no" ]; then
               modprobe -q cfg80211
               if [ $? -ne 0 ]; then
                  echo "*** Failed to install cfg80211 kernel module"
               fi
   	    fi
   	fi
	echo "loading AR6K module... Args = ($AR6K_MODULE_ARGS) , logfile:$AR6K_TGT_LOGFILE"
	$IMAGEPATH/recEvent --logfile=$AR6K_TGT_LOGFILE --srcdir=$WORKAREA/include/ /dev/null 2>&1 &
    if [ "$ENABLE_BT_HCI" = "yes" ]; then
        if [ -f $IMAGEPATH/bt_hci_sdio.ko ]; then
            /sbin/insmod $IMAGEPATH/$AR6K_MODULE_NAME.ko $AR6K_MODULE_ARGS
            if [ $? -ne 0 ]; then
                echo "*** Failed to install AR6K Module"
                exit -1
            fi
            /sbin/insmod $IMAGEPATH/bt_hci_sdio.ko $ATH_BTHCI_ARGS
            if [ $? -ne 0 ]; then
                echo "*** Failed to install HCI SDIO Module"
                exit -1
            fi
        else
            /sbin/insmod $IMAGEPATH/$AR6K_MODULE_NAME.ko $AR6K_MODULE_ARGS $ATH_BTHCI_ARGS
        fi
    else
        /sbin/insmod $IMAGEPATH/$AR6K_MODULE_NAME.ko $AR6K_MODULE_ARGS
        if [ $? -ne 0 ]; then
            echo "*** Failed to install AR6K Module"
            exit -1
        fi
    fi
	;;
	unloadAR6K)
	echo "unloading AR6K module..."
    if [ "$ATH_AR6K_HCI_PAL" = "yes" ]; then
           if [ -n "$HCI1_IF" ]; then
                  /usr/sbin/hciconfig "$HCI1_IF" down &> /dev/null
                  echo "HCI1 interface is up now. Need to shutdown it"
                  sleep 1
           fi
    fi

    if [ -n "$HCI_IF" ]; then
        /usr/sbin/hciconfig "$HCI_IF" down &> /dev/null
        sleep 0.5
        # only if BlueZ is installed 
        if [ -f /usr/sbin/bluetoothd ]; then
            service bluetooth stop
            sleep 0.5
        fi
    fi
    if [ "$BT_HCI_SDIO_LOADED" = "yes" ]; then
        /sbin/rmmod -w bt_hci_sdio.ko
    fi
	/sbin/rmmod -w $AR6K_MODULE_NAME.ko
	killall recEvent
	;;
	*)
		echo "Unknown option : $1"
	
esac



