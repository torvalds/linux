// SPDX-License-Identifier: GPL-2.0

use crate::helpers::*;
use proc_macro::{token_stream, Delimiter, Literal, TokenStream, TokenTree};
use std::fmt::Write;

fn expect_string_array(it: &mut token_stream::IntoIter) -> Vec<String> {
    let group = expect_group(it);
    assert_eq!(group.delimiter(), Delimiter::Bracket);
    let mut values = Vec::new();
    let mut it = group.stream().into_iter();

    while let Some(val) = try_string(&mut it) {
        assert!(val.is_ascii(), "Expected ASCII string");
        values.push(val);
        match it.next() {
            Some(TokenTree::Punct(punct)) => assert_eq!(punct.as_char(), ','),
            None => break,
            _ => panic!("Expected ',' or end of array"),
        }
    }
    values
}

struct ModInfoBuilder<'a> {
    module: &'a str,
    counter: usize,
    buffer: String,
}

impl<'a> ModInfoBuilder<'a> {
    fn new(module: &'a str) -> Self {
        ModInfoBuilder {
            module,
            counter: 0,
            buffer: String::new(),
        }
    }

    fn emit_base(&mut self, field: &str, content: &str, builtin: bool) {
        let string = if builtin {
            // Built-in modules prefix their modinfo strings by `module.`.
            format!(
                "{module}.{field}={content}\0",
                module = self.module,
                field = field,
                content = content
            )
        } else {
            // Loadable modules' modinfo strings go as-is.
            format!("{field}={content}\0", field = field, content = content)
        };

        write!(
            &mut self.buffer,
            "
                {cfg}
                #[doc(hidden)]
                #[link_section = \".modinfo\"]
                #[used]
                pub static __{module}_{counter}: [u8; {length}] = *{string};
            ",
            cfg = if builtin {
                "#[cfg(not(MODULE))]"
            } else {
                "#[cfg(MODULE)]"
            },
            module = self.module.to_uppercase(),
            counter = self.counter,
            length = string.len(),
            string = Literal::byte_string(string.as_bytes()),
        )
        .unwrap();

        self.counter += 1;
    }

    fn emit_only_builtin(&mut self, field: &str, content: &str) {
        self.emit_base(field, content, true)
    }

    fn emit_only_loadable(&mut self, field: &str, content: &str) {
        self.emit_base(field, content, false)
    }

    fn emit(&mut self, field: &str, content: &str) {
        self.emit_only_builtin(field, content);
        self.emit_only_loadable(field, content);
    }
}

#[derive(Debug, Default)]
struct ModuleInfo {
    type_: String,
    license: String,
    name: String,
    author: Option<String>,
    description: Option<String>,
    alias: Option<Vec<String>>,
}

impl ModuleInfo {
    fn parse(it: &mut token_stream::IntoIter) -> Self {
        let mut info = ModuleInfo::default();

        const EXPECTED_KEYS: &[&str] =
            &["type", "name", "author", "description", "license", "alias"];
        const REQUIRED_KEYS: &[&str] = &["type", "name", "license"];
        let mut seen_keys = Vec::new();

        loop {
            let key = match it.next() {
                Some(TokenTree::Ident(ident)) => ident.to_string(),
                Some(_) => panic!("Expected Ident or end"),
                None => break,
            };

            if seen_keys.contains(&key) {
                panic!(
                    "Duplicated key \"{}\". Keys can only be specified once.",
                    key
                );
            }

            assert_eq!(expect_punct(it), ':');

            match key.as_str() {
                "type" => info.type_ = expect_ident(it),
                "name" => info.name = expect_string_ascii(it),
                "author" => info.author = Some(expect_string(it)),
                "description" => info.description = Some(expect_string(it)),
                "license" => info.license = expect_string_ascii(it),
                "alias" => info.alias = Some(expect_string_array(it)),
                _ => panic!(
                    "Unknown key \"{}\". Valid keys are: {:?}.",
                    key, EXPECTED_KEYS
                ),
            }

            assert_eq!(expect_punct(it), ',');

            seen_keys.push(key);
        }

        expect_end(it);

        for key in REQUIRED_KEYS {
            if !seen_keys.iter().any(|e| e == key) {
                panic!("Missing required key \"{}\".", key);
            }
        }

        let mut ordered_keys: Vec<&str> = Vec::new();
        for key in EXPECTED_KEYS {
            if seen_keys.iter().any(|e| e == key) {
                ordered_keys.push(key);
            }
        }

        if seen_keys != ordered_keys {
            panic!(
                "Keys are not ordered as expected. Order them like: {:?}.",
                ordered_keys
            );
        }

        info
    }
}

