#!/bin/bash


usage()
{
    echo "##############################################################################"
    echo "#"
    echo "# HOW TO MAKE A RELEASE FOR I&V"
    echo "#"
    echo "# Install all required kernels and toolchains according to "
    echo "# smb://nanofs01/project/P821 Linux platform/README"
    echo "#"
    echo "# Get a fresh source instance from the repository."
    echo "#"
    echo "# To make an I&V release:"
    echo "# ./release.sh -r 4.6.4 iv"
    echo "#"
    echo "# To make cd releases (Development and Evaluation) do the following :"
    echo "# ./release.sh -r releaseversion -x xmacversion -e xtestversion"
    echo "               -k kernelmajorversion -p platform -c chiprevision cd"
    echo "#"
    echo "# A "release" directory will be created in \"release\"."
    echo "# Copy the contents to the current subdir of"
    echo "# smb://nanofs01/project/P0401/Design/fw/Releases "
    echo "#"
    echo "##############################################################################"
}

RELEASE_DIR=release
IV_TMP_DIR=$RELEASE_DIR/DYD009_3_F-NanoLinux
CD_TMP_DIR=$RELEASE_DIR/DRF001_3_Linux_SDK
OBJ_DIR=obj
TMP_DIR=../linux_driver
RELEASE_REQ=release_require
KERNEL=2.6
CHIP_REVISION=nrx511CD_nrx701C

create_dir ()
{
    local DIR_NAME=$1

    mkdir -p $1
}

