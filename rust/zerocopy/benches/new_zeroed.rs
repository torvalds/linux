use zerocopy::*;

#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_new_zeroed() -> format::LocoPacket {
    FromZeros::new_zeroed()
}
