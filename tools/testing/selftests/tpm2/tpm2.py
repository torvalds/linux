# SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)

import hashlib
import os
import socket
import struct
import sys
import unittest
import fcntl
import select

TPM2_ST_NO_SESSIONS = 0x8001
TPM2_ST_SESSIONS = 0x8002

TPM2_CC_FIRST = 0x01FF

TPM2_CC_CREATE_PRIMARY = 0x0131
TPM2_CC_DICTIONARY_ATTACK_LOCK_RESET = 0x0139
TPM2_CC_CREATE = 0x0153
TPM2_CC_LOAD = 0x0157
TPM2_CC_UNSEAL = 0x015E
TPM2_CC_FLUSH_CONTEXT = 0x0165
TPM2_CC_START_AUTH_SESSION = 0x0176
TPM2_CC_GET_CAPABILITY	= 0x017A
TPM2_CC_GET_RANDOM = 0x017B
TPM2_CC_PCR_READ = 0x017E
TPM2_CC_POLICY_PCR = 0x017F
TPM2_CC_PCR_EXTEND = 0x0182
TPM2_CC_POLICY_PASSWORD = 0x018C
TPM2_CC_POLICY_GET_DIGEST = 0x0189

TPM2_SE_POLICY = 0x01
TPM2_SE_TRIAL = 0x03

TPM2_ALG_RSA = 0x0001
TPM2_ALG_SHA1 = 0x0004
TPM2_ALG_AES = 0x0006
TPM2_ALG_KEYEDHASH = 0x0008
TPM2_ALG_SHA256 = 0x000B
TPM2_ALG_NULL = 0x0010
TPM2_ALG_CBC = 0x0042
TPM2_ALG_CFB = 0x0043

TPM2_RH_OWNER = 0x40000001
TPM2_RH_NULL = 0x40000007
TPM2_RH_LOCKOUT = 0x4000000A
TPM2_RS_PW = 0x40000009

TPM2_RC_SIZE            = 0x01D5
TPM2_RC_AUTH_FAIL       = 0x098E
TPM2_RC_POLICY_FAIL     = 0x099D
TPM2_RC_COMMAND_CODE    = 0x0143

TSS2_RC_LAYER_SHIFT = 16
TSS2_RESMGR_TPM_RC_LAYER = (11 << TSS2_RC_LAYER_SHIFT)

TPM2_CAP_HANDLES = 0x00000001
TPM2_CAP_COMMANDS = 0x00000002
TPM2_CAP_PCRS = 0x00000005
TPM2_CAP_TPM_PROPERTIES = 0x00000006

TPM2_PT_FIXED = 0x100
TPM2_PT_TOTAL_COMMANDS = TPM2_PT_FIXED + 41

HR_SHIFT = 24
HR_LOADED_SESSION = 0x02000000
HR_TRANSIENT = 0x80000000

SHA1_DIGEST_SIZE = 20
SHA256_DIGEST_SIZE = 32

TPM2_VER0_ERRORS = {
    0x000: "TPM_RC_SUCCESS",
    0x030: "TPM_RC_BAD_TAG",
}

