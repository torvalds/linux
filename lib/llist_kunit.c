#include <kunit/test.h>
#include <linux/llist.h>

struct llist_test_struct{
    int data;
    struct llist_node node;
};

static void llist_test_init_llist(struct kunit *test)
{
    struct llist_head llist1 = LLIST_HEAD_INIT(llist1);
    LLIST_HEAD(llist2);
    struct llist_head llist3;
    struct llist_head *llist4;
    struct llist_head *llist5;
    
    init_llist_head(&llist3);

    llist4 = kzalloc(sizeof(*llist4), GFP_KERNEL | __GFP_NOFAIL);
    init_llist_head(llist4); 

    llist5 = kmalloc(sizeof(*llist5), GFP_KERNEL | __GFP_NOFAIL);
    memset(llist5, 0xFF, sizeof(*llist5));
    init_llist_head(llist5);

    KUNIT_EXPECT_TRUE(test, llist_empty(&llist1));
    KUNIT_EXPECT_TRUE(test, llist_empty(&llist2));
    KUNIT_EXPECT_TRUE(test, llist_empty(&llist3));
    KUNIT_EXPECT_TRUE(test, llist_empty(llist4));
    KUNIT_EXPECT_TRUE(test, llist_empty(llist5));

    kfree(llist4);
    kfree(llist5);
}

static void llist_test_init_llist_node(struct kunit *test)
{
    struct llist_node a;
    init_llist_node(&a);
    
    KUNIT_EXPECT_PTR_EQ(test, a.next, &a);
}

static void llist_test_llist_entry(struct kunit *test)
{
    struct llist_test_struct test_struct;

    KUNIT_EXPECT_PTR_EQ(test, &test_struct, llist_entry(&(test_struct.node), struct llist_test_struct, node));
}

static void llist_test_member_address_is_nonnull(struct kunit *test)
{
    struct llist_test_struct test_struct;
    
    KUNIT_EXPECT_TRUE(test, member_address_is_nonnull(&test_struct, node)); 

    /* TODO conseguir testar essa bagaca*/
    //test_struct_ptr = NULL;
    //KUNIT_EXPECT_FALSE(test, member_address_is_nonnull(test_struct_ptr, node)); 
}

static void llist_test_add(struct kunit *test)
{
    struct llist_node a, b;
    LLIST_HEAD(llist);

    init_llist_node(&a);
    init_llist_node(&b);
    
    /*The first assertion must be true, given that llist is empty */
    KUNIT_EXPECT_TRUE(test, llist_add(&a, &llist));
    KUNIT_EXPECT_FALSE(test, llist_add(&b, &llist));

    /* Should be [List] -> b -> a */
    KUNIT_EXPECT_PTR_EQ(test, llist.first, &b);
    KUNIT_EXPECT_PTR_EQ(test, b.next, &a);
}

static void llist_test_add_batch(struct kunit *test)
{
    struct llist_node a, b, c;
    LLIST_HEAD(llist);
    LLIST_HEAD(llist2);

    init_llist_node(&a);
    init_llist_node(&b);
    init_llist_node(&c);

    llist_add(&a, &llist2);
    llist_add(&b, &llist2);
    llist_add(&c, &llist2);

    /*This assertion must be true, given that llist is empty */
    KUNIT_EXPECT_TRUE(test, llist_add_batch(&c, &a, &llist));

    /* should be [List] -> c -> b -> a */
    KUNIT_EXPECT_PTR_EQ(test, llist.first, &c);
    KUNIT_EXPECT_PTR_EQ(test, c.next, &b);
    KUNIT_EXPECT_PTR_EQ(test, b.next, &a);
}

static void llist_test_llist_next(struct kunit *test)
{
    struct llist_node a, b;
    LLIST_HEAD(llist);

    init_llist_node(&a);
    init_llist_node(&b);

    llist_add(&a, &llist);
    llist_add(&b, &llist);

    /* should be [List] -> b -> a */
    KUNIT_EXPECT_PTR_EQ(test, llist_next(&b), &a);
    KUNIT_EXPECT_NULL(test, llist_next(&a));
}

