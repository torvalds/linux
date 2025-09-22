//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//  Models register sets for supported processors.
//
//===----------------------------------------------------------------------===//

#ifndef __REGISTERS_HPP__
#define __REGISTERS_HPP__

#include <stdint.h>
#include <string.h>

#include "cet_unwind.h"
#include "config.h"
#include "libunwind.h"

namespace libunwind {

// For emulating 128-bit registers
struct v128 { uint32_t vec[4]; };

enum {
  REGISTERS_X86,
  REGISTERS_X86_64,
  REGISTERS_PPC,
  REGISTERS_PPC64,
  REGISTERS_ARM64,
  REGISTERS_ARM,
  REGISTERS_OR1K,
  REGISTERS_MIPS_O32,
  REGISTERS_MIPS_NEWABI,
  REGISTERS_SPARC,
  REGISTERS_SPARC64,
  REGISTERS_HEXAGON,
  REGISTERS_RISCV,
  REGISTERS_VE,
  REGISTERS_S390X,
  REGISTERS_LOONGARCH,
};

#if defined(_LIBUNWIND_TARGET_I386)
class _LIBUNWIND_HIDDEN Registers_x86;
extern "C" void __libunwind_Registers_x86_jumpto(Registers_x86 *);

#if defined(_LIBUNWIND_USE_CET)
extern "C" void *__libunwind_cet_get_jump_target() {
  return reinterpret_cast<void *>(&__libunwind_Registers_x86_jumpto);
}
#endif

/// Registers_x86 holds the register state of a thread in a 32-bit intel
/// process.
class _LIBUNWIND_HIDDEN Registers_x86 {
public:
  Registers_x86();
  Registers_x86(const void *registers);

  bool        validRegister(int num) const;
  uint32_t    getRegister(int num) const;
  void        setRegister(int num, uint32_t value);
  bool        validFloatRegister(int) const { return false; }
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int) const { return false; }
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto() { __libunwind_Registers_x86_jumpto(this); }
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_X86;
  }
  static int  getArch() { return REGISTERS_X86; }

  uint32_t  getSP() const          { return _registers.__esp; }
  void      setSP(uint32_t value)  { _registers.__esp = value; }
  uint32_t  getIP() const          { return _registers.__eip; }
  void      setIP(uint32_t value)  { _registers.__eip = value; }
  uint32_t  getEBP() const         { return _registers.__ebp; }
  void      setEBP(uint32_t value) { _registers.__ebp = value; }
  uint32_t  getEBX() const         { return _registers.__ebx; }
  void      setEBX(uint32_t value) { _registers.__ebx = value; }
  uint32_t  getECX() const         { return _registers.__ecx; }
  void      setECX(uint32_t value) { _registers.__ecx = value; }
  uint32_t  getEDX() const         { return _registers.__edx; }
  void      setEDX(uint32_t value) { _registers.__edx = value; }
  uint32_t  getESI() const         { return _registers.__esi; }
  void      setESI(uint32_t value) { _registers.__esi = value; }
  uint32_t  getEDI() const         { return _registers.__edi; }
  void      setEDI(uint32_t value) { _registers.__edi = value; }

private:
  struct GPRs {
    unsigned int __eax;
    unsigned int __ebx;
    unsigned int __ecx;
    unsigned int __edx;
    unsigned int __edi;
    unsigned int __esi;
    unsigned int __ebp;
    unsigned int __esp;
    unsigned int __ss;
    unsigned int __eflags;
    unsigned int __eip;
    unsigned int __cs;
    unsigned int __ds;
    unsigned int __es;
    unsigned int __fs;
    unsigned int __gs;
  };

  GPRs _registers;
};

inline Registers_x86::Registers_x86(const void *registers) {
  static_assert((check_fit<Registers_x86, unw_context_t>::does_fit),
                "x86 registers do not fit into unw_context_t");
  memcpy(&_registers, registers, sizeof(_registers));
}

inline Registers_x86::Registers_x86() {
  memset(&_registers, 0, sizeof(_registers));
}

inline bool Registers_x86::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return true;
  if (regNum == UNW_REG_SP)
    return true;
  if (regNum < 0)
    return false;
  if (regNum > 7)
    return false;
  return true;
}

inline uint32_t Registers_x86::getRegister(int regNum) const {
  switch (regNum) {
  case UNW_REG_IP:
    return _registers.__eip;
  case UNW_REG_SP:
    return _registers.__esp;
  case UNW_X86_EAX:
    return _registers.__eax;
  case UNW_X86_ECX:
    return _registers.__ecx;
  case UNW_X86_EDX:
    return _registers.__edx;
  case UNW_X86_EBX:
    return _registers.__ebx;
#if !defined(__APPLE__)
  case UNW_X86_ESP:
#else
  case UNW_X86_EBP:
#endif
    return _registers.__ebp;
#if !defined(__APPLE__)
  case UNW_X86_EBP:
#else
  case UNW_X86_ESP:
#endif
    return _registers.__esp;
  case UNW_X86_ESI:
    return _registers.__esi;
  case UNW_X86_EDI:
    return _registers.__edi;
  }
  _LIBUNWIND_ABORT("unsupported x86 register");
}

inline void Registers_x86::setRegister(int regNum, uint32_t value) {
  switch (regNum) {
  case UNW_REG_IP:
    _registers.__eip = value;
    return;
  case UNW_REG_SP:
    _registers.__esp = value;
    return;
  case UNW_X86_EAX:
    _registers.__eax = value;
    return;
  case UNW_X86_ECX:
    _registers.__ecx = value;
    return;
  case UNW_X86_EDX:
    _registers.__edx = value;
    return;
  case UNW_X86_EBX:
    _registers.__ebx = value;
    return;
#if !defined(__APPLE__)
  case UNW_X86_ESP:
#else
  case UNW_X86_EBP:
#endif
    _registers.__ebp = value;
    return;
#if !defined(__APPLE__)
  case UNW_X86_EBP:
#else
  case UNW_X86_ESP:
#endif
    _registers.__esp = value;
    return;
  case UNW_X86_ESI:
    _registers.__esi = value;
    return;
  case UNW_X86_EDI:
    _registers.__edi = value;
    return;
  }
  _LIBUNWIND_ABORT("unsupported x86 register");
}

inline const char *Registers_x86::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
    return "ip";
  case UNW_REG_SP:
    return "esp";
  case UNW_X86_EAX:
    return "eax";
  case UNW_X86_ECX:
    return "ecx";
  case UNW_X86_EDX:
    return "edx";
  case UNW_X86_EBX:
    return "ebx";
  case UNW_X86_EBP:
    return "ebp";
  case UNW_X86_ESP:
    return "esp";
  case UNW_X86_ESI:
    return "esi";
  case UNW_X86_EDI:
    return "edi";
  default:
    return "unknown register";
  }
}

inline double Registers_x86::getFloatRegister(int) const {
  _LIBUNWIND_ABORT("no x86 float registers");
}

inline void Registers_x86::setFloatRegister(int, double) {
  _LIBUNWIND_ABORT("no x86 float registers");
}

inline v128 Registers_x86::getVectorRegister(int) const {
  _LIBUNWIND_ABORT("no x86 vector registers");
}

inline void Registers_x86::setVectorRegister(int, v128) {
  _LIBUNWIND_ABORT("no x86 vector registers");
}
#endif // _LIBUNWIND_TARGET_I386


#if defined(_LIBUNWIND_TARGET_X86_64)
/// Registers_x86_64  holds the register state of a thread in a 64-bit intel
/// process.
class _LIBUNWIND_HIDDEN Registers_x86_64;
extern "C" void __libunwind_Registers_x86_64_jumpto(Registers_x86_64 *);

#if defined(_LIBUNWIND_USE_CET)
extern "C" void *__libunwind_cet_get_jump_target() {
  return reinterpret_cast<void *>(&__libunwind_Registers_x86_64_jumpto);
}
#endif

class _LIBUNWIND_HIDDEN Registers_x86_64 {
public:
  Registers_x86_64();
  Registers_x86_64(const void *registers);

  bool        validRegister(int num) const;
  uint64_t    getRegister(int num) const;
  void        setRegister(int num, uint64_t value);
  bool        validFloatRegister(int) const { return false; }
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto() { __libunwind_Registers_x86_64_jumpto(this); }
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_X86_64;
  }
  static int  getArch() { return REGISTERS_X86_64; }

  uint64_t  getSP() const          { return _registers.__rsp; }
  void      setSP(uint64_t value)  { _registers.__rsp = value; }
  uint64_t  getIP() const          { return _registers.__rip; }
  void      setIP(uint64_t value)  { _registers.__rip = value; }
  uint64_t  getRBP() const         { return _registers.__rbp; }
  void      setRBP(uint64_t value) { _registers.__rbp = value; }
  uint64_t  getRBX() const         { return _registers.__rbx; }
  void      setRBX(uint64_t value) { _registers.__rbx = value; }
  uint64_t  getR12() const         { return _registers.__r12; }
  void      setR12(uint64_t value) { _registers.__r12 = value; }
  uint64_t  getR13() const         { return _registers.__r13; }
  void      setR13(uint64_t value) { _registers.__r13 = value; }
  uint64_t  getR14() const         { return _registers.__r14; }
  void      setR14(uint64_t value) { _registers.__r14 = value; }
  uint64_t  getR15() const         { return _registers.__r15; }
  void      setR15(uint64_t value) { _registers.__r15 = value; }

private:
  struct GPRs {
    uint64_t __rax;
    uint64_t __rbx;
    uint64_t __rcx;
    uint64_t __rdx;
    uint64_t __rdi;
    uint64_t __rsi;
    uint64_t __rbp;
    uint64_t __rsp;
    uint64_t __r8;
    uint64_t __r9;
    uint64_t __r10;
    uint64_t __r11;
    uint64_t __r12;
    uint64_t __r13;
    uint64_t __r14;
    uint64_t __r15;
    uint64_t __rip;
    uint64_t __rflags;
    uint64_t __cs;
    uint64_t __fs;
    uint64_t __gs;
#if defined(_WIN64)
    uint64_t __padding; // 16-byte align
#endif
  };
  GPRs _registers;
#if defined(_WIN64)
  v128 _xmm[16];
#endif
};

inline Registers_x86_64::Registers_x86_64(const void *registers) {
  static_assert((check_fit<Registers_x86_64, unw_context_t>::does_fit),
                "x86_64 registers do not fit into unw_context_t");
  memcpy(&_registers, registers, sizeof(_registers));
}

inline Registers_x86_64::Registers_x86_64() {
  memset(&_registers, 0, sizeof(_registers));
}

inline bool Registers_x86_64::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return true;
  if (regNum == UNW_REG_SP)
    return true;
  if (regNum < 0)
    return false;
  if (regNum > 16)
    return false;
  return true;
}

inline uint64_t Registers_x86_64::getRegister(int regNum) const {
  switch (regNum) {
  case UNW_REG_IP:
  case UNW_X86_64_RIP:
    return _registers.__rip;
  case UNW_REG_SP:
    return _registers.__rsp;
  case UNW_X86_64_RAX:
    return _registers.__rax;
  case UNW_X86_64_RDX:
    return _registers.__rdx;
  case UNW_X86_64_RCX:
    return _registers.__rcx;
  case UNW_X86_64_RBX:
    return _registers.__rbx;
  case UNW_X86_64_RSI:
    return _registers.__rsi;
  case UNW_X86_64_RDI:
    return _registers.__rdi;
  case UNW_X86_64_RBP:
    return _registers.__rbp;
  case UNW_X86_64_RSP:
    return _registers.__rsp;
  case UNW_X86_64_R8:
    return _registers.__r8;
  case UNW_X86_64_R9:
    return _registers.__r9;
  case UNW_X86_64_R10:
    return _registers.__r10;
  case UNW_X86_64_R11:
    return _registers.__r11;
  case UNW_X86_64_R12:
    return _registers.__r12;
  case UNW_X86_64_R13:
    return _registers.__r13;
  case UNW_X86_64_R14:
    return _registers.__r14;
  case UNW_X86_64_R15:
    return _registers.__r15;
  }
  _LIBUNWIND_ABORT("unsupported x86_64 register");
}

inline void Registers_x86_64::setRegister(int regNum, uint64_t value) {
  switch (regNum) {
  case UNW_REG_IP:
  case UNW_X86_64_RIP:
    _registers.__rip = value;
    return;
  case UNW_REG_SP:
    _registers.__rsp = value;
    return;
  case UNW_X86_64_RAX:
    _registers.__rax = value;
    return;
  case UNW_X86_64_RDX:
    _registers.__rdx = value;
    return;
  case UNW_X86_64_RCX:
    _registers.__rcx = value;
    return;
  case UNW_X86_64_RBX:
    _registers.__rbx = value;
    return;
  case UNW_X86_64_RSI:
    _registers.__rsi = value;
    return;
  case UNW_X86_64_RDI:
    _registers.__rdi = value;
    return;
  case UNW_X86_64_RBP:
    _registers.__rbp = value;
    return;
  case UNW_X86_64_RSP:
    _registers.__rsp = value;
    return;
  case UNW_X86_64_R8:
    _registers.__r8 = value;
    return;
  case UNW_X86_64_R9:
    _registers.__r9 = value;
    return;
  case UNW_X86_64_R10:
    _registers.__r10 = value;
    return;
  case UNW_X86_64_R11:
    _registers.__r11 = value;
    return;
  case UNW_X86_64_R12:
    _registers.__r12 = value;
    return;
  case UNW_X86_64_R13:
    _registers.__r13 = value;
    return;
  case UNW_X86_64_R14:
    _registers.__r14 = value;
    return;
  case UNW_X86_64_R15:
    _registers.__r15 = value;
    return;
  }
  _LIBUNWIND_ABORT("unsupported x86_64 register");
}

inline const char *Registers_x86_64::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
  case UNW_X86_64_RIP:
    return "rip";
  case UNW_REG_SP:
    return "rsp";
  case UNW_X86_64_RAX:
    return "rax";
  case UNW_X86_64_RDX:
    return "rdx";
  case UNW_X86_64_RCX:
    return "rcx";
  case UNW_X86_64_RBX:
    return "rbx";
  case UNW_X86_64_RSI:
    return "rsi";
  case UNW_X86_64_RDI:
    return "rdi";
  case UNW_X86_64_RBP:
    return "rbp";
  case UNW_X86_64_RSP:
    return "rsp";
  case UNW_X86_64_R8:
    return "r8";
  case UNW_X86_64_R9:
    return "r9";
  case UNW_X86_64_R10:
    return "r10";
  case UNW_X86_64_R11:
    return "r11";
  case UNW_X86_64_R12:
    return "r12";
  case UNW_X86_64_R13:
    return "r13";
  case UNW_X86_64_R14:
    return "r14";
  case UNW_X86_64_R15:
    return "r15";
  case UNW_X86_64_XMM0:
    return "xmm0";
  case UNW_X86_64_XMM1:
    return "xmm1";
  case UNW_X86_64_XMM2:
    return "xmm2";
  case UNW_X86_64_XMM3:
    return "xmm3";
  case UNW_X86_64_XMM4:
    return "xmm4";
  case UNW_X86_64_XMM5:
    return "xmm5";
  case UNW_X86_64_XMM6:
    return "xmm6";
  case UNW_X86_64_XMM7:
    return "xmm7";
  case UNW_X86_64_XMM8:
    return "xmm8";
  case UNW_X86_64_XMM9:
    return "xmm9";
  case UNW_X86_64_XMM10:
    return "xmm10";
  case UNW_X86_64_XMM11:
    return "xmm11";
  case UNW_X86_64_XMM12:
    return "xmm12";
  case UNW_X86_64_XMM13:
    return "xmm13";
  case UNW_X86_64_XMM14:
    return "xmm14";
  case UNW_X86_64_XMM15:
    return "xmm15";
  default:
    return "unknown register";
  }
}

inline double Registers_x86_64::getFloatRegister(int) const {
  _LIBUNWIND_ABORT("no x86_64 float registers");
}

inline void Registers_x86_64::setFloatRegister(int, double) {
  _LIBUNWIND_ABORT("no x86_64 float registers");
}

inline bool Registers_x86_64::validVectorRegister(int regNum) const {
#if defined(_WIN64)
  if (regNum < UNW_X86_64_XMM0)
    return false;
  if (regNum > UNW_X86_64_XMM15)
    return false;
  return true;
#else
  (void)regNum; // suppress unused parameter warning
  return false;
#endif
}

inline v128 Registers_x86_64::getVectorRegister(int regNum) const {
#if defined(_WIN64)
  assert(validVectorRegister(regNum));
  return _xmm[regNum - UNW_X86_64_XMM0];
#else
  (void)regNum; // suppress unused parameter warning
  _LIBUNWIND_ABORT("no x86_64 vector registers");
#endif
}

inline void Registers_x86_64::setVectorRegister(int regNum, v128 value) {
#if defined(_WIN64)
  assert(validVectorRegister(regNum));
  _xmm[regNum - UNW_X86_64_XMM0] = value;
#else
  (void)regNum; (void)value; // suppress unused parameter warnings
  _LIBUNWIND_ABORT("no x86_64 vector registers");
#endif
}
#endif // _LIBUNWIND_TARGET_X86_64


#if defined(_LIBUNWIND_TARGET_PPC)
/// Registers_ppc holds the register state of a thread in a 32-bit PowerPC
/// process.
class _LIBUNWIND_HIDDEN Registers_ppc {
public:
  Registers_ppc();
  Registers_ppc(const void *registers);

  bool        validRegister(int num) const;
  uint32_t    getRegister(int num) const;
  void        setRegister(int num, uint32_t value);
  bool        validFloatRegister(int num) const;
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_PPC;
  }
  static int  getArch() { return REGISTERS_PPC; }

  uint64_t  getSP() const         { return _registers.__r1; }
  void      setSP(uint32_t value) { _registers.__r1 = value; }
  uint64_t  getIP() const         { return _registers.__srr0; }
  void      setIP(uint32_t value) { _registers.__srr0 = value; }
  uint64_t  getCR() const         { return _registers.__cr; }
  void      setCR(uint32_t value) { _registers.__cr = value; }
  uint64_t  getLR() const         { return _registers.__lr; }
  void      setLR(uint32_t value) { _registers.__lr = value; }

private:
  struct ppc_thread_state_t {
    unsigned int __srr0; /* Instruction address register (PC) */
    unsigned int __srr1; /* Machine state register (supervisor) */
    unsigned int __r0;
    unsigned int __r1;
    unsigned int __r2;
    unsigned int __r3;
    unsigned int __r4;
    unsigned int __r5;
    unsigned int __r6;
    unsigned int __r7;
    unsigned int __r8;
    unsigned int __r9;
    unsigned int __r10;
    unsigned int __r11;
    unsigned int __r12;
    unsigned int __r13;
    unsigned int __r14;
    unsigned int __r15;
    unsigned int __r16;
    unsigned int __r17;
    unsigned int __r18;
    unsigned int __r19;
    unsigned int __r20;
    unsigned int __r21;
    unsigned int __r22;
    unsigned int __r23;
    unsigned int __r24;
    unsigned int __r25;
    unsigned int __r26;
    unsigned int __r27;
    unsigned int __r28;
    unsigned int __r29;
    unsigned int __r30;
    unsigned int __r31;
    unsigned int __cr;     /* Condition register */
    unsigned int __xer;    /* User's integer exception register */
    unsigned int __lr;     /* Link register */
    unsigned int __ctr;    /* Count register */
    unsigned int __mq;     /* MQ register (601 only) */
    unsigned int __vrsave; /* Vector Save Register */
  };

  struct ppc_float_state_t {
    double __fpregs[32];

    unsigned int __fpscr_pad; /* fpscr is 64 bits, 32 bits of rubbish */
    unsigned int __fpscr;     /* floating point status register */
  };

  ppc_thread_state_t _registers;
  ppc_float_state_t  _floatRegisters;
  v128               _vectorRegisters[32]; // offset 424
};