TPM2_VER1_ERRORS = {
    0x000: "TPM_RC_FAILURE",
    0x001: "TPM_RC_FAILURE",
    0x003: "TPM_RC_SEQUENCE",
    0x00B: "TPM_RC_PRIVATE",
    0x019: "TPM_RC_HMAC",
    0x020: "TPM_RC_DISABLED",
    0x021: "TPM_RC_EXCLUSIVE",
    0x024: "TPM_RC_AUTH_TYPE",
    0x025: "TPM_RC_AUTH_MISSING",
    0x026: "TPM_RC_POLICY",
    0x027: "TPM_RC_PCR",
    0x028: "TPM_RC_PCR_CHANGED",
    0x02D: "TPM_RC_UPGRADE",
    0x02E: "TPM_RC_TOO_MANY_CONTEXTS",
    0x02F: "TPM_RC_AUTH_UNAVAILABLE",
    0x030: "TPM_RC_REBOOT",
    0x031: "TPM_RC_UNBALANCED",
    0x042: "TPM_RC_COMMAND_SIZE",
    0x043: "TPM_RC_COMMAND_CODE",
    0x044: "TPM_RC_AUTHSIZE",
    0x045: "TPM_RC_AUTH_CONTEXT",
    0x046: "TPM_RC_NV_RANGE",
    0x047: "TPM_RC_NV_SIZE",
    0x048: "TPM_RC_NV_LOCKED",
    0x049: "TPM_RC_NV_AUTHORIZATION",
    0x04A: "TPM_RC_NV_UNINITIALIZED",
    0x04B: "TPM_RC_NV_SPACE",
    0x04C: "TPM_RC_NV_DEFINED",
    0x050: "TPM_RC_BAD_CONTEXT",
    0x051: "TPM_RC_CPHASH",
    0x052: "TPM_RC_PARENT",
    0x053: "TPM_RC_NEEDS_TEST",
    0x054: "TPM_RC_NO_RESULT",
    0x055: "TPM_RC_SENSITIVE",
    0x07F: "RC_MAX_FM0",
}

TPM2_FMT1_ERRORS = {
    0x001: "TPM_RC_ASYMMETRIC",
    0x002: "TPM_RC_ATTRIBUTES",
    0x003: "TPM_RC_HASH",
    0x004: "TPM_RC_VALUE",
    0x005: "TPM_RC_HIERARCHY",
    0x007: "TPM_RC_KEY_SIZE",
    0x008: "TPM_RC_MGF",
    0x009: "TPM_RC_MODE",
    0x00A: "TPM_RC_TYPE",
    0x00B: "TPM_RC_HANDLE",
    0x00C: "TPM_RC_KDF",
    0x00D: "TPM_RC_RANGE",
    0x00E: "TPM_RC_AUTH_FAIL",
    0x00F: "TPM_RC_NONCE",
    0x010: "TPM_RC_PP",
    0x012: "TPM_RC_SCHEME",
    0x015: "TPM_RC_SIZE",
    0x016: "TPM_RC_SYMMETRIC",
    0x017: "TPM_RC_TAG",
    0x018: "TPM_RC_SELECTOR",
    0x01A: "TPM_RC_INSUFFICIENT",
    0x01B: "TPM_RC_SIGNATURE",
    0x01C: "TPM_RC_KEY",
    0x01D: "TPM_RC_POLICY_FAIL",
    0x01F: "TPM_RC_INTEGRITY",
    0x020: "TPM_RC_TICKET",
    0x021: "TPM_RC_RESERVED_BITS",
    0x022: "TPM_RC_BAD_AUTH",
    0x023: "TPM_RC_EXPIRED",
    0x024: "TPM_RC_POLICY_CC",
    0x025: "TPM_RC_BINDING",
    0x026: "TPM_RC_CURVE",
    0x027: "TPM_RC_ECC_POINT",
}

