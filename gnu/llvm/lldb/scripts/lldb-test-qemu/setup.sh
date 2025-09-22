#!/bin/bash

print_usage() {
  echo "Usage: $(basename $0) [options]"
  echo -e "Builds QEMU and Linux kernel from source.\n"
  echo -e "  --help\t\t\tDisplay this information."
  echo -e "  --kernel {arm|arm64}\t\tBuild Linux kernel for the architecture."
  echo -e "  --qemu\t\t\tBuild QEMU from source."
  echo -e "  --clean\t\t\tRemove qemu.git and linux.git directories in current directory."
  exit "$1"
}

update_repositories() {
  echo -e "\nUpdating apt repositories. "
  echo -e "\nPress 'y' to continue or any other key to exit..."
  read -s -n 1 user_input
  if [[ $user_input == 'Y' ]] || [[ $user_input == 'y' ]]; then
    sudo apt update
  else
    exit
  fi
}

check_dir_exists() {
  user_input=
  if [ -d "$1" ]; then
    echo -e "\n$1 already exists in working directory and will not be updated."
    echo -e "\nPress 'y' to continue or any other key to exit..."
    read -s -n 1 user_input
    if [[ $user_input != 'Y' ]] && [[ $user_input != 'y' ]]; then
      exit
    fi
  fi
}

invalid_arg() {
  echo "ERROR: Unrecognized argument: $1" >&2
  print_usage 1
}

build_qemu() {
  echo "Installing QEMU build dependencies ..."
  sudo apt install git python3-dev libsdl1.2-dev build-essential libpixman-1-dev

  # Checkout source code
  check_dir_exists "qemu.git"
  if [ ! -d "qemu.git" ]; then
    git clone --depth 1 https://gitlab.com/qemu-project/qemu.git qemu.git
  fi

  cd qemu.git
  # We are going to build QEMU Arm and AArch64 system mode emulation.
  # ./configure --help emits a list of other possible targets supported by QEMU.
  ./configure --target-list=arm-softmmu,aarch64-softmmu
  make -j`getconf _NPROCESSORS_ONLN`
}

build_linux() {
  echo "Installing Linux kernel build dependencies ..."
  sudo apt install git bison flex build-essential libssl-dev bc

  check_dir_exists "linux.git"

  if [ ! -d "linux.git" ]; then
    git clone --depth 1 \
    https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git linux.git
  fi

  cd linux.git
  make mrproper

  if [[ "$1" == "arm" ]]; then
    echo "Installing gcc-arm-linux-gnueabihf ..."
    sudo apt install gcc-arm-linux-gnueabihf

    # Configure kernel_branch=master arch=arm config=vexpress_defconfig
    make O=../linux.build/arm ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
    vexpress_defconfig

    # Trigger Arm kernel build
    make -j`getconf _NPROCESSORS_ONLN` O=../linux.build/arm ARCH=arm \
    CROSS_COMPILE=arm-linux-gnueabihf-
  elif [[ "$1" == "arm64" ]]; then
    echo "Installing gcc-aarch64-linux-gnu ..."
    sudo apt install gcc-aarch64-linux-gnu

    # Configure kernel_branch=master arch=arm64 config=defconfig
    make O=../linux.build/arm64 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    defconfig

    # Trigger AArch64 kernel build
    make -j`getconf _NPROCESSORS_ONLN` O=../linux.build/arm64 ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu-
  else
    echo "ERROR: Unrecognized architecture: $1" >&2
    print_usage 1
    exit
  fi
}

clean() {
  if [ -d "linux.git" ]; then
    echo "Removing linux.git ..."
    rm -rf linux.git
  fi

  if [ -d "linux.build" ]; then
    echo "Removing linux.build ..."
    rm -rf linux.build
  fi

  if [ -d "qemu.git" ]; then
    echo "Removing qemu.git ..."
    rm -rf qemu.git
  fi

  exit
}

# Parse options
while [[ $# -gt 0 ]]; do
  case "${END_OF_OPT}${1}" in
    -h|--help)   print_usage 0 ;;
    -k|--kernel)
      if [ "$2" == "arm64" ] || [ "$2" == "arm" ]; then
      KERNEL_ARCH=$2
      else
        invalid_arg "$2"
      fi
      shift;;
    -q|--qemu)
        QEMU=1;;
    -c|--clean)  clean ;;
    *)           invalid_arg "$1" ;;
  esac
  shift
done

update_repositories

if [ "$KERNEL_ARCH" != "" ]; then
  pushd .
  build_linux $KERNEL_ARCH
  popd
fi

if [[ $QEMU -eq 1 ]]; then
  pushd .
  build_qemu
  popd
fi
