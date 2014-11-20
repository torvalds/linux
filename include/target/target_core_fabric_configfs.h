/*
 * Used for tfc_wwn_cit attributes
 */

#include <target/configfs_macros.h>

CONFIGFS_EATTR_STRUCT(target_fabric_nacl_attrib, se_node_acl);
#define TF_NACL_ATTRIB_ATTR(_fabric, _name, _mode)			\
static struct target_fabric_nacl_attrib_attribute _fabric##_nacl_attrib_##_name = \
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_nacl_attrib_show_##_name,				\
	_fabric##_nacl_attrib_store_##_name);

CONFIGFS_EATTR_STRUCT(target_fabric_nacl_auth, se_node_acl);
#define TF_NACL_AUTH_ATTR(_fabric, _name, _mode)			\
static struct target_fabric_nacl_auth_attribute _fabric##_nacl_auth_##_name = \
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_nacl_auth_show_##_name,				\
	_fabric##_nacl_auth_store_##_name);

#define TF_NACL_AUTH_ATTR_RO(_fabric, _name)				\
static struct target_fabric_nacl_auth_attribute _fabric##_nacl_auth_##_name = \
	__CONFIGFS_EATTR_RO(_name,					\
	_fabric##_nacl_auth_show_##_name);

CONFIGFS_EATTR_STRUCT(target_fabric_nacl_param, se_node_acl);
#define TF_NACL_PARAM_ATTR(_fabric, _name, _mode)			\
static struct target_fabric_nacl_param_attribute _fabric##_nacl_param_##_name = \
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_nacl_param_show_##_name,				\
	_fabric##_nacl_param_store_##_name);

#define TF_NACL_PARAM_ATTR_RO(_fabric, _name)				\
static struct target_fabric_nacl_param_attribute _fabric##_nacl_param_##_name = \
	__CONFIGFS_EATTR_RO(_name,					\
	_fabric##_nacl_param_show_##_name);


CONFIGFS_EATTR_STRUCT(target_fabric_nacl_base, se_node_acl);
#define TF_NACL_BASE_ATTR(_fabric, _name, _mode)			\
static struct target_fabric_nacl_base_attribute _fabric##_nacl_##_name = \
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_nacl_show_##_name,					\
	_fabric##_nacl_store_##_name);

#define TF_NACL_BASE_ATTR_RO(_fabric, _name)				\
static struct target_fabric_nacl_base_attribute _fabric##_nacl_##_name = \
	__CONFIGFS_EATTR_RO(_name,					\
	_fabric##_nacl_show_##_name);

CONFIGFS_EATTR_STRUCT(target_fabric_np_base, se_tpg_np);
#define TF_NP_BASE_ATTR(_fabric, _name, _mode)				\
static struct target_fabric_np_base_attribute _fabric##_np_##_name =	\
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_np_show_##_name,					\
	_fabric##_np_store_##_name);

CONFIGFS_EATTR_STRUCT(target_fabric_tpg_attrib, se_portal_group);
#define TF_TPG_ATTRIB_ATTR(_fabric, _name, _mode)			\
static struct target_fabric_tpg_attrib_attribute _fabric##_tpg_attrib_##_name = \
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_tpg_attrib_show_##_name,				\
	_fabric##_tpg_attrib_store_##_name);

CONFIGFS_EATTR_STRUCT(target_fabric_tpg_auth, se_portal_group);
#define TF_TPG_AUTH_ATTR(_fabric, _name, _mode) 			\
static struct target_fabric_tpg_auth_attribute _fabric##_tpg_auth_##_name = \
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_tpg_auth_show_##_name,				\
	_fabric##_tpg_auth_store_##_name);

#define TF_TPG_AUTH_ATTR_RO(_fabric, _name)				\
static struct target_fabric_tpg_auth_attribute _fabric##_tpg_auth_##_name = \
	__CONFIGFS_EATTR_RO(_name,					\
	_fabric##_tpg_auth_show_##_name);

CONFIGFS_EATTR_STRUCT(target_fabric_tpg_param, se_portal_group);
#define TF_TPG_PARAM_ATTR(_fabric, _name, _mode)			\
static struct target_fabric_tpg_param_attribute _fabric##_tpg_param_##_name = \
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_tpg_param_show_##_name,				\
	_fabric##_tpg_param_store_##_name);


CONFIGFS_EATTR_STRUCT(target_fabric_tpg, se_portal_group);
#define TF_TPG_BASE_ATTR(_fabric, _name, _mode)				\
static struct target_fabric_tpg_attribute _fabric##_tpg_##_name =	\
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_tpg_show_##_name,					\
	_fabric##_tpg_store_##_name);


CONFIGFS_EATTR_STRUCT(target_fabric_wwn, target_fabric_configfs);
#define TF_WWN_ATTR(_fabric, _name, _mode)				\
static struct target_fabric_wwn_attribute _fabric##_wwn_##_name =	\
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_wwn_show_attr_##_name,				\
	_fabric##_wwn_store_attr_##_name);

#define TF_WWN_ATTR_RO(_fabric, _name)					\
static struct target_fabric_wwn_attribute _fabric##_wwn_##_name =	\
	__CONFIGFS_EATTR_RO(_name,					\
	_fabric##_wwn_show_attr_##_name);

CONFIGFS_EATTR_STRUCT(target_fabric_discovery, target_fabric_configfs);
#define TF_DISC_ATTR(_fabric, _name, _mode)				\
static struct target_fabric_discovery_attribute _fabric##_disc_##_name = \
	__CONFIGFS_EATTR(_name, _mode,					\
	_fabric##_disc_show_##_name,					\
	_fabric##_disc_store_##_name);

#define TF_DISC_ATTR_RO(_fabric, _name)					\
static struct target_fabric_discovery_attribute _fabric##_disc_##_name = \
	__CONFIGFS_EATTR_RO(_name,					\
	_fabric##_disc_show_##_name);

extern int target_fabric_setup_cits(struct target_fabric_configfs *);
