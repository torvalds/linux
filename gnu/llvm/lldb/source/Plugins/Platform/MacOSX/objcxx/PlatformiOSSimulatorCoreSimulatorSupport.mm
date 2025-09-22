//===-- PlatformiOSSimulatorCoreSimulatorSupport.cpp ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PlatformiOSSimulatorCoreSimulatorSupport.h"

// C Includes
// C++ Includes
// Other libraries and framework includes
#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>
// Project includes
#include "lldb/Host/PseudoTerminal.h"
#include "lldb/Host/FileAction.h"

#include "llvm/ADT/StringRef.h"

using namespace lldb_private;
// CoreSimulator lives as part of Xcode, which means we can't really link
// against it, so we dlopen()
// it at runtime, and error out nicely if that fails
@interface SimServiceContext {
}
+ (id)sharedServiceContextForDeveloperDir:(NSString *)dir
                                    error:(NSError **)error;
@end
// However, the drawback is that the compiler will not know about the selectors
// we're trying to use
// until runtime; to appease clang in this regard, define a fake protocol on
// NSObject that exposes
// the needed interface names for us
@protocol LLDBCoreSimulatorSupport <NSObject>
- (id)defaultDeviceSetWithError:(NSError **)error;
- (NSArray *)devices;
- (id)deviceType;
- (NSString *)name;
- (NSString *)identifier;
- (NSString *)modelIdentifier;
- (NSString *)productFamily;
- (int32_t)productFamilyID;
- (id)runtime;
- (BOOL)available;
- (NSString *)versionString;
- (NSString *)buildVersionString;
- (BOOL)bootWithOptions:(NSDictionary *)options error:(NSError **)error;
- (NSUInteger)state;
- (BOOL)shutdownWithError:(NSError **)error;
- (NSUUID *)UDID;
- (BOOL)spawnWithPath:(NSString *)path
               options:(nullable NSDictionary<NSString *, id> *)options
      terminationQueue:(nullable dispatch_queue_t)terminationQueue
    terminationHandler:(nullable void (^)(int status))terminationHandler
                   pid:(pid_t *_Nullable)pid
                 error:(NSError *__autoreleasing _Nullable *_Nullable)error;
@end

CoreSimulatorSupport::Process::Process(lldb::pid_t p) : m_pid(p), m_error() {}

CoreSimulatorSupport::Process::Process(Status error)
    : m_pid(LLDB_INVALID_PROCESS_ID), m_error(error) {}

CoreSimulatorSupport::Process::Process(lldb::pid_t p, Status error)
    : m_pid(p), m_error(error) {}

CoreSimulatorSupport::DeviceType::DeviceType() : m_model_identifier() {}

CoreSimulatorSupport::DeviceType::DeviceType(id d)
    : m_dev(d), m_model_identifier() {}

CoreSimulatorSupport::DeviceType::operator bool() { return m_dev != nil; }

ConstString CoreSimulatorSupport::DeviceType::GetIdentifier() {
  return ConstString([[m_dev identifier] UTF8String]);
}

ConstString CoreSimulatorSupport::DeviceType::GetProductFamily() {
  return ConstString([[m_dev productFamily] UTF8String]);
}

CoreSimulatorSupport::DeviceType::ProductFamilyID
CoreSimulatorSupport::DeviceType::GetProductFamilyID() {
  return ProductFamilyID([m_dev productFamilyID]);
}

CoreSimulatorSupport::DeviceRuntime::DeviceRuntime() : m_os_version() {}

CoreSimulatorSupport::DeviceRuntime::DeviceRuntime(id d)
    : m_dev(d), m_os_version() {}

CoreSimulatorSupport::DeviceRuntime::operator bool() { return m_dev != nil; }

bool CoreSimulatorSupport::DeviceRuntime::IsAvailable() {
  return [m_dev available];
}

CoreSimulatorSupport::Device::Device() : m_dev_type(), m_dev_runtime() {}

CoreSimulatorSupport::Device::Device(id d)
    : m_dev(d), m_dev_type(), m_dev_runtime() {}

CoreSimulatorSupport::Device::operator bool() { return m_dev != nil; }

CoreSimulatorSupport::Device::State CoreSimulatorSupport::Device::GetState() {
  return (State)([m_dev state]);
}

CoreSimulatorSupport::ModelIdentifier::ModelIdentifier(const std::string &mi)
    : m_family(), m_versions() {
  bool first_digit = false;
  unsigned int val = 0;

  for (char c : mi) {
    if (::isdigit(c)) {
      if (!first_digit)
        first_digit = true;
      val = 10 * val + (c - '0');
    } else if (c == ',') {
      if (first_digit) {
        m_versions.push_back(val);
        val = 0;
      } else
        m_family.push_back(c);
    } else {
      if (first_digit) {
        m_family.clear();
        m_versions.clear();
        return;
      } else {
        m_family.push_back(c);
      }
    }
  }

  if (first_digit)
    m_versions.push_back(val);
}

