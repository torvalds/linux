#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/hashtable.h>
#include <linux/slab.h>
#include <linux/livepatch.h>

static DEFINE_HASHTABLE(klp_shadow_hash, 12);
static DEFINE_SPINLOCK(klp_shadow_lock);

/* Shadow variable structure */
struct klp_shadow {
    struct hlist_node node;
    struct rcu_head rcu_head;
    void *obj;
    unsigned long id;
    char data[];
};

static inline bool klp_shadow_match(struct klp_shadow *shadow, void *obj,
                                    unsigned long id)
{
    return shadow->obj == obj && shadow->id == id;
}

void *klp_shadow_get(void *obj, unsigned long id)
{
    struct klp_shadow *shadow;

    rcu_read_lock();
    hash_for_each_possible_rcu(klp_shadow_hash, shadow, node, (unsigned long)obj) {
        if (klp_shadow_match(shadow, obj, id)) {
            rcu_read_unlock();
            return shadow->data;
        }
    }
    rcu_read_unlock();

    return NULL;
}
EXPORT_SYMBOL_GPL(klp_shadow_get);

static void *__klp_shadow_get_or_alloc(void *obj, unsigned long id, size_t size,
                                       gfp_t gfp_flags, klp_shadow_ctor_t ctor,
                                       void *ctor_data, bool warn_on_exist)
{
    struct klp_shadow *new_shadow;
    void *shadow_data;
    unsigned long flags;

    /* Check if the shadow variable already exists */
    shadow_data = klp_shadow_get(obj, id);
    if (shadow_data)
        goto exists;

    /* Allocate a new shadow variable */
    new_shadow = kzalloc(size + sizeof(*new_shadow), gfp_flags);
    if (unlikely(!new_shadow))
        return NULL;

    /* Lock only the necessary critical section */
    spin_lock_irqsave(&klp_shadow_lock, flags);
    shadow_data = klp_shadow_get(obj, id);
    if (unlikely(shadow_data)) {
        spin_unlock_irqrestore(&klp_shadow_lock, flags);
        kfree(new_shadow);
        goto exists;
    }

    /* Initialize shadow variable */
    new_shadow->obj = obj;
    new_shadow->id = id;

    if (ctor) {
        int err = ctor(obj, new_shadow->data, ctor_data);
        if (unlikely(err)) {
            spin_unlock_irqrestore(&klp_shadow_lock, flags);
            kfree(new_shadow);
            pr_err("Failed to construct shadow variable <%p, %lx> (%d)\n",
                   obj, id, err);
            return NULL;
        }
    }

    hash_add_rcu(klp_shadow_hash, &new_shadow->node, (unsigned long)new_shadow->obj);
    spin_unlock_irqrestore(&klp_shadow_lock, flags);

    return new_shadow->data;

exists:
    if (warn_on_exist) {
        WARN(1, "Duplicate shadow variable <%p, %lx>\n", obj, id);
        return NULL;
    }

    return shadow_data;
}

void *klp_shadow_alloc(void *obj, unsigned long id, size_t size, gfp_t gfp_flags,
                       klp_shadow_ctor_t ctor, void *ctor_data)
{
    return __klp_shadow_get_or_alloc(obj, id, size, gfp_flags, ctor, ctor_data, true);
}
EXPORT_SYMBOL_GPL(klp_shadow_alloc);

void *klp_shadow_get_or_alloc(void *obj, unsigned long id, size_t size,
                              gfp_t gfp_flags, klp_shadow_ctor_t ctor,
                              void *ctor_data)
{
    return __klp_shadow_get_or_alloc(obj, id, size, gfp_flags, ctor, ctor_data, false);
}
EXPORT_SYMBOL_GPL(klp_shadow_get_or_alloc);

static void klp_shadow_free_struct(struct klp_shadow *shadow,
                                   klp_shadow_dtor_t dtor)
{
    hash_del_rcu(&shadow->node);
    if (dtor)
        dtor(shadow->obj, shadow->data);
    kfree_rcu(shadow, rcu_head);
}

void klp_shadow_free(void *obj, unsigned long id, klp_shadow_dtor_t dtor)
{
    struct klp_shadow *shadow;
    unsigned long flags;

    spin_lock_irqsave(&klp_shadow_lock, flags);
    hash_for_each_possible(klp_shadow_hash, shadow, node, (unsigned long)obj) {
        if (klp_shadow_match(shadow, obj, id)) {
            klp_shadow_free_struct(shadow, dtor);
            break;
        }
    }
    spin_unlock_irqrestore(&klp_shadow_lock, flags);
}
EXPORT_SYMBOL_GPL(klp_shadow_free);

void klp_shadow_free_all(unsigned long id, klp_shadow_dtor_t dtor)
{
    struct klp_shadow *shadow;
    unsigned long flags;
    int i;

    spin_lock_irqsave(&klp_shadow_lock, flags);
    hash_for_each_rcu(klp_shadow_hash, i, shadow, node) {
        if (klp_shadow_match(shadow, shadow->obj, id)) {
            klp_shadow_free_struct(shadow, dtor);
        }
    }
    spin_unlock_irqrestore(&klp_shadow_lock, flags);
}
EXPORT_SYMBOL_GPL(klp_shadow_free_all);
