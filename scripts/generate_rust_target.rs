// SPDX-License-Identifier: GPL-2.0

//! The custom target specification file generator for `rustc`.
//!
//! To configure a target from scratch, a JSON-encoded file has to be passed
//! to `rustc` (introduced in [RFC 131]). These options and the file itself are
//! unstable. Eventually, `rustc` should provide a way to do this in a stable
//! manner. For instance, via command-line arguments. Therefore, this file
//! should avoid using keys which can be set via `-C` or `-Z` options.
//!
//! [RFC 131]: https://rust-lang.github.io/rfcs/0131-target-specification.html

use std::{
    collections::HashMap,
    fmt::{Display, Formatter, Result},
    io::BufRead,
};

enum Value {
    Boolean(bool),
    Number(i32),
    String(String),
    Object(Object),
}

type Object = Vec<(String, Value)>;

/// Minimal "almost JSON" generator (e.g. no `null`s, no arrays, no escaping),
/// enough for this purpose.
impl Display for Value {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> Result {
        match self {
            Value::Boolean(boolean) => write!(formatter, "{}", boolean),
            Value::Number(number) => write!(formatter, "{}", number),
            Value::String(string) => write!(formatter, "\"{}\"", string),
            Value::Object(object) => {
                formatter.write_str("{")?;
                if let [ref rest @ .., ref last] = object[..] {
                    for (key, value) in rest {
                        write!(formatter, "\"{}\": {},", key, value)?;
                    }
                    write!(formatter, "\"{}\": {}", last.0, last.1)?;
                }
                formatter.write_str("}")
            }
        }
    }
}

struct TargetSpec(Object);

impl TargetSpec {
    fn new() -> TargetSpec {
        TargetSpec(Vec::new())
    }
}

trait Push<T> {
    fn push(&mut self, key: &str, value: T);
}

impl Push<bool> for TargetSpec {
    fn push(&mut self, key: &str, value: bool) {
        self.0.push((key.to_string(), Value::Boolean(value)));
    }
}

impl Push<i32> for TargetSpec {
    fn push(&mut self, key: &str, value: i32) {
        self.0.push((key.to_string(), Value::Number(value)));
    }
}

impl Push<String> for TargetSpec {
    fn push(&mut self, key: &str, value: String) {
        self.0.push((key.to_string(), Value::String(value)));
    }
}

impl Push<&str> for TargetSpec {
    fn push(&mut self, key: &str, value: &str) {
        self.push(key, value.to_string());
    }
}

impl Push<Object> for TargetSpec {
    fn push(&mut self, key: &str, value: Object) {
        self.0.push((key.to_string(), Value::Object(value)));
    }
}

impl Display for TargetSpec {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> Result {
        // We add some newlines for clarity.
        formatter.write_str("{\n")?;
        if let [ref rest @ .., ref last] = self.0[..] {
            for (key, value) in rest {
                write!(formatter, "    \"{}\": {},\n", key, value)?;
            }
            write!(formatter, "    \"{}\": {}\n", last.0, last.1)?;
        }
        formatter.write_str("}")
    }
}

struct KernelConfig(HashMap<String, String>);

impl KernelConfig {
    /// Parses `include/config/auto.conf` from `stdin`.
    fn from_stdin() -> KernelConfig {
        let mut result = HashMap::new();

        let stdin = std::io::stdin();
        let mut handle = stdin.lock();
        let mut line = String::new();

        loop {
            line.clear();

            if handle.read_line(&mut line).unwrap() == 0 {
                break;
            }

            if line.starts_with('#') {
                continue;
            }

            let (key, value) = line.split_once('=').expect("Missing `=` in line.");
            result.insert(key.to_string(), value.trim_end_matches('\n').to_string());
        }

        KernelConfig(result)
    }

    /// Does the option exist in the configuration (any value)?
    ///
    /// The argument must be passed without the `CONFIG_` prefix.
    /// This avoids repetition and it also avoids `fixdep` making us
    /// depend on it.
    fn has(&self, option: &str) -> bool {
        let option = "CONFIG_".to_owned() + option;
        self.0.contains_key(&option)
    }
}

fn main() {
    let cfg = KernelConfig::from_stdin();
    let mut ts = TargetSpec::new();

    // `llvm-target`s are taken from `scripts/Makefile.clang`.
    if cfg.has("X86_64") {
        ts.push("arch", "x86_64");
        ts.push(
            "data-layout",
            "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128",
        );
        let mut features = "-3dnow,-3dnowa,-mmx,+soft-float".to_string();
        if cfg.has("RETPOLINE") {
            features += ",+retpoline-external-thunk";
        }
        ts.push("features", features);
        ts.push("llvm-target", "x86_64-linux-gnu");
        ts.push("target-pointer-width", "64");
    } else if cfg.has("LOONGARCH") {
        ts.push("arch", "loongarch64");
        ts.push("data-layout", "e-m:e-p:64:64-i64:64-i128:128-n64-S128");
        ts.push("features", "-f,-d");
        ts.push("llvm-target", "loongarch64-linux-gnusf");
        ts.push("llvm-abiname", "lp64s");
        ts.push("target-pointer-width", "64");
    } else {
        panic!("Unsupported architecture");
    }

    ts.push("emit-debug-gdb-scripts", false);
    ts.push("frame-pointer", "may-omit");
    ts.push(
        "stack-probes",
        vec![("kind".to_string(), Value::String("none".to_string()))],
    );

    // Everything else is LE, whether `CPU_LITTLE_ENDIAN` is declared or not
    // (e.g. x86). It is also `rustc`'s default.
    if cfg.has("CPU_BIG_ENDIAN") {
        ts.push("target-endian", "big");
    }

    println!("{}", ts);
}
