#!/bin/sh
#
# Platform-Dependent Setup script Template.
# This file should be re-name as plat_<ATH_PLATFORM>.sh
#    ATH_PLATFORM identifies the build and bus type, ie:
#       LOCAL_i686-SDIO
#
# The script argument is a operation to be carried out:
#  
# 	loadbus - load system support modules for the bus (optional)
#	unloadbus - unload system support modules for the bus (optional)
#	loadAR6K - load the AR6K module with any additional module parameters
#	unloadAR6K - unload the AR6K module
#

# note IMAGEPATH is exported by the top level script 
#

case $1 in
	loadbus)
	# TODO : load any bus dependent kernel modules here
	;;
	
	unloadbus)
	# TODO: unload any bus dependent kernel modules
	;;
	
	loadAR6K)
	# load ar6k kernel module, the following variables are exported by the calling script:
	#     AR6K_MODULE_ARGS 
	#     AR6K_TGT_LOGFILE 
	#     AR6K_MODULE_NAME 
	
	# TODO - start logging:
	#     $IMAGEPATH/recEvent $AR6K_TGT_LOGFILE /dev/null 2>&1 &
	
	/sbin/insmod $IMAGEPATH/$AR6K_MODULE_NAME.ko $AR6K_MODULE_ARGS
	;;
	
	unloadAR6K)
	# TODO - kill logging:   
	#     killall recEvent
	
	/sbin/rmmod -w $AR6K_MODULE_NAME.ko
	;;
	*)
		echo "Unknown option : $1"
	
esac