inline Registers_ppc::Registers_ppc(const void *registers) {
  static_assert((check_fit<Registers_ppc, unw_context_t>::does_fit),
                "ppc registers do not fit into unw_context_t");
  memcpy(&_registers, static_cast<const uint8_t *>(registers),
         sizeof(_registers));
  static_assert(sizeof(ppc_thread_state_t) == 160,
                "expected float register offset to be 160");
  memcpy(&_floatRegisters,
         static_cast<const uint8_t *>(registers) + sizeof(ppc_thread_state_t),
         sizeof(_floatRegisters));
  static_assert(sizeof(ppc_thread_state_t) + sizeof(ppc_float_state_t) == 424,
                "expected vector register offset to be 424 bytes");
  memcpy(_vectorRegisters,
         static_cast<const uint8_t *>(registers) + sizeof(ppc_thread_state_t) +
             sizeof(ppc_float_state_t),
         sizeof(_vectorRegisters));
}

inline Registers_ppc::Registers_ppc() {
  memset(&_registers, 0, sizeof(_registers));
  memset(&_floatRegisters, 0, sizeof(_floatRegisters));
  memset(&_vectorRegisters, 0, sizeof(_vectorRegisters));
}

inline bool Registers_ppc::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return true;
  if (regNum == UNW_REG_SP)
    return true;
  if (regNum == UNW_PPC_VRSAVE)
    return true;
  if (regNum < 0)
    return false;
  if (regNum <= UNW_PPC_R31)
    return true;
  if (regNum == UNW_PPC_MQ)
    return true;
  if (regNum == UNW_PPC_LR)
    return true;
  if (regNum == UNW_PPC_CTR)
    return true;
  if ((UNW_PPC_CR0 <= regNum) && (regNum <= UNW_PPC_CR7))
    return true;
  return false;
}

inline uint32_t Registers_ppc::getRegister(int regNum) const {
  switch (regNum) {
  case UNW_REG_IP:
    return _registers.__srr0;
  case UNW_REG_SP:
    return _registers.__r1;
  case UNW_PPC_R0:
    return _registers.__r0;
  case UNW_PPC_R1:
    return _registers.__r1;
  case UNW_PPC_R2:
    return _registers.__r2;
  case UNW_PPC_R3:
    return _registers.__r3;
  case UNW_PPC_R4:
    return _registers.__r4;
  case UNW_PPC_R5:
    return _registers.__r5;
  case UNW_PPC_R6:
    return _registers.__r6;
  case UNW_PPC_R7:
    return _registers.__r7;
  case UNW_PPC_R8:
    return _registers.__r8;
  case UNW_PPC_R9:
    return _registers.__r9;
  case UNW_PPC_R10:
    return _registers.__r10;
  case UNW_PPC_R11:
    return _registers.__r11;
  case UNW_PPC_R12:
    return _registers.__r12;
  case UNW_PPC_R13:
    return _registers.__r13;
  case UNW_PPC_R14:
    return _registers.__r14;
  case UNW_PPC_R15:
    return _registers.__r15;
  case UNW_PPC_R16:
    return _registers.__r16;
  case UNW_PPC_R17:
    return _registers.__r17;
  case UNW_PPC_R18:
    return _registers.__r18;
  case UNW_PPC_R19:
    return _registers.__r19;
  case UNW_PPC_R20:
    return _registers.__r20;
  case UNW_PPC_R21:
    return _registers.__r21;
  case UNW_PPC_R22:
    return _registers.__r22;
  case UNW_PPC_R23:
    return _registers.__r23;
  case UNW_PPC_R24:
    return _registers.__r24;
  case UNW_PPC_R25:
    return _registers.__r25;
  case UNW_PPC_R26:
    return _registers.__r26;
  case UNW_PPC_R27:
    return _registers.__r27;
  case UNW_PPC_R28:
    return _registers.__r28;
  case UNW_PPC_R29:
    return _registers.__r29;
  case UNW_PPC_R30:
    return _registers.__r30;
  case UNW_PPC_R31:
    return _registers.__r31;
  case UNW_PPC_LR:
    return _registers.__lr;
  case UNW_PPC_CR0:
    return (_registers.__cr & 0xF0000000);
  case UNW_PPC_CR1:
    return (_registers.__cr & 0x0F000000);
  case UNW_PPC_CR2:
    return (_registers.__cr & 0x00F00000);
  case UNW_PPC_CR3:
    return (_registers.__cr & 0x000F0000);
  case UNW_PPC_CR4:
    return (_registers.__cr & 0x0000F000);
  case UNW_PPC_CR5:
    return (_registers.__cr & 0x00000F00);
  case UNW_PPC_CR6:
    return (_registers.__cr & 0x000000F0);
  case UNW_PPC_CR7:
    return (_registers.__cr & 0x0000000F);
  case UNW_PPC_VRSAVE:
    return _registers.__vrsave;
  }
  _LIBUNWIND_ABORT("unsupported ppc register");
}

inline void Registers_ppc::setRegister(int regNum, uint32_t value) {
  //fprintf(stderr, "Registers_ppc::setRegister(%d, 0x%08X)\n", regNum, value);
  switch (regNum) {
  case UNW_REG_IP:
    _registers.__srr0 = value;
    return;
  case UNW_REG_SP:
    _registers.__r1 = value;
    return;
  case UNW_PPC_R0:
    _registers.__r0 = value;
    return;
  case UNW_PPC_R1:
    _registers.__r1 = value;
    return;
  case UNW_PPC_R2:
    _registers.__r2 = value;
    return;
  case UNW_PPC_R3:
    _registers.__r3 = value;
    return;
  case UNW_PPC_R4:
    _registers.__r4 = value;
    return;
  case UNW_PPC_R5:
    _registers.__r5 = value;
    return;
  case UNW_PPC_R6:
    _registers.__r6 = value;
    return;
  case UNW_PPC_R7:
    _registers.__r7 = value;
    return;
  case UNW_PPC_R8:
    _registers.__r8 = value;
    return;
  case UNW_PPC_R9:
    _registers.__r9 = value;
    return;
  case UNW_PPC_R10:
    _registers.__r10 = value;
    return;
  case UNW_PPC_R11:
    _registers.__r11 = value;
    return;
  case UNW_PPC_R12:
    _registers.__r12 = value;
    return;
  case UNW_PPC_R13:
    _registers.__r13 = value;
    return;
  case UNW_PPC_R14:
    _registers.__r14 = value;
    return;
  case UNW_PPC_R15:
    _registers.__r15 = value;
    return;
  case UNW_PPC_R16:
    _registers.__r16 = value;
    return;
  case UNW_PPC_R17:
    _registers.__r17 = value;
    return;
  case UNW_PPC_R18:
    _registers.__r18 = value;
    return;
  case UNW_PPC_R19:
    _registers.__r19 = value;
    return;
  case UNW_PPC_R20:
    _registers.__r20 = value;
    return;
  case UNW_PPC_R21:
    _registers.__r21 = value;
    return;
  case UNW_PPC_R22:
    _registers.__r22 = value;
    return;
  case UNW_PPC_R23:
    _registers.__r23 = value;
    return;
  case UNW_PPC_R24:
    _registers.__r24 = value;
    return;
  case UNW_PPC_R25:
    _registers.__r25 = value;
    return;
  case UNW_PPC_R26:
    _registers.__r26 = value;
    return;
  case UNW_PPC_R27:
    _registers.__r27 = value;
    return;
  case UNW_PPC_R28:
    _registers.__r28 = value;
    return;
  case UNW_PPC_R29:
    _registers.__r29 = value;
    return;
  case UNW_PPC_R30:
    _registers.__r30 = value;
    return;
  case UNW_PPC_R31:
    _registers.__r31 = value;
    return;
  case UNW_PPC_MQ:
    _registers.__mq = value;
    return;
  case UNW_PPC_LR:
    _registers.__lr = value;
    return;
  case UNW_PPC_CTR:
    _registers.__ctr = value;
    return;
  case UNW_PPC_CR0:
    _registers.__cr &= 0x0FFFFFFF;
    _registers.__cr |= (value & 0xF0000000);
    return;
  case UNW_PPC_CR1:
    _registers.__cr &= 0xF0FFFFFF;
    _registers.__cr |= (value & 0x0F000000);
    return;
  case UNW_PPC_CR2:
    _registers.__cr &= 0xFF0FFFFF;
    _registers.__cr |= (value & 0x00F00000);
    return;
  case UNW_PPC_CR3:
    _registers.__cr &= 0xFFF0FFFF;
    _registers.__cr |= (value & 0x000F0000);
    return;
  case UNW_PPC_CR4:
    _registers.__cr &= 0xFFFF0FFF;
    _registers.__cr |= (value & 0x0000F000);
    return;
  case UNW_PPC_CR5:
    _registers.__cr &= 0xFFFFF0FF;
    _registers.__cr |= (value & 0x00000F00);
    return;
  case UNW_PPC_CR6:
    _registers.__cr &= 0xFFFFFF0F;
    _registers.__cr |= (value & 0x000000F0);
    return;
  case UNW_PPC_CR7:
    _registers.__cr &= 0xFFFFFFF0;
    _registers.__cr |= (value & 0x0000000F);
    return;
  case UNW_PPC_VRSAVE:
    _registers.__vrsave = value;
    return;
    // not saved
    return;
  case UNW_PPC_XER:
    _registers.__xer = value;
    return;
  case UNW_PPC_AP:
  case UNW_PPC_VSCR:
  case UNW_PPC_SPEFSCR:
    // not saved
    return;
  }
  _LIBUNWIND_ABORT("unsupported ppc register");
}

inline bool Registers_ppc::validFloatRegister(int regNum) const {
  if (regNum < UNW_PPC_F0)
    return false;
  if (regNum > UNW_PPC_F31)
    return false;
  return true;
}

inline double Registers_ppc::getFloatRegister(int regNum) const {
  assert(validFloatRegister(regNum));
  return _floatRegisters.__fpregs[regNum - UNW_PPC_F0];
}

inline void Registers_ppc::setFloatRegister(int regNum, double value) {
  assert(validFloatRegister(regNum));
  _floatRegisters.__fpregs[regNum - UNW_PPC_F0] = value;
}

inline bool Registers_ppc::validVectorRegister(int regNum) const {
  if (regNum < UNW_PPC_V0)
    return false;
  if (regNum > UNW_PPC_V31)
    return false;
  return true;
}

inline v128 Registers_ppc::getVectorRegister(int regNum) const {
  assert(validVectorRegister(regNum));
  v128 result = _vectorRegisters[regNum - UNW_PPC_V0];
  return result;
}

inline void Registers_ppc::setVectorRegister(int regNum, v128 value) {
  assert(validVectorRegister(regNum));
  _vectorRegisters[regNum - UNW_PPC_V0] = value;
}

inline const char *Registers_ppc::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
    return "ip";
  case UNW_REG_SP:
    return "sp";
  case UNW_PPC_R0:
    return "r0";
  case UNW_PPC_R1:
    return "r1";
  case UNW_PPC_R2:
    return "r2";
  case UNW_PPC_R3:
    return "r3";
  case UNW_PPC_R4:
    return "r4";
  case UNW_PPC_R5:
    return "r5";
  case UNW_PPC_R6:
    return "r6";
  case UNW_PPC_R7:
    return "r7";
  case UNW_PPC_R8:
    return "r8";
  case UNW_PPC_R9:
    return "r9";
  case UNW_PPC_R10:
    return "r10";
  case UNW_PPC_R11:
    return "r11";
  case UNW_PPC_R12:
    return "r12";
  case UNW_PPC_R13:
    return "r13";
  case UNW_PPC_R14:
    return "r14";
  case UNW_PPC_R15:
    return "r15";
  case UNW_PPC_R16:
    return "r16";
  case UNW_PPC_R17:
    return "r17";
  case UNW_PPC_R18:
    return "r18";
  case UNW_PPC_R19:
    return "r19";
  case UNW_PPC_R20:
    return "r20";
  case UNW_PPC_R21:
    return "r21";
  case UNW_PPC_R22:
    return "r22";
  case UNW_PPC_R23:
    return "r23";
  case UNW_PPC_R24:
    return "r24";
  case UNW_PPC_R25:
    return "r25";
  case UNW_PPC_R26:
    return "r26";
  case UNW_PPC_R27:
    return "r27";
  case UNW_PPC_R28:
    return "r28";
  case UNW_PPC_R29:
    return "r29";
  case UNW_PPC_R30:
    return "r30";
  case UNW_PPC_R31:
    return "r31";
  case UNW_PPC_F0:
    return "fp0";
  case UNW_PPC_F1:
    return "fp1";
  case UNW_PPC_F2:
    return "fp2";
  case UNW_PPC_F3:
    return "fp3";
  case UNW_PPC_F4:
    return "fp4";
  case UNW_PPC_F5:
    return "fp5";
  case UNW_PPC_F6:
    return "fp6";
  case UNW_PPC_F7:
    return "fp7";
  case UNW_PPC_F8:
    return "fp8";
  case UNW_PPC_F9:
    return "fp9";
  case UNW_PPC_F10:
    return "fp10";
  case UNW_PPC_F11:
    return "fp11";
  case UNW_PPC_F12:
    return "fp12";
  case UNW_PPC_F13:
    return "fp13";
  case UNW_PPC_F14:
    return "fp14";
  case UNW_PPC_F15:
    return "fp15";
  case UNW_PPC_F16:
    return "fp16";
  case UNW_PPC_F17:
    return "fp17";
  case UNW_PPC_F18:
    return "fp18";
  case UNW_PPC_F19:
    return "fp19";
  case UNW_PPC_F20:
    return "fp20";
  case UNW_PPC_F21:
    return "fp21";
  case UNW_PPC_F22:
    return "fp22";
  case UNW_PPC_F23:
    return "fp23";
  case UNW_PPC_F24:
    return "fp24";
  case UNW_PPC_F25:
    return "fp25";
  case UNW_PPC_F26:
    return "fp26";
  case UNW_PPC_F27:
    return "fp27";
  case UNW_PPC_F28:
    return "fp28";
  case UNW_PPC_F29:
    return "fp29";
  case UNW_PPC_F30:
    return "fp30";
  case UNW_PPC_F31:
    return "fp31";
  case UNW_PPC_LR:
    return "lr";
  default:
    return "unknown register";
  }

}
#endif // _LIBUNWIND_TARGET_PPC

#if defined(_LIBUNWIND_TARGET_PPC64)
/// Registers_ppc64 holds the register state of a thread in a 64-bit PowerPC
/// process.
class _LIBUNWIND_HIDDEN Registers_ppc64 {
public:
  Registers_ppc64();
  Registers_ppc64(const void *registers);

  bool        validRegister(int num) const;
  uint64_t    getRegister(int num) const;
  void        setRegister(int num, uint64_t value);
  bool        validFloatRegister(int num) const;
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_PPC64;
  }
  static int  getArch() { return REGISTERS_PPC64; }

  uint64_t  getSP() const         { return _registers.__r1; }
  void      setSP(uint64_t value) { _registers.__r1 = value; }
  uint64_t  getIP() const         { return _registers.__srr0; }
  void      setIP(uint64_t value) { _registers.__srr0 = value; }
  uint64_t  getCR() const         { return _registers.__cr; }
  void      setCR(uint64_t value) { _registers.__cr = value; }
  uint64_t  getLR() const         { return _registers.__lr; }
  void      setLR(uint64_t value) { _registers.__lr = value; }

private:
  struct ppc64_thread_state_t {
    uint64_t __srr0;    // Instruction address register (PC)
    uint64_t __srr1;    // Machine state register (supervisor)
    uint64_t __r0;
    uint64_t __r1;
    uint64_t __r2;
    uint64_t __r3;
    uint64_t __r4;
    uint64_t __r5;
    uint64_t __r6;
    uint64_t __r7;
    uint64_t __r8;
    uint64_t __r9;
    uint64_t __r10;
    uint64_t __r11;
    uint64_t __r12;
    uint64_t __r13;
    uint64_t __r14;
    uint64_t __r15;
    uint64_t __r16;
    uint64_t __r17;
    uint64_t __r18;
    uint64_t __r19;
    uint64_t __r20;
    uint64_t __r21;
    uint64_t __r22;
    uint64_t __r23;
    uint64_t __r24;
    uint64_t __r25;
    uint64_t __r26;
    uint64_t __r27;
    uint64_t __r28;
    uint64_t __r29;
    uint64_t __r30;
    uint64_t __r31;
    uint64_t __cr;      // Condition register
    uint64_t __xer;     // User's integer exception register
    uint64_t __lr;      // Link register
    uint64_t __ctr;     // Count register
    uint64_t __vrsave;  // Vector Save Register
  };

  union ppc64_vsr_t {
    struct asfloat_s {
      double f;
      uint64_t v2;
    } asfloat;
    v128 v;
  };

  ppc64_thread_state_t _registers;
  ppc64_vsr_t          _vectorScalarRegisters[64];

  static int getVectorRegNum(int num);
};

inline Registers_ppc64::Registers_ppc64(const void *registers) {
  static_assert((check_fit<Registers_ppc64, unw_context_t>::does_fit),
                "ppc64 registers do not fit into unw_context_t");
  memcpy(&_registers, static_cast<const uint8_t *>(registers),
         sizeof(_registers));
  static_assert(sizeof(_registers) == 312,
                "expected vector scalar register offset to be 312");
  memcpy(&_vectorScalarRegisters,
         static_cast<const uint8_t *>(registers) + sizeof(_registers),
         sizeof(_vectorScalarRegisters));
  static_assert(sizeof(_registers) +
                sizeof(_vectorScalarRegisters) == 1336,
                "expected vector register offset to be 1336 bytes");
}

inline Registers_ppc64::Registers_ppc64() {
  memset(&_registers, 0, sizeof(_registers));
  memset(&_vectorScalarRegisters, 0, sizeof(_vectorScalarRegisters));
}

inline bool Registers_ppc64::validRegister(int regNum) const {
  switch (regNum) {
  case UNW_REG_IP:
  case UNW_REG_SP:
  case UNW_PPC64_XER:
  case UNW_PPC64_LR:
  case UNW_PPC64_CTR:
  case UNW_PPC64_VRSAVE:
      return true;
  }

  if (regNum >= UNW_PPC64_R0 && regNum <= UNW_PPC64_R31)
    return true;
  if (regNum >= UNW_PPC64_CR0 && regNum <= UNW_PPC64_CR7)
    return true;

  return false;
}