wifi-source()
{
    make PLATFORM=$PLATFORM clean
    
    local TRANSPORT=`grep TRANSPORT platform/$PLATFORM/platform.inc | sed s/.*=//`
    local HOST_PLATFORM_SRC_DIR=`grep HOST_PLATFORM_SRC_DIR platform/$PLATFORM/platform.inc | sed s/.*=//`
    local KERNEL=`grep KERNEL_MAJOR_VERSION platform/$PLATFORM/platform.inc | sed s/.*=//`
    if [ "$KERNEL" = "2.6" ]; then
	SUFFIX=.ko
    else
	SUFFIX=.mod
    fi

    echo "==========> Making wifi-source at TMP_DIR =  " $TMP_DIR
    mkdir -p $TMP_DIR
    mkdir -p $TMP_DIR/kernel
    mkdir -p $TMP_DIR/userspace
    cp -LRf userspace/libnrx $TMP_DIR/userspace

    if [ "$PLATFORM" = "pmc" ]; then
	    cp -LRf userspace/libpmc $TMP_DIR/userspace
    fi
    
    # Ugly: Makefile breaks if timestamps are not updated after cvs export
    touch userspace/libnrx/*.c

    cp -LRf kernel/ic $TMP_DIR/kernel
    mkdir -p $TMP_DIR/kernel/$TRANSPORT
    cp -Lf kernel/$TRANSPORT/* $TMP_DIR/kernel/$TRANSPORT
    
    cp -LRf kernel/$TRANSPORT/$HOST_PLATFORM_SRC_DIR $TMP_DIR/kernel/$TRANSPORT
    cp -LRf WiFiEngine $TMP_DIR
    rm -Rf $TMP_DIR/WiFiEngine/_osedrv
    rm -Rf $TMP_DIR/WiFiEngine/_win
    rm -Rf $TMP_DIR/WiFiEngine/_windrv

    mkdir -p $TMP_DIR/platform
    cp -LRf platform/$PLATFORM $TMP_DIR/platform

    find $TMP_DIR -name .svn | xargs rm -Rf
    tar cvzf $OBJ_DIR/HostDriver-$RELEASE_VERSION.tar.gz $TMP_DIR

    make PLATFORM=$PLATFORM

    if [ ! -f obj/nano_$TRANSPORT$SUFFIX ]; then
	echo "Broken source tree, fix release.sh"
	rm $OBJ_DIR/HostDriver-$RELEASE_VERSION.tar.gz
    fi

    rm -Rf $TMP_DIR
}

sdio-source()
{
    make PLATFORM=$PLATFORM clean

    local TRANSPORT=`grep TRANSPORT platform/$PLATFORM/platform.inc | sed s/.*=//`
    local HOST_PLATFORM_SRC_DIR=`grep HOST_PLATFORM_SRC_DIR platform/$PLATFORM/platform.inc | sed s/.*=//`
    local KERNEL=`grep KERNEL_MAJOR_VERSION platform/$PLATFORM/platform.inc | sed s/.*=//`
    if [ "$KERNEL" = "2.6" ]; then
	SUFFIX=.ko
    else
	SUFFIX=.mod
    fi

    if [ "$TRANSPORT" != "sdio" ]; then
    	echo "Only sdio transport is currently supported."
        exit 2
    fi

    echo "==========> Making sdio-source at TMP_DIR =  " $TMP_DIR
    mkdir -p $TMP_DIR
    mkdir -p $TMP_DIR/kernel
    mkdir -p $TMP_DIR/kernel/ic
    cp -LRf kernel/ic/transport.h $TMP_DIR/kernel/ic
    cp -LRf kernel/ic/compat24.h $TMP_DIR/kernel/ic
    cp -LRf kernel/ic/nanoutil.h $TMP_DIR/kernel/ic
    mkdir -p $TMP_DIR/kernel/sdio
    cp -Lf kernel/sdio/* $TMP_DIR/kernel/sdio
    cp -LRf kernel/sdio/$HOST_PLATFORM_SRC_DIR $TMP_DIR/kernel/sdio

    mkdir -p $TMP_DIR/platform
    cp -LRf platform/$PLATFORM $TMP_DIR/platform

    find $TMP_DIR -name .svn | xargs rm -Rf
    tar cvzf $OBJ_DIR/SdioDriver-$RELEASE_VERSION.tar.gz $TMP_DIR

    make transport PLATFORM=$PLATFORM
    
    if [ ! -f obj/nano_$TRANSPORT$SUFFIX ]; then
	echo "Broken SDIO source tree, fix release.sh"
	return
    fi

    rm -Rf $TMP_DIR
}


mm6-ramdisk()
{
    local FINAL_PATH="$RELEASE_REQ/NeoMagic/$KERNEL"
    if   ! [ -f $FINAL_PATH/vmlinux.bin ] ||  ! [ -f $FINAL_PATH/ramdisk-noqt.bin.gz ] ; then
        mkdir -p $FINAL_PATH
        local link="smb://172.16.1.1/project/P821%20Linux%20platform/release_require/Neomagic/$KERNEL"
        echo "Downloading Neomagic kernel + rootfs: " $link
        (cd $FINAL_PATH; smbget -w WEP -R $link)
    fi

    cp $RELEASE_REQ/NeoMagic/$KERNEL/vmlinux.bin $OBJ_DIR
    cp $RELEASE_REQ/NeoMagic/$KERNEL/ramdisk-noqt.bin.gz $OBJ_DIR
    (cd $OBJ_DIR; gunzip ramdisk-noqt.bin.gz)
    mkdir -p $OBJ_DIR/mnt
    sudo mount -t ext2 -o loop $OBJ_DIR/ramdisk-noqt.bin $OBJ_DIR/mnt

    if [ $KERNEL == "2.6" ]; then
    sudo cp -f $OBJ_DIR/nano_if.ko $OBJ_DIR/mnt/lib/modules/2.6.14/misc
    sudo cp -f $OBJ_DIR/nano_sdio.ko $OBJ_DIR/mnt/lib/modules/2.6.14/misc
    fi

    if [ $KERNEL == "2.4" ]; then
    sudo cp -f $OBJ_DIR/nano_if.mod $OBJ_DIR/mnt/lib/modules/2.4.21-rmk1/misc
    sudo cp -f $OBJ_DIR/nano_sdio.mod $OBJ_DIR/mnt/lib/modules/2.4.21-rmk1/misc
    fi

    sudo cp -f $OBJ_DIR/ifrename $OBJ_DIR/mnt/sbin
    sudo cp -f $OBJ_DIR/iwconfig $OBJ_DIR/mnt/sbin
    sudo cp -f $OBJ_DIR/iwlist $OBJ_DIR/mnt/sbin
    sudo cp -f $OBJ_DIR/iwpriv $OBJ_DIR/mnt/sbin
    sudo cp -f $OBJ_DIR/iwevent $OBJ_DIR/mnt/sbin
    sudo cp -f $OBJ_DIR/wpa_supplicant $OBJ_DIR/mnt/sbin
    sudo cp -f $OBJ_DIR/wpa_cli $OBJ_DIR/mnt/sbin
    sudo cp -f $OBJ_DIR/x_mac.axf $OBJ_DIR/mnt/lib/firmware
    sudo cp -f $OBJ_DIR/ettcp $OBJ_DIR/mnt/sbin
    sudo cp -f $OBJ_DIR/iperf $OBJ_DIR/mnt/sbin
    sudo umount $OBJ_DIR/mnt
    (cd $OBJ_DIR; gzip ramdisk-noqt.bin)
    rmdir $OBJ_DIR/mnt
}

imx31-ramdisk()
{
    local FINAL_PATH="$RELEASE_REQ/Freescale_iMX31"
    if ![ -f $FINAL_PATH/rootfs.tar.gz ]; then
        mkdir -p $FINAL_PATH
        local link="smb://172.16.1.1/project/P821%20Linux%20platform/release_require/Freescale_iMX31/rootfs.tar.gz"
        echo "Downloading Freescale i.mx31 development board rootfs: " $link
        (cd $FINAL_PATH; smbget -w WEP -R $link)
    fi

    cp $RELEASE_REQ/Freescale_iMX31/rootfs.tar.gz $OBJ_DIR
    (cd $OBJ_DIR; tar -xvf rootfs.tar.gz)
    sudo cp -f $OBJ_DIR/ifrename $OBJ_DIR/rootfs/home
    sudo cp -f $OBJ_DIR/iwconfig $OBJ_DIR/rootfs/home
    sudo cp -f $OBJ_DIR/iwlist $OBJ_DIR/rootfs/home
    sudo cp -f $OBJ_DIR/iwpriv $OBJ_DIR/rootfs/home
    sudo cp -f $OBJ_DIR/iwevent $OBJ_DIR/rootfs/home
    sudo cp -f $OBJ_DIR/wpa_supplicant $OBJ_DIR/rootfs/home
    sudo cp -f $OBJ_DIR/wpa_cli $OBJ_DIR/rootfs/home
    sudo cp -f $OBJ_DIR/x_mac.axf $OBJ_DIR/rootfs/home
    sudo cp -f $OBJ_DIR/ettcp $OBJ_DIR/rootfs/home
    sudo cp -f $OBJ_DIR/iperf $OBJ_DIR/rootfs/home

}

xmac()
{
    local XMAC_MAJOR_VERSION=`echo $XMAC_VERSION | sed -e 's%^\([0-9]*\)\.\([0-9]*\)\..*%\1.\2%'`
    cd $OBJ_DIR
    local link=smb://172.16.1.1/project/P0401/Design/fw/Releases/Release%20$XMAC_MAJOR_VERSION/DYD004%20WiFiCore/$XMAC_VERSION/$CHIP_REVISION/nosymbols/x_mac.axf
    echo "Downloading fw: " $link
    smbget -w WEP $link
    cd -
}

xtest()
{
    local XTEST_MAJOR_VERSION=`echo $XTEST_VERSION | sed -e 's%^\([0-9]*\)\.\([0-9]*\)\..*%\1.\2%'`
    cd $OBJ_DIR
    local link=smb://172.16.1.1/project/P0401/Design/fw/Releases/Release%20$XTEST_MAJOR_VERSION/DYD005%20WiFiTest/$XTEST_VERSION/$CHIP_REVISION/nosymbols/x_test.axf
    echo "Downloading test fw: " $link
    smbget -w WEP $link
    cd -
}

do-cd-release()
{
    # Compile MM6 device driver, release flags
    echo "===========> Now building at `pwd` platform $PLATFORM"
    make clean PLATFORM=$PLATFORM
    make all strip PLATFORM=$PLATFORM CONFIG=release

    # Get firmware
    xmac 

    # Cross compile tools
    make ettcp PLATFORM=$PLATFORM
    make iperf PLATFORM=$PLATFORM
    make iw PLATFORM=$PLATFORM
    make wpa-supplicant PLATFORM=$PLATFORM
    make hic-proxy PLATFORM=$PLATFORM
   
    if [ "$KERNEL" = "2.6" ]; then
	SUFFIX=.ko
    else
	SUFFIX=.mod
    fi

    # Generate MM6 ramdisk
    if [ "$PLATFORM" = "neomagic26" ]; then
        mm6-ramdisk
    fi

    # Generate documentation
    (cd docs; doxygen NrxApi.dox)
    (cd docs; doxygen SdioDriverApi.dox)

    # Get source code tree
    sdio-source
    wifi-source

    # Check for missing files
    for f in nano_if.ko nano_sdio.ko iwconfig iwlist iwpriv ifrename iwevent wpa_supplicant wpa_cli \
	x_mac.axf ettcp iperf hic-proxy ramdisk-noqt.bin.gz \
	vmlinux.bin HostDriver-$RELEASE_VERSION.tar.gz \
	SdioDriver-$RELEASE_VERSION.tar.gz; do
      if [ ! -f $OBJ_DIR/$f ]; then
	  echo !! WARNING $f missing !!
      fi
    done

    for f in SdioPortingGuide LinuxUserGuide MacMgmtGuide \
	SrcCodeDesc RelaseNote; do
      echo !! INFO $f has to be put on the CD:s manually !!
    done

    # Create local release directory
    echo "=============> Making local release from `pwd`"
    echo "=============> Target location =  $CD_TMP_DIR  - Release $RELEASE_VERSION"
    mkdir -p $RELEASE_DIR
    mkdir -p $CD_TMP_DIR
    mkdir -p $CD_TMP_DIR/$RELEASE_VERSION
    mkdir -p $CD_TMP_DIR/$RELEASE_VERSION/DRF001_3_1_SW_development_kit
    mkdir -p $CD_TMP_DIR/$RELEASE_VERSION/DRF001_3_2_SW_development_kit_with_WiFi_engine_source
    EVALUATION_CD=$CD_TMP_DIR/$RELEASE_VERSION/DRF001_3_1_SW_development_kit
    DEVELOPMENT_CD=$CD_TMP_DIR/$RELEASE_VERSION/DRF001_3_2_SW_development_kit_with_WiFi_engine_source

    # Create Directory structure for EVALUATION_CD
    create_dir $EVALUATION_CD/Documentation
    create_dir $EVALUATION_CD/LinuxDriver
    create_dir $EVALUATION_CD/SourceCode
    create_dir $EVALUATION_CD/$PLATFORM
    create_dir $EVALUATION_CD/Tools
    create_dir $EVALUATION_CD/WifiCore

    # Create Directory structure for DEVELOPMENT_CD
    create_dir $DEVELOPMENT_CD/Documentation
    create_dir $DEVELOPMENT_CD/LinuxDriver
    create_dir $DEVELOPMENT_CD/SourceCode
    create_dir $EVALUATION_CD/$PLATFORM
    create_dir $DEVELOPMENT_CD/Tools
    create_dir $DEVELOPMENT_CD/WifiCore

    # Copy files that are shared by Evaluation CD and Development CD
    cp -Rf $OBJ_DIR/nano_if$SUFFIX $EVALUATION_CD/LinuxDriver
    cp -Rf $OBJ_DIR/nano_sdio$SUFFIX $EVALUATION_CD/LinuxDriver
    cp -Rf $OBJ_DIR/ifrename $EVALUATION_CD/Tools
    cp -Rf $OBJ_DIR/iwconfig $EVALUATION_CD/Tools
    cp -Rf $OBJ_DIR/iwlist $EVALUATION_CD/Tools
    cp -Rf $OBJ_DIR/iwpriv $EVALUATION_CD/Tools
    cp -Rf $OBJ_DIR/iwevent $EVALUATION_CD/Tools
    cp -Rf $OBJ_DIR/wpa_supplicant $EVALUATION_CD/Tools
    cp -Rf $OBJ_DIR/wpa_cli $EVALUATION_CD/Tools
    cp -Rf $OBJ_DIR/ettcp $EVALUATION_CD/Tools
    cp -Rf $OBJ_DIR/iperf $EVALUATION_CD/Tools
    cp -Rf $OBJ_DIR/x_mac.axf $EVALUATION_CD/WifiCore
    #cp -Rf $OBJ_DIR/nanoloader_proxy $EVALUATION_CD
    cp -Rf $OBJ_DIR/SdioDriverApiDocumentation $EVALUATION_CD/Documentation
    cp -Rf $OBJ_DIR/NrxApiDocumentation $EVALUATION_CD/Documentation
    if [ "$PLATFORM" = "neomagic26" ]; then
        cp -Rf $OBJ_DIR/ramdisk-noqt.bin.gz $EVALUATION_CD/$PLATFORM
        cp -Rf $OBJ_DIR/vmlinux.bin $EVALUATION_CD/$PLATFORM
    fi
    cp -Rf $EVALUATION_CD/* $DEVELOPMENT_CD

    # Copy Evaluation CD specific files
    cp -Rf $OBJ_DIR/SdioDriver-$RELEASE_VERSION.tar.gz $EVALUATION_CD/SourceCode

    # Copy Development CD specific files
    cp -Rf $OBJ_DIR/HostDriver-$RELEASE_VERSION.tar.gz $DEVELOPMENT_CD/SourceCode

    rm -Rf $DOXY_OBJ_DIR
}

do-iv-release()
{
    mkdir -p $RELEASE_DIR
    mkdir -p $IV_TMP_DIR
    mkdir -p $IV_TMP_DIR/$RELEASE_VERSION

    # Update release tag
    sed -i {s/"Release info not updated in build"/"$RELEASE_VERSION"/} WiFiEngine/wifi_drv/inc/release_tag.h

    for PLATFORM in neomagic26 imx31 ubuntu ubuntu-kdb; do 
	mkdir -p $IV_TMP_DIR/$RELEASE_VERSION/$PLATFORM
	rm -Rf $OBJ_DIR/*

	make clean PLATFORM=$PLATFORM
	make PLATFORM=$PLATFORM
	make nrxapi PLATFORM=$PLATFORM
	make hic-proxy PLATFORM=$PLATFORM
	cd -

	cp $OBJ_DIR/nano_* $IV_TMP_DIR/$RELEASE_VERSION/$PLATFORM/
	cp $OBJ_DIR/nrxpriv $IV_TMP_DIR/$RELEASE_VERSION/$PLATFORM/
    cp $OBJ_DIR/libnrx.a $IV_TMP_DIR/$RELEASE_VERSION/$PLATFORM/
	cp $OBJ_DIR/hic-proxy $IV_TMP_DIR/$RELEASE_VERSION/$PLATFORM/

	if [ "$PLATFORM" = "pmc" ]; then
	    make pmcapi PLATFORM=$PLATFORM
	else
	    # Ugly way to force compilation of pmc-only targets
	    # for other platforms.
	    cat Makefile platform/pmc/targets.inc | make -f - pmcapi PLATFORM=$PLATFORM
	fi

	cp $OBJ_DIR/local XMAC_MAJOR_VERSION=`echo $XMAC_VERSION | sed -e 's%^\([0-9]*\)\.\([0-9]*\)\..*%\1.\2%'`pmcpriv $IV_TMP_DIR/$RELEASE_VERSION/$PLATFORM/

	rm -Rf $OBJ_DIR/*
  
	make clean PLATFORM=$PLATFORM
	make PLATFORM=$PLATFORM CONFIG=release
	cd -

	# ugly
	for suffix in mod ko; do
	    for module in nano_if nano_pci nano_sdio; do
		file=$OBJ_DIR/$module.$suffix
		if [ -e $file ]; then 
		    cp $file $IV_TMP_DIR/$RELEASE_VERSION/$PLATFORM/$module-release.$suffix
		fi
	    done
	done

    done

    mkdir -p $IV_TMP_DIR/$RELEASE_VERSION/docs/headers/
    cp userspace/libnrx/nrx_proto.h userspace/libnrx/nrx_lib.h $IV_TMP_DIR/$RELEASE_VERSION/docs/headers/        
    if [ "$PLATFORM" = "pmc" ]; then
	    cp userspace/libpmc/pmc_proto.h $IV_TMP_DIR/$RELEASE_VERSION/docs/headers/
    fi

    (cd docs; doxygen NrxApi.dox; doxygen PmcApi.dox)
    mkdir -p $IV_TMP_DIR/$RELEASE_VERSION/docs
    cp -R $OBJ_DIR/NrxApiDocumentation $IV_TMP_DIR/$RELEASE_VERSION/docs/
    if [ "$PLATFORM" = "pmc" ]; then
        cp -R $OBJ_DIR/PmcApiDocumentation $IV_TMP_DIR/$RELEASE_VERSION/docs/
    fi
}

clean()
{
    rm -Rf $RELEASE_DIR
    rm -Rf $OBJ_DIR
}



while getopts :r:x:e:k:p:c: option; do

    case $option in
 	r)  
 	    RELEASE_VERSION=$OPTARG
 	    ;;
	
 	x)  
 	    XMAC_VERSION=$OPTARG
 	    ;;

 	e)  
 	    XTEST_VERSION=$OPTARG
 	    ;;

 	k)  
 	    KERNEL=$OPTARG
 	    ;;

 	p)  
 	    PLATFORM=$OPTARG
 	    ;;
	
 	c)  
 	    CHIP_REVISION=$OPTARG
 	    ;;
	
 	\?) 
	usage
	exit 2
	;;

    esac
done

shift $(($OPTIND - 1))
case $1 in

    iv | do-iv-release) 
 	do-iv-release
 	;;

    cd | do-cd-release) 
 	do-cd-release
 	;;

    xmac)
 	xmac 
 	;;
    
    xtest)
 	xtest
 	;;

    sdio-source)
 	sdio-source
 	;;

    wifi-source)
 	wifi-source
 	;;

    mm6-ramdisk)
 	mm6-ramdisk
 	;;

    clean)
	clean
	;;
    
    *) 
    usage
    exit 2 	
 	;;
esac