CoreSimulatorSupport::ModelIdentifier::ModelIdentifier()
    : ModelIdentifier("") {}

CoreSimulatorSupport::OSVersion::OSVersion(const std::string &ver,
                                           const std::string &build)
    : m_versions(), m_build(build) {
  bool any = false;
  unsigned int val = 0;
  for (char c : ver) {
    if (c == '.') {
      m_versions.push_back(val);
      val = 0;
    } else if (::isdigit(c)) {
      val = 10 * val + (c - '0');
      any = true;
    } else {
      m_versions.clear();
      return;
    }
  }
  if (any)
    m_versions.push_back(val);
}

CoreSimulatorSupport::OSVersion::OSVersion() : OSVersion("", "") {}

CoreSimulatorSupport::ModelIdentifier
CoreSimulatorSupport::DeviceType::GetModelIdentifier() {
  if (!m_model_identifier.has_value()) {
    auto utf8_model_id = [[m_dev modelIdentifier] UTF8String];
    if (utf8_model_id && *utf8_model_id)
      m_model_identifier = ModelIdentifier(utf8_model_id);
  }

  if (m_model_identifier.has_value())
    return m_model_identifier.value();
  else
    return ModelIdentifier();
}

CoreSimulatorSupport::OSVersion
CoreSimulatorSupport::DeviceRuntime::GetVersion() {
  if (!m_os_version.has_value()) {
    auto utf8_ver_string = [[m_dev versionString] UTF8String];
    auto utf8_build_ver = [[m_dev buildVersionString] UTF8String];
    if (utf8_ver_string && *utf8_ver_string && utf8_build_ver &&
        *utf8_build_ver) {
      m_os_version = OSVersion(utf8_ver_string, utf8_build_ver);
    }
  }

  if (m_os_version.has_value())
    return m_os_version.value();
  return OSVersion();
}

std::string CoreSimulatorSupport::DeviceType::GetName() {
  auto utf8_name = [[m_dev name] UTF8String];
  if (utf8_name)
    return std::string(utf8_name);
  return "";
}

std::string CoreSimulatorSupport::Device::GetName() const {
  auto utf8_name = [[m_dev name] UTF8String];
  if (utf8_name)
    return std::string(utf8_name);
  return "";
}

std::string CoreSimulatorSupport::Device::GetUDID() const {
  auto utf8_udid = [[[m_dev UDID] UUIDString] UTF8String];
  if (utf8_udid)
    return std::string(utf8_udid);
  else
    return std::string();
}

CoreSimulatorSupport::DeviceType CoreSimulatorSupport::Device::GetDeviceType() {
  if (!m_dev_type.has_value())
    m_dev_type = DeviceType([m_dev deviceType]);

  return m_dev_type.value();
}

CoreSimulatorSupport::DeviceRuntime
CoreSimulatorSupport::Device::GetDeviceRuntime() {
  if (!m_dev_runtime.has_value())
    m_dev_runtime = DeviceRuntime([m_dev runtime]);

  return m_dev_runtime.value();
}

bool CoreSimulatorSupport::
operator>(const CoreSimulatorSupport::OSVersion &lhs,
          const CoreSimulatorSupport::OSVersion &rhs) {
  for (size_t i = 0; i < rhs.GetNumVersions(); i++) {
    unsigned int l = lhs.GetVersionAtIndex(i);
    unsigned int r = rhs.GetVersionAtIndex(i);
    if (l > r)
      return true;
  }
  return false;
}

bool CoreSimulatorSupport::
operator>(const CoreSimulatorSupport::ModelIdentifier &lhs,
          const CoreSimulatorSupport::ModelIdentifier &rhs) {
  if (lhs.GetFamily() != rhs.GetFamily())
    return false;
  for (size_t i = 0; i < rhs.GetNumVersions(); i++) {
    unsigned int l = lhs.GetVersionAtIndex(i);
    unsigned int r = rhs.GetVersionAtIndex(i);
    if (l > r)
      return true;
  }
  return false;
}

bool CoreSimulatorSupport::
operator<(const CoreSimulatorSupport::OSVersion &lhs,
          const CoreSimulatorSupport::OSVersion &rhs) {
  for (size_t i = 0; i < rhs.GetNumVersions(); i++) {
    unsigned int l = lhs.GetVersionAtIndex(i);
    unsigned int r = rhs.GetVersionAtIndex(i);
    if (l < r)
      return true;
  }
  return false;
}