inline uint64_t Registers_ppc64::getRegister(int regNum) const {
  switch (regNum) {
  case UNW_REG_IP:
    return _registers.__srr0;
  case UNW_PPC64_R0:
    return _registers.__r0;
  case UNW_PPC64_R1:
  case UNW_REG_SP:
    return _registers.__r1;
  case UNW_PPC64_R2:
    return _registers.__r2;
  case UNW_PPC64_R3:
    return _registers.__r3;
  case UNW_PPC64_R4:
    return _registers.__r4;
  case UNW_PPC64_R5:
    return _registers.__r5;
  case UNW_PPC64_R6:
    return _registers.__r6;
  case UNW_PPC64_R7:
    return _registers.__r7;
  case UNW_PPC64_R8:
    return _registers.__r8;
  case UNW_PPC64_R9:
    return _registers.__r9;
  case UNW_PPC64_R10:
    return _registers.__r10;
  case UNW_PPC64_R11:
    return _registers.__r11;
  case UNW_PPC64_R12:
    return _registers.__r12;
  case UNW_PPC64_R13:
    return _registers.__r13;
  case UNW_PPC64_R14:
    return _registers.__r14;
  case UNW_PPC64_R15:
    return _registers.__r15;
  case UNW_PPC64_R16:
    return _registers.__r16;
  case UNW_PPC64_R17:
    return _registers.__r17;
  case UNW_PPC64_R18:
    return _registers.__r18;
  case UNW_PPC64_R19:
    return _registers.__r19;
  case UNW_PPC64_R20:
    return _registers.__r20;
  case UNW_PPC64_R21:
    return _registers.__r21;
  case UNW_PPC64_R22:
    return _registers.__r22;
  case UNW_PPC64_R23:
    return _registers.__r23;
  case UNW_PPC64_R24:
    return _registers.__r24;
  case UNW_PPC64_R25:
    return _registers.__r25;
  case UNW_PPC64_R26:
    return _registers.__r26;
  case UNW_PPC64_R27:
    return _registers.__r27;
  case UNW_PPC64_R28:
    return _registers.__r28;
  case UNW_PPC64_R29:
    return _registers.__r29;
  case UNW_PPC64_R30:
    return _registers.__r30;
  case UNW_PPC64_R31:
    return _registers.__r31;
  case UNW_PPC64_CR0:
    return (_registers.__cr & 0xF0000000);
  case UNW_PPC64_CR1:
    return (_registers.__cr & 0x0F000000);
  case UNW_PPC64_CR2:
    return (_registers.__cr & 0x00F00000);
  case UNW_PPC64_CR3:
    return (_registers.__cr & 0x000F0000);
  case UNW_PPC64_CR4:
    return (_registers.__cr & 0x0000F000);
  case UNW_PPC64_CR5:
    return (_registers.__cr & 0x00000F00);
  case UNW_PPC64_CR6:
    return (_registers.__cr & 0x000000F0);
  case UNW_PPC64_CR7:
    return (_registers.__cr & 0x0000000F);
  case UNW_PPC64_XER:
    return _registers.__xer;
  case UNW_PPC64_LR:
    return _registers.__lr;
  case UNW_PPC64_CTR:
    return _registers.__ctr;
  case UNW_PPC64_VRSAVE:
    return _registers.__vrsave;
  }
  _LIBUNWIND_ABORT("unsupported ppc64 register");
}

inline void Registers_ppc64::setRegister(int regNum, uint64_t value) {
  switch (regNum) {
  case UNW_REG_IP:
    _registers.__srr0 = value;
    return;
  case UNW_PPC64_R0:
    _registers.__r0 = value;
    return;
  case UNW_PPC64_R1:
  case UNW_REG_SP:
    _registers.__r1 = value;
    return;
  case UNW_PPC64_R2:
    _registers.__r2 = value;
    return;
  case UNW_PPC64_R3:
    _registers.__r3 = value;
    return;
  case UNW_PPC64_R4:
    _registers.__r4 = value;
    return;
  case UNW_PPC64_R5:
    _registers.__r5 = value;
    return;
  case UNW_PPC64_R6:
    _registers.__r6 = value;
    return;
  case UNW_PPC64_R7:
    _registers.__r7 = value;
    return;
  case UNW_PPC64_R8:
    _registers.__r8 = value;
    return;
  case UNW_PPC64_R9:
    _registers.__r9 = value;
    return;
  case UNW_PPC64_R10:
    _registers.__r10 = value;
    return;
  case UNW_PPC64_R11:
    _registers.__r11 = value;
    return;
  case UNW_PPC64_R12:
    _registers.__r12 = value;
    return;
  case UNW_PPC64_R13:
    _registers.__r13 = value;
    return;
  case UNW_PPC64_R14:
    _registers.__r14 = value;
    return;
  case UNW_PPC64_R15:
    _registers.__r15 = value;
    return;
  case UNW_PPC64_R16:
    _registers.__r16 = value;
    return;
  case UNW_PPC64_R17:
    _registers.__r17 = value;
    return;
  case UNW_PPC64_R18:
    _registers.__r18 = value;
    return;
  case UNW_PPC64_R19:
    _registers.__r19 = value;
    return;
  case UNW_PPC64_R20:
    _registers.__r20 = value;
    return;
  case UNW_PPC64_R21:
    _registers.__r21 = value;
    return;
  case UNW_PPC64_R22:
    _registers.__r22 = value;
    return;
  case UNW_PPC64_R23:
    _registers.__r23 = value;
    return;
  case UNW_PPC64_R24:
    _registers.__r24 = value;
    return;
  case UNW_PPC64_R25:
    _registers.__r25 = value;
    return;
  case UNW_PPC64_R26:
    _registers.__r26 = value;
    return;
  case UNW_PPC64_R27:
    _registers.__r27 = value;
    return;
  case UNW_PPC64_R28:
    _registers.__r28 = value;
    return;
  case UNW_PPC64_R29:
    _registers.__r29 = value;
    return;
  case UNW_PPC64_R30:
    _registers.__r30 = value;
    return;
  case UNW_PPC64_R31:
    _registers.__r31 = value;
    return;
  case UNW_PPC64_CR0:
    _registers.__cr &= 0x0FFFFFFF;
    _registers.__cr |= (value & 0xF0000000);
    return;
  case UNW_PPC64_CR1:
    _registers.__cr &= 0xF0FFFFFF;
    _registers.__cr |= (value & 0x0F000000);
    return;
  case UNW_PPC64_CR2:
    _registers.__cr &= 0xFF0FFFFF;
    _registers.__cr |= (value & 0x00F00000);
    return;
  case UNW_PPC64_CR3:
    _registers.__cr &= 0xFFF0FFFF;
    _registers.__cr |= (value & 0x000F0000);
    return;
  case UNW_PPC64_CR4:
    _registers.__cr &= 0xFFFF0FFF;
    _registers.__cr |= (value & 0x0000F000);
    return;
  case UNW_PPC64_CR5:
    _registers.__cr &= 0xFFFFF0FF;
    _registers.__cr |= (value & 0x00000F00);
    return;
  case UNW_PPC64_CR6:
    _registers.__cr &= 0xFFFFFF0F;
    _registers.__cr |= (value & 0x000000F0);
    return;
  case UNW_PPC64_CR7:
    _registers.__cr &= 0xFFFFFFF0;
    _registers.__cr |= (value & 0x0000000F);
    return;
  case UNW_PPC64_XER:
    _registers.__xer = value;
    return;
  case UNW_PPC64_LR:
    _registers.__lr = value;
    return;
  case UNW_PPC64_CTR:
    _registers.__ctr = value;
    return;
  case UNW_PPC64_VRSAVE:
    _registers.__vrsave = value;
    return;
  }
  _LIBUNWIND_ABORT("unsupported ppc64 register");
}

inline bool Registers_ppc64::validFloatRegister(int regNum) const {
  return regNum >= UNW_PPC64_F0 && regNum <= UNW_PPC64_F31;
}

inline double Registers_ppc64::getFloatRegister(int regNum) const {
  assert(validFloatRegister(regNum));
  return _vectorScalarRegisters[regNum - UNW_PPC64_F0].asfloat.f;
}

inline void Registers_ppc64::setFloatRegister(int regNum, double value) {
  assert(validFloatRegister(regNum));
  _vectorScalarRegisters[regNum - UNW_PPC64_F0].asfloat.f = value;
}

inline bool Registers_ppc64::validVectorRegister(int regNum) const {
#if defined(__VSX__)
  if (regNum >= UNW_PPC64_VS0 && regNum <= UNW_PPC64_VS31)
    return true;
  if (regNum >= UNW_PPC64_VS32 && regNum <= UNW_PPC64_VS63)
    return true;
#elif defined(__ALTIVEC__)
  if (regNum >= UNW_PPC64_V0 && regNum <= UNW_PPC64_V31)
    return true;
#endif
  return false;
}

inline int Registers_ppc64::getVectorRegNum(int num)
{
  if (num >= UNW_PPC64_VS0 && num <= UNW_PPC64_VS31)
    return num - UNW_PPC64_VS0;
  else
    return num - UNW_PPC64_VS32 + 32;
}

inline v128 Registers_ppc64::getVectorRegister(int regNum) const {
  assert(validVectorRegister(regNum));
  return _vectorScalarRegisters[getVectorRegNum(regNum)].v;
}

inline void Registers_ppc64::setVectorRegister(int regNum, v128 value) {
  assert(validVectorRegister(regNum));
  _vectorScalarRegisters[getVectorRegNum(regNum)].v = value;
}

inline const char *Registers_ppc64::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
    return "ip";
  case UNW_REG_SP:
    return "sp";
  case UNW_PPC64_R0:
    return "r0";
  case UNW_PPC64_R1:
    return "r1";
  case UNW_PPC64_R2:
    return "r2";
  case UNW_PPC64_R3:
    return "r3";
  case UNW_PPC64_R4:
    return "r4";
  case UNW_PPC64_R5:
    return "r5";
  case UNW_PPC64_R6:
    return "r6";
  case UNW_PPC64_R7:
    return "r7";
  case UNW_PPC64_R8:
    return "r8";
  case UNW_PPC64_R9:
    return "r9";
  case UNW_PPC64_R10:
    return "r10";
  case UNW_PPC64_R11:
    return "r11";
  case UNW_PPC64_R12:
    return "r12";
  case UNW_PPC64_R13:
    return "r13";
  case UNW_PPC64_R14:
    return "r14";
  case UNW_PPC64_R15:
    return "r15";
  case UNW_PPC64_R16:
    return "r16";
  case UNW_PPC64_R17:
    return "r17";
  case UNW_PPC64_R18:
    return "r18";
  case UNW_PPC64_R19:
    return "r19";
  case UNW_PPC64_R20:
    return "r20";
  case UNW_PPC64_R21:
    return "r21";
  case UNW_PPC64_R22:
    return "r22";
  case UNW_PPC64_R23:
    return "r23";
  case UNW_PPC64_R24:
    return "r24";
  case UNW_PPC64_R25:
    return "r25";
  case UNW_PPC64_R26:
    return "r26";
  case UNW_PPC64_R27:
    return "r27";
  case UNW_PPC64_R28:
    return "r28";
  case UNW_PPC64_R29:
    return "r29";
  case UNW_PPC64_R30:
    return "r30";
  case UNW_PPC64_R31:
    return "r31";
  case UNW_PPC64_CR0:
    return "cr0";
  case UNW_PPC64_CR1:
    return "cr1";
  case UNW_PPC64_CR2:
    return "cr2";
  case UNW_PPC64_CR3:
    return "cr3";
  case UNW_PPC64_CR4:
    return "cr4";
  case UNW_PPC64_CR5:
    return "cr5";
  case UNW_PPC64_CR6:
    return "cr6";
  case UNW_PPC64_CR7:
    return "cr7";
  case UNW_PPC64_XER:
    return "xer";
  case UNW_PPC64_LR:
    return "lr";
  case UNW_PPC64_CTR:
    return "ctr";
  case UNW_PPC64_VRSAVE:
    return "vrsave";
  case UNW_PPC64_F0:
    return "fp0";
  case UNW_PPC64_F1:
    return "fp1";
  case UNW_PPC64_F2:
    return "fp2";
  case UNW_PPC64_F3:
    return "fp3";
  case UNW_PPC64_F4:
    return "fp4";
  case UNW_PPC64_F5:
    return "fp5";
  case UNW_PPC64_F6:
    return "fp6";
  case UNW_PPC64_F7:
    return "fp7";
  case UNW_PPC64_F8:
    return "fp8";
  case UNW_PPC64_F9:
    return "fp9";
  case UNW_PPC64_F10:
    return "fp10";
  case UNW_PPC64_F11:
    return "fp11";
  case UNW_PPC64_F12:
    return "fp12";
  case UNW_PPC64_F13:
    return "fp13";
  case UNW_PPC64_F14:
    return "fp14";
  case UNW_PPC64_F15:
    return "fp15";
  case UNW_PPC64_F16:
    return "fp16";
  case UNW_PPC64_F17:
    return "fp17";
  case UNW_PPC64_F18:
    return "fp18";
  case UNW_PPC64_F19:
    return "fp19";
  case UNW_PPC64_F20:
    return "fp20";
  case UNW_PPC64_F21:
    return "fp21";
  case UNW_PPC64_F22:
    return "fp22";
  case UNW_PPC64_F23:
    return "fp23";
  case UNW_PPC64_F24:
    return "fp24";
  case UNW_PPC64_F25:
    return "fp25";
  case UNW_PPC64_F26:
    return "fp26";
  case UNW_PPC64_F27:
    return "fp27";
  case UNW_PPC64_F28:
    return "fp28";
  case UNW_PPC64_F29:
    return "fp29";
  case UNW_PPC64_F30:
    return "fp30";
  case UNW_PPC64_F31:
    return "fp31";
  case UNW_PPC64_V0:
    return "v0";
  case UNW_PPC64_V1:
    return "v1";
  case UNW_PPC64_V2:
    return "v2";
  case UNW_PPC64_V3:
    return "v3";
  case UNW_PPC64_V4:
    return "v4";
  case UNW_PPC64_V5:
    return "v5";
  case UNW_PPC64_V6:
    return "v6";
  case UNW_PPC64_V7:
    return "v7";
  case UNW_PPC64_V8:
    return "v8";
  case UNW_PPC64_V9:
    return "v9";
  case UNW_PPC64_V10:
    return "v10";
  case UNW_PPC64_V11:
    return "v11";
  case UNW_PPC64_V12:
    return "v12";
  case UNW_PPC64_V13:
    return "v13";
  case UNW_PPC64_V14:
    return "v14";
  case UNW_PPC64_V15:
    return "v15";
  case UNW_PPC64_V16:
    return "v16";
  case UNW_PPC64_V17:
    return "v17";
  case UNW_PPC64_V18:
    return "v18";
  case UNW_PPC64_V19:
    return "v19";
  case UNW_PPC64_V20:
    return "v20";
  case UNW_PPC64_V21:
    return "v21";
  case UNW_PPC64_V22:
    return "v22";
  case UNW_PPC64_V23:
    return "v23";
  case UNW_PPC64_V24:
    return "v24";
  case UNW_PPC64_V25:
    return "v25";
  case UNW_PPC64_V26:
    return "v26";
  case UNW_PPC64_V27:
    return "v27";
  case UNW_PPC64_V28:
    return "v28";
  case UNW_PPC64_V29:
    return "v29";
  case UNW_PPC64_V30:
    return "v30";
  case UNW_PPC64_V31:
    return "v31";
  }
  return "unknown register";
}
#endif // _LIBUNWIND_TARGET_PPC64


#if defined(_LIBUNWIND_TARGET_AARCH64)
/// Registers_arm64  holds the register state of a thread in a 64-bit arm
/// process.
class _LIBUNWIND_HIDDEN Registers_arm64;
extern "C" void __libunwind_Registers_arm64_jumpto(Registers_arm64 *);

#if defined(_LIBUNWIND_USE_GCS)
extern "C" void *__libunwind_cet_get_jump_target() {
  return reinterpret_cast<void *>(&__libunwind_Registers_arm64_jumpto);
}
#endif

class _LIBUNWIND_HIDDEN Registers_arm64 {
public:
  Registers_arm64();
  Registers_arm64(const void *registers);

  bool        validRegister(int num) const;
  uint64_t    getRegister(int num) const;
  void        setRegister(int num, uint64_t value);
  bool        validFloatRegister(int num) const;
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto() { __libunwind_Registers_arm64_jumpto(this); }
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_ARM64;
  }
  static int  getArch() { return REGISTERS_ARM64; }

  uint64_t  getSP() const         { return _registers.__sp; }
  void      setSP(uint64_t value) { _registers.__sp = value; }
  uint64_t  getIP() const         { return _registers.__pc; }
  void      setIP(uint64_t value) { _registers.__pc = value; }
  uint64_t  getFP() const         { return _registers.__fp; }
  void      setFP(uint64_t value) { _registers.__fp = value; }

private:
  struct GPRs {
    uint64_t __x[29]; // x0-x28
    uint64_t __fp;    // Frame pointer x29
    uint64_t __lr;    // Link register x30
    uint64_t __sp;    // Stack pointer x31
    uint64_t __pc;    // Program counter
    uint64_t __ra_sign_state; // RA sign state register
  };

  GPRs    _registers;
  double  _vectorHalfRegisters[32];
  // Currently only the lower double in 128-bit vectore registers
  // is perserved during unwinding.  We could define new register
  // numbers (> 96) which mean whole vector registers, then this
  // struct would need to change to contain whole vector registers.
};

inline Registers_arm64::Registers_arm64(const void *registers) {
  static_assert((check_fit<Registers_arm64, unw_context_t>::does_fit),
                "arm64 registers do not fit into unw_context_t");
  memcpy(&_registers, registers, sizeof(_registers));
  static_assert(sizeof(GPRs) == 0x110,
                "expected VFP registers to be at offset 272");
  memcpy(_vectorHalfRegisters,
         static_cast<const uint8_t *>(registers) + sizeof(GPRs),
         sizeof(_vectorHalfRegisters));
}

inline Registers_arm64::Registers_arm64() {
  memset(&_registers, 0, sizeof(_registers));
  memset(&_vectorHalfRegisters, 0, sizeof(_vectorHalfRegisters));
}

inline bool Registers_arm64::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return true;
  if (regNum == UNW_REG_SP)
    return true;
  if (regNum < 0)
    return false;
  if (regNum > 95)
    return false;
  if (regNum == UNW_AARCH64_RA_SIGN_STATE)
    return true;
  if ((regNum > 32) && (regNum < 64))
    return false;
  return true;
}

inline uint64_t Registers_arm64::getRegister(int regNum) const {
  if (regNum == UNW_REG_IP || regNum == UNW_AARCH64_PC)
    return _registers.__pc;
  if (regNum == UNW_REG_SP || regNum == UNW_AARCH64_SP)
    return _registers.__sp;
  if (regNum == UNW_AARCH64_RA_SIGN_STATE)
    return _registers.__ra_sign_state;
  if (regNum == UNW_AARCH64_FP)
    return _registers.__fp;
  if (regNum == UNW_AARCH64_LR)
    return _registers.__lr;
  if ((regNum >= 0) && (regNum < 29))
    return _registers.__x[regNum];
  _LIBUNWIND_ABORT("unsupported arm64 register");
}

inline void Registers_arm64::setRegister(int regNum, uint64_t value) {
  if (regNum == UNW_REG_IP || regNum == UNW_AARCH64_PC)
    _registers.__pc = value;
  else if (regNum == UNW_REG_SP || regNum == UNW_AARCH64_SP)
    _registers.__sp = value;
  else if (regNum == UNW_AARCH64_RA_SIGN_STATE)
    _registers.__ra_sign_state = value;
  else if (regNum == UNW_AARCH64_FP)
    _registers.__fp = value;
  else if (regNum == UNW_AARCH64_LR)
    _registers.__lr = value;
  else if ((regNum >= 0) && (regNum < 29))
    _registers.__x[regNum] = value;
  else
    _LIBUNWIND_ABORT("unsupported arm64 register");
}

