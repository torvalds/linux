#![allow(dead_code)]

#[cfg(proc_macro_span)]
pub(crate) mod proc_macro_span;

#[cfg(proc_macro_span_file)]
pub(crate) mod proc_macro_span_file;

#[cfg(proc_macro_span_location)]
pub(crate) mod proc_macro_span_location;
