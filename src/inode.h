#ifndef SQFS_INODE_H
#define SQFS_INODE_H

#include "common.h"
#include "id_table.h"
#include "super.h"

typedef struct {
	size_t base_offset;
	size_t count;
	sqfs_id_t *ids;
} sqfs_export_table_t;

sqfs_err sqfs_inode_get(sqfs_inode_generic_t *inode,
	const sqfs_super_t *super, sqfs_off_t inode_ref);

sqfs_err sqfs_export_table_init(sqfs_export_table_t *table, sqfs_fd_t fd,
	const sqfs_super_t *super, sqfs_compressor_t *cmp);

sqfs_err sqfs_export_table_resolve(sqfs_export_table_t *table,
	size_t index, sqfs_off_t *inode_ref);

void sqfs_export_table_cleanup(sqfs_export_table_t *table);

#endif
