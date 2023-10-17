#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0

import ctypes
import os

DBC_UID_SIZE = 16
DBC_NONCE_SIZE = 16
DBC_SIG_SIZE = 32

PARAM_GET_FMAX_CAP = (0x3,)
PARAM_SET_FMAX_CAP = (0x4,)
PARAM_GET_PWR_CAP = (0x5,)
PARAM_SET_PWR_CAP = (0x6,)
PARAM_GET_GFX_MODE = (0x7,)
PARAM_SET_GFX_MODE = (0x8,)
PARAM_GET_CURR_TEMP = (0x9,)
PARAM_GET_FMAX_MAX = (0xA,)
PARAM_GET_FMAX_MIN = (0xB,)
PARAM_GET_SOC_PWR_MAX = (0xC,)
PARAM_GET_SOC_PWR_MIN = (0xD,)
PARAM_GET_SOC_PWR_CUR = (0xE,)

DEVICE_NODE = "/dev/dbc"

lib = ctypes.CDLL("./dbc_library.so", mode=ctypes.RTLD_GLOBAL)


def handle_error(code):
    val = code * -1
    raise OSError(val, os.strerror(val))


def get_nonce(device, signature):
    if not device:
        raise ValueError("Device required")
    buf = ctypes.create_string_buffer(DBC_NONCE_SIZE)
    ret = lib.get_nonce(device.fileno(), ctypes.byref(buf), signature)
    if ret:
        handle_error(ret)
    return buf.value


def set_uid(device, new_uid, signature):
    if not signature:
        raise ValueError("Signature required")
    if not new_uid:
        raise ValueError("UID required")
    ret = lib.set_uid(device.fileno(), new_uid, signature)
    if ret:
        handle_error(ret)
    return True


def process_param(device, message, signature, data=None):
    if not signature:
        raise ValueError("Signature required")
    if type(message) != tuple:
        raise ValueError("Expected message tuple")
    arg = ctypes.c_int(data if data else 0)
    ret = lib.process_param(device.fileno(), message[0], signature, ctypes.pointer(arg))
    if ret:
        handle_error(ret)
    return arg, signature
