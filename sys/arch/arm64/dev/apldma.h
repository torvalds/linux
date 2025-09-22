/* public domain */

struct apldma_channel *apldma_alloc_channel(unsigned int);
void	apldma_free_channel(struct apldma_channel *);
void	*apldma_allocm(struct apldma_channel *, size_t, int);
void	apldma_freem(struct apldma_channel *);
int	apldma_trigger_output(struct apldma_channel *, void *, void *, int,
	    void (*)(void *), void *, struct audio_params *);
int	apldma_halt_output(struct apldma_channel *);