TPM2_WARN_ERRORS = {
    0x001: "TPM_RC_CONTEXT_GAP",
    0x002: "TPM_RC_OBJECT_MEMORY",
    0x003: "TPM_RC_SESSION_MEMORY",
    0x004: "TPM_RC_MEMORY",
    0x005: "TPM_RC_SESSION_HANDLES",
    0x006: "TPM_RC_OBJECT_HANDLES",
    0x007: "TPM_RC_LOCALITY",
    0x008: "TPM_RC_YIELDED",
    0x009: "TPM_RC_CANCELED",
    0x00A: "TPM_RC_TESTING",
    0x010: "TPM_RC_REFERENCE_H0",
    0x011: "TPM_RC_REFERENCE_H1",
    0x012: "TPM_RC_REFERENCE_H2",
    0x013: "TPM_RC_REFERENCE_H3",
    0x014: "TPM_RC_REFERENCE_H4",
    0x015: "TPM_RC_REFERENCE_H5",
    0x016: "TPM_RC_REFERENCE_H6",
    0x018: "TPM_RC_REFERENCE_S0",
    0x019: "TPM_RC_REFERENCE_S1",
    0x01A: "TPM_RC_REFERENCE_S2",
    0x01B: "TPM_RC_REFERENCE_S3",
    0x01C: "TPM_RC_REFERENCE_S4",
    0x01D: "TPM_RC_REFERENCE_S5",
    0x01E: "TPM_RC_REFERENCE_S6",
    0x020: "TPM_RC_NV_RATE",
    0x021: "TPM_RC_LOCKOUT",
    0x022: "TPM_RC_RETRY",
    0x023: "TPM_RC_NV_UNAVAILABLE",
    0x7F: "TPM_RC_NOT_USED",
}

RC_VER1 = 0x100
RC_FMT1 = 0x080
RC_WARN = 0x900

ALG_DIGEST_SIZE_MAP = {
    TPM2_ALG_SHA1: SHA1_DIGEST_SIZE,
    TPM2_ALG_SHA256: SHA256_DIGEST_SIZE,
}

ALG_HASH_FUNCTION_MAP = {
    TPM2_ALG_SHA1: hashlib.sha1,
    TPM2_ALG_SHA256: hashlib.sha256
}

NAME_ALG_MAP = {
    "sha1": TPM2_ALG_SHA1,
    "sha256": TPM2_ALG_SHA256,
}


class UnknownAlgorithmIdError(Exception):
    def __init__(self, alg):
        self.alg = alg

    def __str__(self):
        return '0x%0x' % (alg)


class UnknownAlgorithmNameError(Exception):
    def __init__(self, name):
        self.name = name

    def __str__(self):
        return name


class UnknownPCRBankError(Exception):
    def __init__(self, alg):
        self.alg = alg

    def __str__(self):
        return '0x%0x' % (alg)


class ProtocolError(Exception):
    def __init__(self, cc, rc):
        self.cc = cc
        self.rc = rc

        if (rc & RC_FMT1) == RC_FMT1:
            self.name = TPM2_FMT1_ERRORS.get(rc & 0x3f, "TPM_RC_UNKNOWN")
        elif (rc & RC_WARN) == RC_WARN:
            self.name = TPM2_WARN_ERRORS.get(rc & 0x7f, "TPM_RC_UNKNOWN")
        elif (rc & RC_VER1) == RC_VER1:
            self.name = TPM2_VER1_ERRORS.get(rc & 0x7f, "TPM_RC_UNKNOWN")
        else:
            self.name = TPM2_VER0_ERRORS.get(rc & 0x7f, "TPM_RC_UNKNOWN")

    def __str__(self):
        if self.cc:
            return '%s: cc=0x%08x, rc=0x%08x' % (self.name, self.cc, self.rc)
        else:
            return '%s: rc=0x%08x' % (self.name, self.rc)


class AuthCommand(object):
    """TPMS_AUTH_COMMAND"""

    def __init__(self, session_handle=TPM2_RS_PW, nonce=bytes(),
                 session_attributes=0, hmac=bytes()):
        self.session_handle = session_handle
        self.nonce = nonce
        self.session_attributes = session_attributes
        self.hmac = hmac

    def __bytes__(self):
        fmt = '>I H%us B H%us' % (len(self.nonce), len(self.hmac))
        return struct.pack(fmt, self.session_handle, len(self.nonce),
                           self.nonce, self.session_attributes, len(self.hmac),
                           self.hmac)

    def __len__(self):
        fmt = '>I H%us B H%us' % (len(self.nonce), len(self.hmac))
        return struct.calcsize(fmt)