inline const char *Registers_arm64::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
    return "pc";
  case UNW_REG_SP:
    return "sp";
  case UNW_AARCH64_X0:
    return "x0";
  case UNW_AARCH64_X1:
    return "x1";
  case UNW_AARCH64_X2:
    return "x2";
  case UNW_AARCH64_X3:
    return "x3";
  case UNW_AARCH64_X4:
    return "x4";
  case UNW_AARCH64_X5:
    return "x5";
  case UNW_AARCH64_X6:
    return "x6";
  case UNW_AARCH64_X7:
    return "x7";
  case UNW_AARCH64_X8:
    return "x8";
  case UNW_AARCH64_X9:
    return "x9";
  case UNW_AARCH64_X10:
    return "x10";
  case UNW_AARCH64_X11:
    return "x11";
  case UNW_AARCH64_X12:
    return "x12";
  case UNW_AARCH64_X13:
    return "x13";
  case UNW_AARCH64_X14:
    return "x14";
  case UNW_AARCH64_X15:
    return "x15";
  case UNW_AARCH64_X16:
    return "x16";
  case UNW_AARCH64_X17:
    return "x17";
  case UNW_AARCH64_X18:
    return "x18";
  case UNW_AARCH64_X19:
    return "x19";
  case UNW_AARCH64_X20:
    return "x20";
  case UNW_AARCH64_X21:
    return "x21";
  case UNW_AARCH64_X22:
    return "x22";
  case UNW_AARCH64_X23:
    return "x23";
  case UNW_AARCH64_X24:
    return "x24";
  case UNW_AARCH64_X25:
    return "x25";
  case UNW_AARCH64_X26:
    return "x26";
  case UNW_AARCH64_X27:
    return "x27";
  case UNW_AARCH64_X28:
    return "x28";
  case UNW_AARCH64_FP:
    return "fp";
  case UNW_AARCH64_LR:
    return "lr";
  case UNW_AARCH64_SP:
    return "sp";
  case UNW_AARCH64_PC:
    return "pc";
  case UNW_AARCH64_V0:
    return "d0";
  case UNW_AARCH64_V1:
    return "d1";
  case UNW_AARCH64_V2:
    return "d2";
  case UNW_AARCH64_V3:
    return "d3";
  case UNW_AARCH64_V4:
    return "d4";
  case UNW_AARCH64_V5:
    return "d5";
  case UNW_AARCH64_V6:
    return "d6";
  case UNW_AARCH64_V7:
    return "d7";
  case UNW_AARCH64_V8:
    return "d8";
  case UNW_AARCH64_V9:
    return "d9";
  case UNW_AARCH64_V10:
    return "d10";
  case UNW_AARCH64_V11:
    return "d11";
  case UNW_AARCH64_V12:
    return "d12";
  case UNW_AARCH64_V13:
    return "d13";
  case UNW_AARCH64_V14:
    return "d14";
  case UNW_AARCH64_V15:
    return "d15";
  case UNW_AARCH64_V16:
    return "d16";
  case UNW_AARCH64_V17:
    return "d17";
  case UNW_AARCH64_V18:
    return "d18";
  case UNW_AARCH64_V19:
    return "d19";
  case UNW_AARCH64_V20:
    return "d20";
  case UNW_AARCH64_V21:
    return "d21";
  case UNW_AARCH64_V22:
    return "d22";
  case UNW_AARCH64_V23:
    return "d23";
  case UNW_AARCH64_V24:
    return "d24";
  case UNW_AARCH64_V25:
    return "d25";
  case UNW_AARCH64_V26:
    return "d26";
  case UNW_AARCH64_V27:
    return "d27";
  case UNW_AARCH64_V28:
    return "d28";
  case UNW_AARCH64_V29:
    return "d29";
  case UNW_AARCH64_V30:
    return "d30";
  case UNW_AARCH64_V31:
    return "d31";
  default:
    return "unknown register";
  }
}

inline bool Registers_arm64::validFloatRegister(int regNum) const {
  if (regNum < UNW_AARCH64_V0)
    return false;
  if (regNum > UNW_AARCH64_V31)
    return false;
  return true;
}

inline double Registers_arm64::getFloatRegister(int regNum) const {
  assert(validFloatRegister(regNum));
  return _vectorHalfRegisters[regNum - UNW_AARCH64_V0];
}

inline void Registers_arm64::setFloatRegister(int regNum, double value) {
  assert(validFloatRegister(regNum));
  _vectorHalfRegisters[regNum - UNW_AARCH64_V0] = value;
}

inline bool Registers_arm64::validVectorRegister(int) const {
  return false;
}

inline v128 Registers_arm64::getVectorRegister(int) const {
  _LIBUNWIND_ABORT("no arm64 vector register support yet");
}

inline void Registers_arm64::setVectorRegister(int, v128) {
  _LIBUNWIND_ABORT("no arm64 vector register support yet");
}
#endif // _LIBUNWIND_TARGET_AARCH64

#if defined(_LIBUNWIND_TARGET_ARM)
/// Registers_arm holds the register state of a thread in a 32-bit arm
/// process.
///
/// NOTE: Assumes VFPv3. On ARM processors without a floating point unit,
/// this uses more memory than required.
class _LIBUNWIND_HIDDEN Registers_arm {
public:
  Registers_arm();
  Registers_arm(const void *registers);

  bool        validRegister(int num) const;
  uint32_t    getRegister(int num) const;
  void        setRegister(int num, uint32_t value);
  bool        validFloatRegister(int num) const;
  unw_fpreg_t getFloatRegister(int num);
  void        setFloatRegister(int num, unw_fpreg_t value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto() {
    restoreSavedFloatRegisters();
    restoreCoreAndJumpTo();
  }
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_ARM;
  }
  static int  getArch() { return REGISTERS_ARM; }

  uint32_t  getSP() const         { return _registers.__sp; }
  void      setSP(uint32_t value) { _registers.__sp = value; }
  uint32_t  getIP() const         { return _registers.__pc; }
  void      setIP(uint32_t value) { _registers.__pc = value; }

  void saveVFPAsX() {
    assert(_use_X_for_vfp_save || !_saved_vfp_d0_d15);
    _use_X_for_vfp_save = true;
  }

  void restoreSavedFloatRegisters() {
    if (_saved_vfp_d0_d15) {
      if (_use_X_for_vfp_save)
        restoreVFPWithFLDMX(_vfp_d0_d15_pad);
      else
        restoreVFPWithFLDMD(_vfp_d0_d15_pad);
    }
    if (_saved_vfp_d16_d31)
      restoreVFPv3(_vfp_d16_d31);
#if defined(__ARM_WMMX)
    if (_saved_iwmmx)
      restoreiWMMX(_iwmmx);
    if (_saved_iwmmx_control)
      restoreiWMMXControl(_iwmmx_control);
#endif
  }

private:
  struct GPRs {
    uint32_t __r[13]; // r0-r12
    uint32_t __sp;    // Stack pointer r13
    uint32_t __lr;    // Link register r14
    uint32_t __pc;    // Program counter r15
  };

  struct PseudoRegisters {
    uint32_t __pac; // Return Authentication Code (PAC)
  };

  static void saveVFPWithFSTMD(void*);
  static void saveVFPWithFSTMX(void*);
  static void saveVFPv3(void*);
  static void restoreVFPWithFLDMD(void*);
  static void restoreVFPWithFLDMX(void*);
  static void restoreVFPv3(void*);
#if defined(__ARM_WMMX)
  static void saveiWMMX(void*);
  static void saveiWMMXControl(uint32_t*);
  static void restoreiWMMX(void*);
  static void restoreiWMMXControl(uint32_t*);
#endif
  void restoreCoreAndJumpTo();

  // ARM registers
  GPRs _registers;
  PseudoRegisters _pseudo_registers;

  // We save floating point registers lazily because we can't know ahead of
  // time which ones are used. See EHABI #4.7.

  // Whether D0-D15 are saved in the FTSMX instead of FSTMD format.
  //
  // See EHABI #7.5 that explains how matching instruction sequences for load
  // and store need to be used to correctly restore the exact register bits.
  bool _use_X_for_vfp_save;
  // Whether VFP D0-D15 are saved.
  bool _saved_vfp_d0_d15;
  // Whether VFPv3 D16-D31 are saved.
  bool _saved_vfp_d16_d31;
  // VFP registers D0-D15, + padding if saved using FSTMX
  unw_fpreg_t _vfp_d0_d15_pad[17];
  // VFPv3 registers D16-D31, always saved using FSTMD
  unw_fpreg_t _vfp_d16_d31[16];
#if defined(__ARM_WMMX)
  // Whether iWMMX data registers are saved.
  bool _saved_iwmmx;
  // Whether iWMMX control registers are saved.
  mutable bool _saved_iwmmx_control;
  // iWMMX registers
  unw_fpreg_t _iwmmx[16];
  // iWMMX control registers
  mutable uint32_t _iwmmx_control[4];
#endif
};

inline Registers_arm::Registers_arm(const void *registers)
  : _use_X_for_vfp_save(false),
    _saved_vfp_d0_d15(false),
    _saved_vfp_d16_d31(false) {
  static_assert((check_fit<Registers_arm, unw_context_t>::does_fit),
                "arm registers do not fit into unw_context_t");
  // See __unw_getcontext() note about data.
  memcpy(&_registers, registers, sizeof(_registers));
  memset(&_pseudo_registers, 0, sizeof(_pseudo_registers));
  memset(&_vfp_d0_d15_pad, 0, sizeof(_vfp_d0_d15_pad));
  memset(&_vfp_d16_d31, 0, sizeof(_vfp_d16_d31));
#if defined(__ARM_WMMX)
  _saved_iwmmx = false;
  _saved_iwmmx_control = false;
  memset(&_iwmmx, 0, sizeof(_iwmmx));
  memset(&_iwmmx_control, 0, sizeof(_iwmmx_control));
#endif
}

inline Registers_arm::Registers_arm()
  : _use_X_for_vfp_save(false),
    _saved_vfp_d0_d15(false),
    _saved_vfp_d16_d31(false) {
  memset(&_registers, 0, sizeof(_registers));
  memset(&_pseudo_registers, 0, sizeof(_pseudo_registers));
  memset(&_vfp_d0_d15_pad, 0, sizeof(_vfp_d0_d15_pad));
  memset(&_vfp_d16_d31, 0, sizeof(_vfp_d16_d31));
#if defined(__ARM_WMMX)
  _saved_iwmmx = false;
  _saved_iwmmx_control = false;
  memset(&_iwmmx, 0, sizeof(_iwmmx));
  memset(&_iwmmx_control, 0, sizeof(_iwmmx_control));
#endif
}

inline bool Registers_arm::validRegister(int regNum) const {
  // Returns true for all non-VFP registers supported by the EHABI
  // virtual register set (VRS).
  if (regNum == UNW_REG_IP)
    return true;

  if (regNum == UNW_REG_SP)
    return true;

  if (regNum >= UNW_ARM_R0 && regNum <= UNW_ARM_R15)
    return true;

#if defined(__ARM_WMMX)
  if (regNum >= UNW_ARM_WC0 && regNum <= UNW_ARM_WC3)
    return true;
#endif

#ifdef __ARM_FEATURE_PAUTH
  if (regNum == UNW_ARM_RA_AUTH_CODE)
    return true;
#endif

  return false;
}

inline uint32_t Registers_arm::getRegister(int regNum) const {
  if (regNum == UNW_REG_SP || regNum == UNW_ARM_SP)
    return _registers.__sp;

  if (regNum == UNW_ARM_LR)
    return _registers.__lr;

  if (regNum == UNW_REG_IP || regNum == UNW_ARM_IP)
    return _registers.__pc;

  if (regNum >= UNW_ARM_R0 && regNum <= UNW_ARM_R12)
    return _registers.__r[regNum];

#if defined(__ARM_WMMX)
  if (regNum >= UNW_ARM_WC0 && regNum <= UNW_ARM_WC3) {
    if (!_saved_iwmmx_control) {
      _saved_iwmmx_control = true;
      saveiWMMXControl(_iwmmx_control);
    }
    return _iwmmx_control[regNum - UNW_ARM_WC0];
  }
#endif

#ifdef __ARM_FEATURE_PAUTH
  if (regNum == UNW_ARM_RA_AUTH_CODE)
    return _pseudo_registers.__pac;
#endif

  _LIBUNWIND_ABORT("unsupported arm register");
}

inline void Registers_arm::setRegister(int regNum, uint32_t value) {
  if (regNum == UNW_REG_SP || regNum == UNW_ARM_SP) {
    _registers.__sp = value;
    return;
  }

  if (regNum == UNW_ARM_LR) {
    _registers.__lr = value;
    return;
  }

  if (regNum == UNW_REG_IP || regNum == UNW_ARM_IP) {
    _registers.__pc = value;
    return;
  }

  if (regNum >= UNW_ARM_R0 && regNum <= UNW_ARM_R12) {
    _registers.__r[regNum] = value;
    return;
  }

#if defined(__ARM_WMMX)
  if (regNum >= UNW_ARM_WC0 && regNum <= UNW_ARM_WC3) {
    if (!_saved_iwmmx_control) {
      _saved_iwmmx_control = true;
      saveiWMMXControl(_iwmmx_control);
    }
    _iwmmx_control[regNum - UNW_ARM_WC0] = value;
    return;
  }
#endif

  if (regNum == UNW_ARM_RA_AUTH_CODE) {
    _pseudo_registers.__pac = value;
    return;
  }

  _LIBUNWIND_ABORT("unsupported arm register");
}

inline const char *Registers_arm::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
  case UNW_ARM_IP: // UNW_ARM_R15 is alias
    return "pc";
  case UNW_ARM_LR: // UNW_ARM_R14 is alias
    return "lr";
  case UNW_REG_SP:
  case UNW_ARM_SP: // UNW_ARM_R13 is alias
    return "sp";
  case UNW_ARM_R0:
    return "r0";
  case UNW_ARM_R1:
    return "r1";
  case UNW_ARM_R2:
    return "r2";
  case UNW_ARM_R3:
    return "r3";
  case UNW_ARM_R4:
    return "r4";
  case UNW_ARM_R5:
    return "r5";
  case UNW_ARM_R6:
    return "r6";
  case UNW_ARM_R7:
    return "r7";
  case UNW_ARM_R8:
    return "r8";
  case UNW_ARM_R9:
    return "r9";
  case UNW_ARM_R10:
    return "r10";
  case UNW_ARM_R11:
    return "r11";
  case UNW_ARM_R12:
    return "r12";
  case UNW_ARM_S0:
    return "s0";
  case UNW_ARM_S1:
    return "s1";
  case UNW_ARM_S2:
    return "s2";
  case UNW_ARM_S3:
    return "s3";
  case UNW_ARM_S4:
    return "s4";
  case UNW_ARM_S5:
    return "s5";
  case UNW_ARM_S6:
    return "s6";
  case UNW_ARM_S7:
    return "s7";
  case UNW_ARM_S8:
    return "s8";
  case UNW_ARM_S9:
    return "s9";
  case UNW_ARM_S10:
    return "s10";
  case UNW_ARM_S11:
    return "s11";
  case UNW_ARM_S12:
    return "s12";
  case UNW_ARM_S13:
    return "s13";
  case UNW_ARM_S14:
    return "s14";
  case UNW_ARM_S15:
    return "s15";
  case UNW_ARM_S16:
    return "s16";
  case UNW_ARM_S17:
    return "s17";
  case UNW_ARM_S18:
    return "s18";
  case UNW_ARM_S19:
    return "s19";
  case UNW_ARM_S20:
    return "s20";
  case UNW_ARM_S21:
    return "s21";
  case UNW_ARM_S22:
    return "s22";
  case UNW_ARM_S23:
    return "s23";
  case UNW_ARM_S24:
    return "s24";
  case UNW_ARM_S25:
    return "s25";
  case UNW_ARM_S26:
    return "s26";
  case UNW_ARM_S27:
    return "s27";
  case UNW_ARM_S28:
    return "s28";
  case UNW_ARM_S29:
    return "s29";
  case UNW_ARM_S30:
    return "s30";
  case UNW_ARM_S31:
    return "s31";
  case UNW_ARM_D0:
    return "d0";
  case UNW_ARM_D1:
    return "d1";
  case UNW_ARM_D2:
    return "d2";
  case UNW_ARM_D3:
    return "d3";
  case UNW_ARM_D4:
    return "d4";
  case UNW_ARM_D5:
    return "d5";
  case UNW_ARM_D6:
    return "d6";
  case UNW_ARM_D7:
    return "d7";
  case UNW_ARM_D8:
    return "d8";
  case UNW_ARM_D9:
    return "d9";
  case UNW_ARM_D10:
    return "d10";
  case UNW_ARM_D11:
    return "d11";
  case UNW_ARM_D12:
    return "d12";
  case UNW_ARM_D13:
    return "d13";
  case UNW_ARM_D14:
    return "d14";
  case UNW_ARM_D15:
    return "d15";
  case UNW_ARM_D16:
    return "d16";
  case UNW_ARM_D17:
    return "d17";
  case UNW_ARM_D18:
    return "d18";
  case UNW_ARM_D19:
    return "d19";
  case UNW_ARM_D20:
    return "d20";
  case UNW_ARM_D21:
    return "d21";
  case UNW_ARM_D22:
    return "d22";
  case UNW_ARM_D23:
    return "d23";
  case UNW_ARM_D24:
    return "d24";
  case UNW_ARM_D25:
    return "d25";
  case UNW_ARM_D26:
    return "d26";
  case UNW_ARM_D27:
    return "d27";
  case UNW_ARM_D28:
    return "d28";
  case UNW_ARM_D29:
    return "d29";
  case UNW_ARM_D30:
    return "d30";
  case UNW_ARM_D31:
    return "d31";
  default:
    return "unknown register";
  }
}

inline bool Registers_arm::validFloatRegister(int regNum) const {
  // NOTE: Consider the intel MMX registers floating points so the
  // __unw_get_fpreg can be used to transmit the 64-bit data back.
  return ((regNum >= UNW_ARM_D0) && (regNum <= UNW_ARM_D31))
#if defined(__ARM_WMMX)
      || ((regNum >= UNW_ARM_WR0) && (regNum <= UNW_ARM_WR15))
#endif
      ;
}

inline unw_fpreg_t Registers_arm::getFloatRegister(int regNum) {
  if (regNum >= UNW_ARM_D0 && regNum <= UNW_ARM_D15) {
    if (!_saved_vfp_d0_d15) {
      _saved_vfp_d0_d15 = true;
      if (_use_X_for_vfp_save)
        saveVFPWithFSTMX(_vfp_d0_d15_pad);
      else
        saveVFPWithFSTMD(_vfp_d0_d15_pad);
    }
    return _vfp_d0_d15_pad[regNum - UNW_ARM_D0];
  }

  if (regNum >= UNW_ARM_D16 && regNum <= UNW_ARM_D31) {
    if (!_saved_vfp_d16_d31) {
      _saved_vfp_d16_d31 = true;
      saveVFPv3(_vfp_d16_d31);
    }
    return _vfp_d16_d31[regNum - UNW_ARM_D16];
  }

#if defined(__ARM_WMMX)
  if (regNum >= UNW_ARM_WR0 && regNum <= UNW_ARM_WR15) {
    if (!_saved_iwmmx) {
      _saved_iwmmx = true;
      saveiWMMX(_iwmmx);
    }
    return _iwmmx[regNum - UNW_ARM_WR0];
  }
#endif

  _LIBUNWIND_ABORT("Unknown ARM float register");
}

inline void Registers_arm::setFloatRegister(int regNum, unw_fpreg_t value) {
  if (regNum >= UNW_ARM_D0 && regNum <= UNW_ARM_D15) {
    if (!_saved_vfp_d0_d15) {
      _saved_vfp_d0_d15 = true;
      if (_use_X_for_vfp_save)
        saveVFPWithFSTMX(_vfp_d0_d15_pad);
      else
        saveVFPWithFSTMD(_vfp_d0_d15_pad);
    }
    _vfp_d0_d15_pad[regNum - UNW_ARM_D0] = value;
    return;
  }

  if (regNum >= UNW_ARM_D16 && regNum <= UNW_ARM_D31) {
    if (!_saved_vfp_d16_d31) {
      _saved_vfp_d16_d31 = true;
      saveVFPv3(_vfp_d16_d31);
    }
    _vfp_d16_d31[regNum - UNW_ARM_D16] = value;
    return;
  }

#if defined(__ARM_WMMX)
  if (regNum >= UNW_ARM_WR0 && regNum <= UNW_ARM_WR15) {
    if (!_saved_iwmmx) {
      _saved_iwmmx = true;
      saveiWMMX(_iwmmx);
    }
    _iwmmx[regNum - UNW_ARM_WR0] = value;
    return;
  }
#endif

  _LIBUNWIND_ABORT("Unknown ARM float register");
}

inline bool Registers_arm::validVectorRegister(int) const {
  return false;
}

inline v128 Registers_arm::getVectorRegister(int) const {
  _LIBUNWIND_ABORT("ARM vector support not implemented");
}

