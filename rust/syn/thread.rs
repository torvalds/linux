use std::fmt::{self, Debug};
use std::thread::{self, ThreadId};

/// ThreadBound is a Sync-maker and Send-maker that allows accessing a value
/// of type T only from the original thread on which the ThreadBound was
/// constructed.
pub(crate) struct ThreadBound<T> {
    value: T,
    thread_id: ThreadId,
}

unsafe impl<T> Sync for ThreadBound<T> {}

// Send bound requires Copy, as otherwise Drop could run in the wrong place.
//
// Today Copy and Drop are mutually exclusive so `T: Copy` implies `T: !Drop`.
// This impl needs to be revisited if that restriction is relaxed in the future.
unsafe impl<T: Copy> Send for ThreadBound<T> {}

impl<T> ThreadBound<T> {
    pub(crate) fn new(value: T) -> Self {
        ThreadBound {
            value,
            thread_id: thread::current().id(),
        }
    }

    pub(crate) fn get(&self) -> Option<&T> {
        if thread::current().id() == self.thread_id {
            Some(&self.value)
        } else {
            None
        }
    }
}

impl<T: Debug> Debug for ThreadBound<T> {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        match self.get() {
            Some(value) => Debug::fmt(value, formatter),
            None => formatter.write_str("unknown"),
        }
    }
}

// Copy the bytes of T, even if the currently running thread is the "wrong"
// thread. This is fine as long as the original thread is not simultaneously
// mutating this value via interior mutability, which would be a data race.
//
// Currently `T: Copy` is sufficient to guarantee that T contains no interior
// mutability, because _all_ interior mutability in Rust is built on
// std::cell::UnsafeCell, which has no Copy impl. This impl needs to be
// revisited if that restriction is relaxed in the future.
impl<T: Copy> Copy for ThreadBound<T> {}

impl<T: Copy> Clone for ThreadBound<T> {
    fn clone(&self) -> Self {
        *self
    }
}