class SensitiveCreate(object):
    """TPMS_SENSITIVE_CREATE"""

    def __init__(self, user_auth=bytes(), data=bytes()):
        self.user_auth = user_auth
        self.data = data

    def __bytes__(self):
        fmt = '>H%us H%us' % (len(self.user_auth), len(self.data))
        return struct.pack(fmt, len(self.user_auth), self.user_auth,
                           len(self.data), self.data)

    def __len__(self):
        fmt = '>H%us H%us' % (len(self.user_auth), len(self.data))
        return struct.calcsize(fmt)


class Public(object):
    """TPMT_PUBLIC"""

    FIXED_TPM = (1 << 1)
    FIXED_PARENT = (1 << 4)
    SENSITIVE_DATA_ORIGIN = (1 << 5)
    USER_WITH_AUTH = (1 << 6)
    RESTRICTED = (1 << 16)
    DECRYPT = (1 << 17)

    def __fmt(self):
        return '>HHIH%us%usH%us' % \
            (len(self.auth_policy), len(self.parameters), len(self.unique))

    def __init__(self, object_type, name_alg, object_attributes,
                 auth_policy=bytes(), parameters=bytes(),
                 unique=bytes()):
        self.object_type = object_type
        self.name_alg = name_alg
        self.object_attributes = object_attributes
        self.auth_policy = auth_policy
        self.parameters = parameters
        self.unique = unique

    def __bytes__(self):
        return struct.pack(self.__fmt(),
                           self.object_type,
                           self.name_alg,
                           self.object_attributes,
                           len(self.auth_policy),
                           self.auth_policy,
                           self.parameters,
                           len(self.unique),
                           self.unique)

    def __len__(self):
        return struct.calcsize(self.__fmt())


def get_digest_size(alg):
    ds = ALG_DIGEST_SIZE_MAP.get(alg)
    if not ds:
        raise UnknownAlgorithmIdError(alg)
    return ds


def get_hash_function(alg):
    f = ALG_HASH_FUNCTION_MAP.get(alg)
    if not f:
        raise UnknownAlgorithmIdError(alg)
    return f


def get_algorithm(name):
    alg = NAME_ALG_MAP.get(name)
    if not alg:
        raise UnknownAlgorithmNameError(name)
    return alg


def hex_dump(d):
    d = [format(ord(x), '02x') for x in d]
    d = [d[i: i + 16] for i in range(0, len(d), 16)]
    d = [' '.join(x) for x in d]
    d = os.linesep.join(d)

    return d

