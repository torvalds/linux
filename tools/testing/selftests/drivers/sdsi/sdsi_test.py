#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0

from struct import pack
from time import sleep

import errno
import glob
import os
import subprocess

try:
    import pytest
except ImportError:
    print("Unable to import pytest python module.")
    print("\nIf not already installed, you may do so with:")
    print("\t\tpip3 install pytest")
    exit(1)

SOCKETS = glob.glob('/sys/bus/auxiliary/devices/intel_vsec.sdsi.*')
NUM_SOCKETS = len(SOCKETS)

MODULE_NAME = 'intel_sdsi'
DEV_PREFIX = 'intel_vsec.sdsi'
CLASS_DIR = '/sys/bus/auxiliary/devices'
GUID = "0x6dd191"

def read_bin_file(file):
    with open(file, mode='rb') as f:
        content = f.read()
    return content

def get_dev_file_path(socket, file):
    return CLASS_DIR + '/' + DEV_PREFIX + '.' + str(socket) + '/' + file

def kmemleak_enabled():
    kmemleak = "/sys/kernel/debug/kmemleak"
    return os.path.isfile(kmemleak)

class TestSDSiDriver:
    def test_driver_loaded(self):
        lsmod_p = subprocess.Popen(('lsmod'), stdout=subprocess.PIPE)
        result = subprocess.check_output(('grep', '-q', MODULE_NAME), stdin=lsmod_p.stdout)

@pytest.mark.parametrize('socket', range(0, NUM_SOCKETS))
class TestSDSiFilesClass:

    def read_value(self, file):
        f = open(file, "r")
        value = f.read().strip("\n")
        return value

    def get_dev_folder(self, socket):
        return CLASS_DIR + '/' + DEV_PREFIX + '.' + str(socket) + '/'

    def test_sysfs_files_exist(self, socket):
        folder = self.get_dev_folder(socket)
        print (folder)
        assert os.path.isfile(folder + "guid") == True
        assert os.path.isfile(folder + "provision_akc") == True
        assert os.path.isfile(folder + "provision_cap") == True
        assert os.path.isfile(folder + "state_certificate") == True
        assert os.path.isfile(folder + "registers") == True

    def test_sysfs_file_permissions(self, socket):
        folder = self.get_dev_folder(socket)
        mode = os.stat(folder + "guid").st_mode & 0o777
        assert mode == 0o444    # Read all
        mode = os.stat(folder + "registers").st_mode & 0o777
        assert mode == 0o400    # Read owner
        mode = os.stat(folder + "provision_akc").st_mode & 0o777
        assert mode == 0o200    # Read owner
        mode = os.stat(folder + "provision_cap").st_mode & 0o777
        assert mode == 0o200    # Read owner
        mode = os.stat(folder + "state_certificate").st_mode & 0o777
        assert mode == 0o400    # Read owner

    def test_sysfs_file_ownership(self, socket):
        folder = self.get_dev_folder(socket)

        st = os.stat(folder + "guid")
        assert st.st_uid == 0
        assert st.st_gid == 0

        st = os.stat(folder + "registers")
        assert st.st_uid == 0
        assert st.st_gid == 0

        st = os.stat(folder + "provision_akc")
        assert st.st_uid == 0
        assert st.st_gid == 0

        st = os.stat(folder + "provision_cap")
        assert st.st_uid == 0
        assert st.st_gid == 0

        st = os.stat(folder + "state_certificate")
        assert st.st_uid == 0
        assert st.st_gid == 0

    def test_sysfs_file_sizes(self, socket):
        folder = self.get_dev_folder(socket)

        if self.read_value(folder + "guid") == GUID:
            st = os.stat(folder + "registers")
            assert st.st_size == 72

        st = os.stat(folder + "provision_akc")
        assert st.st_size == 1024

        st = os.stat(folder + "provision_cap")
        assert st.st_size == 1024

        st = os.stat(folder + "state_certificate")
        assert st.st_size == 4096

    def test_no_seek_allowed(self, socket):
        folder = self.get_dev_folder(socket)
        rand_file = bytes(os.urandom(8))

        f = open(folder + "provision_cap", "wb", 0)
        f.seek(1)
        with pytest.raises(OSError) as error:
            f.write(rand_file)
        assert error.value.errno == errno.ESPIPE
        f.close()

        f = open(folder + "provision_akc", "wb", 0)
        f.seek(1)
        with pytest.raises(OSError) as error:
            f.write(rand_file)
        assert error.value.errno == errno.ESPIPE
        f.close()

    def test_registers_seek(self, socket):
        folder = self.get_dev_folder(socket)

        # Check that the value read from an offset of the entire
        # file is none-zero and the same as the value read
        # from seeking to the same location
        f = open(folder + "registers", "rb")
        data = f.read()
        f.seek(64)
        id = f.read()
        assert id != bytes(0)
        assert data[64:] == id
        f.close()

