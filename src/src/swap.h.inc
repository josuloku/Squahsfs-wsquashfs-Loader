
SWAP_START(super_block)
	SWAP_DATA(s_magic, 32)
	SWAP_DATA(inodes, 32)
	SWAP_DATA(mkfs_time, 32)
	SWAP_DATA(block_size, 32)
	SWAP_DATA(fragments, 32)
	SWAP_DATA(compression, 16)
	SWAP_DATA(block_log, 16)
	SWAP_DATA(flags, 16)
	SWAP_DATA(no_ids, 16)
	SWAP_DATA(s_major, 16)
	SWAP_DATA(s_minor, 16)
	SWAP_DATA(root_inode, 64)
	SWAP_DATA(bytes_used, 64)
	SWAP_DATA(id_table_start, 64)
	SWAP_DATA(xattr_id_table_start, 64)
	SWAP_DATA(inode_table_start, 64)
	SWAP_DATA(directory_table_start, 64)
	SWAP_DATA(fragment_table_start, 64)
	SWAP_DATA(lookup_table_start, 64)
SWAP_END

SWAP_START(dir_index)
	SWAP_DATA(index, 32)
	SWAP_DATA(start_block, 32)
	SWAP_DATA(size, 32)
SWAP_END

SWAP_START(base_inode)
	SWAP_DATA(inode_type, 16)
	SWAP_DATA(mode, 16)
	SWAP_DATA(uid, 16)
	SWAP_DATA(guid, 16)
	SWAP_DATA(mtime, 32)
	SWAP_DATA(inode_number, 32)
SWAP_END

SWAP_START(ipc_inode)
	SWAP_DATA(inode_type, 16)
	SWAP_DATA(mode, 16)
	SWAP_DATA(uid, 16)
	SWAP_DATA(guid, 16)
	SWAP_DATA(mtime, 32)
	SWAP_DATA(inode_number, 32)
	SWAP_DATA(nlink, 32)
SWAP_END

SWAP_START(lipc_inode)
	SWAP_DATA(inode_type, 16)
	SWAP_DATA(mode, 16)
	SWAP_DATA(uid, 16)
	SWAP_DATA(guid, 16)
	SWAP_DATA(mtime, 32)
	SWAP_DATA(inode_number, 32)
	SWAP_DATA(nlink, 32)
	SWAP_DATA(xattr, 32)
SWAP_END

SWAP_START(dev_inode)
	SWAP_DATA(inode_type, 16)
	SWAP_DATA(mode, 16)
	SWAP_DATA(uid, 16)
	SWAP_DATA(guid, 16)
	SWAP_DATA(mtime, 32)
	SWAP_DATA(inode_number, 32)
	SWAP_DATA(nlink, 32)
	SWAP_DATA(rdev, 32)
SWAP_END

SWAP_START(ldev_inode)
	SWAP_DATA(inode_type, 16)
	SWAP_DATA(mode, 16)
	SWAP_DATA(uid, 16)
	SWAP_DATA(guid, 16)
	SWAP_DATA(mtime, 32)
	SWAP_DATA(inode_number, 32)
	SWAP_DATA(nlink, 32)
	SWAP_DATA(rdev, 32)
	SWAP_DATA(xattr, 32)
SWAP_END

SWAP_START(symlink_inode)
	SWAP_DATA(inode_type, 16)
	SWAP_DATA(mode, 16)
	SWAP_DATA(uid, 16)
	SWAP_DATA(guid, 16)
	SWAP_DATA(mtime, 32)
	SWAP_DATA(inode_number, 32)
	SWAP_DATA(nlink, 32)
	SWAP_DATA(symlink_size, 32)
SWAP_END

SWAP_START(reg_inode)
	SWAP_DATA(inode_type, 16)
	SWAP_DATA(mode, 16)
	SWAP_DATA(uid, 16)
	SWAP_DATA(guid, 16)
	SWAP_DATA(mtime, 32)
	SWAP_DATA(inode_number, 32)
	SWAP_DATA(start_block, 32)
	SWAP_DATA(fragment, 32)
	SWAP_DATA(offset, 32)
	SWAP_DATA(file_size, 32)
SWAP_END

SWAP_START(lreg_inode)
	SWAP_DATA(inode_type, 16)
	SWAP_DATA(mode, 16)
	SWAP_DATA(uid, 16)
	SWAP_DATA(guid, 16)
	SWAP_DATA(mtime, 32)
	SWAP_DATA(inode_number, 32)
	SWAP_DATA(start_block, 64)
	SWAP_DATA(file_size, 64)
	SWAP_DATA(sparse, 64)
	SWAP_DATA(nlink, 32)
	SWAP_DATA(fragment, 32)
	SWAP_DATA(offset, 32)
	SWAP_DATA(xattr, 32)
SWAP_END

SWAP_START(dir_inode)
	SWAP_DATA(inode_type, 16)
	SWAP_DATA(mode, 16)
	SWAP_DATA(uid, 16)
	SWAP_DATA(guid, 16)
	SWAP_DATA(mtime, 32)
	SWAP_DATA(inode_number, 32)
	SWAP_DATA(start_block, 32)
	SWAP_DATA(nlink, 32)
	SWAP_DATA(file_size, 16)
	SWAP_DATA(offset, 16)
	SWAP_DATA(parent_inode, 32)
SWAP_END

SWAP_START(ldir_inode)
	SWAP_DATA(inode_type, 16)
	SWAP_DATA(mode, 16)
	SWAP_DATA(uid, 16)
	SWAP_DATA(guid, 16)
	SWAP_DATA(mtime, 32)
	SWAP_DATA(inode_number, 32)
	SWAP_DATA(nlink, 32)
	SWAP_DATA(file_size, 32)
	SWAP_DATA(start_block, 32)
	SWAP_DATA(parent_inode, 32)
	SWAP_DATA(i_count, 16)
	SWAP_DATA(offset, 16)
	SWAP_DATA(xattr, 32)
SWAP_END

SWAP_START(dir_entry)
	SWAP_DATA(offset, 16)
	SWAP_DATA(inode_number, 16)
	SWAP_DATA(type, 16)
	SWAP_DATA(size, 16)
SWAP_END

SWAP_START(dir_header)
	SWAP_DATA(count, 32)
	SWAP_DATA(start_block, 32)
	SWAP_DATA(inode_number, 32)
SWAP_END

SWAP_START(fragment_entry)
	SWAP_DATA(start_block, 64)
	SWAP_DATA(size, 32)
	SWAP_DATA(unused, 32)
SWAP_END

SWAP_START(xattr_entry)
	SWAP_DATA(type, 16)
	SWAP_DATA(size, 16)
SWAP_END

SWAP_START(xattr_val)
	SWAP_DATA(vsize, 32)
SWAP_END

SWAP_START(xattr_id)
	SWAP_DATA(xattr, 64)
	SWAP_DATA(count, 32)
	SWAP_DATA(size, 32)
SWAP_END

SWAP_START(xattr_id_table)
	SWAP_DATA(xattr_table_start, 64)
	SWAP_DATA(xattr_ids, 32)
	SWAP_DATA(unused, 32)
SWAP_END
