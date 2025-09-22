//===-- StringExtractorGDBRemote.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/StringExtractorGDBRemote.h"

#include <cctype>
#include <cstring>
#include <optional>

constexpr lldb::pid_t StringExtractorGDBRemote::AllProcesses;
constexpr lldb::tid_t StringExtractorGDBRemote::AllThreads;

StringExtractorGDBRemote::ResponseType
StringExtractorGDBRemote::GetResponseType() const {
  if (m_packet.empty())
    return eUnsupported;

  switch (m_packet[0]) {
  case 'E':
    if (isxdigit(m_packet[1]) && isxdigit(m_packet[2])) {
      if (m_packet.size() == 3)
        return eError;
      llvm::StringRef packet_ref(m_packet);
      if (packet_ref[3] == ';') {
        auto err_string = packet_ref.substr(4);
        for (auto e : err_string)
          if (!isxdigit(e))
            return eResponse;
        return eError;
      }
    }
    break;

  case 'O':
    if (m_packet.size() == 2 && m_packet[1] == 'K')
      return eOK;
    break;

  case '+':
    if (m_packet.size() == 1)
      return eAck;
    break;

  case '-':
    if (m_packet.size() == 1)
      return eNack;
    break;
  }
  return eResponse;
}

StringExtractorGDBRemote::ServerPacketType
StringExtractorGDBRemote::GetServerPacketType() const {
#define PACKET_MATCHES(s)                                                      \
  ((packet_size == (sizeof(s) - 1)) && (strcmp((packet_cstr), (s)) == 0))
#define PACKET_STARTS_WITH(s)                                                  \
  ((packet_size >= (sizeof(s) - 1)) &&                                         \
   ::strncmp(packet_cstr, s, (sizeof(s) - 1)) == 0)

  // Empty is not a supported packet...
  if (m_packet.empty())
    return eServerPacketType_invalid;

  const size_t packet_size = m_packet.size();
  const char *packet_cstr = m_packet.c_str();
  switch (m_packet[0]) {

  case '%':
    return eServerPacketType_notify;

  case '\x03':
    if (packet_size == 1)
      return eServerPacketType_interrupt;
    break;

  case '-':
    if (packet_size == 1)
      return eServerPacketType_nack;
    break;

  case '+':
    if (packet_size == 1)
      return eServerPacketType_ack;
    break;

  case 'A':
    return eServerPacketType_A;

  case 'Q':

    switch (packet_cstr[1]) {
    case 'E':
      if (PACKET_STARTS_WITH("QEnvironment:"))
        return eServerPacketType_QEnvironment;
      if (PACKET_STARTS_WITH("QEnvironmentHexEncoded:"))
        return eServerPacketType_QEnvironmentHexEncoded;
      if (PACKET_STARTS_WITH("QEnableErrorStrings"))
        return eServerPacketType_QEnableErrorStrings;
      break;

    case 'P':
      if (PACKET_STARTS_WITH("QPassSignals:"))
        return eServerPacketType_QPassSignals;
      break;

    case 'S':
      if (PACKET_MATCHES("QStartNoAckMode"))
        return eServerPacketType_QStartNoAckMode;
      if (PACKET_STARTS_WITH("QSaveRegisterState"))
        return eServerPacketType_QSaveRegisterState;
      if (PACKET_STARTS_WITH("QSetDisableASLR:"))
        return eServerPacketType_QSetDisableASLR;
      if (PACKET_STARTS_WITH("QSetDetachOnError:"))
        return eServerPacketType_QSetDetachOnError;
      if (PACKET_STARTS_WITH("QSetSTDIN:"))
        return eServerPacketType_QSetSTDIN;
      if (PACKET_STARTS_WITH("QSetSTDOUT:"))
        return eServerPacketType_QSetSTDOUT;
      if (PACKET_STARTS_WITH("QSetSTDERR:"))
        return eServerPacketType_QSetSTDERR;
      if (PACKET_STARTS_WITH("QSetWorkingDir:"))
        return eServerPacketType_QSetWorkingDir;
      if (PACKET_STARTS_WITH("QSetLogging:"))
        return eServerPacketType_QSetLogging;
      if (PACKET_STARTS_WITH("QSetIgnoredExceptions"))
        return eServerPacketType_QSetIgnoredExceptions;
      if (PACKET_STARTS_WITH("QSetMaxPacketSize:"))
        return eServerPacketType_QSetMaxPacketSize;
      if (PACKET_STARTS_WITH("QSetMaxPayloadSize:"))
        return eServerPacketType_QSetMaxPayloadSize;
      if (PACKET_STARTS_WITH("QSetEnableAsyncProfiling;"))
        return eServerPacketType_QSetEnableAsyncProfiling;
      if (PACKET_STARTS_WITH("QSyncThreadState:"))
        return eServerPacketType_QSyncThreadState;
      break;

    case 'L':
      if (PACKET_STARTS_WITH("QLaunchArch:"))
        return eServerPacketType_QLaunchArch;
      if (PACKET_MATCHES("QListThreadsInStopReply"))
        return eServerPacketType_QListThreadsInStopReply;
      break;

    case 'M':
      if (PACKET_STARTS_WITH("QMemTags"))
        return eServerPacketType_QMemTags;
      break;

    case 'N':
      if (PACKET_STARTS_WITH("QNonStop:"))
        return eServerPacketType_QNonStop;
      break;

    case 'R':
      if (PACKET_STARTS_WITH("QRestoreRegisterState:"))
        return eServerPacketType_QRestoreRegisterState;
      break;

    case 'T':
      if (PACKET_MATCHES("QThreadSuffixSupported"))
        return eServerPacketType_QThreadSuffixSupported;
      break;
    }
    break;

  case 'q':
    switch (packet_cstr[1]) {
    case 's':
      if (PACKET_MATCHES("qsProcessInfo"))
        return eServerPacketType_qsProcessInfo;
      if (PACKET_MATCHES("qsThreadInfo"))
        return eServerPacketType_qsThreadInfo;
      break;

    case 'f':
      if (PACKET_STARTS_WITH("qfProcessInfo"))
        return eServerPacketType_qfProcessInfo;
      if (PACKET_STARTS_WITH("qfThreadInfo"))
        return eServerPacketType_qfThreadInfo;
      break;

    case 'C':
      if (packet_size == 2)
        return eServerPacketType_qC;
      break;

    case 'E':
      if (PACKET_STARTS_WITH("qEcho:"))
        return eServerPacketType_qEcho;
      break;

    case 'F':
      if (PACKET_STARTS_WITH("qFileLoadAddress:"))
        return eServerPacketType_qFileLoadAddress;
      break;

    case 'G':
      if (PACKET_STARTS_WITH("qGroupName:"))
        return eServerPacketType_qGroupName;
      if (PACKET_MATCHES("qGetWorkingDir"))
        return eServerPacketType_qGetWorkingDir;
      if (PACKET_MATCHES("qGetPid"))
        return eServerPacketType_qGetPid;
      if (PACKET_STARTS_WITH("qGetProfileData;"))
        return eServerPacketType_qGetProfileData;
      if (PACKET_MATCHES("qGDBServerVersion"))
        return eServerPacketType_qGDBServerVersion;
      break;

    case 'H':
      if (PACKET_MATCHES("qHostInfo"))
        return eServerPacketType_qHostInfo;
      break;

    case 'K':
      if (PACKET_STARTS_WITH("qKillSpawnedProcess"))
        return eServerPacketType_qKillSpawnedProcess;
      break;

    case 'L':
      if (PACKET_STARTS_WITH("qLaunchGDBServer"))
        return eServerPacketType_qLaunchGDBServer;
      if (PACKET_MATCHES("qLaunchSuccess"))
        return eServerPacketType_qLaunchSuccess;
      break;

    case 'M':
      if (PACKET_STARTS_WITH("qMemoryRegionInfo:"))
        return eServerPacketType_qMemoryRegionInfo;
      if (PACKET_MATCHES("qMemoryRegionInfo"))
        return eServerPacketType_qMemoryRegionInfoSupported;
      if (PACKET_STARTS_WITH("qModuleInfo:"))
        return eServerPacketType_qModuleInfo;
      if (PACKET_STARTS_WITH("qMemTags:"))
        return eServerPacketType_qMemTags;
      break;

    case 'P':
      if (PACKET_STARTS_WITH("qProcessInfoPID:"))
        return eServerPacketType_qProcessInfoPID;
      if (PACKET_STARTS_WITH("qPlatform_shell:"))
        return eServerPacketType_qPlatform_shell;
      if (PACKET_STARTS_WITH("qPlatform_mkdir:"))
        return eServerPacketType_qPlatform_mkdir;
      if (PACKET_STARTS_WITH("qPlatform_chmod:"))
        return eServerPacketType_qPlatform_chmod;
      if (PACKET_MATCHES("qProcessInfo"))
        return eServerPacketType_qProcessInfo;
      if (PACKET_STARTS_WITH("qPathComplete:"))
        return eServerPacketType_qPathComplete;
      break;

    case 'Q':
      if (PACKET_MATCHES("qQueryGDBServer"))
        return eServerPacketType_qQueryGDBServer;
      break;

    case 'R':
      if (PACKET_STARTS_WITH("qRcmd,"))
        return eServerPacketType_qRcmd;
      if (PACKET_STARTS_WITH("qRegisterInfo"))
        return eServerPacketType_qRegisterInfo;
      break;

    case 'S':
      if (PACKET_STARTS_WITH("qSaveCore"))
        return eServerPacketType_qLLDBSaveCore;
      if (PACKET_STARTS_WITH("qSpeedTest:"))
        return eServerPacketType_qSpeedTest;
      if (PACKET_MATCHES("qShlibInfoAddr"))
        return eServerPacketType_qShlibInfoAddr;
      if (PACKET_MATCHES("qStepPacketSupported"))
        return eServerPacketType_qStepPacketSupported;
      if (PACKET_STARTS_WITH("qSupported"))
        return eServerPacketType_qSupported;
      if (PACKET_MATCHES("qSyncThreadStateSupported"))
        return eServerPacketType_qSyncThreadStateSupported;
      break;

    case 'T':
      if (PACKET_STARTS_WITH("qThreadExtraInfo,"))
        return eServerPacketType_qThreadExtraInfo;
      if (PACKET_STARTS_WITH("qThreadStopInfo"))
        return eServerPacketType_qThreadStopInfo;
      break;

    case 'U':
      if (PACKET_STARTS_WITH("qUserName:"))
        return eServerPacketType_qUserName;
      break;

    case 'V':
      if (PACKET_MATCHES("qVAttachOrWaitSupported"))
        return eServerPacketType_qVAttachOrWaitSupported;
      break;

    case 'W':
      if (PACKET_STARTS_WITH("qWatchpointSupportInfo:"))
        return eServerPacketType_qWatchpointSupportInfo;
      if (PACKET_MATCHES("qWatchpointSupportInfo"))
        return eServerPacketType_qWatchpointSupportInfoSupported;
      break;

    case 'X':
      if (PACKET_STARTS_WITH("qXfer:"))
        return eServerPacketType_qXfer;
      break;
    }
    break;

  case 'j':
    if (PACKET_STARTS_WITH("jModulesInfo:"))
      return eServerPacketType_jModulesInfo;
    if (PACKET_MATCHES("jSignalsInfo"))
      return eServerPacketType_jSignalsInfo;
    if (PACKET_MATCHES("jThreadsInfo"))
      return eServerPacketType_jThreadsInfo;

    if (PACKET_MATCHES("jLLDBTraceSupported"))
      return eServerPacketType_jLLDBTraceSupported;
    if (PACKET_STARTS_WITH("jLLDBTraceStop:"))
      return eServerPacketType_jLLDBTraceStop;
    if (PACKET_STARTS_WITH("jLLDBTraceStart:"))
      return eServerPacketType_jLLDBTraceStart;
    if (PACKET_STARTS_WITH("jLLDBTraceGetState:"))
      return eServerPacketType_jLLDBTraceGetState;
    if (PACKET_STARTS_WITH("jLLDBTraceGetBinaryData:"))
      return eServerPacketType_jLLDBTraceGetBinaryData;
    break;

  case 'v':
    if (PACKET_STARTS_WITH("vFile:")) {
      if (PACKET_STARTS_WITH("vFile:open:"))
        return eServerPacketType_vFile_open;
      else if (PACKET_STARTS_WITH("vFile:close:"))
        return eServerPacketType_vFile_close;
      else if (PACKET_STARTS_WITH("vFile:pread"))
        return eServerPacketType_vFile_pread;
      else if (PACKET_STARTS_WITH("vFile:pwrite"))
        return eServerPacketType_vFile_pwrite;
      else if (PACKET_STARTS_WITH("vFile:size"))
        return eServerPacketType_vFile_size;
      else if (PACKET_STARTS_WITH("vFile:exists"))
        return eServerPacketType_vFile_exists;
      else if (PACKET_STARTS_WITH("vFile:fstat"))
        return eServerPacketType_vFile_fstat;
      else if (PACKET_STARTS_WITH("vFile:stat"))
        return eServerPacketType_vFile_stat;
      else if (PACKET_STARTS_WITH("vFile:mode"))
        return eServerPacketType_vFile_mode;
      else if (PACKET_STARTS_WITH("vFile:MD5"))
        return eServerPacketType_vFile_md5;
      else if (PACKET_STARTS_WITH("vFile:symlink"))
        return eServerPacketType_vFile_symlink;
      else if (PACKET_STARTS_WITH("vFile:unlink"))
        return eServerPacketType_vFile_unlink;

    } else {
      if (PACKET_STARTS_WITH("vAttach;"))
        return eServerPacketType_vAttach;
      if (PACKET_STARTS_WITH("vAttachWait;"))
        return eServerPacketType_vAttachWait;
      if (PACKET_STARTS_WITH("vAttachOrWait;"))
        return eServerPacketType_vAttachOrWait;
      if (PACKET_STARTS_WITH("vAttachName;"))
        return eServerPacketType_vAttachName;
      if (PACKET_STARTS_WITH("vCont;"))
        return eServerPacketType_vCont;
      if (PACKET_MATCHES("vCont?"))
        return eServerPacketType_vCont_actions;
      if (PACKET_STARTS_WITH("vKill;"))
        return eServerPacketType_vKill;
      if (PACKET_STARTS_WITH("vRun;"))
        return eServerPacketType_vRun;
      if (PACKET_MATCHES("vStopped"))
        return eServerPacketType_vStopped;
      if (PACKET_MATCHES("vCtrlC"))
        return eServerPacketType_vCtrlC;
      if (PACKET_MATCHES("vStdio"))
        return eServerPacketType_vStdio;
      break;

    }
    break;
  case '_':
    switch (packet_cstr[1]) {
    case 'M':
      return eServerPacketType__M;

    case 'm':
      return eServerPacketType__m;
    }
    break;

  case '?':
    if (packet_size == 1)
      return eServerPacketType_stop_reason;
    break;

  case 'c':
    return eServerPacketType_c;

  case 'C':
    return eServerPacketType_C;

  case 'D':
    return eServerPacketType_D;

  case 'g':
    return eServerPacketType_g;

  case 'G':
    return eServerPacketType_G;

  case 'H':
    return eServerPacketType_H;

  case 'I':
    return eServerPacketType_I;

  case 'k':
    if (packet_size == 1)
      return eServerPacketType_k;
    break;

  case 'm':
    return eServerPacketType_m;

  case 'M':
    return eServerPacketType_M;

  case 'p':
    return eServerPacketType_p;

  case 'P':
    return eServerPacketType_P;

  case 's':
    if (packet_size == 1)
      return eServerPacketType_s;
    break;

  case 'S':
    return eServerPacketType_S;

  case 'x':
    return eServerPacketType_x;

  case 'X':
    return eServerPacketType_X;

  case 'T':
    return eServerPacketType_T;

  case 'z':
    if (packet_cstr[1] >= '0' && packet_cstr[1] <= '4')
      return eServerPacketType_z;
    break;

  case 'Z':
    if (packet_cstr[1] >= '0' && packet_cstr[1] <= '4')
      return eServerPacketType_Z;
    break;
  }
  return eServerPacketType_unimplemented;
}