pub(crate) fn module(ts: TokenStream) -> TokenStream {
    let mut it = ts.into_iter();

    let info = ModuleInfo::parse(&mut it);

    let mut modinfo = ModInfoBuilder::new(info.name.as_ref());
    if let Some(author) = info.author {
        modinfo.emit("author", &author);
    }
    if let Some(description) = info.description {
        modinfo.emit("description", &description);
    }
    modinfo.emit("license", &info.license);
    if let Some(aliases) = info.alias {
        for alias in aliases {
            modinfo.emit("alias", &alias);
        }
    }

    // Built-in modules also export the `file` modinfo string.
    let file =
        std::env::var("RUST_MODFILE").expect("Unable to fetch RUST_MODFILE environmental variable");
    modinfo.emit_only_builtin("file", &file);

    format!(
        "
            /// The module name.
            ///
            /// Used by the printing macros, e.g. [`info!`].
            const __LOG_PREFIX: &[u8] = b\"{name}\\0\";

            /// The \"Rust loadable module\" mark.
            //
            // This may be best done another way later on, e.g. as a new modinfo
            // key or a new section. For the moment, keep it simple.
            #[cfg(MODULE)]
            #[doc(hidden)]
            #[used]
            static __IS_RUST_MODULE: () = ();

            static mut __MOD: Option<{type_}> = None;

            // SAFETY: `__this_module` is constructed by the kernel at load time and will not be
            // freed until the module is unloaded.
            #[cfg(MODULE)]
            static THIS_MODULE: kernel::ThisModule = unsafe {{
                kernel::ThisModule::from_ptr(&kernel::bindings::__this_module as *const _ as *mut _)
            }};
            #[cfg(not(MODULE))]
            static THIS_MODULE: kernel::ThisModule = unsafe {{
                kernel::ThisModule::from_ptr(core::ptr::null_mut())
            }};

            // Loadable modules need to export the `{{init,cleanup}}_module` identifiers.
            /// # Safety
            ///
            /// This function must not be called after module initialization, because it may be
            /// freed after that completes.
            #[cfg(MODULE)]
            #[doc(hidden)]
            #[no_mangle]
            #[link_section = \".init.text\"]
            pub unsafe extern \"C\" fn init_module() -> core::ffi::c_int {{
                __init()
            }}

            #[cfg(MODULE)]
            #[doc(hidden)]
            #[no_mangle]
            pub extern \"C\" fn cleanup_module() {{
                __exit()
            }}

            // Built-in modules are initialized through an initcall pointer
            // and the identifiers need to be unique.
            #[cfg(not(MODULE))]
            #[cfg(not(CONFIG_HAVE_ARCH_PREL32_RELOCATIONS))]
            #[doc(hidden)]
            #[link_section = \"{initcall_section}\"]
            #[used]
            pub static __{name}_initcall: extern \"C\" fn() -> core::ffi::c_int = __{name}_init;

            #[cfg(not(MODULE))]
            #[cfg(CONFIG_HAVE_ARCH_PREL32_RELOCATIONS)]
            core::arch::global_asm!(
                r#\".section \"{initcall_section}\", \"a\"
                __{name}_initcall:
                    .long   __{name}_init - .
                    .previous
                \"#
            );

            #[cfg(not(MODULE))]
            #[doc(hidden)]
            #[no_mangle]
            pub extern \"C\" fn __{name}_init() -> core::ffi::c_int {{
                __init()
            }}

            #[cfg(not(MODULE))]
            #[doc(hidden)]
            #[no_mangle]
            pub extern \"C\" fn __{name}_exit() {{
                __exit()
            }}

            fn __init() -> core::ffi::c_int {{
                match <{type_} as kernel::Module>::init(&THIS_MODULE) {{
                    Ok(m) => {{
                        unsafe {{
                            __MOD = Some(m);
                        }}
                        return 0;
                    }}
                    Err(e) => {{
                        return e.to_errno();
                    }}
                }}
            }}

            fn __exit() {{
                unsafe {{
                    // Invokes `drop()` on `__MOD`, which should be used for cleanup.
                    __MOD = None;
                }}
            }}

            {modinfo}
        ",
        type_ = info.type_,
        name = info.name,
        modinfo = modinfo.buffer,
        initcall_section = ".initcall6.init"
    )
    .parse()
    .expect("Error parsing formatted string into token stream.")
}
