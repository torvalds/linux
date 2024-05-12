//!Scull module in Rust

use kernel::prelude::*;

module! {
    type: Scull,
    name: "scull",
    author: "Guilherme Giacomo Simoes <trintaeoitogc@gmail.com>",
    description: "aaa, ze da manga",
    license: "GPL",
}

struct Scull;
impl kernel::Module for Scull {
    fn init(_module: &'static ThisModule) -> Result<Self> {
        Ok(Scull)
    }
}