bool StringExtractorGDBRemote::IsOKResponse() const {
  return GetResponseType() == eOK;
}

bool StringExtractorGDBRemote::IsUnsupportedResponse() const {
  return GetResponseType() == eUnsupported;
}

bool StringExtractorGDBRemote::IsNormalResponse() const {
  return GetResponseType() == eResponse;
}

bool StringExtractorGDBRemote::IsErrorResponse() const {
  return GetResponseType() == eError && isxdigit(m_packet[1]) &&
         isxdigit(m_packet[2]);
}

uint8_t StringExtractorGDBRemote::GetError() {
  if (GetResponseType() == eError) {
    SetFilePos(1);
    return GetHexU8(255);
  }
  return 0;
}

lldb_private::Status StringExtractorGDBRemote::GetStatus() {
  lldb_private::Status error;
  if (GetResponseType() == eError) {
    SetFilePos(1);
    uint8_t errc = GetHexU8(255);
    error.SetError(errc, lldb::eErrorTypeGeneric);

    error.SetErrorStringWithFormat("Error %u", errc);
    std::string error_messg;
    if (GetChar() == ';') {
      GetHexByteString(error_messg);
      error.SetErrorString(error_messg);
    }
  }
  return error;
}

size_t StringExtractorGDBRemote::GetEscapedBinaryData(std::string &str) {
  // Just get the data bytes in the string as
  // GDBRemoteCommunication::CheckForPacket() already removes any 0x7d escaped
  // characters. If any 0x7d characters are left in the packet, then they are
  // supposed to be there...
  str.clear();
  const size_t bytes_left = GetBytesLeft();
  if (bytes_left > 0) {
    str.assign(m_packet, m_index, bytes_left);
    m_index += bytes_left;
  }
  return str.size();
}