inline void Registers_arm::setVectorRegister(int, v128) {
  _LIBUNWIND_ABORT("ARM vector support not implemented");
}
#endif // _LIBUNWIND_TARGET_ARM


#if defined(_LIBUNWIND_TARGET_OR1K)
/// Registers_or1k holds the register state of a thread in an OpenRISC1000
/// process.
class _LIBUNWIND_HIDDEN Registers_or1k {
public:
  Registers_or1k();
  Registers_or1k(const void *registers);

  bool        validRegister(int num) const;
  uint32_t    getRegister(int num) const;
  void        setRegister(int num, uint32_t value);
  bool        validFloatRegister(int num) const;
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_OR1K;
  }
  static int  getArch() { return REGISTERS_OR1K; }

  uint64_t  getSP() const         { return _registers.__r[1]; }
  void      setSP(uint32_t value) { _registers.__r[1] = value; }
  uint64_t  getIP() const         { return _registers.__pc; }
  void      setIP(uint32_t value) { _registers.__pc = value; }

private:
  struct or1k_thread_state_t {
    unsigned int __r[32]; // r0-r31
    unsigned int __pc;    // Program counter
    unsigned int __epcr;  // Program counter at exception
  };

  or1k_thread_state_t _registers;
};

inline Registers_or1k::Registers_or1k(const void *registers) {
  static_assert((check_fit<Registers_or1k, unw_context_t>::does_fit),
                "or1k registers do not fit into unw_context_t");
  memcpy(&_registers, static_cast<const uint8_t *>(registers),
         sizeof(_registers));
}

inline Registers_or1k::Registers_or1k() {
  memset(&_registers, 0, sizeof(_registers));
}

inline bool Registers_or1k::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return true;
  if (regNum == UNW_REG_SP)
    return true;
  if (regNum < 0)
    return false;
  if (regNum <= UNW_OR1K_R31)
    return true;
  if (regNum == UNW_OR1K_EPCR)
    return true;
  return false;
}

inline uint32_t Registers_or1k::getRegister(int regNum) const {
  if (regNum >= UNW_OR1K_R0 && regNum <= UNW_OR1K_R31)
    return _registers.__r[regNum - UNW_OR1K_R0];

  switch (regNum) {
  case UNW_REG_IP:
    return _registers.__pc;
  case UNW_REG_SP:
    return _registers.__r[1];
  case UNW_OR1K_EPCR:
    return _registers.__epcr;
  }
  _LIBUNWIND_ABORT("unsupported or1k register");
}

inline void Registers_or1k::setRegister(int regNum, uint32_t value) {
  if (regNum >= UNW_OR1K_R0 && regNum <= UNW_OR1K_R31) {
    _registers.__r[regNum - UNW_OR1K_R0] = value;
    return;
  }

  switch (regNum) {
  case UNW_REG_IP:
    _registers.__pc = value;
    return;
  case UNW_REG_SP:
    _registers.__r[1] = value;
    return;
  case UNW_OR1K_EPCR:
    _registers.__epcr = value;
    return;
  }
  _LIBUNWIND_ABORT("unsupported or1k register");
}

inline bool Registers_or1k::validFloatRegister(int /* regNum */) const {
  return false;
}

inline double Registers_or1k::getFloatRegister(int /* regNum */) const {
  _LIBUNWIND_ABORT("or1k float support not implemented");
}

inline void Registers_or1k::setFloatRegister(int /* regNum */,
                                             double /* value */) {
  _LIBUNWIND_ABORT("or1k float support not implemented");
}

inline bool Registers_or1k::validVectorRegister(int /* regNum */) const {
  return false;
}

inline v128 Registers_or1k::getVectorRegister(int /* regNum */) const {
  _LIBUNWIND_ABORT("or1k vector support not implemented");
}

inline void Registers_or1k::setVectorRegister(int /* regNum */, v128 /* value */) {
  _LIBUNWIND_ABORT("or1k vector support not implemented");
}

inline const char *Registers_or1k::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_OR1K_R0:
    return "r0";
  case UNW_OR1K_R1:
    return "r1";
  case UNW_OR1K_R2:
    return "r2";
  case UNW_OR1K_R3:
    return "r3";
  case UNW_OR1K_R4:
    return "r4";
  case UNW_OR1K_R5:
    return "r5";
  case UNW_OR1K_R6:
    return "r6";
  case UNW_OR1K_R7:
    return "r7";
  case UNW_OR1K_R8:
    return "r8";
  case UNW_OR1K_R9:
    return "r9";
  case UNW_OR1K_R10:
    return "r10";
  case UNW_OR1K_R11:
    return "r11";
  case UNW_OR1K_R12:
    return "r12";
  case UNW_OR1K_R13:
    return "r13";
  case UNW_OR1K_R14:
    return "r14";
  case UNW_OR1K_R15:
    return "r15";
  case UNW_OR1K_R16:
    return "r16";
  case UNW_OR1K_R17:
    return "r17";
  case UNW_OR1K_R18:
    return "r18";
  case UNW_OR1K_R19:
    return "r19";
  case UNW_OR1K_R20:
    return "r20";
  case UNW_OR1K_R21:
    return "r21";
  case UNW_OR1K_R22:
    return "r22";
  case UNW_OR1K_R23:
    return "r23";
  case UNW_OR1K_R24:
    return "r24";
  case UNW_OR1K_R25:
    return "r25";
  case UNW_OR1K_R26:
    return "r26";
  case UNW_OR1K_R27:
    return "r27";
  case UNW_OR1K_R28:
    return "r28";
  case UNW_OR1K_R29:
    return "r29";
  case UNW_OR1K_R30:
    return "r30";
  case UNW_OR1K_R31:
    return "r31";
  case UNW_OR1K_EPCR:
    return "EPCR";
  default:
    return "unknown register";
  }

}
#endif // _LIBUNWIND_TARGET_OR1K

#if defined(_LIBUNWIND_TARGET_MIPS_O32)
/// Registers_mips_o32 holds the register state of a thread in a 32-bit MIPS
/// process.
class _LIBUNWIND_HIDDEN Registers_mips_o32 {
public:
  Registers_mips_o32();
  Registers_mips_o32(const void *registers);

  bool        validRegister(int num) const;
  uint32_t    getRegister(int num) const;
  void        setRegister(int num, uint32_t value);
  bool        validFloatRegister(int num) const;
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_MIPS;
  }
  static int  getArch() { return REGISTERS_MIPS_O32; }

  uint32_t  getSP() const         { return _registers.__r[29]; }
  void      setSP(uint32_t value) { _registers.__r[29] = value; }
  uint32_t  getIP() const         { return _registers.__pc; }
  void      setIP(uint32_t value) { _registers.__pc = value; }

private:
  struct mips_o32_thread_state_t {
    uint32_t __r[32];
    uint32_t __pc;
    uint32_t __hi;
    uint32_t __lo;
  };

  mips_o32_thread_state_t _registers;
#ifdef __mips_hard_float
  /// O32 with 32-bit floating point registers only uses half of this
  /// space.  However, using the same layout for 32-bit vs 64-bit
  /// floating point registers results in a single context size for
  /// O32 with hard float.
  uint32_t _padding;
  double _floats[32];
#endif
};

inline Registers_mips_o32::Registers_mips_o32(const void *registers) {
  static_assert((check_fit<Registers_mips_o32, unw_context_t>::does_fit),
                "mips_o32 registers do not fit into unw_context_t");
  memcpy(&_registers, static_cast<const uint8_t *>(registers),
         sizeof(_registers));
}

inline Registers_mips_o32::Registers_mips_o32() {
  memset(&_registers, 0, sizeof(_registers));
}

inline bool Registers_mips_o32::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return true;
  if (regNum == UNW_REG_SP)
    return true;
  if (regNum < 0)
    return false;
  if (regNum <= UNW_MIPS_R31)
    return true;
#if __mips_isa_rev < 6
  if (regNum == UNW_MIPS_HI)
    return true;
  if (regNum == UNW_MIPS_LO)
    return true;
#endif
#if defined(__mips_hard_float) && __mips_fpr == 32
  if (regNum >= UNW_MIPS_F0 && regNum <= UNW_MIPS_F31)
    return true;
#endif
  // FIXME: DSP accumulator registers, MSA registers
  return false;
}

inline uint32_t Registers_mips_o32::getRegister(int regNum) const {
  if (regNum >= UNW_MIPS_R0 && regNum <= UNW_MIPS_R31)
    return _registers.__r[regNum - UNW_MIPS_R0];
#if defined(__mips_hard_float) && __mips_fpr == 32
  if (regNum >= UNW_MIPS_F0 && regNum <= UNW_MIPS_F31) {
    uint32_t *p;

    if (regNum % 2 == 0)
      p = (uint32_t *)&_floats[regNum - UNW_MIPS_F0];
    else
      p = (uint32_t *)&_floats[(regNum - 1) - UNW_MIPS_F0] + 1;
    return *p;
  }
#endif

  switch (regNum) {
  case UNW_REG_IP:
    return _registers.__pc;
  case UNW_REG_SP:
    return _registers.__r[29];
#if __mips_isa_rev < 6
  case UNW_MIPS_HI:
    return _registers.__hi;
  case UNW_MIPS_LO:
    return _registers.__lo;
#endif
  }
  _LIBUNWIND_ABORT("unsupported mips_o32 register");
}

inline void Registers_mips_o32::setRegister(int regNum, uint32_t value) {
  if (regNum >= UNW_MIPS_R0 && regNum <= UNW_MIPS_R31) {
    _registers.__r[regNum - UNW_MIPS_R0] = value;
    return;
  }
#if defined(__mips_hard_float) && __mips_fpr == 32
  if (regNum >= UNW_MIPS_F0 && regNum <= UNW_MIPS_F31) {
    uint32_t *p;

    if (regNum % 2 == 0)
      p = (uint32_t *)&_floats[regNum - UNW_MIPS_F0];
    else
      p = (uint32_t *)&_floats[(regNum - 1) - UNW_MIPS_F0] + 1;
    *p = value;
    return;
  }
#endif

  switch (regNum) {
  case UNW_REG_IP:
    _registers.__pc = value;
    return;
  case UNW_REG_SP:
    _registers.__r[29] = value;
    return;
#if __mips_isa_rev < 6
  case UNW_MIPS_HI:
    _registers.__hi = value;
    return;
  case UNW_MIPS_LO:
    _registers.__lo = value;
#endif
    return;
  }
  _LIBUNWIND_ABORT("unsupported mips_o32 register");
}

inline bool Registers_mips_o32::validFloatRegister(int regNum) const {
#if defined(__mips_hard_float) && __mips_fpr == 64
  if (regNum >= UNW_MIPS_F0 && regNum <= UNW_MIPS_F31)
    return true;
#else
  (void)regNum;
#endif
  return false;
}

inline double Registers_mips_o32::getFloatRegister(int regNum) const {
#if defined(__mips_hard_float) && __mips_fpr == 64
  assert(validFloatRegister(regNum));
  return _floats[regNum - UNW_MIPS_F0];
#else
  (void)regNum;
  _LIBUNWIND_ABORT("mips_o32 float support not implemented");
#endif
}

inline void Registers_mips_o32::setFloatRegister(int regNum,
                                                 double value) {
#if defined(__mips_hard_float) && __mips_fpr == 64
  assert(validFloatRegister(regNum));
  _floats[regNum - UNW_MIPS_F0] = value;
#else
  (void)regNum;
  (void)value;
  _LIBUNWIND_ABORT("mips_o32 float support not implemented");
#endif
}

inline bool Registers_mips_o32::validVectorRegister(int /* regNum */) const {
  return false;
}

inline v128 Registers_mips_o32::getVectorRegister(int /* regNum */) const {
  _LIBUNWIND_ABORT("mips_o32 vector support not implemented");
}

inline void Registers_mips_o32::setVectorRegister(int /* regNum */, v128 /* value */) {
  _LIBUNWIND_ABORT("mips_o32 vector support not implemented");
}

inline const char *Registers_mips_o32::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_MIPS_R0:
    return "$0";
  case UNW_MIPS_R1:
    return "$1";
  case UNW_MIPS_R2:
    return "$2";
  case UNW_MIPS_R3:
    return "$3";
  case UNW_MIPS_R4:
    return "$4";
  case UNW_MIPS_R5:
    return "$5";
  case UNW_MIPS_R6:
    return "$6";
  case UNW_MIPS_R7:
    return "$7";
  case UNW_MIPS_R8:
    return "$8";
  case UNW_MIPS_R9:
    return "$9";
  case UNW_MIPS_R10:
    return "$10";
  case UNW_MIPS_R11:
    return "$11";
  case UNW_MIPS_R12:
    return "$12";
  case UNW_MIPS_R13:
    return "$13";
  case UNW_MIPS_R14:
    return "$14";
  case UNW_MIPS_R15:
    return "$15";
  case UNW_MIPS_R16:
    return "$16";
  case UNW_MIPS_R17:
    return "$17";
  case UNW_MIPS_R18:
    return "$18";
  case UNW_MIPS_R19:
    return "$19";
  case UNW_MIPS_R20:
    return "$20";
  case UNW_MIPS_R21:
    return "$21";
  case UNW_MIPS_R22:
    return "$22";
  case UNW_MIPS_R23:
    return "$23";
  case UNW_MIPS_R24:
    return "$24";
  case UNW_MIPS_R25:
    return "$25";
  case UNW_MIPS_R26:
    return "$26";
  case UNW_MIPS_R27:
    return "$27";
  case UNW_MIPS_R28:
    return "$28";
  case UNW_MIPS_R29:
    return "$29";
  case UNW_MIPS_R30:
    return "$30";
  case UNW_MIPS_R31:
    return "$31";
  case UNW_MIPS_F0:
    return "$f0";
  case UNW_MIPS_F1:
    return "$f1";
  case UNW_MIPS_F2:
    return "$f2";
  case UNW_MIPS_F3:
    return "$f3";
  case UNW_MIPS_F4:
    return "$f4";
  case UNW_MIPS_F5:
    return "$f5";
  case UNW_MIPS_F6:
    return "$f6";
  case UNW_MIPS_F7:
    return "$f7";
  case UNW_MIPS_F8:
    return "$f8";
  case UNW_MIPS_F9:
    return "$f9";
  case UNW_MIPS_F10:
    return "$f10";
  case UNW_MIPS_F11:
    return "$f11";
  case UNW_MIPS_F12:
    return "$f12";
  case UNW_MIPS_F13:
    return "$f13";
  case UNW_MIPS_F14:
    return "$f14";
  case UNW_MIPS_F15:
    return "$f15";
  case UNW_MIPS_F16:
    return "$f16";
  case UNW_MIPS_F17:
    return "$f17";
  case UNW_MIPS_F18:
    return "$f18";
  case UNW_MIPS_F19:
    return "$f19";
  case UNW_MIPS_F20:
    return "$f20";
  case UNW_MIPS_F21:
    return "$f21";
  case UNW_MIPS_F22:
    return "$f22";
  case UNW_MIPS_F23:
    return "$f23";
  case UNW_MIPS_F24:
    return "$f24";
  case UNW_MIPS_F25:
    return "$f25";
  case UNW_MIPS_F26:
    return "$f26";
  case UNW_MIPS_F27:
    return "$f27";
  case UNW_MIPS_F28:
    return "$f28";
  case UNW_MIPS_F29:
    return "$f29";
  case UNW_MIPS_F30:
    return "$f30";
  case UNW_MIPS_F31:
    return "$f31";
#if __mips_isa_rev < 6
  case UNW_MIPS_HI:
    return "$hi";
  case UNW_MIPS_LO:
    return "$lo";
#endif
  default:
    return "unknown register";
  }
}
#endif // _LIBUNWIND_TARGET_MIPS_O32

#if defined(_LIBUNWIND_TARGET_MIPS_NEWABI)
/// Registers_mips_newabi holds the register state of a thread in a
/// MIPS process using NEWABI (the N32 or N64 ABIs).
class _LIBUNWIND_HIDDEN Registers_mips_newabi {
public:
  Registers_mips_newabi();
  Registers_mips_newabi(const void *registers);

  bool        validRegister(int num) const;
  uint64_t    getRegister(int num) const;
  void        setRegister(int num, uint64_t value);
  bool        validFloatRegister(int num) const;
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_MIPS;
  }
  static int  getArch() { return REGISTERS_MIPS_NEWABI; }

  uint64_t  getSP() const         { return _registers.__r[29]; }
  void      setSP(uint64_t value) { _registers.__r[29] = value; }
  uint64_t  getIP() const         { return _registers.__pc; }
  void      setIP(uint64_t value) { _registers.__pc = value; }

private:
  struct mips_newabi_thread_state_t {
    uint64_t __r[32];
    uint64_t __pc;
    uint64_t __hi;
    uint64_t __lo;
  };

  mips_newabi_thread_state_t _registers;
#ifdef __mips_hard_float
  double _floats[32];
#endif
};

inline Registers_mips_newabi::Registers_mips_newabi(const void *registers) {
  static_assert((check_fit<Registers_mips_newabi, unw_context_t>::does_fit),
                "mips_newabi registers do not fit into unw_context_t");
  memcpy(&_registers, static_cast<const uint8_t *>(registers),
         sizeof(_registers));
}

inline Registers_mips_newabi::Registers_mips_newabi() {
  memset(&_registers, 0, sizeof(_registers));
}

inline bool Registers_mips_newabi::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return true;
  if (regNum == UNW_REG_SP)
    return true;
  if (regNum < 0)
    return false;
  if (regNum <= UNW_MIPS_R31)
    return true;
#if __mips_isa_rev < 6
  if (regNum == UNW_MIPS_HI)
    return true;
  if (regNum == UNW_MIPS_LO)
    return true;
#endif
  // FIXME: Hard float, DSP accumulator registers, MSA registers
  return false;
}

inline uint64_t Registers_mips_newabi::getRegister(int regNum) const {
  if (regNum >= UNW_MIPS_R0 && regNum <= UNW_MIPS_R31)
    return _registers.__r[regNum - UNW_MIPS_R0];

  switch (regNum) {
  case UNW_REG_IP:
    return _registers.__pc;
  case UNW_REG_SP:
    return _registers.__r[29];
#if __mips_isa_rev < 6
  case UNW_MIPS_HI:
    return _registers.__hi;
  case UNW_MIPS_LO:
    return _registers.__lo;
#endif
  }
  _LIBUNWIND_ABORT("unsupported mips_newabi register");
}

inline void Registers_mips_newabi::setRegister(int regNum, uint64_t value) {
  if (regNum >= UNW_MIPS_R0 && regNum <= UNW_MIPS_R31) {
    _registers.__r[regNum - UNW_MIPS_R0] = value;
    return;
  }

  switch (regNum) {
  case UNW_REG_IP:
    _registers.__pc = value;
    return;
  case UNW_REG_SP:
    _registers.__r[29] = value;
    return;
#if __mips_isa_rev < 6
  case UNW_MIPS_HI:
    _registers.__hi = value;
    return;
  case UNW_MIPS_LO:
    _registers.__lo = value;
    return;
#endif
  }
  _LIBUNWIND_ABORT("unsupported mips_newabi register");
}

inline bool Registers_mips_newabi::validFloatRegister(int regNum) const {
#ifdef __mips_hard_float
  if (regNum >= UNW_MIPS_F0 && regNum <= UNW_MIPS_F31)
    return true;
#else
  (void)regNum;
#endif
  return false;
}

inline double Registers_mips_newabi::getFloatRegister(int regNum) const {
#ifdef __mips_hard_float
  assert(validFloatRegister(regNum));
  return _floats[regNum - UNW_MIPS_F0];
#else
  (void)regNum;
  _LIBUNWIND_ABORT("mips_newabi float support not implemented");
#endif
}

