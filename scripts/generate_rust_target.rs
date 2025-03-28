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
    Array(Vec<Value>),
    Object(Object),
}

type Object = Vec<(String, Value)>;

fn comma_sep<T>(
    seq: &[T],
    formatter: &mut Formatter<'_>,
    f: impl Fn(&mut Formatter<'_>, &T) -> Result,
) -> Result {
    if let [ref rest @ .., ref last] = seq[..] {
        for v in rest {
            f(formatter, v)?;
            formatter.write_str(",")?;
        }
        f(formatter, last)?;
    }
    Ok(())
}

/// Minimal "almost JSON" generator (e.g. no `null`s, no escaping),
/// enough for this purpose.
impl Display for Value {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> Result {
        match self {
            Value::Boolean(boolean) => write!(formatter, "{}", boolean),
            Value::Number(number) => write!(formatter, "{}", number),
            Value::String(string) => write!(formatter, "\"{}\"", string),
            Value::Array(values) => {
                formatter.write_str("[")?;
                comma_sep(&values[..], formatter, |formatter, v| v.fmt(formatter))?;
                formatter.write_str("]")
            }
            Value::Object(object) => {
                formatter.write_str("{")?;
                comma_sep(&object[..], formatter, |formatter, v| {
                    write!(formatter, "\"{}\": {}", v.0, v.1)
                })?;
                formatter.write_str("}")
            }
        }
    }
}

impl From<bool> for Value {
    fn from(value: bool) -> Self {
        Self::Boolean(value)
    }
}

impl From<i32> for Value {
    fn from(value: i32) -> Self {
        Self::Number(value)
    }
}

impl From<String> for Value {
    fn from(value: String) -> Self {
        Self::String(value)
    }
}

impl From<&str> for Value {
    fn from(value: &str) -> Self {
        Self::String(value.to_string())
    }
}

impl From<Object> for Value {
    fn from(object: Object) -> Self {
        Self::Object(object)
    }
}

impl<T: Into<Value>, const N: usize> From<[T; N]> for Value {
    fn from(i: [T; N]) -> Self {
        Self::Array(i.into_iter().map(|v| v.into()).collect())
    }
}

struct TargetSpec(Object);

impl TargetSpec {
    fn new() -> TargetSpec {
        TargetSpec(Vec::new())
    }

    fn push(&mut self, key: &str, value: impl Into<Value>) {
        self.0.push((key.to_string(), value.into()));
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

    /// Is the rustc version at least `major.minor.patch`?
    fn rustc_version_atleast(&self, major: u32, minor: u32, patch: u32) -> bool {
        let check_version = 100000 * major + 100 * minor + patch;
        let actual_version = self
            .0
            .get("CONFIG_RUSTC_VERSION")
            .unwrap()
            .parse::<u32>()
            .unwrap();
        check_version <= actual_version
    }
}

fn main() {
    let cfg = KernelConfig::from_stdin();
    let mut ts = TargetSpec::new();

    // `llvm-target`s are taken from `scripts/Makefile.clang`.
    if cfg.has("ARM64") {
        panic!("arm64 uses the builtin rustc aarch64-unknown-none target");
    } else if cfg.has("RISCV") {
        if cfg.has("64BIT") {
            panic!("64-bit RISC-V uses the builtin rustc riscv64-unknown-none-elf target");
        } else {
            panic!("32-bit RISC-V is an unsupported architecture");
        }
    } else if cfg.has("X86_64") {
        ts.push("arch", "x86_64");
        if cfg.rustc_version_atleast(1, 86, 0) {
            ts.push("rustc-abi", "x86-softfloat");
        }
        ts.push(
            "data-layout",
            "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128",
        );
        let mut features = "-mmx,+soft-float".to_string();
        if cfg.has("MITIGATION_RETPOLINE") {
            // The kernel uses `-mretpoline-external-thunk` (for Clang), which Clang maps to the
            // target feature of the same name plus the other two target features in
            // `clang/lib/Driver/ToolChains/Arch/X86.cpp`. These should be eventually enabled via
            // `-Ctarget-feature` when `rustc` starts recognizing them (or via a new dedicated
            // flag); see https://github.com/rust-lang/rust/issues/116852.
            features += ",+retpoline-external-thunk";
            features += ",+retpoline-indirect-branches";
            features += ",+retpoline-indirect-calls";
        }
        if cfg.has("MITIGATION_SLS") {
            // The kernel uses `-mharden-sls=all`, which Clang maps to both these target features in
            // `clang/lib/Driver/ToolChains/Arch/X86.cpp`. These should be eventually enabled via
            // `-Ctarget-feature` when `rustc` starts recognizing them (or via a new dedicated
            // flag); see https://github.com/rust-lang/rust/issues/116851.
            features += ",+harden-sls-ijmp";
            features += ",+harden-sls-ret";
        }
        ts.push("features", features);
        ts.push("llvm-target", "x86_64-linux-gnu");
        ts.push("supported-sanitizers", ["kcfi", "kernel-address"]);
        ts.push("target-pointer-width", "64");
    } else if cfg.has("X86_32") {
        // This only works on UML, as i386 otherwise needs regparm support in rustc
        if !cfg.has("UML") {
            panic!("32-bit x86 only works under UML");
        }
        ts.push("arch", "x86");
        if cfg.rustc_version_atleast(1, 86, 0) {
            ts.push("rustc-abi", "x86-softfloat");
        }
        ts.push(
            "data-layout",
            "e-m:e-p:32:32-p270:32:32-p271:32:32-p272:64:64-i128:128-f64:32:64-f80:32-n8:16:32-S128",
        );
        let mut features = "-mmx,+soft-float".to_string();
        if cfg.has("MITIGATION_RETPOLINE") {
            features += ",+retpoline-external-thunk";
        }
        ts.push("features", features);
        ts.push("llvm-target", "i386-unknown-linux-gnu");
        ts.push("target-pointer-width", "32");
    } else if cfg.has("LOONGARCH") {
        panic!("loongarch uses the builtin rustc loongarch64-unknown-none-softfloat target");
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
