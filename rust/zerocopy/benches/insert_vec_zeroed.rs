use zerocopy::*;

#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_insert_vec_zeroed(
    v: &mut Vec<format::LocoPacket>,
    position: usize,
    additional: usize,
) -> Option<()> {
    FromZeros::insert_vec_zeroed(v, position, additional).ok()
}
