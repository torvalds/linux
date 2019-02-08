#include "kobject_socket.h"
#include "kobject_file.h"

MED_ATTRS(socket_kobject) {
	MED_ATTR_KEY_RO (socket_kobject, dev, "dev", MED_UNSIGNED),
	MED_ATTR_KEY_RO (socket_kobject, ino, "ino", MED_UNSIGNED),

	MED_ATTR_RO (socket_kobject, type, "type", MED_UNSIGNED),
	MED_ATTR_RO (socket_kobject, family, "family", MED_UNSIGNED),
	MED_ATTR (socket_kobject, uid, "uid", MED_UNSIGNED),
	MED_ATTR_OBJECT	(socket_kobject),

	MED_ATTR_END
};

int socket_kobj2kern(struct socket_kobject * sock_kobj, struct socket * sock)
{
}

int socket_kern2kobj(struct socket_kobject * sock_kobj, struct socket * sock)
{
}

static struct socket_kobject storage;

struct medusa_kobject_s *socket_fetch(struct medusa_kobject_s *kobj)
{
	struct socket * sock;
	struct inode *inode;
	struct super_block *sb;
	struct socket_kobject *s_kobj = (struct socket_kobject*) kobj;

	if(s_kobj)
		sb = user_get_super(s_kobj->dev);
	if(sb)
		inode = ilookup(sb, s_kobj->ino);
	if(inode)
		sock = SOCKET_I(inode);
	if(sock) {
		socket_kern2kobj(&storage, sock);
		return (struct medusa_kobject_s *)&storage;
	}

	return NULL;
}

medusa_answer_t socket_update(struct medusa_kobject_s *kobj)
{
	struct socket * sock;
	struct inode *inode;
	struct super_block *sb;
	struct socket_kobject *s_kobj = (struct socket_kobject*) kobj;

	if(s_kobj)
		sb = user_get_super(s_kobj->dev);
	if(sb)
		inode = ilookup(sb, s_kobj->ino);
	if(inode)
		sock = SOCKET_I(inode);
	if(sock) {
		socket_kobj2kern(s_kobj, sock);
		return MED_YES;
	}

	return MED_ERR;
}

MED_KCLASS(socket_kobject) {
	MEDUSA_KCLASS_HEADER(socket_kobject),
	"socket",
	NULL,		/* init kclass */
	NULL,		/* destroy kclass */
	socket_fetch,
	socket_update,
	NULL,		/* unmonitor */
};

int __init socket_kobject_init( void ) {
	MED_REGISTER_KCLASS( socket_kobject );
	return 0;
}
__initcall( socket_kobject_init );
