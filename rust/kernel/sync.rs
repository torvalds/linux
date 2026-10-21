// SPDX-License-Identifier: GPL-2.0

//! Synchronisation primitives.
//!
//! This module contains the kernel APIs related to synchronisation that have been ported or
//! wrapped for usage by Rust code in the kernel.

mod arc;
pub mod aref;
pub mod atomic;
pub mod barrier;
pub mod completion;
mod condvar;
pub mod lock;
pub mod lockdep;
mod locked_by;
pub mod poll;
pub mod rcu;
mod refcount;
mod set_once;
pub mod srcu;

pub use arc::{Arc, ArcBorrow, UniqueArc};
pub use completion::Completion;
pub use condvar::{new_condvar, CondVar, CondVarTimeoutResult};
pub use lock::global::{global_lock, GlobalGuard, GlobalLock, GlobalLockBackend, GlobalLockedBy};
pub use lock::mutex::{new_mutex, Mutex, MutexGuard};
pub use lock::spinlock::{
    new_spinlock,
    new_spinlock_irq,
    SpinLock,
    SpinLockGuard,
    SpinLockIrq,
    SpinLockIrqGuard, //
};
pub use lockdep::{
    static_lock_class,
    LockClassKey, //
};
pub use locked_by::LockedBy;
pub use refcount::Refcount;
pub use set_once::SetOnce;
pub use srcu::Srcu;
