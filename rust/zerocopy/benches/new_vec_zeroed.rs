use zerocopy::*;

#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_new_vec_zeroed(len: usize) -> Option<Vec<format::LocoPacket>> {
    FromZeros::new_vec_zeroed(len).ok()
}
