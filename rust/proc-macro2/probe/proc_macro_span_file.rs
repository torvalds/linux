// The subset of Span's API stabilized in Rust 1.88.

extern crate proc_macro;

use proc_macro::Span;
use std::path::PathBuf;

pub fn file(this: &Span) -> String {
    this.file()
}

pub fn local_file(this: &Span) -> Option<PathBuf> {
    this.local_file()
}