inline void Registers_mips_newabi::setFloatRegister(int regNum,
                                                    double value) {
#ifdef __mips_hard_float
  assert(validFloatRegister(regNum));
  _floats[regNum - UNW_MIPS_F0] = value;
#else
  (void)regNum;
  (void)value;
  _LIBUNWIND_ABORT("mips_newabi float support not implemented");
#endif
}

inline bool Registers_mips_newabi::validVectorRegister(int /* regNum */) const {
  return false;
}

inline v128 Registers_mips_newabi::getVectorRegister(int /* regNum */) const {
  _LIBUNWIND_ABORT("mips_newabi vector support not implemented");
}

inline void Registers_mips_newabi::setVectorRegister(int /* regNum */, v128 /* value */) {
  _LIBUNWIND_ABORT("mips_newabi vector support not implemented");
}

inline const char *Registers_mips_newabi::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_MIPS_R0:
    return "$0";
  case UNW_MIPS_R1:
    return "$1";
  case UNW_MIPS_R2:
    return "$2";
  case UNW_MIPS_R3:
    return "$3";
  case UNW_MIPS_R4:
    return "$4";
  case UNW_MIPS_R5:
    return "$5";
  case UNW_MIPS_R6:
    return "$6";
  case UNW_MIPS_R7:
    return "$7";
  case UNW_MIPS_R8:
    return "$8";
  case UNW_MIPS_R9:
    return "$9";
  case UNW_MIPS_R10:
    return "$10";
  case UNW_MIPS_R11:
    return "$11";
  case UNW_MIPS_R12:
    return "$12";
  case UNW_MIPS_R13:
    return "$13";
  case UNW_MIPS_R14:
    return "$14";
  case UNW_MIPS_R15:
    return "$15";
  case UNW_MIPS_R16:
    return "$16";
  case UNW_MIPS_R17:
    return "$17";
  case UNW_MIPS_R18:
    return "$18";
  case UNW_MIPS_R19:
    return "$19";
  case UNW_MIPS_R20:
    return "$20";
  case UNW_MIPS_R21:
    return "$21";
  case UNW_MIPS_R22:
    return "$22";
  case UNW_MIPS_R23:
    return "$23";
  case UNW_MIPS_R24:
    return "$24";
  case UNW_MIPS_R25:
    return "$25";
  case UNW_MIPS_R26:
    return "$26";
  case UNW_MIPS_R27:
    return "$27";
  case UNW_MIPS_R28:
    return "$28";
  case UNW_MIPS_R29:
    return "$29";
  case UNW_MIPS_R30:
    return "$30";
  case UNW_MIPS_R31:
    return "$31";
  case UNW_MIPS_F0:
    return "$f0";
  case UNW_MIPS_F1:
    return "$f1";
  case UNW_MIPS_F2:
    return "$f2";
  case UNW_MIPS_F3:
    return "$f3";
  case UNW_MIPS_F4:
    return "$f4";
  case UNW_MIPS_F5:
    return "$f5";
  case UNW_MIPS_F6:
    return "$f6";
  case UNW_MIPS_F7:
    return "$f7";
  case UNW_MIPS_F8:
    return "$f8";
  case UNW_MIPS_F9:
    return "$f9";
  case UNW_MIPS_F10:
    return "$f10";
  case UNW_MIPS_F11:
    return "$f11";
  case UNW_MIPS_F12:
    return "$f12";
  case UNW_MIPS_F13:
    return "$f13";
  case UNW_MIPS_F14:
    return "$f14";
  case UNW_MIPS_F15:
    return "$f15";
  case UNW_MIPS_F16:
    return "$f16";
  case UNW_MIPS_F17:
    return "$f17";
  case UNW_MIPS_F18:
    return "$f18";
  case UNW_MIPS_F19:
    return "$f19";
  case UNW_MIPS_F20:
    return "$f20";
  case UNW_MIPS_F21:
    return "$f21";
  case UNW_MIPS_F22:
    return "$f22";
  case UNW_MIPS_F23:
    return "$f23";
  case UNW_MIPS_F24:
    return "$f24";
  case UNW_MIPS_F25:
    return "$f25";
  case UNW_MIPS_F26:
    return "$f26";
  case UNW_MIPS_F27:
    return "$f27";
  case UNW_MIPS_F28:
    return "$f28";
  case UNW_MIPS_F29:
    return "$f29";
  case UNW_MIPS_F30:
    return "$f30";
  case UNW_MIPS_F31:
    return "$f31";
#if __mips_isa_rev < 6
  case UNW_MIPS_HI:
    return "$hi";
  case UNW_MIPS_LO:
    return "$lo";
#endif
  default:
    return "unknown register";
  }
}
#endif // _LIBUNWIND_TARGET_MIPS_NEWABI

#if defined(_LIBUNWIND_TARGET_SPARC)
/// Registers_sparc holds the register state of a thread in a 32-bit Sparc
/// process.
class _LIBUNWIND_HIDDEN Registers_sparc {
public:
  Registers_sparc();
  Registers_sparc(const void *registers);

  bool        validRegister(int num) const;
  uint32_t    getRegister(int num) const;
  void        setRegister(int num, uint32_t value);
  bool        validFloatRegister(int num) const;
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_SPARC;
  }
  static int  getArch() { return REGISTERS_SPARC; }

  uint64_t  getSP() const         { return _registers.__regs[UNW_SPARC_O6]; }
  void      setSP(uint32_t value) { _registers.__regs[UNW_SPARC_O6] = value; }
  uint64_t  getIP() const         { return _registers.__regs[UNW_SPARC_O7]; }
  void      setIP(uint32_t value) { _registers.__regs[UNW_SPARC_O7] = value; }

private:
  struct sparc_thread_state_t {
    unsigned int __regs[32];
  };

  sparc_thread_state_t _registers;
};

inline Registers_sparc::Registers_sparc(const void *registers) {
  static_assert((check_fit<Registers_sparc, unw_context_t>::does_fit),
                "sparc registers do not fit into unw_context_t");
  memcpy(&_registers, static_cast<const uint8_t *>(registers),
         sizeof(_registers));
}

inline Registers_sparc::Registers_sparc() {
  memset(&_registers, 0, sizeof(_registers));
}

inline bool Registers_sparc::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return true;
  if (regNum == UNW_REG_SP)
    return true;
  if (regNum < 0)
    return false;
  if (regNum <= UNW_SPARC_I7)
    return true;
  return false;
}

inline uint32_t Registers_sparc::getRegister(int regNum) const {
  if ((UNW_SPARC_G0 <= regNum) && (regNum <= UNW_SPARC_I7)) {
    return _registers.__regs[regNum];
  }

  switch (regNum) {
  case UNW_REG_IP:
    return _registers.__regs[UNW_SPARC_O7];
  case UNW_REG_SP:
    return _registers.__regs[UNW_SPARC_O6];
  }
  _LIBUNWIND_ABORT("unsupported sparc register");
}

inline void Registers_sparc::setRegister(int regNum, uint32_t value) {
  if ((UNW_SPARC_G0 <= regNum) && (regNum <= UNW_SPARC_I7)) {
    _registers.__regs[regNum] = value;
    return;
  }

  switch (regNum) {
  case UNW_REG_IP:
    _registers.__regs[UNW_SPARC_O7] = value;
    return;
  case UNW_REG_SP:
    _registers.__regs[UNW_SPARC_O6] = value;
    return;
  }
  _LIBUNWIND_ABORT("unsupported sparc register");
}

inline bool Registers_sparc::validFloatRegister(int) const { return false; }

inline double Registers_sparc::getFloatRegister(int) const {
  _LIBUNWIND_ABORT("no Sparc float registers");
}

inline void Registers_sparc::setFloatRegister(int, double) {
  _LIBUNWIND_ABORT("no Sparc float registers");
}

inline bool Registers_sparc::validVectorRegister(int) const { return false; }

inline v128 Registers_sparc::getVectorRegister(int) const {
  _LIBUNWIND_ABORT("no Sparc vector registers");
}

inline void Registers_sparc::setVectorRegister(int, v128) {
  _LIBUNWIND_ABORT("no Sparc vector registers");
}

inline const char *Registers_sparc::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
    return "pc";
  case UNW_SPARC_G0:
    return "g0";
  case UNW_SPARC_G1:
    return "g1";
  case UNW_SPARC_G2:
    return "g2";
  case UNW_SPARC_G3:
    return "g3";
  case UNW_SPARC_G4:
    return "g4";
  case UNW_SPARC_G5:
    return "g5";
  case UNW_SPARC_G6:
    return "g6";
  case UNW_SPARC_G7:
    return "g7";
  case UNW_SPARC_O0:
    return "o0";
  case UNW_SPARC_O1:
    return "o1";
  case UNW_SPARC_O2:
    return "o2";
  case UNW_SPARC_O3:
    return "o3";
  case UNW_SPARC_O4:
    return "o4";
  case UNW_SPARC_O5:
    return "o5";
  case UNW_REG_SP:
  case UNW_SPARC_O6:
    return "sp";
  case UNW_SPARC_O7:
    return "o7";
  case UNW_SPARC_L0:
    return "l0";
  case UNW_SPARC_L1:
    return "l1";
  case UNW_SPARC_L2:
    return "l2";
  case UNW_SPARC_L3:
    return "l3";
  case UNW_SPARC_L4:
    return "l4";
  case UNW_SPARC_L5:
    return "l5";
  case UNW_SPARC_L6:
    return "l6";
  case UNW_SPARC_L7:
    return "l7";
  case UNW_SPARC_I0:
    return "i0";
  case UNW_SPARC_I1:
    return "i1";
  case UNW_SPARC_I2:
    return "i2";
  case UNW_SPARC_I3:
    return "i3";
  case UNW_SPARC_I4:
    return "i4";
  case UNW_SPARC_I5:
    return "i5";
  case UNW_SPARC_I6:
    return "fp";
  case UNW_SPARC_I7:
    return "i7";
  default:
    return "unknown register";
  }
}
#endif // _LIBUNWIND_TARGET_SPARC

#if defined(_LIBUNWIND_TARGET_SPARC64)
/// Registers_sparc64 holds the register state of a thread in a 64-bit
/// sparc process.
class _LIBUNWIND_HIDDEN Registers_sparc64 {
public:
  Registers_sparc64() = default;
  Registers_sparc64(const void *registers);

  bool validRegister(int num) const;
  uint64_t getRegister(int num) const;
  void setRegister(int num, uint64_t value);
  bool validFloatRegister(int num) const;
  double getFloatRegister(int num) const;
  void setFloatRegister(int num, double value);
  bool validVectorRegister(int num) const;
  v128 getVectorRegister(int num) const;
  void setVectorRegister(int num, v128 value);
  const char *getRegisterName(int num);
  void jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_SPARC64;
  }
  static int getArch() { return REGISTERS_SPARC64; }

  uint64_t getSP() const { return _registers.__regs[UNW_SPARC_O6] + 2047; }
  void setSP(uint64_t value) { _registers.__regs[UNW_SPARC_O6] = value - 2047; }
  uint64_t getIP() const { return _registers.__regs[UNW_SPARC_O7]; }
  void setIP(uint64_t value) { _registers.__regs[UNW_SPARC_O7] = value; }
  uint64_t getWCookie() const { return _wcookie; }

private:
  struct sparc64_thread_state_t {
    uint64_t __regs[32];
  };

  sparc64_thread_state_t _registers{};
  uint64_t _wcookie = 0;
};

inline Registers_sparc64::Registers_sparc64(const void *registers) {
  static_assert((check_fit<Registers_sparc64, unw_context_t>::does_fit),
                "sparc64 registers do not fit into unw_context_t");
  memcpy(&_registers, registers, sizeof(_registers));
  memcpy(&_wcookie,
         static_cast<const uint8_t *>(registers) + sizeof(_registers),
         sizeof(_wcookie));
}

inline bool Registers_sparc64::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return true;
  if (regNum == UNW_REG_SP)
    return true;
  if (regNum < 0)
    return false;
  if (regNum <= UNW_SPARC_I7)
    return true;
  return false;
}

inline uint64_t Registers_sparc64::getRegister(int regNum) const {
  if (regNum >= UNW_SPARC_G0 && regNum <= UNW_SPARC_I7)
    return _registers.__regs[regNum];

  switch (regNum) {
  case UNW_REG_IP:
    return _registers.__regs[UNW_SPARC_O7];
  case UNW_REG_SP:
    return _registers.__regs[UNW_SPARC_O6] + 2047;
  }
  _LIBUNWIND_ABORT("unsupported sparc64 register");
}

inline void Registers_sparc64::setRegister(int regNum, uint64_t value) {
  if (regNum >= UNW_SPARC_G0 && regNum <= UNW_SPARC_I7) {
    _registers.__regs[regNum] = value;
    return;
  }

  switch (regNum) {
  case UNW_REG_IP:
    _registers.__regs[UNW_SPARC_O7] = value;
    return;
  case UNW_REG_SP:
    _registers.__regs[UNW_SPARC_O6] = value - 2047;
    return;
  }
  _LIBUNWIND_ABORT("unsupported sparc64 register");
}

inline bool Registers_sparc64::validFloatRegister(int) const { return false; }

inline double Registers_sparc64::getFloatRegister(int) const {
  _LIBUNWIND_ABORT("no sparc64 float registers");
}

inline void Registers_sparc64::setFloatRegister(int, double) {
  _LIBUNWIND_ABORT("no sparc64 float registers");
}

inline bool Registers_sparc64::validVectorRegister(int) const { return false; }

inline v128 Registers_sparc64::getVectorRegister(int) const {
  _LIBUNWIND_ABORT("no sparc64 vector registers");
}

inline void Registers_sparc64::setVectorRegister(int, v128) {
  _LIBUNWIND_ABORT("no sparc64 vector registers");
}

inline const char *Registers_sparc64::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
    return "pc";
  case UNW_SPARC_G0:
    return "g0";
  case UNW_SPARC_G1:
    return "g1";
  case UNW_SPARC_G2:
    return "g2";
  case UNW_SPARC_G3:
    return "g3";
  case UNW_SPARC_G4:
    return "g4";
  case UNW_SPARC_G5:
    return "g5";
  case UNW_SPARC_G6:
    return "g6";
  case UNW_SPARC_G7:
    return "g7";
  case UNW_SPARC_O0:
    return "o0";
  case UNW_SPARC_O1:
    return "o1";
  case UNW_SPARC_O2:
    return "o2";
  case UNW_SPARC_O3:
    return "o3";
  case UNW_SPARC_O4:
    return "o4";
  case UNW_SPARC_O5:
    return "o5";
  case UNW_REG_SP:
  case UNW_SPARC_O6:
    return "o6";
  case UNW_SPARC_O7:
    return "o7";
  case UNW_SPARC_L0:
    return "l0";
  case UNW_SPARC_L1:
    return "l1";
  case UNW_SPARC_L2:
    return "l2";
  case UNW_SPARC_L3:
    return "l3";
  case UNW_SPARC_L4:
    return "l4";
  case UNW_SPARC_L5:
    return "l5";
  case UNW_SPARC_L6:
    return "l6";
  case UNW_SPARC_L7:
    return "l7";
  case UNW_SPARC_I0:
    return "i0";
  case UNW_SPARC_I1:
    return "i1";
  case UNW_SPARC_I2:
    return "i2";
  case UNW_SPARC_I3:
    return "i3";
  case UNW_SPARC_I4:
    return "i4";
  case UNW_SPARC_I5:
    return "i5";
  case UNW_SPARC_I6:
    return "i6";
  case UNW_SPARC_I7:
    return "i7";
  default:
    return "unknown register";
  }
}
#endif // _LIBUNWIND_TARGET_SPARC64

#if defined(_LIBUNWIND_TARGET_HEXAGON)
/// Registers_hexagon holds the register state of a thread in a Hexagon QDSP6
/// process.
class _LIBUNWIND_HIDDEN Registers_hexagon {
public:
  Registers_hexagon();
  Registers_hexagon(const void *registers);

  bool        validRegister(int num) const;
  uint32_t    getRegister(int num) const;
  void        setRegister(int num, uint32_t value);
  bool        validFloatRegister(int num) const;
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  const char *getRegisterName(int num);
  void        jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_HEXAGON;
  }
  static int  getArch() { return REGISTERS_HEXAGON; }

  uint32_t  getSP() const         { return _registers.__r[UNW_HEXAGON_R29]; }
  void      setSP(uint32_t value) { _registers.__r[UNW_HEXAGON_R29] = value; }
  uint32_t  getIP() const         { return _registers.__r[UNW_HEXAGON_PC]; }
  void      setIP(uint32_t value) { _registers.__r[UNW_HEXAGON_PC] = value; }

private:
  struct hexagon_thread_state_t {
    unsigned int __r[35];
  };

  hexagon_thread_state_t _registers;
};

inline Registers_hexagon::Registers_hexagon(const void *registers) {
  static_assert((check_fit<Registers_hexagon, unw_context_t>::does_fit),
                "hexagon registers do not fit into unw_context_t");
  memcpy(&_registers, static_cast<const uint8_t *>(registers),
         sizeof(_registers));
}

inline Registers_hexagon::Registers_hexagon() {
  memset(&_registers, 0, sizeof(_registers));
}

inline bool Registers_hexagon::validRegister(int regNum) const {
  if (regNum <= UNW_HEXAGON_R31)
    return true;
  return false;
}

inline uint32_t Registers_hexagon::getRegister(int regNum) const {
  if (regNum >= UNW_HEXAGON_R0 && regNum <= UNW_HEXAGON_R31)
    return _registers.__r[regNum - UNW_HEXAGON_R0];

  switch (regNum) {
  case UNW_REG_IP:
    return _registers.__r[UNW_HEXAGON_PC];
  case UNW_REG_SP:
    return _registers.__r[UNW_HEXAGON_R29];
  }
  _LIBUNWIND_ABORT("unsupported hexagon register");
}

inline void Registers_hexagon::setRegister(int regNum, uint32_t value) {
  if (regNum >= UNW_HEXAGON_R0 && regNum <= UNW_HEXAGON_R31) {
    _registers.__r[regNum - UNW_HEXAGON_R0] = value;
    return;
  }

  switch (regNum) {
  case UNW_REG_IP:
    _registers.__r[UNW_HEXAGON_PC] = value;
    return;
  case UNW_REG_SP:
    _registers.__r[UNW_HEXAGON_R29] = value;
    return;
  }
  _LIBUNWIND_ABORT("unsupported hexagon register");
}

inline bool Registers_hexagon::validFloatRegister(int /* regNum */) const {
  return false;
}

inline double Registers_hexagon::getFloatRegister(int /* regNum */) const {
  _LIBUNWIND_ABORT("hexagon float support not implemented");
}

inline void Registers_hexagon::setFloatRegister(int /* regNum */,
                                             double /* value */) {
  _LIBUNWIND_ABORT("hexagon float support not implemented");
}

inline bool Registers_hexagon::validVectorRegister(int /* regNum */) const {
  return false;
}

inline v128 Registers_hexagon::getVectorRegister(int /* regNum */) const {
  _LIBUNWIND_ABORT("hexagon vector support not implemented");
}

inline void Registers_hexagon::setVectorRegister(int /* regNum */, v128 /* value */) {
  _LIBUNWIND_ABORT("hexagon vector support not implemented");
}

