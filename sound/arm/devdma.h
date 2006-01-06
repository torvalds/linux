void devdma_hw_free(struct device *dev, struct snd_pcm_substream *substream);
int devdma_hw_alloc(struct device *dev, struct snd_pcm_substream *substream, size_t size);
int devdma_mmap(struct device *dev, struct snd_pcm_substream *substream, struct vm_area_struct *vma);
