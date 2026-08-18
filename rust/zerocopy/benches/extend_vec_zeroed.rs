use zerocopy::*;

#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_extend_vec_zeroed(v: &mut Vec<format::LocoPacket>, additional: usize) -> Option<()> {
    FromZeros::extend_vec_zeroed(v, additional).ok()
}
