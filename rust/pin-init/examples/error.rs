// SPDX-License-Identifier: Apache-2.0 OR MIT

#![cfg_attr(feature = "alloc", feature(allocator_api))]

use core::convert::Infallible;

#[cfg(feature = "alloc")]
use std::alloc::AllocError;

#[derive(Debug)]
pub struct Error;

impl From<Infallible> for Error {
    fn from(e: Infallible) -> Self {
        match e {}
    }
}

#[cfg(feature = "alloc")]
impl From<AllocError> for Error {
    fn from(_: AllocError) -> Self {
        Self
    }
}

#[allow(dead_code)]
fn main() {
    let _ = Error;
}
