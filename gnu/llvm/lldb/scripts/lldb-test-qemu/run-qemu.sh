#!/bin/bash

print_usage() {
  echo "Usage: $(basename $0) --arch [arm|arm64] [options]"
  echo -e "Starts QEMU system mode emulation for the architecture.\n"
  echo -e "  --help\t\t\tDisplay this information."
  echo -e "  --arch {arm|arm64}\t\tSelects architecture QEMU system emulation."
  echo -e "  --sve\t\t\t\tEnables AArch64 SVE mode."
  echo -e "  --mte\t\t\t\tEnables AArch64 MTE mode."
  echo -e "  --sme\t\t\t\tEnables AArch64 SME mode."
  echo -e "  --rootfs {path}\t\tPath of root file system image."
  echo -e "  --qemu {path}\t\t\tPath of pre-installed qemu-system-* executable."
  echo -e "  --kernel {path}\t\tPath of Linux kernel prebuilt image.\n"
  echo -e "By default this utility will use:"
  echo -e "  QEMU image built from source in qemu.git directory"
  echo -e "  Linux kernel image from linux.build/(arm or arm64) directory."
  echo -e "Custom Linux kernel image or QEMU binary can be provided using commandline."
  exit "$1"
}

invalid_arg() {
  echo "ERROR: Unrecognized argument: $1" >&2
  print_usage 1
}

run_qemu() {
  QEMU_CORES=2
  QEMU_MEMORY=1024

  $QEMU_BIN \
  -cpu $QEMU_CPU \
  -m $QEMU_MEMORY \
  -smp $QEMU_CORES \
  -kernel $KERNEL_IMG \
  -machine $QEMU_MACHINE \
  -drive file=$ROOTFS_IMG,if=none,format=raw,id=hd0 \
  -device virtio-blk-device,drive=hd0 \
  -append "root=/dev/vda rw ip=dhcp mem=1024M raid=noautodetect  \
  crashkernel=128M rootwait console=ttyAMA0 devtmpfs.mount=0" \
  -netdev type=tap,id=net0 \
  -device virtio-net-device,netdev=net0 \
  -nographic
}

# Parse options
while [[ $# -gt 0 ]]; do
  case "${END_OF_OPT}${1}" in
    --arch)     ARCH=$2; shift;;
    --rootfs)   ROOTFS_IMG=$2; shift;;
    --kernel)   KERNEL_IMG=$2; shift;;
    --qemu)     QEMU_BIN=$2; shift;;
    --sve)      SVE=1;;
    --mte)      MTE=1;;
    --sme)      SME=1;;
    --help)     print_usage 0 ;;
    *)          invalid_arg "$1" ;;
  esac
  shift
done

if [ "$ARCH" == "arm64" ] && [ "$ARCH" == "arm" ]; then
  echo "Invalid architecture: $ARCH"
  print_usage 1
fi

if [[ ! -f "$ROOTFS_IMG" ]]; then
  echo "No root file system image image available for emulation."
  exit
fi

if [[ ! -f "$KERNEL_IMG" ]]; then
  KERNEL_IMG_PATH=$(pwd)/linux.build/"$ARCH"/arch/"$ARCH"/boot/

  if [[ ! -d "$KERNEL_IMG_PATH" ]]; then
    echo "No Linux kernel image available for emulation."
    exit
  fi

  if [[ "$ARCH" == "arm" ]]; then
    KERNEL_IMG=$KERNEL_IMG_PATH/zImage
  elif [[ "$ARCH" == "arm64" ]]; then
    KERNEL_IMG=$KERNEL_IMG_PATH/Image
  fi
fi

if [[ ! -f "$QEMU_BIN" ]]; then
  if [[ "$ARCH" == "arm" ]]; then
    QEMU_BIN=$(pwd)/qemu.git/arm-softmmu/qemu-system-arm
  elif [[ "$ARCH" == "arm64" ]]; then
    QEMU_BIN=$(pwd)/qemu.git/aarch64-softmmu/qemu-system-aarch64
  fi

  if [[ ! -f "$QEMU_BIN" ]]; then
    echo "QEMU $ARCH system emulation executable not found."
    exit
  fi
fi

if [[ "$ARCH" == "arm" ]]; then
  QEMU_MACHINE="virt,highmem=off"
  QEMU_CPU="cortex-a15"

  if [[ $SVE ]]; then
    echo "warning: --sve is supported by AArch64 targets only"
  fi
  if [[ $MTE ]]; then
    echo "warning: --mte is supported by AArch64 targets only"
  fi
  if [[ $SME ]]; then
    echo "warning: --sme is supported by AArch64 targets only"
  fi
elif [[ "$ARCH" == "arm64" ]]; then
  QEMU_MACHINE=virt
  QEMU_SVE_MAX_VQ=4
  QEMU_CPU="cortex-a53"

  if [[ $SVE ]] || [[ $MTE ]] || [[ $SME ]]; then
    QEMU_CPU="max"
  fi

  if [[ $SVE ]] || [[ $SME ]]; then
    QEMU_CPU="$QEMU_CPU,sve-max-vq=$QEMU_SVE_MAX_VQ"
  fi
  if [[ $MTE ]]; then
    QEMU_MACHINE="$QEMU_MACHINE,mte=on"
  fi
fi

run_qemu