static bool
OKErrorNotSupportedResponseValidator(void *,
                                     const StringExtractorGDBRemote &response) {
  switch (response.GetResponseType()) {
  case StringExtractorGDBRemote::eOK:
  case StringExtractorGDBRemote::eError:
  case StringExtractorGDBRemote::eUnsupported:
    return true;

  case StringExtractorGDBRemote::eAck:
  case StringExtractorGDBRemote::eNack:
  case StringExtractorGDBRemote::eResponse:
    break;
  }
  return false;
}

static bool JSONResponseValidator(void *,
                                  const StringExtractorGDBRemote &response) {
  switch (response.GetResponseType()) {
  case StringExtractorGDBRemote::eUnsupported:
  case StringExtractorGDBRemote::eError:
    return true; // Accept unsupported or EXX as valid responses

  case StringExtractorGDBRemote::eOK:
  case StringExtractorGDBRemote::eAck:
  case StringExtractorGDBRemote::eNack:
    break;

  case StringExtractorGDBRemote::eResponse:
    // JSON that is returned in from JSON query packets is currently always
    // either a dictionary which starts with a '{', or an array which starts
    // with a '['. This is a quick validator to just make sure the response
    // could be valid JSON without having to validate all of the
    // JSON content.
    switch (response.GetStringRef()[0]) {
    case '{':
      return true;
    case '[':
      return true;
    default:
      break;
    }
    break;
  }
  return false;
}