bool CoreSimulatorSupport::
operator<(const CoreSimulatorSupport::ModelIdentifier &lhs,
          const CoreSimulatorSupport::ModelIdentifier &rhs) {
  if (lhs.GetFamily() != rhs.GetFamily())
    return false;

  for (size_t i = 0; i < rhs.GetNumVersions(); i++) {
    unsigned int l = lhs.GetVersionAtIndex(i);
    unsigned int r = rhs.GetVersionAtIndex(i);
    if (l < r)
      return true;
  }
  return false;
}

bool CoreSimulatorSupport::
operator==(const CoreSimulatorSupport::OSVersion &lhs,
           const CoreSimulatorSupport::OSVersion &rhs) {
  for (size_t i = 0; i < rhs.GetNumVersions(); i++) {
    unsigned int l = lhs.GetVersionAtIndex(i);
    unsigned int r = rhs.GetVersionAtIndex(i);
    if (l != r)
      return false;
  }
  return true;
}

bool CoreSimulatorSupport::
operator==(const CoreSimulatorSupport::ModelIdentifier &lhs,
           const CoreSimulatorSupport::ModelIdentifier &rhs) {
  if (lhs.GetFamily() != rhs.GetFamily())
    return false;

  for (size_t i = 0; i < rhs.GetNumVersions(); i++) {
    unsigned int l = lhs.GetVersionAtIndex(i);
    unsigned int r = rhs.GetVersionAtIndex(i);
    if (l != r)
      return false;
  }
  return true;
}

bool CoreSimulatorSupport::
operator!=(const CoreSimulatorSupport::OSVersion &lhs,
           const CoreSimulatorSupport::OSVersion &rhs) {
  for (size_t i = 0; i < rhs.GetNumVersions(); i++) {
    unsigned int l = lhs.GetVersionAtIndex(i);
    unsigned int r = rhs.GetVersionAtIndex(i);
    if (l != r)
      return true;
  }
  return false;
}

bool CoreSimulatorSupport::
operator!=(const CoreSimulatorSupport::ModelIdentifier &lhs,
           const CoreSimulatorSupport::ModelIdentifier &rhs) {
  if (lhs.GetFamily() != rhs.GetFamily())
    return false;

  for (size_t i = 0; i < rhs.GetNumVersions(); i++) {
    unsigned int l = lhs.GetVersionAtIndex(i);
    unsigned int r = rhs.GetVersionAtIndex(i);
    if (l != r)
      return true;
  }
  return false;
}

bool CoreSimulatorSupport::Device::Boot(Status &err) {
  if (m_dev == nil) {
    err.SetErrorString("no valid simulator instance");
    return false;
  }

#define kSimDeviceBootPersist                                                  \
  @"persist" /* An NSNumber (boolean) indicating whether or not the session    \
                should outlive the calling process (default false) */

  NSDictionary *options = @{
    kSimDeviceBootPersist : @NO,
  };

#undef kSimDeviceBootPersist

  NSError *nserror;
  if ([m_dev bootWithOptions:options error:&nserror]) {
    err.Clear();
    return true;
  } else {
    err.SetErrorString([[nserror description] UTF8String]);
    return false;
  }
}

bool CoreSimulatorSupport::Device::Shutdown(Status &err) {
  NSError *nserror;
  if ([m_dev shutdownWithError:&nserror]) {
    err.Clear();
    return true;
  } else {
    err.SetErrorString([[nserror description] UTF8String]);
    return false;
  }
}

