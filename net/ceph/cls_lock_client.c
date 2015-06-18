#include <linux/ceph/ceph_debug.h>

#include <linux/types.h>
#include <linux/slab.h>

#include <linux/ceph/cls_lock_client.h>
#include <linux/ceph/decode.h>

/**
 * ceph_cls_lock - grab rados lock for object
 * @oid, @oloc: object to lock
 * @lock_name: the name of the lock
 * @type: lock type (CEPH_CLS_LOCK_EXCLUSIVE or CEPH_CLS_LOCK_SHARED)
 * @cookie: user-defined identifier for this instance of the lock
 * @tag: user-defined tag
 * @desc: user-defined lock description
 * @flags: lock flags
 *
 * All operations on the same lock should use the same tag.
 */
int ceph_cls_lock(struct ceph_osd_client *osdc,
		  struct ceph_object_id *oid,
		  struct ceph_object_locator *oloc,
		  char *lock_name, u8 type, char *cookie,
		  char *tag, char *desc, u8 flags)
{
	int lock_op_buf_size;
	int name_len = strlen(lock_name);
	int cookie_len = strlen(cookie);
	int tag_len = strlen(tag);
	int desc_len = strlen(desc);
	void *p, *end;
	struct page *lock_op_page;
	struct timespec mtime;
	int ret;

	lock_op_buf_size = name_len + sizeof(__le32) +
			   cookie_len + sizeof(__le32) +
			   tag_len + sizeof(__le32) +
			   desc_len + sizeof(__le32) +
			   sizeof(struct ceph_timespec) +
			   /* flag and type */
			   sizeof(u8) + sizeof(u8) +
			   CEPH_ENCODING_START_BLK_LEN;
	if (lock_op_buf_size > PAGE_SIZE)
		return -E2BIG;

	lock_op_page = alloc_page(GFP_NOIO);
	if (!lock_op_page)
		return -ENOMEM;

	p = page_address(lock_op_page);
	end = p + lock_op_buf_size;

	/* encode cls_lock_lock_op struct */
	ceph_start_encoding(&p, 1, 1,
			    lock_op_buf_size - CEPH_ENCODING_START_BLK_LEN);
	ceph_encode_string(&p, end, lock_name, name_len);
	ceph_encode_8(&p, type);
	ceph_encode_string(&p, end, cookie, cookie_len);
	ceph_encode_string(&p, end, tag, tag_len);
	ceph_encode_string(&p, end, desc, desc_len);
	/* only support infinite duration */
	memset(&mtime, 0, sizeof(mtime));
	ceph_encode_timespec(p, &mtime);
	p += sizeof(struct ceph_timespec);
	ceph_encode_8(&p, flags);

	dout("%s lock_name %s type %d cookie %s tag %s desc %s flags 0x%x\n",
	     __func__, lock_name, type, cookie, tag, desc, flags);
	ret = ceph_osdc_call(osdc, oid, oloc, "lock", "lock",
			     CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			     lock_op_page, lock_op_buf_size, NULL, NULL);

	dout("%s: status %d\n", __func__, ret);
	__free_page(lock_op_page);
	return ret;
}
EXPORT_SYMBOL(ceph_cls_lock);

/**
 * ceph_cls_unlock - release rados lock for object
 * @oid, @oloc: object to lock
 * @lock_name: the name of the lock
 * @cookie: user-defined identifier for this instance of the lock
 */
int ceph_cls_unlock(struct ceph_osd_client *osdc,
		    struct ceph_object_id *oid,
		    struct ceph_object_locator *oloc,
		    char *lock_name, char *cookie)
{
	int unlock_op_buf_size;
	int name_len = strlen(lock_name);
	int cookie_len = strlen(cookie);
	void *p, *end;
	struct page *unlock_op_page;
	int ret;

	unlock_op_buf_size = name_len + sizeof(__le32) +
			     cookie_len + sizeof(__le32) +
			     CEPH_ENCODING_START_BLK_LEN;
	if (unlock_op_buf_size > PAGE_SIZE)
		return -E2BIG;

	unlock_op_page = alloc_page(GFP_NOIO);
	if (!unlock_op_page)
		return -ENOMEM;

	p = page_address(unlock_op_page);
	end = p + unlock_op_buf_size;

	/* encode cls_lock_unlock_op struct */
	ceph_start_encoding(&p, 1, 1,
			    unlock_op_buf_size - CEPH_ENCODING_START_BLK_LEN);
	ceph_encode_string(&p, end, lock_name, name_len);
	ceph_encode_string(&p, end, cookie, cookie_len);

	dout("%s lock_name %s cookie %s\n", __func__, lock_name, cookie);
	ret = ceph_osdc_call(osdc, oid, oloc, "lock", "unlock",
			     CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			     unlock_op_page, unlock_op_buf_size, NULL, NULL);

	dout("%s: status %d\n", __func__, ret);
	__free_page(unlock_op_page);
	return ret;
}
EXPORT_SYMBOL(ceph_cls_unlock);

/**
 * ceph_cls_break_lock - release rados lock for object for specified client
 * @oid, @oloc: object to lock
 * @lock_name: the name of the lock
 * @cookie: user-defined identifier for this instance of the lock
 * @locker: current lock owner
 */
int ceph_cls_break_lock(struct ceph_osd_client *osdc,
			struct ceph_object_id *oid,
			struct ceph_object_locator *oloc,
			char *lock_name, char *cookie,
			struct ceph_entity_name *locker)
{
	int break_op_buf_size;
	int name_len = strlen(lock_name);
	int cookie_len = strlen(cookie);
	struct page *break_op_page;
	void *p, *end;
	int ret;

	break_op_buf_size = name_len + sizeof(__le32) +
			    cookie_len + sizeof(__le32) +
			    sizeof(u8) + sizeof(__le64) +
			    CEPH_ENCODING_START_BLK_LEN;
	if (break_op_buf_size > PAGE_SIZE)
		return -E2BIG;

	break_op_page = alloc_page(GFP_NOIO);
	if (!break_op_page)
		return -ENOMEM;

	p = page_address(break_op_page);
	end = p + break_op_buf_size;

	/* encode cls_lock_break_op struct */
	ceph_start_encoding(&p, 1, 1,
			    break_op_buf_size - CEPH_ENCODING_START_BLK_LEN);
	ceph_encode_string(&p, end, lock_name, name_len);
	ceph_encode_copy(&p, locker, sizeof(*locker));
	ceph_encode_string(&p, end, cookie, cookie_len);

	dout("%s lock_name %s cookie %s locker %s%llu\n", __func__, lock_name,
	     cookie, ENTITY_NAME(*locker));
	ret = ceph_osdc_call(osdc, oid, oloc, "lock", "break_lock",
			     CEPH_OSD_FLAG_WRITE | CEPH_OSD_FLAG_ONDISK,
			     break_op_page, break_op_buf_size, NULL, NULL);

	dout("%s: status %d\n", __func__, ret);
	__free_page(break_op_page);
	return ret;
}
EXPORT_SYMBOL(ceph_cls_break_lock);
