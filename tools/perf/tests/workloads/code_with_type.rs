// SPDX-License-Identifier: GPL-2.0

// We're going to look for this structure in the data type profiling report
#[allow(dead_code)]
struct Buf {
    data1: u64,
    data2: String,
    data3: u64,
}

#[no_mangle]
pub extern "C" fn test_rs(count: u32) {
    let mut b = Buf {
        data1: 0,
        data2: String::from("data"),
        data3: 0,
    };

    for _ in 1..count {
        b.data1 += 1;
        if b.data1 == 123 {
            b.data1 += 1;
        }

        b.data3 += b.data1;
    }
}