class Client:
    FLAG_DEBUG = 0x01
    FLAG_SPACE = 0x02
    FLAG_NONBLOCK = 0x04
    TPM_IOC_NEW_SPACE = 0xa200

    def __init__(self, flags = 0):
        self.flags = flags

        if (self.flags & Client.FLAG_SPACE) == 0:
            self.tpm = open('/dev/tpm0', 'r+b', buffering=0)
        else:
            self.tpm = open('/dev/tpmrm0', 'r+b', buffering=0)

        if (self.flags & Client.FLAG_NONBLOCK):
            flags = fcntl.fcntl(self.tpm, fcntl.F_GETFL)
            flags |= os.O_NONBLOCK
            fcntl.fcntl(self.tpm, fcntl.F_SETFL, flags)
            self.tpm_poll = select.poll()

    def __del__(self):
        if self.tpm:
            self.tpm.close()

    def close(self):
        self.tpm.close()

    def send_cmd(self, cmd):
        self.tpm.write(cmd)

        if (self.flags & Client.FLAG_NONBLOCK):
            self.tpm_poll.register(self.tpm, select.POLLIN)
            self.tpm_poll.poll(10000)

        rsp = self.tpm.read()

        if (self.flags & Client.FLAG_NONBLOCK):
            self.tpm_poll.unregister(self.tpm)

        if (self.flags & Client.FLAG_DEBUG) != 0:
            sys.stderr.write('cmd' + os.linesep)
            sys.stderr.write(hex_dump(cmd) + os.linesep)
            sys.stderr.write('rsp' + os.linesep)
            sys.stderr.write(hex_dump(rsp) + os.linesep)

        rc = struct.unpack('>I', rsp[6:10])[0]
        if rc != 0:
            cc = struct.unpack('>I', cmd[6:10])[0]
            raise ProtocolError(cc, rc)

        return rsp

    def read_pcr(self, i, bank_alg = TPM2_ALG_SHA1):
        pcrsel_len = max((i >> 3) + 1, 3)
        pcrsel = [0] * pcrsel_len
        pcrsel[i >> 3] = 1 << (i & 7)
        pcrsel = ''.join(map(chr, pcrsel)).encode()

        fmt = '>HII IHB%us' % (pcrsel_len)
        cmd = struct.pack(fmt,
                          TPM2_ST_NO_SESSIONS,
                          struct.calcsize(fmt),
                          TPM2_CC_PCR_READ,
                          1,
                          bank_alg,
                          pcrsel_len, pcrsel)

        rsp = self.send_cmd(cmd)

        pcr_update_cnt, pcr_select_cnt = struct.unpack('>II', rsp[10:18])
        assert pcr_select_cnt == 1
        rsp = rsp[18:]

        alg2, pcrsel_len2 = struct.unpack('>HB', rsp[:3])
        assert bank_alg == alg2 and pcrsel_len == pcrsel_len2
        rsp = rsp[3 + pcrsel_len:]

        digest_cnt = struct.unpack('>I', rsp[:4])[0]
        if digest_cnt == 0:
            return None
        rsp = rsp[6:]

        return rsp

    def extend_pcr(self, i, dig, bank_alg = TPM2_ALG_SHA1):
        ds = get_digest_size(bank_alg)
        assert(ds == len(dig))

        auth_cmd = AuthCommand()

        fmt = '>HII I I%us IH%us' % (len(auth_cmd), ds)
        cmd = struct.pack(
            fmt,
            TPM2_ST_SESSIONS,
            struct.calcsize(fmt),
            TPM2_CC_PCR_EXTEND,
            i,
            len(auth_cmd),
            bytes(auth_cmd),
            1, bank_alg, dig)

        self.send_cmd(cmd)

    def start_auth_session(self, session_type, name_alg = TPM2_ALG_SHA1):
        fmt = '>HII IIH16sHBHH'
        cmd = struct.pack(fmt,
                          TPM2_ST_NO_SESSIONS,
                          struct.calcsize(fmt),
                          TPM2_CC_START_AUTH_SESSION,
                          TPM2_RH_NULL,
                          TPM2_RH_NULL,
                          16,
                          ('\0' * 16).encode(),
                          0,
                          session_type,
                          TPM2_ALG_NULL,
                          name_alg)

        return struct.unpack('>I', self.send_cmd(cmd)[10:14])[0]

    def __calc_pcr_digest(self, pcrs, bank_alg = TPM2_ALG_SHA1,
                          digest_alg = TPM2_ALG_SHA1):
        x = []
        f = get_hash_function(digest_alg)

        for i in pcrs:
            pcr = self.read_pcr(i, bank_alg)
            if pcr is None:
                return None
            x += pcr

        return f(bytearray(x)).digest()

    def policy_pcr(self, handle, pcrs, bank_alg = TPM2_ALG_SHA1,
                   name_alg = TPM2_ALG_SHA1):
        ds = get_digest_size(name_alg)
        dig = self.__calc_pcr_digest(pcrs, bank_alg, name_alg)
        if not dig:
            raise UnknownPCRBankError(bank_alg)

        pcrsel_len = max((max(pcrs) >> 3) + 1, 3)
        pcrsel = [0] * pcrsel_len
        for i in pcrs:
            pcrsel[i >> 3] |= 1 << (i & 7)
        pcrsel = ''.join(map(chr, pcrsel)).encode()

        fmt = '>HII IH%usIHB3s' % ds
        cmd = struct.pack(fmt,
                          TPM2_ST_NO_SESSIONS,
                          struct.calcsize(fmt),
                          TPM2_CC_POLICY_PCR,
                          handle,
                          len(dig),
                          bytes(dig),
                          1,
                          bank_alg,
                          pcrsel_len, pcrsel)

        self.send_cmd(cmd)

    def policy_password(self, handle):
        fmt = '>HII I'
        cmd = struct.pack(fmt,
                          TPM2_ST_NO_SESSIONS,
                          struct.calcsize(fmt),
                          TPM2_CC_POLICY_PASSWORD,
                          handle)

        self.send_cmd(cmd)

    def get_policy_digest(self, handle):
        fmt = '>HII I'
        cmd = struct.pack(fmt,
                          TPM2_ST_NO_SESSIONS,
                          struct.calcsize(fmt),
                          TPM2_CC_POLICY_GET_DIGEST,
                          handle)

        return self.send_cmd(cmd)[12:]

    def flush_context(self, handle):
        fmt = '>HIII'
        cmd = struct.pack(fmt,
                          TPM2_ST_NO_SESSIONS,
                          struct.calcsize(fmt),
                          TPM2_CC_FLUSH_CONTEXT,
                          handle)

        self.send_cmd(cmd)

    def create_root_key(self, auth_value = bytes()):
        attributes = \
            Public.FIXED_TPM | \
            Public.FIXED_PARENT | \
            Public.SENSITIVE_DATA_ORIGIN | \
            Public.USER_WITH_AUTH | \
            Public.RESTRICTED | \
            Public.DECRYPT

        auth_cmd = AuthCommand()
        sensitive = SensitiveCreate(user_auth=auth_value)

        public_parms = struct.pack(
            '>HHHHHI',
            TPM2_ALG_AES,
            128,
            TPM2_ALG_CFB,
            TPM2_ALG_NULL,
            2048,
            0)

        public = Public(
            object_type=TPM2_ALG_RSA,
            name_alg=TPM2_ALG_SHA1,
            object_attributes=attributes,
            parameters=public_parms)

        fmt = '>HIII I%us H%us H%us HI' % \
            (len(auth_cmd), len(sensitive), len(public))
        cmd = struct.pack(
            fmt,
            TPM2_ST_SESSIONS,
            struct.calcsize(fmt),
            TPM2_CC_CREATE_PRIMARY,
            TPM2_RH_OWNER,
            len(auth_cmd),
            bytes(auth_cmd),
            len(sensitive),
            bytes(sensitive),
            len(public),
            bytes(public),
            0, 0)

        return struct.unpack('>I', self.send_cmd(cmd)[10:14])[0]

    def seal(self, parent_key, data, auth_value, policy_dig,
             name_alg = TPM2_ALG_SHA1):
        ds = get_digest_size(name_alg)
        assert(not policy_dig or ds == len(policy_dig))

        attributes = 0
        if not policy_dig:
            attributes |= Public.USER_WITH_AUTH
            policy_dig = bytes()

        auth_cmd =  AuthCommand()
        sensitive = SensitiveCreate(user_auth=auth_value, data=data)

        public = Public(
            object_type=TPM2_ALG_KEYEDHASH,
            name_alg=name_alg,
            object_attributes=attributes,
            auth_policy=policy_dig,
            parameters=struct.pack('>H', TPM2_ALG_NULL))

        fmt = '>HIII I%us H%us H%us HI' % \
            (len(auth_cmd), len(sensitive), len(public))
        cmd = struct.pack(
            fmt,
            TPM2_ST_SESSIONS,
            struct.calcsize(fmt),
            TPM2_CC_CREATE,
            parent_key,
            len(auth_cmd),
            bytes(auth_cmd),
            len(sensitive),
            bytes(sensitive),
            len(public),
            bytes(public),
            0, 0)

        rsp = self.send_cmd(cmd)

        return rsp[14:]

    def unseal(self, parent_key, blob, auth_value, policy_handle):
        private_len = struct.unpack('>H', blob[0:2])[0]
        public_start = private_len + 2
        public_len = struct.unpack('>H', blob[public_start:public_start + 2])[0]
        blob = blob[:private_len + public_len + 4]

        auth_cmd = AuthCommand()

        fmt = '>HII I I%us %us' % (len(auth_cmd), len(blob))
        cmd = struct.pack(
            fmt,
            TPM2_ST_SESSIONS,
            struct.calcsize(fmt),
            TPM2_CC_LOAD,
            parent_key,
            len(auth_cmd),
            bytes(auth_cmd),
            blob)

        data_handle = struct.unpack('>I', self.send_cmd(cmd)[10:14])[0]

        if policy_handle:
            auth_cmd = AuthCommand(session_handle=policy_handle, hmac=auth_value)
        else:
            auth_cmd = AuthCommand(hmac=auth_value)

        fmt = '>HII I I%us' % (len(auth_cmd))
        cmd = struct.pack(
            fmt,
            TPM2_ST_SESSIONS,
            struct.calcsize(fmt),
            TPM2_CC_UNSEAL,
            data_handle,
            len(auth_cmd),
            bytes(auth_cmd))

        try:
            rsp = self.send_cmd(cmd)
        finally:
            self.flush_context(data_handle)

        data_len = struct.unpack('>I', rsp[10:14])[0] - 2

        return rsp[16:16 + data_len]

    def reset_da_lock(self):
        auth_cmd = AuthCommand()

        fmt = '>HII I I%us' % (len(auth_cmd))
        cmd = struct.pack(
            fmt,
            TPM2_ST_SESSIONS,
            struct.calcsize(fmt),
            TPM2_CC_DICTIONARY_ATTACK_LOCK_RESET,
            TPM2_RH_LOCKOUT,
            len(auth_cmd),
            bytes(auth_cmd))

        self.send_cmd(cmd)

    def __get_cap_cnt(self, cap, pt, cnt):
        handles = []
        fmt = '>HII III'

        cmd = struct.pack(fmt,
                          TPM2_ST_NO_SESSIONS,
                          struct.calcsize(fmt),
                          TPM2_CC_GET_CAPABILITY,
                          cap, pt, cnt)

        rsp = self.send_cmd(cmd)[10:]
        more_data, cap, cnt = struct.unpack('>BII', rsp[:9])
        rsp = rsp[9:]

        for i in range(0, cnt):
            handle = struct.unpack('>I', rsp[:4])[0]
            handles.append(handle)
            rsp = rsp[4:]

        return handles, more_data

    def get_cap(self, cap, pt):
        handles = []

        more_data = True
        while more_data:
            next_handles, more_data = self.__get_cap_cnt(cap, pt, 1)
            handles += next_handles
            pt += 1

        return handles

    def get_cap_pcrs(self):
        pcr_banks = {}

        fmt = '>HII III'

        cmd = struct.pack(fmt,
                          TPM2_ST_NO_SESSIONS,
                          struct.calcsize(fmt),
                          TPM2_CC_GET_CAPABILITY,
                          TPM2_CAP_PCRS, 0, 1)
        rsp = self.send_cmd(cmd)[10:]
        _, _, cnt = struct.unpack('>BII', rsp[:9])
        rsp = rsp[9:]

        # items are TPMS_PCR_SELECTION's
        for i in range(0, cnt):
              hash, sizeOfSelect = struct.unpack('>HB', rsp[:3])
              rsp = rsp[3:]

              pcrSelect = 0
              if sizeOfSelect > 0:
                  pcrSelect, = struct.unpack('%ds' % sizeOfSelect,
                                             rsp[:sizeOfSelect])
                  rsp = rsp[sizeOfSelect:]
                  pcrSelect = int.from_bytes(pcrSelect, byteorder='big')

              pcr_banks[hash] = pcrSelect

        return pcr_banks