@pytest.mark.parametrize('socket', range(0, NUM_SOCKETS))
class TestSDSiMailboxCmdsClass:
    def test_provision_akc_eoverflow_1017_bytes(self, socket):

        # The buffer for writes is 1k, of with 8 bytes must be
        # reserved for the command, leaving 1016 bytes max.
        # Check that we get an overflow error for 1017 bytes.
        node = get_dev_file_path(socket, "provision_akc")
        rand_file = bytes(os.urandom(1017))

        f = open(node, 'wb', 0)
        with pytest.raises(OSError) as error:
            f.write(rand_file)
        assert error.value.errno == errno.EOVERFLOW
        f.close()

@pytest.mark.parametrize('socket', range(0, NUM_SOCKETS))
class TestSdsiDriverLocksClass:
    def test_enodev_when_pci_device_removed(self, socket):
        node = get_dev_file_path(socket, "provision_akc")
        dev_name = DEV_PREFIX + '.' + str(socket)
        driver_dir = CLASS_DIR + '/' + dev_name + "/driver/"
        rand_file = bytes(os.urandom(8))

        f = open(node, 'wb', 0)
        g = open(node, 'wb', 0)

        with open(driver_dir + 'unbind', 'w') as k:
            print(dev_name, file = k)

        with pytest.raises(OSError) as error:
            f.write(rand_file)
        assert error.value.errno == errno.ENODEV

        with pytest.raises(OSError) as error:
            g.write(rand_file)
        assert error.value.errno == errno.ENODEV

        f.close()
        g.close()

        # Short wait needed to allow file to close before pulling driver
        sleep(1)

        p = subprocess.Popen(('modprobe', '-r', 'intel_sdsi'))
        p.wait()
        p = subprocess.Popen(('modprobe', '-r', 'intel_vsec'))
        p.wait()
        p = subprocess.Popen(('modprobe', 'intel_vsec'))
        p.wait()

        # Short wait needed to allow driver time to get inserted
        # before continuing tests
        sleep(1)

    def test_memory_leak(self, socket):
        if not kmemleak_enabled():
            pytest.skip("kmemleak not enabled in kernel")

        dev_name = DEV_PREFIX + '.' + str(socket)
        driver_dir = CLASS_DIR + '/' + dev_name + "/driver/"

        with open(driver_dir + 'unbind', 'w') as k:
            print(dev_name, file = k)

        sleep(1)

        subprocess.check_output(('modprobe', '-r', 'intel_sdsi'))
        subprocess.check_output(('modprobe', '-r', 'intel_vsec'))

        with open('/sys/kernel/debug/kmemleak', 'w') as f:
            print('scan', file = f)
        sleep(5)

        assert os.stat('/sys/kernel/debug/kmemleak').st_size == 0

        subprocess.check_output(('modprobe', 'intel_vsec'))
        sleep(1)