static void llist_test_empty_llist(struct kunit *test)
{
    struct llist_head llist = LLIST_HEAD_INIT(llist);

    KUNIT_EXPECT_TRUE(test, llist_empty(&llist));

    struct llist_node a;

    llist_add(&a, &llist);

    KUNIT_EXPECT_FALSE(test, llist_empty(&llist));
}

static void llist_test_llist_on_list(struct kunit *test)
{
    struct llist_node a, b;
    LLIST_HEAD(llist);

    init_llist_node(&a);
    init_llist_node(&b);

    llist_add(&a, &llist);

    /* should be [List] -> a */
    KUNIT_EXPECT_TRUE(test, llist_on_list(&a));
    KUNIT_EXPECT_FALSE(test, llist_on_list(&b));
}

static void llist_test_del_first(struct kunit *test)
{
    struct llist_node a, b, *c;
    LLIST_HEAD(llist);

    llist_add(&a, &llist);
    llist_add(&b, &llist);
    
    /* before: [List] -> b -> a */
    c = llist_del_first(&llist);

    /* should be [List] -> a */
    KUNIT_EXPECT_PTR_EQ(test, llist.first, &a);

    /* del must return a pointer to llist_node b 
     * the returned pointer must be marked on list*/
    KUNIT_EXPECT_PTR_EQ(test, c, &b);
    KUNIT_EXPECT_TRUE(test, llist_on_list(c));
}

static void llist_test_del_first_init(struct kunit *test)
{
    struct llist_node a;
    LLIST_HEAD(llist);

    llist_add(&a, &llist);
    
    struct llist_node *b = llist_del_first_init(&llist);
    
    /* should be [List] 
     * the returned pointer must be marked out of the list */
    KUNIT_EXPECT_TRUE(test, llist_empty(&llist));
    KUNIT_EXPECT_FALSE(test, llist_on_list(b));

}

static void llist_test_del_first_this(struct kunit *test)
{
    struct llist_node a, b;
    LLIST_HEAD(llist);

    llist_add(&a, &llist);
    llist_add(&b, &llist);
    
    llist_del_first_this(&llist, &a);
    
    /* before: [List] -> b -> a */
    KUNIT_EXPECT_FALSE(test, llist_del_first_this(&llist, &a)); // it should remove only if is the first node in the llist
                                                                
    KUNIT_EXPECT_TRUE(test, llist_del_first_this(&llist, &b));

    /* shoulde be [List] -> a */
    KUNIT_EXPECT_PTR_EQ(test, llist.first, &a);
}

static void llist_test_del_all(struct kunit *test)
{
    struct llist_node a, b;
    LLIST_HEAD(llist);
    LLIST_HEAD(empty_llist);

    llist_add(&a, &llist);
    llist_add(&b, &llist);

    /* deleting from a empty llist should return NULL */
    KUNIT_EXPECT_NULL(test, llist_del_all(&empty_llist));

    llist_del_all(&llist);

    KUNIT_EXPECT_TRUE(test, llist_empty(&llist));
}


static void llist_test_for_each(struct kunit *test)
{
    struct llist_node entries[3], *pos, *deleted_nodes;
    LLIST_HEAD(llist);
    int i = 0;

    llist_add(&entries[2], &llist);
    llist_add(&entries[1], &llist);
    llist_add(&entries[0], &llist);

    /* before [List] -> entries[0] -> entries[1] -> entries[2] */
    
    llist_for_each(pos, llist.first){
        KUNIT_EXPECT_PTR_EQ(test, pos, &entries[i]);
        i++;        
    }

    i = 0;

    /* transversing deleted node */
    deleted_nodes = llist_del_all(&llist);

    llist_for_each(pos, deleted_nodes){
        KUNIT_EXPECT_PTR_EQ(test, pos, &entries[i]);
        i++;        
    }

    KUNIT_EXPECT_EQ(test, 3, i);
}