static bool
ASCIIHexBytesResponseValidator(void *,
                               const StringExtractorGDBRemote &response) {
  switch (response.GetResponseType()) {
  case StringExtractorGDBRemote::eUnsupported:
  case StringExtractorGDBRemote::eError:
    return true; // Accept unsupported or EXX as valid responses

  case StringExtractorGDBRemote::eOK:
  case StringExtractorGDBRemote::eAck:
  case StringExtractorGDBRemote::eNack:
    break;

  case StringExtractorGDBRemote::eResponse: {
    uint32_t valid_count = 0;
    for (const char ch : response.GetStringRef()) {
      if (!isxdigit(ch)) {
        return false;
      }
      if (++valid_count >= 16)
        break; // Don't validate all the characters in case the packet is very
               // large
    }
    return true;
  } break;
  }
  return false;
}

void StringExtractorGDBRemote::CopyResponseValidator(
    const StringExtractorGDBRemote &rhs) {
  m_validator = rhs.m_validator;
  m_validator_baton = rhs.m_validator_baton;
}

void StringExtractorGDBRemote::SetResponseValidator(
    ResponseValidatorCallback callback, void *baton) {
  m_validator = callback;
  m_validator_baton = baton;
}

void StringExtractorGDBRemote::SetResponseValidatorToOKErrorNotSupported() {
  m_validator = OKErrorNotSupportedResponseValidator;
  m_validator_baton = nullptr;
}