static Status HandleFileAction(ProcessLaunchInfo &launch_info,
                               NSMutableDictionary *options, NSString *key,
                               const int fd, lldb::FileSP &file) {
  Status error;
  const FileAction *file_action = launch_info.GetFileActionForFD(fd);
  if (file_action) {
    switch (file_action->GetAction()) {
    case FileAction::eFileActionNone:
      break;

    case FileAction::eFileActionClose:
      error.SetErrorStringWithFormat("close file action for %i not supported",
                                     fd);
      break;

    case FileAction::eFileActionDuplicate:
      error.SetErrorStringWithFormat(
          "duplication file action for %i not supported", fd);
      break;

    case FileAction::eFileActionOpen: {
      FileSpec file_spec = file_action->GetFileSpec();
      if (file_spec) {
        const int primary_fd = launch_info.GetPTY().GetPrimaryFileDescriptor();
        if (primary_fd != PseudoTerminal::invalid_fd) {
          // Check in case our file action open wants to open the secondary
          FileSpec secondary_spec(launch_info.GetPTY().GetSecondaryName());
          if (file_spec == secondary_spec) {
            int secondary_fd =
                launch_info.GetPTY().GetSecondaryFileDescriptor();
            if (secondary_fd == PseudoTerminal::invalid_fd) {
              if (llvm::Error Err = launch_info.GetPTY().OpenSecondary(O_RDWR))
                return Status(std::move(Err));
            }
            secondary_fd = launch_info.GetPTY().GetSecondaryFileDescriptor();
            assert(secondary_fd != PseudoTerminal::invalid_fd);
            [options setValue:[NSNumber numberWithInteger:secondary_fd]
                       forKey:key];
            return error; // Success
          }
        }
        Status posix_error;
        int oflag = file_action->GetActionArgument();
        int created_fd =
            open(file_spec.GetPath().c_str(), oflag, S_IRUSR | S_IWUSR);
        if (created_fd >= 0) {
          auto file_options = File::OpenOptions(0);
          if (oflag & O_RDWR)
            file_options |= File::eOpenOptionReadWrite;
          else if (oflag & O_WRONLY)
            file_options |= File::eOpenOptionWriteOnly;
          else if (oflag & O_RDONLY)
            file_options |= File::eOpenOptionReadOnly;
          file = std::make_shared<NativeFile>(created_fd, file_options, true);
          [options setValue:[NSNumber numberWithInteger:created_fd] forKey:key];
          return error; // Success
        } else {
          posix_error.SetErrorToErrno();
          error.SetErrorStringWithFormat("unable to open file '%s': %s",
                                         file_spec.GetPath().c_str(),
                                         posix_error.AsCString());
        }
      }
    } break;
    }
  }
  return error; // Success, no file action, nothing to do
}

CoreSimulatorSupport::Process
CoreSimulatorSupport::Device::Spawn(ProcessLaunchInfo &launch_info) {
#define kSimDeviceSpawnEnvironment                                             \
  @"environment" /* An NSDictionary (NSStrings -> NSStrings) of environment    \
                    key/values */
#define kSimDeviceSpawnStdin @"stdin"   /* An NSNumber corresponding to a fd */
#define kSimDeviceSpawnStdout @"stdout" /* An NSNumber corresponding to a fd   \
                                           */
#define kSimDeviceSpawnStderr @"stderr" /* An NSNumber corresponding to a fd   \
                                           */
#define kSimDeviceSpawnArguments                                               \
  @"arguments" /* An NSArray of strings to use as the argv array.  If not      \
                  provided, path will be argv[0] */
#define kSimDeviceSpawnWaitForDebugger                                         \
  @"wait_for_debugger" /* An NSNumber (bool) */
#define kSimDeviceSpawnStandalone @"standalone"

  NSMutableDictionary *options = [[NSMutableDictionary alloc] init];

  options[kSimDeviceSpawnStandalone] = @(YES);

  if (launch_info.GetFlags().Test(lldb::eLaunchFlagDebug))
    [options setObject:@YES forKey:kSimDeviceSpawnWaitForDebugger];

  if (launch_info.GetArguments().GetArgumentCount()) {
    const Args &args(launch_info.GetArguments());
    NSMutableArray *args_array = [[NSMutableArray alloc] init];
    for (size_t idx = 0; idx < args.GetArgumentCount(); idx++)
      [args_array
          addObject:[NSString
                        stringWithUTF8String:args.GetArgumentAtIndex(idx)]];

    [options setObject:args_array forKey:kSimDeviceSpawnArguments];
  }

  NSMutableDictionary *env_dict = [[NSMutableDictionary alloc] init];

  for (const auto &KV : launch_info.GetEnvironment()) {
    NSString *key_ns = [NSString stringWithUTF8String:KV.first().str().c_str()];
    NSString *value_ns = [NSString stringWithUTF8String:KV.second.c_str()];

    [env_dict setValue:value_ns forKey:key_ns];
  }

  [options setObject:env_dict forKey:kSimDeviceSpawnEnvironment];

  Status error;
  lldb::FileSP stdin_file;
  lldb::FileSP stdout_file;
  lldb::FileSP stderr_file;
  error = HandleFileAction(launch_info, options, kSimDeviceSpawnStdin,
                           STDIN_FILENO, stdin_file);

  if (error.Fail())
    return CoreSimulatorSupport::Process(error);

  error = HandleFileAction(launch_info, options, kSimDeviceSpawnStdout,
                           STDOUT_FILENO, stdout_file);

  if (error.Fail())
    return CoreSimulatorSupport::Process(error);

  error = HandleFileAction(launch_info, options, kSimDeviceSpawnStderr,
                           STDERR_FILENO, stderr_file);

  if (error.Fail())
    return CoreSimulatorSupport::Process(error);

#undef kSimDeviceSpawnEnvironment
#undef kSimDeviceSpawnStdin
#undef kSimDeviceSpawnStdout
#undef kSimDeviceSpawnStderr
#undef kSimDeviceSpawnWaitForDebugger
#undef kSimDeviceSpawnArguments

  NSError *nserror;

  pid_t pid;
  BOOL success = [m_dev
           spawnWithPath:[NSString stringWithUTF8String:launch_info
                                                            .GetExecutableFile()
                                                            .GetPath()
                                                            .c_str()]
                 options:options
        terminationQueue:nil
      terminationHandler:nil
                     pid:&pid
                   error:&nserror];

  if (!success) {
    const char *nserror_string = [[nserror description] UTF8String];
    error.SetErrorString(nserror_string ? nserror_string : "unable to launch");
  }

  return CoreSimulatorSupport::Process(pid, error);
}

