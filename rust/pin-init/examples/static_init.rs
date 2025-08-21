// SPDX-License-Identifier: Apache-2.0 OR MIT

#![allow(clippy::undocumented_unsafe_blocks)]
#![cfg_attr(feature = "alloc", feature(allocator_api))]
#![cfg_attr(not(RUSTC_LINT_REASONS_IS_STABLE), feature(lint_reasons))]
#![allow(unused_imports)]

use core::{
    cell::{Cell, UnsafeCell},
    mem::MaybeUninit,
    ops,
    pin::Pin,
    time::Duration,
};
use pin_init::*;
#[cfg(feature = "std")]
use std::{
    sync::Arc,
    thread::{sleep, Builder},
};

#[allow(unused_attributes)]
mod mutex;
use mutex::*;

pub struct StaticInit<T, I> {
    cell: UnsafeCell<MaybeUninit<T>>,
    init: Cell<Option<I>>,
    lock: SpinLock,
    present: Cell<bool>,
}

unsafe impl<T: Sync, I> Sync for StaticInit<T, I> {}
unsafe impl<T: Send, I> Send for StaticInit<T, I> {}

impl<T, I: PinInit<T>> StaticInit<T, I> {
    pub const fn new(init: I) -> Self {
        Self {
            cell: UnsafeCell::new(MaybeUninit::uninit()),
            init: Cell::new(Some(init)),
            lock: SpinLock::new(),
            present: Cell::new(false),
        }
    }
}

impl<T, I: PinInit<T>> ops::Deref for StaticInit<T, I> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        if self.present.get() {
            unsafe { (*self.cell.get()).assume_init_ref() }
        } else {
            println!("acquire spinlock on static init");
            let _guard = self.lock.acquire();
            println!("rechecking present...");
            std::thread::sleep(std::time::Duration::from_millis(200));
            if self.present.get() {
                return unsafe { (*self.cell.get()).assume_init_ref() };
            }
            println!("doing init");
            let ptr = self.cell.get().cast::<T>();
            match self.init.take() {
                Some(f) => unsafe { f.__pinned_init(ptr).unwrap() },
                None => unsafe { core::hint::unreachable_unchecked() },
            }
            self.present.set(true);
            unsafe { (*self.cell.get()).assume_init_ref() }
        }
    }
}

pub struct CountInit;

unsafe impl PinInit<CMutex<usize>> for CountInit {
    unsafe fn __pinned_init(
        self,
        slot: *mut CMutex<usize>,
    ) -> Result<(), core::convert::Infallible> {
        let init = CMutex::new(0);
        std::thread::sleep(std::time::Duration::from_millis(1000));
        unsafe { init.__pinned_init(slot) }
    }
}

pub static COUNT: StaticInit<CMutex<usize>, CountInit> = StaticInit::new(CountInit);

fn main() {
    #[cfg(feature = "std")]
    {
        let mtx: Pin<Arc<CMutex<usize>>> = Arc::pin_init(CMutex::new(0)).unwrap();
        let mut handles = vec![];
        let thread_count = 20;
        let workload = 1_000;
        for i in 0..thread_count {
            let mtx = mtx.clone();
            handles.push(
                Builder::new()
                    .name(format!("worker #{i}"))
                    .spawn(move || {
                        for _ in 0..workload {
                            *COUNT.lock() += 1;
                            std::thread::sleep(std::time::Duration::from_millis(10));
                            *mtx.lock() += 1;
                            std::thread::sleep(std::time::Duration::from_millis(10));
                            *COUNT.lock() += 1;
                        }
                        println!("{i} halfway");
                        sleep(Duration::from_millis((i as u64) * 10));
                        for _ in 0..workload {
                            std::thread::sleep(std::time::Duration::from_millis(10));
                            *mtx.lock() += 1;
                        }
                        println!("{i} finished");
                    })
                    .expect("should not fail"),
            );
        }
        for h in handles {
            h.join().expect("thread panicked");
        }
        println!("{:?}, {:?}", &*mtx.lock(), &*COUNT.lock());
        assert_eq!(*mtx.lock(), workload * thread_count * 2);
    }
}
