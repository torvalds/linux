//===-- UnixSignals.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_UNIXSIGNALS_H
#define LLDB_TARGET_UNIXSIGNALS_H

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lldb/lldb-private.h"
#include "llvm/Support/JSON.h"

namespace lldb_private {

class UnixSignals {
public:
  static lldb::UnixSignalsSP Create(const ArchSpec &arch);
  static lldb::UnixSignalsSP CreateForHost();

  // Constructors and Destructors
  UnixSignals();

  virtual ~UnixSignals();

  llvm::StringRef GetSignalAsStringRef(int32_t signo) const;

  std::string
  GetSignalDescription(int32_t signo,
                       std::optional<int32_t> code = std::nullopt,
                       std::optional<lldb::addr_t> addr = std::nullopt,
                       std::optional<lldb::addr_t> lower = std::nullopt,
                       std::optional<lldb::addr_t> upper = std::nullopt) const;

  bool SignalIsValid(int32_t signo) const;

  int32_t GetSignalNumberFromName(const char *name) const;

  /// Gets the information for a particular signal
  ///
  /// GetSignalInfo takes a signal number and populates 3 out parameters
  /// describing how lldb should react when a particular signal is received in
  /// the inferior.
  ///
  /// \param[in] signo
  ///   The signal number to get information about.
  /// \param[out] should_suppress
  ///   Should we suppress this signal?
  /// \param[out] should_stop
  ///   Should we stop if this signal is received?
  /// \param[out] should_notify
  ///   Should we notify the user if this signal is received?
  ///
  /// \return
  ///   Returns a boolean value. Returns true if the out parameters were
  ///   successfully populated, false otherwise.
  bool GetSignalInfo(int32_t signo, bool &should_suppress, bool &should_stop,
                     bool &should_notify) const;

  bool GetShouldSuppress(int32_t signo) const;

  bool SetShouldSuppress(int32_t signo, bool value);

  bool SetShouldSuppress(const char *signal_name, bool value);

  bool GetShouldStop(int32_t signo) const;

  bool SetShouldStop(int32_t signo, bool value);
  bool SetShouldStop(const char *signal_name, bool value);

  bool GetShouldNotify(int32_t signo) const;

  bool SetShouldNotify(int32_t signo, bool value);

  bool SetShouldNotify(const char *signal_name, bool value);
  
  bool ResetSignal(int32_t signo, bool reset_stop = true, 
                   bool reset_notify = true, bool reset_suppress = true);

  // These provide an iterator through the signals available on this system.
  // Call GetFirstSignalNumber to get the first entry, then iterate on
  // GetNextSignalNumber till you get back LLDB_INVALID_SIGNAL_NUMBER.
  int32_t GetFirstSignalNumber() const;

  int32_t GetNextSignalNumber(int32_t current_signal) const;

  int32_t GetNumSignals() const;

  int32_t GetSignalAtIndex(int32_t index) const;

  // We assume that the elements of this object are constant once it is
  // constructed, since a process should never need to add or remove symbols as
  // it runs.  So don't call these functions anywhere but the constructor of
  // your subclass of UnixSignals or in your Process Plugin's GetUnixSignals
  // method before you return the UnixSignal object.

  void AddSignal(int signo, llvm::StringRef name, bool default_suppress,
                 bool default_stop, bool default_notify,
                 llvm::StringRef description,
                 llvm::StringRef alias = llvm::StringRef());

  enum SignalCodePrintOption { None, Address, Bounds };

  // Instead of calling this directly, use a ADD_SIGCODE macro to get compile
  // time checks when on the native platform.
  void AddSignalCode(
      int signo, int code, const llvm::StringLiteral description,
      SignalCodePrintOption print_option = SignalCodePrintOption::None);

  void RemoveSignal(int signo);

  /// Track how many times signals are hit as stop reasons.
  void IncrementSignalHitCount(int signo);

  /// Get the hit count statistics for signals.
  ///
  /// Gettings statistics on the hit counts of signals can help explain why some
  /// debug sessions are slow since each stop takes a few hundred ms and some
  /// software use signals a lot and can cause slow debugging performance if
  /// they are used too often. Even if a signal is not stopped at, it will auto
  /// continue the process and a delay will happen.
  llvm::json::Value GetHitCountStatistics() const;

  // Returns a current version of the data stored in this class. Version gets
  // incremented each time Set... method is called.
  uint64_t GetVersion() const;

  // Returns a vector of signals that meet criteria provided in arguments. Each
  // should_[suppress|stop|notify] flag can be std::nullopt - no filtering by
  // this flag true - only signals that have it set to true are returned false -
  // only signals that have it set to true are returned
  std::vector<int32_t> GetFilteredSignals(std::optional<bool> should_suppress,
                                          std::optional<bool> should_stop,
                                          std::optional<bool> should_notify);

protected:
  // Classes that inherit from UnixSignals can see and modify these

  struct SignalCode {
    const llvm::StringLiteral m_description;
    const SignalCodePrintOption m_print_option;
  };

  // The StringRefs in Signal are either backed by string literals or reside in
  // persistent storage (e.g. a StringSet).
  struct Signal {
    llvm::StringRef m_name;
    llvm::StringRef m_alias;
    llvm::StringRef m_description;
    std::map<int32_t, SignalCode> m_codes;
    uint32_t m_hit_count = 0;
    bool m_suppress : 1, m_stop : 1, m_notify : 1;
    bool m_default_suppress : 1, m_default_stop : 1, m_default_notify : 1;

    Signal(llvm::StringRef name, bool default_suppress, bool default_stop,
           bool default_notify, llvm::StringRef description,
           llvm::StringRef alias);

    ~Signal() = default;
    void Reset(bool reset_stop, bool reset_notify, bool reset_suppress);
  };

  llvm::StringRef GetShortName(llvm::StringRef name) const;

  virtual void Reset();

  typedef std::map<int32_t, Signal> collection;

  collection m_signals;

  // This version gets incremented every time something is changing in this
  // class, including when we call AddSignal from the constructor. So after the
  // object is constructed m_version is going to be > 0 if it has at least one
  // signal registered in it.
  uint64_t m_version = 0;

  // GDBRemote signals need to be copyable.
  UnixSignals(const UnixSignals &rhs);

  const UnixSignals &operator=(const UnixSignals &rhs) = delete;
};

} // Namespace lldb
#endif // LLDB_TARGET_UNIXSIGNALS_H
