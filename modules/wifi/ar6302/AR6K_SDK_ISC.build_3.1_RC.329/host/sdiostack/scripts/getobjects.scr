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
checkandcopy $SOURCE_BASE_PATH/function bluetooth sdio_bluetooth_fd.$mext $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/function memory    sdio_memory_fd.$mext    $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/function gps       sdio_gps_fd.$mext       $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/function sample    sdio_sample_fd.$mext    $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/function bench     sdio_bench_fd.$mext    $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd pci_ellen    sdio_pciellen_hcd.$mext $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd pci_std      sdio_pcistd_hcd.$mext   $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd pxa255       sdio_pxa255hcd.$mext    $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd pxa270       sdio_pxa270hcd.$mext    $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd omap_raw_spi sdio_omap_raw_spi_hcd.$mext $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd omap         sdio_omap_hcd.$mext     $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd mx21         sdio_mx21_hcd.$mext     $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd omap_2420    sdio_omap_hcd.$mext     $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd omap2420_raw_spi sdio_omap_raw_spi_hcd.$mext $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd omap2420_spi2 athspi_omap2420_hcd.$mext $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd omap2420_spi2 athspi_omap2420_hcd.$mext $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd s3c6400      sdio_s3c6400.$mext $OUTPUT_BASE_PATH
checkandcopy $SOURCE_BASE_PATH/hcd s3c6410      sdio_s3c6410_hcd.$mext $OUTPUT_BASE_PATH


