#!/bin/bash

set -e

print_usage() {
  echo "Usage: $(basename $0) [options]"
  echo -e "Creates a Ubuntu root file system image.\n"
  echo -e "  --help\t\t\tDisplay this information."
  echo -e "  --arch {armhf|arm64}\t\tSelects architecture of rootfs image."
  echo -e "  --distro {bionic|focal}\tSelects Ubuntu distribution of rootfs image."
  echo -e "  --size n{K|M|G}\t\tSets size of rootfs image to n Kilo, Mega or Giga bytes."
  exit "$1"
}

invalid_arg() {
  echo "ERROR: Unrecognized argument: $1" >&2
  print_usage 1
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

# Parse options
while [[ $# -gt 0 ]]; do
  case "${END_OF_OPT}${1}" in
    --help)     print_usage 0 ;;
    --arch)     rfs_arch=$2;   shift;;
    --distro)   rfs_distro=$2; shift;;
    --size)     rfs_size=$2;   shift;;
    *)          invalid_arg "$1" ;;
  esac
  shift
done

if [ -z "$rfs_arch" ]; then
  echo "Missing architecture"
  print_usage 1
fi
if [ -z "$rfs_distro" ]; then
  echo "Missing distribution"
  print_usage 1
fi
if [ -z "$rfs_size" ]; then
  echo "Missing size"
  print_usage 1
fi

if [[ "$rfs_arch" != "arm64" && "$rfs_arch" != "armhf" ]]; then
  echo "Invalid architecture: $rfs_arch"
  print_usage 1
fi

pat='^[0-9]+[K|M|G]$'
if [[ ! $rfs_size =~ $pat ]]; then
  echo "Invalid size: $rfs_size"
  print_usage 1
fi

update_repositories

echo "Installing build dependencies ..."
sudo apt-get install debootstrap qemu-user-static schroot qemu-utils

image_name=$rfs_distro-$rfs_arch-"rootfs"
echo "Creating $rfs_distro ($rfs_arch) root file system ..."
echo "Image name: $image_name.img"
echo "Image size: $rfs_size"

qemu-img create $image_name.img $rfs_size

mkfs.ext4 $image_name.img
mkdir $image_name.dir
sudo mount -o loop $image_name.img $image_name.dir

sudo qemu-debootstrap --arch $rfs_arch $rfs_distro $image_name.dir

sudo chroot $image_name.dir locale-gen en_US.UTF-8

sudo chroot $image_name.dir sed -i \
's/main/main restricted multiverse universe/g' /etc/apt/sources.list

sudo chroot $image_name.dir sed -i '$ a\nameserver 8.8.8.8' /etc/resolv.conf

sudo chroot $image_name.dir apt update
sudo chroot $image_name.dir apt -y install ssh bash-completion
sudo chroot $image_name.dir adduser --gecos "" $USER
sudo chroot $image_name.dir adduser $USER sudo
sudo umount $image_name.dir
rmdir $image_name.dir