void StringExtractorGDBRemote::SetResponseValidatorToASCIIHexBytes() {
  m_validator = ASCIIHexBytesResponseValidator;
  m_validator_baton = nullptr;
}

void StringExtractorGDBRemote::SetResponseValidatorToJSON() {
  m_validator = JSONResponseValidator;
  m_validator_baton = nullptr;
}

bool StringExtractorGDBRemote::ValidateResponse() const {
  // If we have a validator callback, try to validate the callback
  if (m_validator)
    return m_validator(m_validator_baton, *this);
  else
    return true; // No validator, so response is valid
}

std::optional<std::pair<lldb::pid_t, lldb::tid_t>>
StringExtractorGDBRemote::GetPidTid(lldb::pid_t default_pid) {
  llvm::StringRef view = llvm::StringRef(m_packet).substr(m_index);
  size_t initial_length = view.size();
  lldb::pid_t pid = LLDB_INVALID_PROCESS_ID;
  lldb::tid_t tid;

  if (view.consume_front("p")) {
    // process identifier
    if (view.consume_front("-1")) {
      // -1 is a special case
      pid = AllProcesses;
    } else if (view.consumeInteger(16, pid) || pid == 0) {
      // not a valid hex integer OR unsupported pid 0
      m_index = UINT64_MAX;
      return std::nullopt;
    }

    // "." must follow if we expect TID too; otherwise, we assume -1
    if (!view.consume_front(".")) {
      // update m_index
      m_index += initial_length - view.size();

      return {{pid, AllThreads}};
    }
  }

  // thread identifier
  if (view.consume_front("-1")) {
    // -1 is a special case
    tid = AllThreads;
  } else if (view.consumeInteger(16, tid) || tid == 0 || pid == AllProcesses) {
    // not a valid hex integer OR tid 0 OR pid -1 + a specific tid
    m_index = UINT64_MAX;
    return std::nullopt;
  }

  // update m_index
  m_index += initial_length - view.size();

  return {{pid != LLDB_INVALID_PROCESS_ID ? pid : default_pid, tid}};
}