inline const char *Registers_hexagon::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_HEXAGON_R0:
    return "r0";
  case UNW_HEXAGON_R1:
    return "r1";
  case UNW_HEXAGON_R2:
    return "r2";
  case UNW_HEXAGON_R3:
    return "r3";
  case UNW_HEXAGON_R4:
    return "r4";
  case UNW_HEXAGON_R5:
    return "r5";
  case UNW_HEXAGON_R6:
    return "r6";
  case UNW_HEXAGON_R7:
    return "r7";
  case UNW_HEXAGON_R8:
    return "r8";
  case UNW_HEXAGON_R9:
    return "r9";
  case UNW_HEXAGON_R10:
    return "r10";
  case UNW_HEXAGON_R11:
    return "r11";
  case UNW_HEXAGON_R12:
    return "r12";
  case UNW_HEXAGON_R13:
    return "r13";
  case UNW_HEXAGON_R14:
    return "r14";
  case UNW_HEXAGON_R15:
    return "r15";
  case UNW_HEXAGON_R16:
    return "r16";
  case UNW_HEXAGON_R17:
    return "r17";
  case UNW_HEXAGON_R18:
    return "r18";
  case UNW_HEXAGON_R19:
    return "r19";
  case UNW_HEXAGON_R20:
    return "r20";
  case UNW_HEXAGON_R21:
    return "r21";
  case UNW_HEXAGON_R22:
    return "r22";
  case UNW_HEXAGON_R23:
    return "r23";
  case UNW_HEXAGON_R24:
    return "r24";
  case UNW_HEXAGON_R25:
    return "r25";
  case UNW_HEXAGON_R26:
    return "r26";
  case UNW_HEXAGON_R27:
    return "r27";
  case UNW_HEXAGON_R28:
    return "r28";
  case UNW_HEXAGON_R29:
    return "r29";
  case UNW_HEXAGON_R30:
    return "r30";
  case UNW_HEXAGON_R31:
    return "r31";
  default:
    return "unknown register";
  }

}
#endif // _LIBUNWIND_TARGET_HEXAGON


#if defined(_LIBUNWIND_TARGET_RISCV)
/// Registers_riscv holds the register state of a thread in a RISC-V
/// process.

// This check makes it safe when LIBUNWIND_ENABLE_CROSS_UNWINDING enabled.
# ifdef __riscv
#  if __riscv_xlen == 32
typedef uint32_t reg_t;
#  elif __riscv_xlen == 64
typedef uint64_t reg_t;
#  else
#   error "Unsupported __riscv_xlen"
#  endif

#  if defined(__riscv_flen)
#   if __riscv_flen == 64
typedef double fp_t;
#   elif __riscv_flen == 32
typedef float fp_t;
#   else
#    error "Unsupported __riscv_flen"
#   endif
#  else
// This is just for suppressing undeclared error of fp_t.
typedef double fp_t;
#  endif
# else
// Use Max possible width when cross unwinding
typedef uint64_t reg_t;
typedef double fp_t;
# define __riscv_xlen 64
# define __riscv_flen 64
#endif

/// Registers_riscv holds the register state of a thread.
class _LIBUNWIND_HIDDEN Registers_riscv {
public:
  Registers_riscv();
  Registers_riscv(const void *registers);

  bool        validRegister(int num) const;
  reg_t       getRegister(int num) const;
  void        setRegister(int num, reg_t value);
  bool        validFloatRegister(int num) const;
  fp_t        getFloatRegister(int num) const;
  void        setFloatRegister(int num, fp_t value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_RISCV;
  }
  static int  getArch() { return REGISTERS_RISCV; }

  reg_t       getSP() const { return _registers[2]; }
  void        setSP(reg_t value) { _registers[2] = value; }
  reg_t       getIP() const { return _registers[0]; }
  void        setIP(reg_t value) { _registers[0] = value; }

private:
  // _registers[0] holds the pc
  reg_t _registers[32];
# if defined(__riscv_flen)
  fp_t _floats[32];
# endif
};

inline Registers_riscv::Registers_riscv(const void *registers) {
  static_assert((check_fit<Registers_riscv, unw_context_t>::does_fit),
                "riscv registers do not fit into unw_context_t");
  memcpy(&_registers, registers, sizeof(_registers));
# if __riscv_xlen == 32
  static_assert(sizeof(_registers) == 0x80,
                "expected float registers to be at offset 128");
# elif __riscv_xlen == 64
  static_assert(sizeof(_registers) == 0x100,
                "expected float registers to be at offset 256");
# else
# error "Unexpected float registers."
# endif

# if defined(__riscv_flen)
  memcpy(_floats,
         static_cast<const uint8_t *>(registers) + sizeof(_registers),
         sizeof(_floats));
# endif
}

inline Registers_riscv::Registers_riscv() {
  memset(&_registers, 0, sizeof(_registers));
# if defined(__riscv_flen)
  memset(&_floats, 0, sizeof(_floats));
# endif
}

inline bool Registers_riscv::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return true;
  if (regNum == UNW_REG_SP)
    return true;
  if (regNum < 0)
    return false;
  if (regNum == UNW_RISCV_VLENB)
    return true;
  if (regNum > UNW_RISCV_F31)
    return false;
  return true;
}

inline reg_t Registers_riscv::getRegister(int regNum) const {
  if (regNum == UNW_REG_IP)
    return _registers[0];
  if (regNum == UNW_REG_SP)
    return _registers[2];
  if (regNum == UNW_RISCV_X0)
    return 0;
  if ((regNum > 0) && (regNum < 32))
    return _registers[regNum];
  if (regNum == UNW_RISCV_VLENB) {
    reg_t vlenb;
    __asm__("csrr %0, 0xC22" : "=r"(vlenb));
    return vlenb;
  }
  _LIBUNWIND_ABORT("unsupported riscv register");
}

inline void Registers_riscv::setRegister(int regNum, reg_t value) {
  if (regNum == UNW_REG_IP)
    _registers[0] = value;
  else if (regNum == UNW_REG_SP)
    _registers[2] = value;
  else if (regNum == UNW_RISCV_X0)
    /* x0 is hardwired to zero */
    return;
  else if ((regNum > 0) && (regNum < 32))
    _registers[regNum] = value;
  else
    _LIBUNWIND_ABORT("unsupported riscv register");
}

inline const char *Registers_riscv::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
    return "pc";
  case UNW_REG_SP:
    return "sp";
  case UNW_RISCV_X0:
    return "zero";
  case UNW_RISCV_X1:
    return "ra";
  case UNW_RISCV_X2:
    return "sp";
  case UNW_RISCV_X3:
    return "gp";
  case UNW_RISCV_X4:
    return "tp";
  case UNW_RISCV_X5:
    return "t0";
  case UNW_RISCV_X6:
    return "t1";
  case UNW_RISCV_X7:
    return "t2";
  case UNW_RISCV_X8:
    return "s0";
  case UNW_RISCV_X9:
    return "s1";
  case UNW_RISCV_X10:
    return "a0";
  case UNW_RISCV_X11:
    return "a1";
  case UNW_RISCV_X12:
    return "a2";
  case UNW_RISCV_X13:
    return "a3";
  case UNW_RISCV_X14:
    return "a4";
  case UNW_RISCV_X15:
    return "a5";
  case UNW_RISCV_X16:
    return "a6";
  case UNW_RISCV_X17:
    return "a7";
  case UNW_RISCV_X18:
    return "s2";
  case UNW_RISCV_X19:
    return "s3";
  case UNW_RISCV_X20:
    return "s4";
  case UNW_RISCV_X21:
    return "s5";
  case UNW_RISCV_X22:
    return "s6";
  case UNW_RISCV_X23:
    return "s7";
  case UNW_RISCV_X24:
    return "s8";
  case UNW_RISCV_X25:
    return "s9";
  case UNW_RISCV_X26:
    return "s10";
  case UNW_RISCV_X27:
    return "s11";
  case UNW_RISCV_X28:
    return "t3";
  case UNW_RISCV_X29:
    return "t4";
  case UNW_RISCV_X30:
    return "t5";
  case UNW_RISCV_X31:
    return "t6";
  case UNW_RISCV_F0:
    return "ft0";
  case UNW_RISCV_F1:
    return "ft1";
  case UNW_RISCV_F2:
    return "ft2";
  case UNW_RISCV_F3:
    return "ft3";
  case UNW_RISCV_F4:
    return "ft4";
  case UNW_RISCV_F5:
    return "ft5";
  case UNW_RISCV_F6:
    return "ft6";
  case UNW_RISCV_F7:
    return "ft7";
  case UNW_RISCV_F8:
    return "fs0";
  case UNW_RISCV_F9:
    return "fs1";
  case UNW_RISCV_F10:
    return "fa0";
  case UNW_RISCV_F11:
    return "fa1";
  case UNW_RISCV_F12:
    return "fa2";
  case UNW_RISCV_F13:
    return "fa3";
  case UNW_RISCV_F14:
    return "fa4";
  case UNW_RISCV_F15:
    return "fa5";
  case UNW_RISCV_F16:
    return "fa6";
  case UNW_RISCV_F17:
    return "fa7";
  case UNW_RISCV_F18:
    return "fs2";
  case UNW_RISCV_F19:
    return "fs3";
  case UNW_RISCV_F20:
    return "fs4";
  case UNW_RISCV_F21:
    return "fs5";
  case UNW_RISCV_F22:
    return "fs6";
  case UNW_RISCV_F23:
    return "fs7";
  case UNW_RISCV_F24:
    return "fs8";
  case UNW_RISCV_F25:
    return "fs9";
  case UNW_RISCV_F26:
    return "fs10";
  case UNW_RISCV_F27:
    return "fs11";
  case UNW_RISCV_F28:
    return "ft8";
  case UNW_RISCV_F29:
    return "ft9";
  case UNW_RISCV_F30:
    return "ft10";
  case UNW_RISCV_F31:
    return "ft11";
  case UNW_RISCV_VLENB:
    return "vlenb";
  default:
    return "unknown register";
  }
}

inline bool Registers_riscv::validFloatRegister(int regNum) const {
# if defined(__riscv_flen)
  if (regNum < UNW_RISCV_F0)
    return false;
  if (regNum > UNW_RISCV_F31)
    return false;
  return true;
# else
  (void)regNum;
  return false;
# endif
}

inline fp_t Registers_riscv::getFloatRegister(int regNum) const {
# if defined(__riscv_flen)
  assert(validFloatRegister(regNum));
  return _floats[regNum - UNW_RISCV_F0];
# else
  (void)regNum;
  _LIBUNWIND_ABORT("libunwind not built with float support");
# endif
}

inline void Registers_riscv::setFloatRegister(int regNum, fp_t value) {
# if defined(__riscv_flen)
  assert(validFloatRegister(regNum));
  _floats[regNum - UNW_RISCV_F0] = value;
# else
  (void)regNum;
  (void)value;
  _LIBUNWIND_ABORT("libunwind not built with float support");
# endif
}

inline bool Registers_riscv::validVectorRegister(int) const {
  return false;
}

inline v128 Registers_riscv::getVectorRegister(int) const {
  _LIBUNWIND_ABORT("no riscv vector register support yet");
}

inline void Registers_riscv::setVectorRegister(int, v128) {
  _LIBUNWIND_ABORT("no riscv vector register support yet");
}
#endif // _LIBUNWIND_TARGET_RISCV

#if defined(_LIBUNWIND_TARGET_VE)
/// Registers_ve holds the register state of a thread in a VE process.
class _LIBUNWIND_HIDDEN Registers_ve {
public:
  Registers_ve();
  Registers_ve(const void *registers);

  bool        validRegister(int num) const;
  uint64_t    getRegister(int num) const;
  void        setRegister(int num, uint64_t value);
  bool        validFloatRegister(int num) const;
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_VE;
  }
  static int  getArch() { return REGISTERS_VE; }

  uint64_t  getSP() const         { return _registers.__s[11]; }
  void      setSP(uint64_t value) { _registers.__s[11] = value; }
  uint64_t  getIP() const         { return _registers.__ic; }
  void      setIP(uint64_t value) { _registers.__ic = value; }

private:
  // FIXME: Need to store not only scalar registers but also vector and vector
  // mask registers.  VEOS uses mcontext_t defined in ucontext.h.  It takes
  // 524288 bytes (65536*8 bytes), though.  Currently, we use libunwind for
  // SjLj exception support only, so Registers_ve is not implemented completely.
  struct ve_thread_state_t {
    uint64_t __s[64]; // s0-s64
    uint64_t __ic;    // Instruction counter (IC)
    uint64_t __vixr;  // Vector Index Register
    uint64_t __vl;    // Vector Length Register
  };

  ve_thread_state_t _registers; // total 67 registers

  // Currently no vector register is preserved.
};

inline Registers_ve::Registers_ve(const void *registers) {
  static_assert((check_fit<Registers_ve, unw_context_t>::does_fit),
                "ve registers do not fit into unw_context_t");
  memcpy(&_registers, static_cast<const uint8_t *>(registers),
         sizeof(_registers));
  static_assert(sizeof(_registers) == 536,
                "expected vector register offset to be 536");
}

inline Registers_ve::Registers_ve() {
  memset(&_registers, 0, sizeof(_registers));
}

inline bool Registers_ve::validRegister(int regNum) const {
  if (regNum >= UNW_VE_S0 && regNum <= UNW_VE_S63)
    return true;

  switch (regNum) {
  case UNW_REG_IP:
  case UNW_REG_SP:
  case UNW_VE_VIXR:
  case UNW_VE_VL:
    return true;
  default:
    return false;
  }
}

inline uint64_t Registers_ve::getRegister(int regNum) const {
  if (regNum >= UNW_VE_S0 && regNum <= UNW_VE_S63)
    return _registers.__s[regNum - UNW_VE_S0];

  switch (regNum) {
  case UNW_REG_IP:
    return _registers.__ic;
  case UNW_REG_SP:
    return _registers.__s[11];
  case UNW_VE_VIXR:
    return _registers.__vixr;
  case UNW_VE_VL:
    return _registers.__vl;
  }
  _LIBUNWIND_ABORT("unsupported ve register");
}

inline void Registers_ve::setRegister(int regNum, uint64_t value) {
  if (regNum >= UNW_VE_S0 && regNum <= UNW_VE_S63) {
    _registers.__s[regNum - UNW_VE_S0] = value;
    return;
  }

  switch (regNum) {
  case UNW_REG_IP:
    _registers.__ic = value;
    return;
  case UNW_REG_SP:
    _registers.__s[11] = value;
    return;
  case UNW_VE_VIXR:
    _registers.__vixr = value;
    return;
  case UNW_VE_VL:
    _registers.__vl = value;
    return;
  }
  _LIBUNWIND_ABORT("unsupported ve register");
}

inline bool Registers_ve::validFloatRegister(int /* regNum */) const {
  return false;
}

inline double Registers_ve::getFloatRegister(int /* regNum */) const {
  _LIBUNWIND_ABORT("VE doesn't have float registers");
}

inline void Registers_ve::setFloatRegister(int /* regNum */,
                                           double /* value */) {
  _LIBUNWIND_ABORT("VE doesn't have float registers");
}

inline bool Registers_ve::validVectorRegister(int /* regNum */) const {
  return false;
}

inline v128 Registers_ve::getVectorRegister(int /* regNum */) const {
  _LIBUNWIND_ABORT("VE vector support not implemented");
}

inline void Registers_ve::setVectorRegister(int /* regNum */,
                                            v128 /* value */) {
  _LIBUNWIND_ABORT("VE vector support not implemented");
}

inline const char *Registers_ve::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
    return "ip";
  case UNW_REG_SP:
    return "sp";
  case UNW_VE_VIXR:
    return "vixr";
  case UNW_VE_VL:
    return "vl";
  case UNW_VE_S0:
    return "s0";
  case UNW_VE_S1:
    return "s1";
  case UNW_VE_S2:
    return "s2";
  case UNW_VE_S3:
    return "s3";
  case UNW_VE_S4:
    return "s4";
  case UNW_VE_S5:
    return "s5";
  case UNW_VE_S6:
    return "s6";
  case UNW_VE_S7:
    return "s7";
  case UNW_VE_S8:
    return "s8";
  case UNW_VE_S9:
    return "s9";
  case UNW_VE_S10:
    return "s10";
  case UNW_VE_S11:
    return "s11";
  case UNW_VE_S12:
    return "s12";
  case UNW_VE_S13:
    return "s13";
  case UNW_VE_S14:
    return "s14";
  case UNW_VE_S15:
    return "s15";
  case UNW_VE_S16:
    return "s16";
  case UNW_VE_S17:
    return "s17";
  case UNW_VE_S18:
    return "s18";
  case UNW_VE_S19:
    return "s19";
  case UNW_VE_S20:
    return "s20";
  case UNW_VE_S21:
    return "s21";
  case UNW_VE_S22:
    return "s22";
  case UNW_VE_S23:
    return "s23";
  case UNW_VE_S24:
    return "s24";
  case UNW_VE_S25:
    return "s25";
  case UNW_VE_S26:
    return "s26";
  case UNW_VE_S27:
    return "s27";
  case UNW_VE_S28:
    return "s28";
  case UNW_VE_S29:
    return "s29";
  case UNW_VE_S30:
    return "s30";
  case UNW_VE_S31:
    return "s31";
  case UNW_VE_S32:
    return "s32";
  case UNW_VE_S33:
    return "s33";
  case UNW_VE_S34:
    return "s34";
  case UNW_VE_S35:
    return "s35";
  case UNW_VE_S36:
    return "s36";
  case UNW_VE_S37:
    return "s37";
  case UNW_VE_S38:
    return "s38";
  case UNW_VE_S39:
    return "s39";
  case UNW_VE_S40:
    return "s40";
  case UNW_VE_S41:
    return "s41";
  case UNW_VE_S42:
    return "s42";
  case UNW_VE_S43:
    return "s43";
  case UNW_VE_S44:
    return "s44";
  case UNW_VE_S45:
    return "s45";
  case UNW_VE_S46:
    return "s46";
  case UNW_VE_S47:
    return "s47";
  case UNW_VE_S48:
    return "s48";
  case UNW_VE_S49:
    return "s49";
  case UNW_VE_S50:
    return "s50";
  case UNW_VE_S51:
    return "s51";
  case UNW_VE_S52:
    return "s52";
  case UNW_VE_S53:
    return "s53";
  case UNW_VE_S54:
    return "s54";
  case UNW_VE_S55:
    return "s55";
  case UNW_VE_S56:
    return "s56";
  case UNW_VE_S57:
    return "s57";
  case UNW_VE_S58:
    return "s58";
  case UNW_VE_S59:
    return "s59";
  case UNW_VE_S60:
    return "s60";
  case UNW_VE_S61:
    return "s61";
  case UNW_VE_S62:
    return "s62";
  case UNW_VE_S63:
    return "s63";
  case UNW_VE_V0:
    return "v0";
  case UNW_VE_V1:
    return "v1";
  case UNW_VE_V2:
    return "v2";
  case UNW_VE_V3:
    return "v3";
  case UNW_VE_V4:
    return "v4";
  case UNW_VE_V5:
    return "v5";
  case UNW_VE_V6:
    return "v6";
  case UNW_VE_V7:
    return "v7";
  case UNW_VE_V8:
    return "v8";
  case UNW_VE_V9:
    return "v9";
  case UNW_VE_V10:
    return "v10";
  case UNW_VE_V11:
    return "v11";
  case UNW_VE_V12:
    return "v12";
  case UNW_VE_V13:
    return "v13";
  case UNW_VE_V14:
    return "v14";
  case UNW_VE_V15:
    return "v15";
  case UNW_VE_V16:
    return "v16";
  case UNW_VE_V17:
    return "v17";
  case UNW_VE_V18:
    return "v18";
  case UNW_VE_V19:
    return "v19";
  case UNW_VE_V20:
    return "v20";
  case UNW_VE_V21:
    return "v21";
  case UNW_VE_V22:
    return "v22";
  case UNW_VE_V23:
    return "v23";
  case UNW_VE_V24:
    return "v24";
  case UNW_VE_V25:
    return "v25";
  case UNW_VE_V26:
    return "v26";
  case UNW_VE_V27:
    return "v27";
  case UNW_VE_V28:
    return "v28";
  case UNW_VE_V29:
    return "v29";
  case UNW_VE_V30:
    return "v30";
  case UNW_VE_V31:
    return "v31";
  case UNW_VE_V32:
    return "v32";
  case UNW_VE_V33:
    return "v33";
  case UNW_VE_V34:
    return "v34";
  case UNW_VE_V35:
    return "v35";
  case UNW_VE_V36:
    return "v36";
  case UNW_VE_V37:
    return "v37";
  case UNW_VE_V38:
    return "v38";
  case UNW_VE_V39:
    return "v39";
  case UNW_VE_V40:
    return "v40";
  case UNW_VE_V41:
    return "v41";
  case UNW_VE_V42:
    return "v42";
  case UNW_VE_V43:
    return "v43";
  case UNW_VE_V44:
    return "v44";
  case UNW_VE_V45:
    return "v45";
  case UNW_VE_V46:
    return "v46";
  case UNW_VE_V47:
    return "v47";
  case UNW_VE_V48:
    return "v48";
  case UNW_VE_V49:
    return "v49";
  case UNW_VE_V50:
    return "v50";
  case UNW_VE_V51:
    return "v51";
  case UNW_VE_V52:
    return "v52";
  case UNW_VE_V53:
    return "v53";
  case UNW_VE_V54:
    return "v54";
  case UNW_VE_V55:
    return "v55";
  case UNW_VE_V56:
    return "v56";
  case UNW_VE_V57:
    return "v57";
  case UNW_VE_V58:
    return "v58";
  case UNW_VE_V59:
    return "v59";
  case UNW_VE_V60:
    return "v60";
  case UNW_VE_V61:
    return "v61";
  case UNW_VE_V62:
    return "v62";
  case UNW_VE_V63:
    return "v63";
  case UNW_VE_VM0:
    return "vm0";
  case UNW_VE_VM1:
    return "vm1";
  case UNW_VE_VM2:
    return "vm2";
  case UNW_VE_VM3:
    return "vm3";
  case UNW_VE_VM4:
    return "vm4";
  case UNW_VE_VM5:
    return "vm5";
  case UNW_VE_VM6:
    return "vm6";
  case UNW_VE_VM7:
    return "vm7";
  case UNW_VE_VM8:
    return "vm8";
  case UNW_VE_VM9:
    return "vm9";
  case UNW_VE_VM10:
    return "vm10";
  case UNW_VE_VM11:
    return "vm11";
  case UNW_VE_VM12:
    return "vm12";
  case UNW_VE_VM13:
    return "vm13";
  case UNW_VE_VM14:
    return "vm14";
  case UNW_VE_VM15:
    return "vm15";
  }
  return "unknown register";
}
#endif // _LIBUNWIND_TARGET_VE

