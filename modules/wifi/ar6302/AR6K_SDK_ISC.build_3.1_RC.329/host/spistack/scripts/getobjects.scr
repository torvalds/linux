#!/bin/sh
#  
#  script to collect linux binary modules
#  Copyright 2004-2006 Atheros Communications, Inc.
#

SOURCE_BASE_PATH=$1
OUTPUT_BASE_PATH=$2
mext=$3

checkandcopy()
{
  if [ -f $1/$2/$3 ]; then
	 cp -f -v $1/$2/$3 $4/$3
     if [ -f $1/$2/$2_readme.txt ]; then
          cp -f -v $1/$2/$2_readme.txt $4/$2_readme.txt
     fi
  fi  
}

checkandcopy $SOURCE_BASE_PATH          busdriver sdio_busdriver.$mext    $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH          lib       sdio_lib.$mext          $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd omap2420_spi2 athspi_omap2420_hcd.$mext $OUTPUT_BASE_PATH