CoreSimulatorSupport::DeviceSet
CoreSimulatorSupport::DeviceSet::GetAllDevices(const char *developer_dir) {
  if (!developer_dir || !developer_dir[0])
    return DeviceSet([NSArray new]);

  Class SimServiceContextClass = NSClassFromString(@"SimServiceContext");
  NSString *dev_dir = @(developer_dir);
  NSError *error = nil;

  id serviceContext =
      [SimServiceContextClass sharedServiceContextForDeveloperDir:dev_dir
                                                            error:&error];
  if (!serviceContext)
    return DeviceSet([NSArray new]);

  return DeviceSet([[serviceContext defaultDeviceSetWithError:&error] devices]);
}

CoreSimulatorSupport::DeviceSet
CoreSimulatorSupport::DeviceSet::GetAvailableDevices(
    const char *developer_dir) {
  return GetAllDevices(developer_dir).GetDevicesIf([](Device d) -> bool {
    return (d && d.GetDeviceType() && d.GetDeviceRuntime() &&
            d.GetDeviceRuntime().IsAvailable());
  });
}

size_t CoreSimulatorSupport::DeviceSet::GetNumDevices() {
  return [m_dev count];
}

CoreSimulatorSupport::Device
CoreSimulatorSupport::DeviceSet::GetDeviceAtIndex(size_t idx) {
  if (idx < GetNumDevices())
    return Device([m_dev objectAtIndex:idx]);
  return Device();
}

CoreSimulatorSupport::DeviceSet CoreSimulatorSupport::DeviceSet::GetDevicesIf(
    std::function<bool(CoreSimulatorSupport::Device)> f) {
  NSMutableArray *array = [[NSMutableArray alloc] init];
  for (NSUInteger i = 0; i < GetNumDevices(); i++) {
    Device d(GetDeviceAtIndex(i));
    if (f(d))
      [array addObject:(id)d.m_dev];
  }

  return DeviceSet(array);
}

void CoreSimulatorSupport::DeviceSet::ForEach(
    std::function<bool(const Device &)> f) {
  const size_t n = GetNumDevices();
  for (NSUInteger i = 0; i < n; ++i) {
    if (!f(GetDeviceAtIndex(i)))
      break;
  }
}

CoreSimulatorSupport::DeviceSet CoreSimulatorSupport::DeviceSet::GetDevices(
    CoreSimulatorSupport::DeviceType::ProductFamilyID dev_id) {
  NSMutableArray *array = [[NSMutableArray alloc] init];
  const size_t n = GetNumDevices();
  for (NSUInteger i = 0; i < n; ++i) {
    Device d(GetDeviceAtIndex(i));
    if (d && d.GetDeviceType() &&
        d.GetDeviceType().GetProductFamilyID() == dev_id)
      [array addObject:(id)d.m_dev];
  }

  return DeviceSet(array);
}

CoreSimulatorSupport::Device CoreSimulatorSupport::DeviceSet::GetFanciest(
    CoreSimulatorSupport::DeviceType::ProductFamilyID dev_id) {
  Device dev;

  for (NSUInteger i = 0; i < GetNumDevices(); i++) {
    Device d(GetDeviceAtIndex(i));
    if (d && d.GetDeviceType() &&
        d.GetDeviceType().GetProductFamilyID() == dev_id) {
      if (!dev)
        dev = d;
      else {
        if ((d.GetDeviceType().GetModelIdentifier() >
             dev.GetDeviceType().GetModelIdentifier()) ||
            d.GetDeviceRuntime().GetVersion() >
                dev.GetDeviceRuntime().GetVersion())
          dev = d;
      }
    }
  }

  return dev;
}
