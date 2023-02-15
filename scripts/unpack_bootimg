#!/usr/bin/env python3
#
# Copyright 2018, The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Unpacks the boot image.

Extracts the kernel, ramdisk, second bootloader, dtb and recovery dtbo images.
"""

from argparse import ArgumentParser, RawDescriptionHelpFormatter
from struct import unpack
import os
import shlex

BOOT_IMAGE_HEADER_V3_PAGESIZE = 4096
VENDOR_RAMDISK_NAME_SIZE = 32
VENDOR_RAMDISK_TABLE_ENTRY_BOARD_ID_SIZE = 16


def create_out_dir(dir_path):
    """creates a directory 'dir_path' if it does not exist"""
    if not os.path.exists(dir_path):
        os.makedirs(dir_path)


def extract_image(offset, size, bootimage, extracted_image_name):
    """extracts an image from the bootimage"""
    bootimage.seek(offset)
    with open(extracted_image_name, 'wb') as file_out:
        file_out.write(bootimage.read(size))


def get_number_of_pages(image_size, page_size):
    """calculates the number of pages required for the image"""
    return (image_size + page_size - 1) // page_size


def cstr(s):
    """Remove first NULL character and any character beyond."""
    return s.split('\0', 1)[0]


def format_os_version(os_version):
    if os_version == 0:
        return None
    a = os_version >> 14
    b = os_version >> 7 & ((1<<7) - 1)
    c = os_version & ((1<<7) - 1)
    return f'{a}.{b}.{c}'


def format_os_patch_level(os_patch_level):
    if os_patch_level == 0:
        return None
    y = os_patch_level >> 4
    y += 2000
    m = os_patch_level & ((1<<4) - 1)
    return f'{y:04d}-{m:02d}'


def decode_os_version_patch_level(os_version_patch_level):
    """Returns a tuple of (os_version, os_patch_level)."""
    os_version = os_version_patch_level >> 11
    os_patch_level = os_version_patch_level & ((1<<11) - 1)
    return (format_os_version(os_version),
            format_os_patch_level(os_patch_level))


class BootImageInfoFormatter:
    """Formats the boot image info."""

    def format_pretty_text(self):
        lines = []
        lines.append(f'boot magic: {self.boot_magic}')

        if self.header_version < 3:
            lines.append(f'kernel_size: {self.kernel_size}')
            lines.append(
                f'kernel load address: {self.kernel_load_address:#010x}')
            lines.append(f'ramdisk size: {self.ramdisk_size}')
            lines.append(
                f'ramdisk load address: {self.ramdisk_load_address:#010x}')
            lines.append(f'second bootloader size: {self.second_size}')
            lines.append(
                f'second bootloader load address: '
                f'{self.second_load_address:#010x}')
            lines.append(
                f'kernel tags load address: {self.tags_load_address:#010x}')
            lines.append(f'page size: {self.page_size}')
        else:
            lines.append(f'kernel_size: {self.kernel_size}')
            lines.append(f'ramdisk size: {self.ramdisk_size}')

        lines.append(f'os version: {self.os_version}')
        lines.append(f'os patch level: {self.os_patch_level}')
        lines.append(f'boot image header version: {self.header_version}')

        if self.header_version < 3:
            lines.append(f'product name: {self.product_name}')

        lines.append(f'command line args: {self.cmdline}')

        if self.header_version < 3:
            lines.append(f'additional command line args: {self.extra_cmdline}')

        if self.header_version in {1, 2}:
            lines.append(f'recovery dtbo size: {self.recovery_dtbo_size}')
            lines.append(
                f'recovery dtbo offset: {self.recovery_dtbo_offset:#018x}')
            lines.append(f'boot header size: {self.boot_header_size}')

        if self.header_version == 2:
            lines.append(f'dtb size: {self.dtb_size}')
            lines.append(f'dtb address: {self.dtb_load_address:#018x}')

        if self.header_version >= 4:
            lines.append(
                f'boot.img signature size: {self.boot_signature_size}')

        return '\n'.join(lines)

    def format_mkbootimg_argument(self):
        args = []
        args.extend(['--header_version', str(self.header_version)])
        if self.os_version:
            args.extend(['--os_version', self.os_version])
        if self.os_patch_level:
            args.extend(['--os_patch_level', self.os_patch_level])

        args.extend(['--kernel', os.path.join(self.image_dir, 'kernel')])
        args.extend(['--ramdisk', os.path.join(self.image_dir, 'ramdisk')])

        if self.header_version <= 2:
            if self.second_size > 0:
                args.extend(['--second',
                             os.path.join(self.image_dir, 'second')])
            if self.recovery_dtbo_size > 0:
                args.extend(['--recovery_dtbo',
                             os.path.join(self.image_dir, 'recovery_dtbo')])
            if self.dtb_size > 0:
                args.extend(['--dtb', os.path.join(self.image_dir, 'dtb')])

            args.extend(['--pagesize', f'{self.page_size:#010x}'])

            # Kernel load address is base + kernel_offset in mkbootimg.py.
            # However we don't know the value of 'base' when unpacking a boot
            # image in this script, so we set 'base' to zero and 'kernel_offset'
            # to the kernel load address, 'ramdisk_offset' to the ramdisk load
            # address, ... etc.
            args.extend(['--base', f'{0:#010x}'])
            args.extend(['--kernel_offset',
                         f'{self.kernel_load_address:#010x}'])
            args.extend(['--ramdisk_offset',
                         f'{self.ramdisk_load_address:#010x}'])
            args.extend(['--second_offset',
                         f'{self.second_load_address:#010x}'])
            args.extend(['--tags_offset', f'{self.tags_load_address:#010x}'])

            # dtb is added in boot image v2, and is absent in v1 or v0.
            if self.header_version == 2:
                # dtb_offset is uint64_t.
                args.extend(['--dtb_offset', f'{self.dtb_load_address:#018x}'])

            args.extend(['--board', self.product_name])
            args.extend(['--cmdline', self.cmdline + self.extra_cmdline])
        else:
            args.extend(['--cmdline', self.cmdline])

        return args


def unpack_boot_image(boot_img, output_dir):
    """extracts kernel, ramdisk, second bootloader and recovery dtbo"""
    info = BootImageInfoFormatter()
    info.boot_magic = unpack('8s', boot_img.read(8))[0].decode()

    kernel_ramdisk_second_info = unpack('9I', boot_img.read(9 * 4))
    # header_version is always at [8] regardless of the value of header_version.
    info.header_version = kernel_ramdisk_second_info[8]

    if info.header_version < 3:
        info.kernel_size = kernel_ramdisk_second_info[0]
        info.kernel_load_address = kernel_ramdisk_second_info[1]
        info.ramdisk_size = kernel_ramdisk_second_info[2]
        info.ramdisk_load_address = kernel_ramdisk_second_info[3]
        info.second_size = kernel_ramdisk_second_info[4]
        info.second_load_address = kernel_ramdisk_second_info[5]
        info.tags_load_address = kernel_ramdisk_second_info[6]
        info.page_size = kernel_ramdisk_second_info[7]
        os_version_patch_level = unpack('I', boot_img.read(1 * 4))[0]
    else:
        info.kernel_size = kernel_ramdisk_second_info[0]
        info.ramdisk_size = kernel_ramdisk_second_info[1]
        os_version_patch_level = kernel_ramdisk_second_info[2]
        info.second_size = 0
        info.page_size = BOOT_IMAGE_HEADER_V3_PAGESIZE

    info.os_version, info.os_patch_level = decode_os_version_patch_level(
        os_version_patch_level)

    if info.header_version < 3:
        info.product_name = cstr(unpack('16s',
                                        boot_img.read(16))[0].decode())
        info.cmdline = cstr(unpack('512s', boot_img.read(512))[0].decode())
        boot_img.read(32)  # ignore SHA
        info.extra_cmdline = cstr(unpack('1024s',
                                         boot_img.read(1024))[0].decode())
    else:
        info.cmdline = cstr(unpack('1536s',
                                   boot_img.read(1536))[0].decode())

    if info.header_version in {1, 2}:
        info.recovery_dtbo_size = unpack('I', boot_img.read(1 * 4))[0]
        info.recovery_dtbo_offset = unpack('Q', boot_img.read(8))[0]
        info.boot_header_size = unpack('I', boot_img.read(4))[0]
    else:
        info.recovery_dtbo_size = 0

    if info.header_version == 2:
        info.dtb_size = unpack('I', boot_img.read(4))[0]
        info.dtb_load_address = unpack('Q', boot_img.read(8))[0]
    else:
        info.dtb_size = 0
        info.dtb_load_address = 0

    if info.header_version >= 4:
        info.boot_signature_size = unpack('I', boot_img.read(4))[0]
    else:
        info.boot_signature_size = 0

    # The first page contains the boot header
    num_header_pages = 1

    # Convenient shorthand.
    page_size = info.page_size

    num_kernel_pages = get_number_of_pages(info.kernel_size, page_size)
    kernel_offset = page_size * num_header_pages  # header occupies a page
    image_info_list = [(kernel_offset, info.kernel_size, 'kernel')]

    num_ramdisk_pages = get_number_of_pages(info.ramdisk_size, page_size)
    ramdisk_offset = page_size * (num_header_pages + num_kernel_pages
                                 ) # header + kernel
    image_info_list.append((ramdisk_offset, info.ramdisk_size, 'ramdisk'))

    if info.second_size > 0:
        second_offset = page_size * (
            num_header_pages + num_kernel_pages + num_ramdisk_pages
            )  # header + kernel + ramdisk
        image_info_list.append((second_offset, info.second_size, 'second'))

    if info.recovery_dtbo_size > 0:
        image_info_list.append((info.recovery_dtbo_offset,
                                info.recovery_dtbo_size,
                                'recovery_dtbo'))
    if info.dtb_size > 0:
        num_second_pages = get_number_of_pages(info.second_size, page_size)
        num_recovery_dtbo_pages = get_number_of_pages(
            info.recovery_dtbo_size, page_size)
        dtb_offset = page_size * (
            num_header_pages + num_kernel_pages + num_ramdisk_pages +
            num_second_pages + num_recovery_dtbo_pages)

        image_info_list.append((dtb_offset, info.dtb_size, 'dtb'))

    if info.boot_signature_size > 0:
        # boot signature only exists in boot.img version >= v4.
        # There are only kernel and ramdisk pages before the signature.
        boot_signature_offset = page_size * (
            num_header_pages + num_kernel_pages + num_ramdisk_pages)

        image_info_list.append((boot_signature_offset, info.boot_signature_size,
                                'boot_signature'))

    create_out_dir(output_dir)
    for offset, size, name in image_info_list:
        extract_image(offset, size, boot_img, os.path.join(output_dir, name))
    info.image_dir = output_dir

    return info


class VendorBootImageInfoFormatter:
    """Formats the vendor_boot image info."""

    def format_pretty_text(self):
        lines = []
        lines.append(f'boot magic: {self.boot_magic}')
        lines.append(f'vendor boot image header version: {self.header_version}')
        lines.append(f'page size: {self.page_size:#010x}')
        lines.append(f'kernel load address: {self.kernel_load_address:#010x}')
        lines.append(f'ramdisk load address: {self.ramdisk_load_address:#010x}')
        if self.header_version > 3:
            lines.append(
                f'vendor ramdisk total size: {self.vendor_ramdisk_size}')
        else:
            lines.append(f'vendor ramdisk size: {self.vendor_ramdisk_size}')
        lines.append(f'vendor command line args: {self.cmdline}')
        lines.append(
            f'kernel tags load address: {self.tags_load_address:#010x}')
        lines.append(f'product name: {self.product_name}')
        lines.append(f'vendor boot image header size: {self.header_size}')
        lines.append(f'dtb size: {self.dtb_size}')
        lines.append(f'dtb address: {self.dtb_load_address:#018x}')
        if self.header_version > 3:
            lines.append(
                f'vendor ramdisk table size: {self.vendor_ramdisk_table_size}')
            lines.append('vendor ramdisk table: [')
            indent = lambda level: ' ' * 4 * level
            for entry in self.vendor_ramdisk_table:
                (output_ramdisk_name, ramdisk_size, ramdisk_offset,
                 ramdisk_type, ramdisk_name, board_id) = entry
                lines.append(indent(1) + f'{output_ramdisk_name}: ''{')
                lines.append(indent(2) + f'size: {ramdisk_size}')
                lines.append(indent(2) + f'offset: {ramdisk_offset}')
                lines.append(indent(2) + f'type: {ramdisk_type:#x}')
                lines.append(indent(2) + f'name: {ramdisk_name}')
                lines.append(indent(2) + 'board_id: [')
                stride = 4
                for row_idx in range(0, len(board_id), stride):
                    row = board_id[row_idx:row_idx + stride]
                    lines.append(
                        indent(3) + ' '.join(f'{e:#010x},' for e in row))
                lines.append(indent(2) + ']')
                lines.append(indent(1) + '}')
            lines.append(']')
            lines.append(
                f'vendor bootconfig size: {self.vendor_bootconfig_size}')

        return '\n'.join(lines)

    def format_mkbootimg_argument(self):
        args = []
        args.extend(['--header_version', str(self.header_version)])
        args.extend(['--pagesize', f'{self.page_size:#010x}'])
        args.extend(['--base', f'{0:#010x}'])
        args.extend(['--kernel_offset', f'{self.kernel_load_address:#010x}'])
        args.extend(['--ramdisk_offset', f'{self.ramdisk_load_address:#010x}'])
        args.extend(['--tags_offset', f'{self.tags_load_address:#010x}'])
        args.extend(['--dtb_offset', f'{self.dtb_load_address:#018x}'])
        args.extend(['--vendor_cmdline', self.cmdline])
        args.extend(['--board', self.product_name])

        if self.dtb_size > 0:
            args.extend(['--dtb', os.path.join(self.image_dir, 'dtb')])

        if self.header_version > 3:
            args.extend(['--vendor_bootconfig',
                         os.path.join(self.image_dir, 'bootconfig')])

            for entry in self.vendor_ramdisk_table:
                (output_ramdisk_name, _, _, ramdisk_type,
                 ramdisk_name, board_id) = entry
                args.extend(['--ramdisk_type', str(ramdisk_type)])
                args.extend(['--ramdisk_name', ramdisk_name])
                for idx, e in enumerate(board_id):
                    if e:
                        args.extend([f'--board_id{idx}', f'{e:#010x}'])
                vendor_ramdisk_path = os.path.join(
                    self.image_dir, output_ramdisk_name)
                args.extend(['--vendor_ramdisk_fragment', vendor_ramdisk_path])
        else:
            args.extend(['--vendor_ramdisk',
                         os.path.join(self.image_dir, 'vendor_ramdisk')])

        return args


def unpack_vendor_boot_image(boot_img, output_dir):
    info = VendorBootImageInfoFormatter()
    info.boot_magic = unpack('8s', boot_img.read(8))[0].decode()
    info.header_version = unpack('I', boot_img.read(4))[0]
    info.page_size = unpack('I', boot_img.read(4))[0]
    info.kernel_load_address = unpack('I', boot_img.read(4))[0]
    info.ramdisk_load_address = unpack('I', boot_img.read(4))[0]
    info.vendor_ramdisk_size = unpack('I', boot_img.read(4))[0]
    info.cmdline = cstr(unpack('2048s', boot_img.read(2048))[0].decode())
    info.tags_load_address = unpack('I', boot_img.read(4))[0]
    info.product_name = cstr(unpack('16s', boot_img.read(16))[0].decode())
    info.header_size = unpack('I', boot_img.read(4))[0]
    info.dtb_size = unpack('I', boot_img.read(4))[0]
    info.dtb_load_address = unpack('Q', boot_img.read(8))[0]

    # Convenient shorthand.
    page_size = info.page_size
    # The first pages contain the boot header
    num_boot_header_pages = get_number_of_pages(info.header_size, page_size)
    num_boot_ramdisk_pages = get_number_of_pages(
        info.vendor_ramdisk_size, page_size)
    num_boot_dtb_pages = get_number_of_pages(info.dtb_size, page_size)

    ramdisk_offset_base = page_size * num_boot_header_pages
    image_info_list = []

    if info.header_version > 3:
        info.vendor_ramdisk_table_size = unpack('I', boot_img.read(4))[0]
        vendor_ramdisk_table_entry_num = unpack('I', boot_img.read(4))[0]
        vendor_ramdisk_table_entry_size = unpack('I', boot_img.read(4))[0]
        info.vendor_bootconfig_size = unpack('I', boot_img.read(4))[0]
        num_vendor_ramdisk_table_pages = get_number_of_pages(
            info.vendor_ramdisk_table_size, page_size)
        vendor_ramdisk_table_offset = page_size * (
            num_boot_header_pages + num_boot_ramdisk_pages + num_boot_dtb_pages)

        vendor_ramdisk_table = []
        vendor_ramdisk_symlinks = []
        for idx in range(vendor_ramdisk_table_entry_num):
            entry_offset = vendor_ramdisk_table_offset + (
                vendor_ramdisk_table_entry_size * idx)
            boot_img.seek(entry_offset)
            ramdisk_size = unpack('I', boot_img.read(4))[0]
            ramdisk_offset = unpack('I', boot_img.read(4))[0]
            ramdisk_type = unpack('I', boot_img.read(4))[0]
            ramdisk_name = cstr(unpack(
                f'{VENDOR_RAMDISK_NAME_SIZE}s',
                boot_img.read(VENDOR_RAMDISK_NAME_SIZE))[0].decode())
            board_id = unpack(
                f'{VENDOR_RAMDISK_TABLE_ENTRY_BOARD_ID_SIZE}I',
                boot_img.read(
                    4 * VENDOR_RAMDISK_TABLE_ENTRY_BOARD_ID_SIZE))
            output_ramdisk_name = f'vendor_ramdisk{idx:02}'

            image_info_list.append((ramdisk_offset_base + ramdisk_offset,
                                    ramdisk_size, output_ramdisk_name))
            vendor_ramdisk_symlinks.append((output_ramdisk_name, ramdisk_name))
            vendor_ramdisk_table.append(
                (output_ramdisk_name, ramdisk_size, ramdisk_offset,
                 ramdisk_type, ramdisk_name, board_id))

        info.vendor_ramdisk_table = vendor_ramdisk_table

        bootconfig_offset = page_size * (num_boot_header_pages
            + num_boot_ramdisk_pages + num_boot_dtb_pages
            + num_vendor_ramdisk_table_pages)
        image_info_list.append((bootconfig_offset, info.vendor_bootconfig_size,
            'bootconfig'))
    else:
        image_info_list.append(
            (ramdisk_offset_base, info.vendor_ramdisk_size, 'vendor_ramdisk'))

    dtb_offset = page_size * (num_boot_header_pages + num_boot_ramdisk_pages
                             ) # header + vendor_ramdisk
    if info.dtb_size > 0:
        image_info_list.append((dtb_offset, info.dtb_size, 'dtb'))

    create_out_dir(output_dir)
    for offset, size, name in image_info_list:
        extract_image(offset, size, boot_img, os.path.join(output_dir, name))
    info.image_dir = output_dir

    if info.header_version > 3:
        vendor_ramdisk_by_name_dir = os.path.join(
            output_dir, 'vendor-ramdisk-by-name')
        create_out_dir(vendor_ramdisk_by_name_dir)
        for src, dst in vendor_ramdisk_symlinks:
            src_pathname = os.path.join('..', src)
            dst_pathname = os.path.join(
                vendor_ramdisk_by_name_dir, f'ramdisk_{dst}')
            if os.path.lexists(dst_pathname):
                os.remove(dst_pathname)
            os.symlink(src_pathname, dst_pathname)

    return info


def unpack_bootimg(boot_img, output_dir):
    """Unpacks the |boot_img| to |output_dir|, and returns the 'info' object."""
    with open(boot_img, 'rb') as image_file:
        boot_magic = unpack('8s', image_file.read(8))[0].decode()
        image_file.seek(0)
        if boot_magic == 'ANDROID!':
            info = unpack_boot_image(image_file, output_dir)
        elif boot_magic == 'VNDRBOOT':
            info = unpack_vendor_boot_image(image_file, output_dir)
        else:
            raise ValueError(f'Not an Android boot image, magic: {boot_magic}')

    return info


def print_bootimg_info(info, output_format, null_separator):
    """Format and print boot image info."""
    if output_format == 'mkbootimg':
        mkbootimg_args = info.format_mkbootimg_argument()
        if null_separator:
            print('\0'.join(mkbootimg_args) + '\0', end='')
        else:
            print(shlex.join(mkbootimg_args))
    else:
        print(info.format_pretty_text())


def get_unpack_usage():
    return """Output format:

  * info

    Pretty-printed info-rich text format suitable for human inspection.

  * mkbootimg

    Output shell-escaped (quoted) argument strings that can be used to
    reconstruct the boot image. For example:

    $ unpack_bootimg --boot_img vendor_boot.img --out out --format=mkbootimg |
        tee mkbootimg_args
    $ sh -c "mkbootimg $(cat mkbootimg_args) --vendor_boot repacked.img"

    vendor_boot.img and repacked.img would be equivalent.

    If the -0 option is specified, output unescaped null-terminated argument
    strings that are suitable to be parsed by a shell script (xargs -0 format):

    $ unpack_bootimg --boot_img vendor_boot.img --out out --format=mkbootimg \\
        -0 | tee mkbootimg_args
    $ declare -a MKBOOTIMG_ARGS=()
    $ while IFS= read -r -d '' ARG; do
        MKBOOTIMG_ARGS+=("${ARG}")
      done <mkbootimg_args
    $ mkbootimg "${MKBOOTIMG_ARGS[@]}" --vendor_boot repacked.img
"""


def parse_cmdline():
    """parse command line arguments"""
    parser = ArgumentParser(
        formatter_class=RawDescriptionHelpFormatter,
        description='Unpacks boot, recovery or vendor_boot image.',
        epilog=get_unpack_usage(),
    )
    parser.add_argument('--boot_img', required=True,
                        help='path to the boot, recovery or vendor_boot image')
    parser.add_argument('--out', default='out',
                        help='output directory of the unpacked images')
    parser.add_argument('--format', choices=['info', 'mkbootimg'],
                        default='info',
                        help='text output format (default: info)')
    parser.add_argument('-0', '--null', action='store_true',
                        help='output null-terminated argument strings')
    return parser.parse_args()


def main():
    """parse arguments and unpack boot image"""
    args = parse_cmdline()
    info = unpack_bootimg(args.boot_img, args.out)
    print_bootimg_info(info, args.format, args.null)


if __name__ == '__main__':
    main()
