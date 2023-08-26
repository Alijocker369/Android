// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "mos/filesystem/vfs_types.h"

void inode_init(inode_t *inode, superblock_t *sb, u64 ino, file_type_t type);
inode_t *inode_create(superblock_t *sb, u64 ino, file_type_t type);

/**
 * @brief Create a new dentry with the given name and parent
 *
 * @param name The name of the dentry
 * @param parent The parent dentry
 *
 * @return The new dentry, or NULL if the dentry could not be created
 * @note The returned dentry will have its reference count of 0.
 */
dentry_t *dentry_create(superblock_t *sb, dentry_t *parent, const char *name);