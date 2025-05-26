#!/bin/bash
if command -v apt &> /dev/null; then
sudo apt update
sudo apt-get install build-essential ncurses-dev bison flex libssl-dev libelf-dev -y
sudo apt-get install -y debhelper-compat=12
sudo apt install libdw-dev libelf-dev binutils-dev

fi

if command -v yum &> /dev/null; then
yum -y install  build-essential ncurses-dev bison flex libssl-dev libelf-dev
yum -y install openssl-devel
fi

\cp /boot/$(ls /boot/ | grep config | head -1)  arch/x86/configs/my_defconfig
sed -i 's/^CONFIG_MODULE_SIG=.*/CONFIG_MODULE_SIG=n/' arch/x86/configs/my_defconfig
sed -i 's/^CONFIG_DEBUG_INFO=.*/CONFIG_DEBUG_INFO=n/' arch/x86/configs/my_defconfig
sed -i 's/^CONFIG_MODULE_SIG_ALL=.*/CONFIG_MODULE_SIG_ALL=n/' arch/x86/configs/my_defconfig
sed -i 's/^CONFIG_SYSTEM_REVOCATION_KEYS=.*/CONFIG_SYSTEM_REVOCATION_KEYS=""/' arch/x86/configs/my_defconfig
sed -i 's/^CONFIG_MODULE_SIG_KEY=.*/CONFIG_MODULE_SIG_KEY=""/' arch/x86/configs/my_defconfig
sed -i 's/^CONFIG_SYSTEM_TRUSTED_KEYS=.*/CONFIG_SYSTEM_TRUSTED_KEYS=""/' arch/x86/configs/my_defconfig

sed -i 's/^CONFIG_ZRAM=.*/CONFIG_ZRAM=m/' arch/x86/configs/my_defconfig
sed -i 's/^CONFIG_ZRAM_DEF_COMP=.*/CONFIG_ZRAM_DEF_COMP="zstd"/' arch/x86/configs/my_defconfig
sed -i 's/^CONFIG_ZRAM_BACKEND_FORCE_LZO=.*/CONFIG_ZRAM_BACKEND_FORCE_LZO=n/' arch/x86/configs/my_defconfig
sed -i 's/^CONFIG_ZRAM_BACKEND_LZO=.*/CONFIG_ZRAM_BACKEND_LZO=n/' arch/x86/configs/my_defconfig
echo "CONFIG_ZRAM_BACKEND_ZSTD=y" >> arch/x86/configs/my_defconfig
echo "CONFIG_ZRAM_DEF_COMP_ZSTD=y" >> arch/x86/configs/my_defconfig


# sed -i 's/^CONFIG_ZSWAP_COMPRESSOR_DEFAULT=.*/CONFIG_ZSWAP_COMPRESSOR_DEFAULT="zstd"/' arch/x86/configs/my_defconfig
# sed -i 's/^CONFIG_ZSWAP_COMPRESSOR_DEFAULT_LZO=.*/CONFIG_ZSWAP_COMPRESSOR_DEFAULT_LZO=n/' arch/x86/configs/my_defconfig

# echo "CONFIG_ZSWAP_COMPRESSOR_DEFAULT_ZSTD=y"  >> arch/x86/configs/my_defconfig
# echo "CONFIG_ZSWAP_DEFAULT_ON=y" >> arch/x86/configs/my_defconfig

echo "CONFIG_MEMCG_V1=y" >> arch/x86/configs/my_defconfig
echo "CONFIG_CPUSETS_V1=y" >> arch/x86/configs/my_defconfig

make my_defconfig


LOCALVERSION= make -j$(nproc)

make modules -j$(nproc)
make bindeb-pkg -j$(nproc)

#make modules_install
#rm -rf /boot/*6.15*
#make install