#if defined(_LIBUNWIND_TARGET_S390X)
/// Registers_s390x holds the register state of a thread in a
/// 64-bit Linux on IBM zSystems process.
class _LIBUNWIND_HIDDEN Registers_s390x {
public:
  Registers_s390x();
  Registers_s390x(const void *registers);

  bool        validRegister(int num) const;
  uint64_t    getRegister(int num) const;
  void        setRegister(int num, uint64_t value);
  bool        validFloatRegister(int num) const;
  double      getFloatRegister(int num) const;
  void        setFloatRegister(int num, double value);
  bool        validVectorRegister(int num) const;
  v128        getVectorRegister(int num) const;
  void        setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void        jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_S390X;
  }
  static int  getArch() { return REGISTERS_S390X; }

  uint64_t  getSP() const         { return _registers.__gpr[15]; }
  void      setSP(uint64_t value) { _registers.__gpr[15] = value; }
  uint64_t  getIP() const         { return _registers.__pswa; }
  void      setIP(uint64_t value) { _registers.__pswa = value; }

private:
  struct s390x_thread_state_t {
    uint64_t __pswm;    // Problem Status Word: Mask
    uint64_t __pswa;    // Problem Status Word: Address (PC)
    uint64_t __gpr[16]; // General Purpose Registers
    double __fpr[16];   // Floating-Point Registers
  };

  s390x_thread_state_t _registers;
};

inline Registers_s390x::Registers_s390x(const void *registers) {
  static_assert((check_fit<Registers_s390x, unw_context_t>::does_fit),
                "s390x registers do not fit into unw_context_t");
  memcpy(&_registers, static_cast<const uint8_t *>(registers),
         sizeof(_registers));
}

inline Registers_s390x::Registers_s390x() {
  memset(&_registers, 0, sizeof(_registers));
}

inline bool Registers_s390x::validRegister(int regNum) const {
  switch (regNum) {
  case UNW_S390X_PSWM:
  case UNW_S390X_PSWA:
  case UNW_REG_IP:
  case UNW_REG_SP:
      return true;
  }

  if (regNum >= UNW_S390X_R0 && regNum <= UNW_S390X_R15)
    return true;

  return false;
}

inline uint64_t Registers_s390x::getRegister(int regNum) const {
  if (regNum >= UNW_S390X_R0 && regNum <= UNW_S390X_R15)
    return _registers.__gpr[regNum - UNW_S390X_R0];

  switch (regNum) {
  case UNW_S390X_PSWM:
    return _registers.__pswm;
  case UNW_S390X_PSWA:
  case UNW_REG_IP:
    return _registers.__pswa;
  case UNW_REG_SP:
    return _registers.__gpr[15];
  }
  _LIBUNWIND_ABORT("unsupported s390x register");
}

inline void Registers_s390x::setRegister(int regNum, uint64_t value) {
  if (regNum >= UNW_S390X_R0 && regNum <= UNW_S390X_R15) {
    _registers.__gpr[regNum - UNW_S390X_R0] = value;
    return;
  }

  switch (regNum) {
  case UNW_S390X_PSWM:
    _registers.__pswm = value;
    return;
  case UNW_S390X_PSWA:
  case UNW_REG_IP:
    _registers.__pswa = value;
    return;
  case UNW_REG_SP:
    _registers.__gpr[15] = value;
    return;
  }
  _LIBUNWIND_ABORT("unsupported s390x register");
}

inline bool Registers_s390x::validFloatRegister(int regNum) const {
  return regNum >= UNW_S390X_F0 && regNum <= UNW_S390X_F15;
}

inline double Registers_s390x::getFloatRegister(int regNum) const {
  // NOTE: FPR DWARF register numbers are not consecutive.
  switch (regNum) {
  case UNW_S390X_F0:
    return _registers.__fpr[0];
  case UNW_S390X_F1:
    return _registers.__fpr[1];
  case UNW_S390X_F2:
    return _registers.__fpr[2];
  case UNW_S390X_F3:
    return _registers.__fpr[3];
  case UNW_S390X_F4:
    return _registers.__fpr[4];
  case UNW_S390X_F5:
    return _registers.__fpr[5];
  case UNW_S390X_F6:
    return _registers.__fpr[6];
  case UNW_S390X_F7:
    return _registers.__fpr[7];
  case UNW_S390X_F8:
    return _registers.__fpr[8];
  case UNW_S390X_F9:
    return _registers.__fpr[9];
  case UNW_S390X_F10:
    return _registers.__fpr[10];
  case UNW_S390X_F11:
    return _registers.__fpr[11];
  case UNW_S390X_F12:
    return _registers.__fpr[12];
  case UNW_S390X_F13:
    return _registers.__fpr[13];
  case UNW_S390X_F14:
    return _registers.__fpr[14];
  case UNW_S390X_F15:
    return _registers.__fpr[15];
  }
  _LIBUNWIND_ABORT("unsupported s390x register");
}

inline void Registers_s390x::setFloatRegister(int regNum, double value) {
  // NOTE: FPR DWARF register numbers are not consecutive.
  switch (regNum) {
  case UNW_S390X_F0:
    _registers.__fpr[0] = value;
    return;
  case UNW_S390X_F1:
    _registers.__fpr[1] = value;
    return;
  case UNW_S390X_F2:
    _registers.__fpr[2] = value;
    return;
  case UNW_S390X_F3:
    _registers.__fpr[3] = value;
    return;
  case UNW_S390X_F4:
    _registers.__fpr[4] = value;
    return;
  case UNW_S390X_F5:
    _registers.__fpr[5] = value;
    return;
  case UNW_S390X_F6:
    _registers.__fpr[6] = value;
    return;
  case UNW_S390X_F7:
    _registers.__fpr[7] = value;
    return;
  case UNW_S390X_F8:
    _registers.__fpr[8] = value;
    return;
  case UNW_S390X_F9:
    _registers.__fpr[9] = value;
    return;
  case UNW_S390X_F10:
    _registers.__fpr[10] = value;
    return;
  case UNW_S390X_F11:
    _registers.__fpr[11] = value;
    return;
  case UNW_S390X_F12:
    _registers.__fpr[12] = value;
    return;
  case UNW_S390X_F13:
    _registers.__fpr[13] = value;
    return;
  case UNW_S390X_F14:
    _registers.__fpr[14] = value;
    return;
  case UNW_S390X_F15:
    _registers.__fpr[15] = value;
    return;
  }
  _LIBUNWIND_ABORT("unsupported s390x register");
}

inline bool Registers_s390x::validVectorRegister(int /*regNum*/) const {
  return false;
}

inline v128 Registers_s390x::getVectorRegister(int /*regNum*/) const {
  _LIBUNWIND_ABORT("s390x vector support not implemented");
}

inline void Registers_s390x::setVectorRegister(int /*regNum*/, v128 /*value*/) {
  _LIBUNWIND_ABORT("s390x vector support not implemented");
}

inline const char *Registers_s390x::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
    return "ip";
  case UNW_REG_SP:
    return "sp";
  case UNW_S390X_R0:
    return "r0";
  case UNW_S390X_R1:
    return "r1";
  case UNW_S390X_R2:
    return "r2";
  case UNW_S390X_R3:
    return "r3";
  case UNW_S390X_R4:
    return "r4";
  case UNW_S390X_R5:
    return "r5";
  case UNW_S390X_R6:
    return "r6";
  case UNW_S390X_R7:
    return "r7";
  case UNW_S390X_R8:
    return "r8";
  case UNW_S390X_R9:
    return "r9";
  case UNW_S390X_R10:
    return "r10";
  case UNW_S390X_R11:
    return "r11";
  case UNW_S390X_R12:
    return "r12";
  case UNW_S390X_R13:
    return "r13";
  case UNW_S390X_R14:
    return "r14";
  case UNW_S390X_R15:
    return "r15";
  case UNW_S390X_F0:
    return "f0";
  case UNW_S390X_F1:
    return "f1";
  case UNW_S390X_F2:
    return "f2";
  case UNW_S390X_F3:
    return "f3";
  case UNW_S390X_F4:
    return "f4";
  case UNW_S390X_F5:
    return "f5";
  case UNW_S390X_F6:
    return "f6";
  case UNW_S390X_F7:
    return "f7";
  case UNW_S390X_F8:
    return "f8";
  case UNW_S390X_F9:
    return "f9";
  case UNW_S390X_F10:
    return "f10";
  case UNW_S390X_F11:
    return "f11";
  case UNW_S390X_F12:
    return "f12";
  case UNW_S390X_F13:
    return "f13";
  case UNW_S390X_F14:
    return "f14";
  case UNW_S390X_F15:
    return "f15";
  }
  return "unknown register";
}
#endif // _LIBUNWIND_TARGET_S390X

#if defined(_LIBUNWIND_TARGET_LOONGARCH)
/// Registers_loongarch holds the register state of a thread in a 64-bit
/// LoongArch process.
class _LIBUNWIND_HIDDEN Registers_loongarch {
public:
  Registers_loongarch();
  Registers_loongarch(const void *registers);

  bool validRegister(int num) const;
  uint64_t getRegister(int num) const;
  void setRegister(int num, uint64_t value);
  bool validFloatRegister(int num) const;
  double getFloatRegister(int num) const;
  void setFloatRegister(int num, double value);
  bool validVectorRegister(int num) const;
  v128 getVectorRegister(int num) const;
  void setVectorRegister(int num, v128 value);
  static const char *getRegisterName(int num);
  void jumpto();
  static constexpr int lastDwarfRegNum() {
    return _LIBUNWIND_HIGHEST_DWARF_REGISTER_LOONGARCH;
  }
  static int getArch() { return REGISTERS_LOONGARCH; }

  uint64_t getSP() const { return _registers.__r[3]; }
  void setSP(uint64_t value) { _registers.__r[3] = value; }
  uint64_t getIP() const { return _registers.__pc; }
  void setIP(uint64_t value) { _registers.__pc = value; }

private:
  struct loongarch_thread_state_t {
    uint64_t __r[32];
    uint64_t __pc;
  };

  loongarch_thread_state_t _registers;
#if __loongarch_frlen == 64
  double _floats[32];
#endif
};

inline Registers_loongarch::Registers_loongarch(const void *registers) {
  static_assert((check_fit<Registers_loongarch, unw_context_t>::does_fit),
                "loongarch registers do not fit into unw_context_t");
  memcpy(&_registers, registers, sizeof(_registers));
  static_assert(sizeof(_registers) == 0x108,
                "expected float registers to be at offset 264");
#if __loongarch_frlen == 64
  memcpy(_floats, static_cast<const uint8_t *>(registers) + sizeof(_registers),
         sizeof(_floats));
#endif
}

inline Registers_loongarch::Registers_loongarch() {
  memset(&_registers, 0, sizeof(_registers));
#if __loongarch_frlen == 64
  memset(&_floats, 0, sizeof(_floats));
#endif
}

inline bool Registers_loongarch::validRegister(int regNum) const {
  if (regNum == UNW_REG_IP || regNum == UNW_REG_SP)
    return true;
  if (regNum < 0 || regNum > UNW_LOONGARCH_F31)
    return false;
  return true;
}

inline uint64_t Registers_loongarch::getRegister(int regNum) const {
  if (regNum >= UNW_LOONGARCH_R0 && regNum <= UNW_LOONGARCH_R31)
    return _registers.__r[regNum - UNW_LOONGARCH_R0];

  if (regNum == UNW_REG_IP)
    return _registers.__pc;
  if (regNum == UNW_REG_SP)
    return _registers.__r[3];
  _LIBUNWIND_ABORT("unsupported loongarch register");
}

inline void Registers_loongarch::setRegister(int regNum, uint64_t value) {
  if (regNum >= UNW_LOONGARCH_R0 && regNum <= UNW_LOONGARCH_R31)
    _registers.__r[regNum - UNW_LOONGARCH_R0] = value;
  else if (regNum == UNW_REG_IP)
    _registers.__pc = value;
  else if (regNum == UNW_REG_SP)
    _registers.__r[3] = value;
  else
    _LIBUNWIND_ABORT("unsupported loongarch register");
}

inline const char *Registers_loongarch::getRegisterName(int regNum) {
  switch (regNum) {
  case UNW_REG_IP:
    return "$pc";
  case UNW_REG_SP:
    return "$sp";
  case UNW_LOONGARCH_R0:
    return "$r0";
  case UNW_LOONGARCH_R1:
    return "$r1";
  case UNW_LOONGARCH_R2:
    return "$r2";
  case UNW_LOONGARCH_R3:
    return "$r3";
  case UNW_LOONGARCH_R4:
    return "$r4";
  case UNW_LOONGARCH_R5:
    return "$r5";
  case UNW_LOONGARCH_R6:
    return "$r6";
  case UNW_LOONGARCH_R7:
    return "$r7";
  case UNW_LOONGARCH_R8:
    return "$r8";
  case UNW_LOONGARCH_R9:
    return "$r9";
  case UNW_LOONGARCH_R10:
    return "$r10";
  case UNW_LOONGARCH_R11:
    return "$r11";
  case UNW_LOONGARCH_R12:
    return "$r12";
  case UNW_LOONGARCH_R13:
    return "$r13";
  case UNW_LOONGARCH_R14:
    return "$r14";
  case UNW_LOONGARCH_R15:
    return "$r15";
  case UNW_LOONGARCH_R16:
    return "$r16";
  case UNW_LOONGARCH_R17:
    return "$r17";
  case UNW_LOONGARCH_R18:
    return "$r18";
  case UNW_LOONGARCH_R19:
    return "$r19";
  case UNW_LOONGARCH_R20:
    return "$r20";
  case UNW_LOONGARCH_R21:
    return "$r21";
  case UNW_LOONGARCH_R22:
    return "$r22";
  case UNW_LOONGARCH_R23:
    return "$r23";
  case UNW_LOONGARCH_R24:
    return "$r24";
  case UNW_LOONGARCH_R25:
    return "$r25";
  case UNW_LOONGARCH_R26:
    return "$r26";
  case UNW_LOONGARCH_R27:
    return "$r27";
  case UNW_LOONGARCH_R28:
    return "$r28";
  case UNW_LOONGARCH_R29:
    return "$r29";
  case UNW_LOONGARCH_R30:
    return "$r30";
  case UNW_LOONGARCH_R31:
    return "$r31";
  case UNW_LOONGARCH_F0:
    return "$f0";
  case UNW_LOONGARCH_F1:
    return "$f1";
  case UNW_LOONGARCH_F2:
    return "$f2";
  case UNW_LOONGARCH_F3:
    return "$f3";
  case UNW_LOONGARCH_F4:
    return "$f4";
  case UNW_LOONGARCH_F5:
    return "$f5";
  case UNW_LOONGARCH_F6:
    return "$f6";
  case UNW_LOONGARCH_F7:
    return "$f7";
  case UNW_LOONGARCH_F8:
    return "$f8";
  case UNW_LOONGARCH_F9:
    return "$f9";
  case UNW_LOONGARCH_F10:
    return "$f10";
  case UNW_LOONGARCH_F11:
    return "$f11";
  case UNW_LOONGARCH_F12:
    return "$f12";
  case UNW_LOONGARCH_F13:
    return "$f13";
  case UNW_LOONGARCH_F14:
    return "$f14";
  case UNW_LOONGARCH_F15:
    return "$f15";
  case UNW_LOONGARCH_F16:
    return "$f16";
  case UNW_LOONGARCH_F17:
    return "$f17";
  case UNW_LOONGARCH_F18:
    return "$f18";
  case UNW_LOONGARCH_F19:
    return "$f19";
  case UNW_LOONGARCH_F20:
    return "$f20";
  case UNW_LOONGARCH_F21:
    return "$f21";
  case UNW_LOONGARCH_F22:
    return "$f22";
  case UNW_LOONGARCH_F23:
    return "$f23";
  case UNW_LOONGARCH_F24:
    return "$f24";
  case UNW_LOONGARCH_F25:
    return "$f25";
  case UNW_LOONGARCH_F26:
    return "$f26";
  case UNW_LOONGARCH_F27:
    return "$f27";
  case UNW_LOONGARCH_F28:
    return "$f28";
  case UNW_LOONGARCH_F29:
    return "$f29";
  case UNW_LOONGARCH_F30:
    return "$f30";
  case UNW_LOONGARCH_F31:
    return "$f31";
  default:
    return "unknown register";
  }
}

inline bool Registers_loongarch::validFloatRegister(int regNum) const {
  if (regNum < UNW_LOONGARCH_F0 || regNum > UNW_LOONGARCH_F31)
    return false;
  return true;
}

inline double Registers_loongarch::getFloatRegister(int regNum) const {
#if __loongarch_frlen == 64
  assert(validFloatRegister(regNum));
  return _floats[regNum - UNW_LOONGARCH_F0];
#else
  _LIBUNWIND_ABORT("libunwind not built with float support");
#endif
}

inline void Registers_loongarch::setFloatRegister(int regNum, double value) {
#if __loongarch_frlen == 64
  assert(validFloatRegister(regNum));
  _floats[regNum - UNW_LOONGARCH_F0] = value;
#else
  _LIBUNWIND_ABORT("libunwind not built with float support");
#endif
}

inline bool Registers_loongarch::validVectorRegister(int) const {
  return false;
}

inline v128 Registers_loongarch::getVectorRegister(int) const {
  _LIBUNWIND_ABORT("loongarch vector support not implemented");
}

inline void Registers_loongarch::setVectorRegister(int, v128) {
  _LIBUNWIND_ABORT("loongarch vector support not implemented");
}
#endif //_LIBUNWIND_TARGET_LOONGARCH

} // namespace libunwind

#endif // __REGISTERS_HPP__
