// SPDX-License-Identifier: GPL-2.0

// Why .data, .bss and .rodata of a program in Rust are in arena.
//
// A reference in Rust is an address. It doesn't say what it points to and it
// can be stored in data. The list below has a node in each of the sections.
// The nodes are linked by references that are in the data:
//   IN_BSS.next is stored by the program,
//   IN_DATA.next is a relocation in .data against .rodata.
// sum() loads the references back and reads the three nodes with the same insn.
//
// When the sections are array maps libbpf skips the relocation in .data, and
// what sum() loads from a node is a number that can't be dereferenced:
//   R1 invalid mem access 'scalar'
// In arena the address of a node is a number to begin with.

#![no_std]
#![no_main]

// Tell libbpf to keep .data, .bss and .rodata in arena.
#[used]
#[link_section = ".arena.data"]
static DATA_IN_ARENA: u8 = 0;

#[used]
#[link_section = "license"]
static LICENSE: [u8; 4] = *b"GPL\0";

// panic=abort is a stop gap until panic=unwind is supported.
// Nothing here panics, so the handler is not a part of the program.
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}

pub struct Node {
    val: u32,
    next: Option<&'static Node>,
}

// no_mangle makes them visible outside, so LLVM can't fold the list into a constant.
#[no_mangle]
static IN_RODATA: Node = Node { val: 3, next: None };
#[no_mangle]
static mut IN_DATA: Node = Node {
    val: 20,
    next: Some(&IN_RODATA),
};
#[no_mangle]
static mut IN_BSS: Node = Node { val: 0, next: None };

#[inline(never)]
fn sum(mut node: Option<&Node>) -> u32 {
    let mut sum = 0;
    // The verifier wants a bound.
    for _ in 0..8 {
        let Some(n) = node else { break };
        sum += n.val;
        node = n.next;
    }
    sum
}

#[no_mangle]
#[link_section = "syscall"]
pub extern "C" fn list_in_data(_ctx: *mut u8) -> u32 {
    unsafe {
        let head = &mut *&raw mut IN_BSS;
        head.val = 100;
        head.next = Some(&*&raw const IN_DATA);
        sum(Some(head))
    }
}
