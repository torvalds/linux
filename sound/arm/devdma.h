void devdma_hw_free(struct device *dev, snd_pcm_substream_t *substream);
int devdma_hw_alloc(struct device *dev, snd_pcm_substream_t *substream, size_t size);
int devdma_mmap(struct device *dev, snd_pcm_substream_t *substream, struct vm_area_struct *vma);