static void llist_test_for_each_safe(struct kunit *test)
{
    struct llist_node entries[3], *pos, *n;
    LLIST_HEAD(llist);
    int i = 0;

    llist_add(&entries[2], &llist);
    llist_add(&entries[1], &llist);
    llist_add(&entries[0], &llist);


    llist_for_each_safe(pos, n, llist.first){
        KUNIT_EXPECT_PTR_EQ(test, pos, &entries[i]);
        llist_del_first(&llist);
        i++;        
    }

    KUNIT_EXPECT_EQ(test, 3, i);
    KUNIT_EXPECT_TRUE(test, llist_empty(&llist));
}

static void llist_test_for_each_entry(struct kunit *test)
{
    struct llist_test_struct entries[5], *pos;
    LLIST_HEAD(llist);
    int i = 0;

    for(int i = 4; i >= 0; --i){
        entries[i].data = i;
        llist_add(&entries[i].node,  &llist);
    }

    i = 0;

    llist_for_each_entry(pos, llist.first, node){
        KUNIT_EXPECT_EQ(test, pos->data, i);
        i++;
    }

    KUNIT_EXPECT_EQ(test, 5, i);
}

static void llist_test_for_each_entry_safe(struct kunit *test)
{
    struct llist_test_struct entries[5], *pos, *n;
    LLIST_HEAD(llist);
    int i = 0;

    for(int i = 4; i >= 0; --i){
        entries[i].data = i;
        llist_add(&entries[i].node,  &llist);
    }

    i = 0;

    llist_for_each_entry_safe(pos, n, llist.first, node){
        KUNIT_EXPECT_EQ(test, pos->data, i);
        llist_del_first(&llist);
        i++;
    }

    KUNIT_EXPECT_EQ(test, 5, i);
    KUNIT_EXPECT_TRUE(test, llist_empty(&llist));
}

static void llist_test_reverse_order(struct kunit *test)
{
    struct llist_node entries[3], *pos, *reversed_llist;
    LLIST_HEAD(llist);
    int i = 0;

    llist_add(&entries[0], &llist);
    llist_add(&entries[1], &llist);
    llist_add(&entries[2], &llist);

    /* before [List] -> entries[2] -> entries[1] -> entries[0] */
    reversed_llist = llist_reverse_order(llist_del_first(&llist));

    /* should be [List] -> entries[0] -> entries[1] -> entrires[0] */
    llist_for_each(pos, reversed_llist){
        KUNIT_EXPECT_PTR_EQ(test, pos, &entries[i]);
        i++;        
    }

    KUNIT_EXPECT_EQ(test, 3, i);
}

static struct kunit_case llist_test_cases[] = {
    KUNIT_CASE(llist_test_init_llist),
    KUNIT_CASE(llist_test_init_llist_node),
    KUNIT_CASE(llist_test_llist_entry),
    KUNIT_CASE(llist_test_member_address_is_nonnull),
    KUNIT_CASE(llist_test_add),
    KUNIT_CASE(llist_test_add_batch),
    KUNIT_CASE(llist_test_llist_next),
    KUNIT_CASE(llist_test_empty_llist),
    KUNIT_CASE(llist_test_llist_on_list),
    KUNIT_CASE(llist_test_del_first),
    KUNIT_CASE(llist_test_del_first_init),
    KUNIT_CASE(llist_test_del_first_this),
    KUNIT_CASE(llist_test_del_all),
    KUNIT_CASE(llist_test_for_each),
    KUNIT_CASE(llist_test_for_each_safe),
    KUNIT_CASE(llist_test_for_each_entry),
    KUNIT_CASE(llist_test_for_each_entry_safe),
    KUNIT_CASE(llist_test_reverse_order),
    {}
};

static struct kunit_suite llist_test_suite = {
    .name = "lib-llist",
    .test_cases = llist_test_cases,
};

kunit_test_suite(llist_test_suite);

MODULE_LICENSE("GPL v2");
