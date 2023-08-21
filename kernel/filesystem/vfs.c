#include "mos/filesystem/sysfs/sysfs.h"
#include "mos/filesystem/sysfs/sysfs_autoinit.h"
#include "mos/mm/mm.h"
#include "mos/mm/paging/table_ops.h"
#include "mos/mm/physical/pmm.h"
#include "mos/mm/slab_autoinit.h"

#include <mos/filesystem/dentry.h>
#include <mos/filesystem/fs_types.h>
#include <mos/filesystem/vfs.h>
#include <mos/filesystem/vfs_types.h>
#include <mos/io/io.h>
#include <mos/lib/structures/list.h>
#include <mos/lib/structures/tree.h>
#include <mos/lib/sync/spinlock.h>
#include <mos/mos_global.h>
#include <mos/platform/platform.h>
#include <mos/printk.h>
#include <mos/tasks/process.h>
#include <mos/types.h>
#include <stdlib.h>
#include <string.h>

static list_head vfs_fs_list = LIST_HEAD_INIT(vfs_fs_list); // filesystem_t
static spinlock_t vfs_fs_list_lock = SPINLOCK_INIT;

dentry_t *root_dentry = NULL;

slab_t *inode_cache = NULL, *superblock_cache = NULL, *dentry_cache = NULL, *mount_cache = NULL, *file_cache = NULL;
SLAB_AUTOINIT("inode", inode_cache, inode_t);
SLAB_AUTOINIT("superblock", superblock_cache, superblock_t);
SLAB_AUTOINIT("dentry", dentry_cache, dentry_t);
SLAB_AUTOINIT("mount", mount_cache, mount_t);
SLAB_AUTOINIT("file", file_cache, file_t);

// BEGIN: filesystem's io_t operations
static void vfs_io_ops_close(io_t *io)
{
    file_t *file = container_of(io, file_t, io);
    const file_ops_t *file_ops = file_get_ops(file);
    if (file_ops && file_ops->flush)
        file_ops->flush(file);
    if (file_ops && file_ops->release)
        file_ops->release(file);
    dentry_unref(file->dentry);
    kfree(file);
}

static size_t vfs_io_ops_read(io_t *io, void *buf, size_t count)
{
    file_t *file = container_of(io, file_t, io);
    const file_ops_t *const file_ops = file_get_ops(file);
    if (!file_ops || !file_ops->read)
        return 0;

    spinlock_acquire(&file->offset_lock);
    size_t ret = file_ops->read(file, buf, count, file->offset);
    if (ret != (size_t) -1)
        file->offset += ret;
    spinlock_release(&file->offset_lock);

    return ret;
}

static size_t vfs_io_ops_write(io_t *io, const void *buf, size_t count)
{
    file_t *file = container_of(io, file_t, io);
    const file_ops_t *const file_ops = file_get_ops(file);
    if (!file_ops || !file_ops->write)
        return 0;

    spinlock_acquire(&file->offset_lock);
    size_t ret = file_ops->write(file, buf, count, file->offset);
    file->offset += ret;
    spinlock_release(&file->offset_lock);

    return ret;
}

static off_t vfs_io_ops_seek(io_t *io, off_t offset, io_seek_whence_t whence)
{
    file_t *file = container_of(io, file_t, io);

    if (file_get_ops(file)->seek)
        return file_get_ops(file)->seek(file, offset, whence); // use the filesystem's lseek if it exists

    spinlock_acquire(&file->offset_lock);

    off_t ret = 0;
    switch (whence)
    {
        case IO_SEEK_SET:
        {
            if (unlikely(offset < 0))
            {
                ret = 0;
                break;
            }

            if ((size_t) offset > file->dentry->inode->size)
                ret = file->dentry->inode->size; // beyond the end of the file
            else
                ret = offset;

            file->offset = ret;
            break;
        }
        case IO_SEEK_CURRENT:
        {
            if (offset < 0)
            {
                if (file->offset < (size_t) -offset)
                    ret = 0; // before the beginning of the file
                else
                    ret = file->offset + offset;
            }
            else
            {
                if (file->offset + offset > file->dentry->inode->size)
                    ret = file->dentry->inode->size; // beyond the end of the file
                else
                    ret = file->offset + offset;
            }

            file->offset = ret;
            break;
        }
        case IO_SEEK_END:
        {
            if (offset < 0)
            {
                if (file->dentry->inode->size < (size_t) -offset)
                    ret = 0; // before the beginning of the file
                else
                    ret = file->dentry->inode->size + offset;
            }
            else
            {
                // don't allow seeking past the end of the file, (yet)
                pr_warn("vfs: seeking past the end of the file is not supported yet");
            }

            file->offset = ret;
            break;
        }
        case IO_SEEK_DATA: mos_warn("vfs: IO_SEEK_DATA is not supported"); break;
        case IO_SEEK_HOLE: mos_warn("vfs: IO_SEEK_HOLE is not supported"); break;
    };

    spinlock_release(&file->offset_lock);
    return ret;
}

static bool vfs_mmap_fault_handler(vmap_t *vmap, ptr_t fault_addr, const pagefault_info_t *info)
{
    io_t *io = vmap->io;
    file_t *file = container_of(io, file_t, io);
    const file_ops_t *const file_ops = file_get_ops(file);

    phyframe_t *page = mm_get_free_page();
    const size_t fault_offset = (ALIGN_DOWN_TO_PAGE(fault_addr) - vmap->vaddr);
    file_ops->read(file, (void *) phyframe_va(page), MOS_PAGE_SIZE, vmap->io_offset + fault_offset);

    mm_do_map(vmap->mmctx->pgd, fault_addr, phyframe_pfn(page), 1, info->userfault ? VM_USER_RW : VM_RW, true);

    return true;
}

static bool vfs_io_ops_mmap(io_t *io, vmap_t *vmap, off_t offset)
{
    file_t *file = container_of(io, file_t, io);
    const file_ops_t *const file_ops = file_get_ops(file);

    MOS_ASSERT(!vmap->on_fault); // there should be no fault handler set
    vmap->on_fault = vfs_mmap_fault_handler;

    if (file_ops->mmap)
        return file_ops->mmap(file, vmap, offset);

    return true;
}

static io_op_t fs_io_ops = {
    .read = vfs_io_ops_read,
    .write = vfs_io_ops_write,
    .close = vfs_io_ops_close,
    .seek = vfs_io_ops_seek,
    .mmap = vfs_io_ops_mmap,
};
// END: filesystem's io_t operations

static void vfs_copy_stat(file_stat_t *statbuf, inode_t *inode)
{
    statbuf->ino = inode->ino;
    statbuf->type = inode->type;
    statbuf->perm = inode->perm;
    statbuf->size = inode->size;
    statbuf->uid = inode->uid;
    statbuf->gid = inode->gid;
    statbuf->sticky = inode->sticky;
    statbuf->suid = inode->suid;
    statbuf->sgid = inode->sgid;
    statbuf->nlinks = inode->nlinks;
    statbuf->accessed = inode->accessed;
    statbuf->modified = inode->modified;
    statbuf->created = inode->created;
}

static filesystem_t *vfs_find_filesystem(const char *name)
{
    filesystem_t *fs_found = NULL;
    spinlock_acquire(&vfs_fs_list_lock);
    list_foreach(filesystem_t, fs, vfs_fs_list)
    {
        if (strcmp(fs->name, name) == 0)
        {
            fs_found = fs;
            break;
        }
    }
    spinlock_release(&vfs_fs_list_lock);
    return fs_found;
}

static bool vfs_verify_permissions(dentry_t *file_dentry, bool open, bool read, bool create, bool execute, bool write)
{
    MOS_ASSERT(file_dentry != NULL && file_dentry->inode != NULL);
    const file_perm_t file_perm = file_dentry->inode->perm;

    // TODO: we are treating all users as root for now, only checks for execute permission
    MOS_UNUSED(open);
    MOS_UNUSED(read);
    MOS_UNUSED(create);
    MOS_UNUSED(write);

    if (execute && !(file_perm & PERM_EXEC))
        return false; // execute permission denied

    return true;
}

static file_t *vfs_do_open_relative(dentry_t *base, const char *path, open_flags flags)
{
    if (base == NULL)
        return NULL;

    const bool may_create = flags & OPEN_CREATE;
    const bool read = flags & OPEN_READ;
    const bool write = flags & OPEN_WRITE;
    const bool exec = flags & OPEN_EXECUTE;
    const bool no_follow = flags & OPEN_NO_FOLLOW;
    const bool expect_dir = flags & OPEN_DIR;
    const bool truncate = flags & OPEN_TRUNCATE;

    lastseg_resolve_flags_t resolve_flags = RESOLVE_EXPECT_FILE |                                            //
                                            (no_follow ? RESOLVE_SYMLINK_NOFOLLOW : 0) |                     //
                                            (may_create ? RESOLVE_EXPECT_ANY_EXIST : RESOLVE_EXPECT_EXIST) | //
                                            (expect_dir ? RESOLVE_EXPECT_DIR : 0);
    dentry_t *entry = dentry_get(base, root_dentry, path, resolve_flags);

    if (entry == NULL)
    {
        mos_debug(vfs, "failed to resolve '%s', create=%d, read=%d, exec=%d, nfollow=%d, dir=%d, trun=%d", path, may_create, read, exec, no_follow, expect_dir, truncate);
        return NULL;
    }

    if (!vfs_verify_permissions(entry, true, read, may_create, exec, write))
        return NULL;

    file_t *file = kmemcache_alloc(file_cache);
    file->dentry = entry;

    io_flags_t io_flags = IO_SEEKABLE;

    if (read)
        io_flags |= IO_READABLE;

    if (write)
        io_flags |= IO_WRITABLE;

    if (exec)
        io_flags |= IO_EXECUTABLE;

    // only regular files are mmapable
    if (entry->inode->type == FILE_TYPE_REGULAR)
        io_flags |= IO_MMAPABLE;

    io_init(&file->io, IO_FILE, io_flags, &fs_io_ops);

    const file_ops_t *ops = file_get_ops(file);
    if (ops && ops->open)
    {
        bool opened = ops->open(file->dentry->inode, file);
        if (!opened)
        {
            pr_warn("failed to open file '%s'", path);
            kfree(file);
            return NULL;
        }
    }

    return file;
}

void vfs_init(void)
{
    pr_info("initializing VFS layer");
    dentry_cache_init();
}

void vfs_register_filesystem(filesystem_t *fs)
{
    spinlock_acquire(&vfs_fs_list_lock);
    list_node_append(&vfs_fs_list, list_node(fs));
    spinlock_release(&vfs_fs_list_lock);

    pr_info("filesystem '%s' registered", fs->name);
}

bool vfs_mount(const char *device, const char *path, const char *fs, const char *options)
{
    filesystem_t *real_fs = vfs_find_filesystem(fs);
    if (unlikely(real_fs == NULL))
    {
        mos_warn("filesystem '%s' not found", fs);
        return false;
    }

    if (unlikely(root_dentry == NULL))
    {
        // special case: mount root filesystem
        MOS_ASSERT(strcmp(path, "/") == 0);
        mos_debug(vfs, "mounting root filesystem '%s'...", fs);
        root_dentry = real_fs->mount(real_fs, device, options);
        if (root_dentry == NULL)
        {
            mos_warn("failed to mount root filesystem");
            return false;
        }
        mos_debug(vfs, "root filesystem mounted, dentry=%p", (void *) root_dentry);

        root_dentry->name = NULL;
        dentry_ref(root_dentry); // it itself is a mount point
        return true;
    }

    dentry_t *base = path_is_absolute(path) ? root_dentry : dentry_from_fd(FD_CWD);
    dentry_t *mountpoint = dentry_get(base, root_dentry, path, RESOLVE_EXPECT_DIR | RESOLVE_EXPECT_EXIST);
    if (unlikely(mountpoint == NULL))
    {
        mos_warn("mount point does not exist");
        return false;
    }

    // when mounting:
    // mounted_root will have a reference of 1
    // the mount_point will have its reference incremented by 1
    dentry_t *mounted_root = real_fs->mount(real_fs, device, options);
    if (mounted_root == NULL)
    {
        mos_warn("failed to mount filesystem");
        return false;
    }

    const bool mounted = dentry_mount(mountpoint, mounted_root, real_fs);
    if (unlikely(!mounted))
    {
        mos_warn("failed to mount filesystem");
        return false;
    }

    pr_info2("mounted filesystem '%s' on '%s'", fs, path);
    return true;
}

file_t *vfs_openat(int fd, const char *path, open_flags flags)
{
    mos_debug(vfs, "vfs_openat(fd=%d, path='%s', flags=%x)", fd, path, flags);
    dentry_t *base = path_is_absolute(path) ? root_dentry : dentry_from_fd(fd);
    file_t *file = vfs_do_open_relative(base, path, flags);
    return file;
}

bool vfs_fstatat(fd_t fd, const char *path, file_stat_t *restrict statbuf, fstatat_flags flags)
{
    if (flags & FSTATAT_FILE)
    {
        mos_debug(vfs, "vfs_fstatat(fd=%d, path='%p', stat=%p, flags=%x)", fd, (void *) path, (void *) statbuf, flags);
        io_t *io = process_get_fd(current_process, fd);
        if (!io_valid(io))
            return false;

        file_t *file = container_of(io, file_t, io);
        if (file == NULL)
            return false;

        if (statbuf)
            vfs_copy_stat(statbuf, file->dentry->inode);
        return true;
    }

    mos_debug(vfs, "vfs_fstatat(fd=%d, path='%s', stat=%p, flags=%x)", fd, path, (void *) statbuf, flags);
    dentry_t *basedir = path_is_absolute(path) ? root_dentry : dentry_from_fd(fd);
    lastseg_resolve_flags_t resolve_flags = RESOLVE_EXPECT_FILE | RESOLVE_EXPECT_DIR | RESOLVE_EXPECT_EXIST;
    if (flags & FSTATAT_NOFOLLOW)
        resolve_flags |= RESOLVE_SYMLINK_NOFOLLOW;

    dentry_t *dentry = dentry_get(basedir, root_dentry, path, resolve_flags);
    if (dentry == NULL)
        return false;

    if (statbuf)
        vfs_copy_stat(statbuf, dentry->inode);
    dentry_unref(dentry);
    return true;
}

size_t vfs_readlinkat(fd_t dirfd, const char *path, char *buf, size_t size)
{
    dentry_t *base = path_is_absolute(path) ? root_dentry : dentry_from_fd(dirfd);
    dentry_t *dentry = dentry_get(base, root_dentry, path, RESOLVE_SYMLINK_NOFOLLOW | RESOLVE_EXPECT_EXIST);
    if (dentry == NULL)
        return 0;

    if (dentry->inode->type != FILE_TYPE_SYMLINK)
    {
        dentry_unref(dentry);
        return 0;
    }

    const size_t len = dentry->inode->ops->readlink(dentry, buf, size);

    dentry_unref(dentry);

    if (len >= size) // buffer too small
        return 0;
    return len;
}

bool vfs_touch(const char *path, file_type_t type, u32 perms)
{
    mos_debug(vfs, "vfs_touch(path='%s', type=%d, perms=%o)", path, type, perms);
    dentry_t *base = path_is_absolute(path) ? root_dentry : dentry_from_fd(FD_CWD);
    dentry_t *dentry = dentry_get(base, root_dentry, path, RESOLVE_EXPECT_ANY_EXIST | RESOLVE_EXPECT_ANY_TYPE);
    if (dentry == NULL)
        return false;

    dentry_t *parentdir = dentry_parent(dentry);

    if (!(parentdir && parentdir->inode && parentdir->inode->ops && parentdir->inode->ops->newfile))
    {
        mos_debug(vfs, "vfs_touch: parent directory does not support newfile() operation");
        dentry_unref(dentry);
        return false;
    }

    parentdir->inode->ops->newfile(parentdir->inode, dentry, type, perms);
    return true;
}

bool vfs_symlink(const char *path, const char *target)
{
    mos_debug(vfs, "vfs_symlink(path='%s', target='%s')", path, target);
    dentry_t *base = path_is_absolute(path) ? root_dentry : dentry_from_fd(FD_CWD);
    dentry_t *dentry = dentry_get(base, root_dentry, path, RESOLVE_EXPECT_NONEXIST);
    if (dentry == NULL)
        return false;

    dentry_t *parent_dir = dentry_parent(dentry);
    const bool created = parent_dir->inode->ops->symlink(parent_dir->inode, dentry, target);

    if (!created)
        mos_warn("failed to create symlink '%s'", path);

    return created;
}

bool vfs_mkdir(const char *path)
{
    mos_debug(vfs, "vfs_mkdir('%s')", path);
    dentry_t *base = path_is_absolute(path) ? root_dentry : dentry_from_fd(FD_CWD);
    dentry_t *dentry = dentry_get(base, root_dentry, path, RESOLVE_EXPECT_NONEXIST);
    if (dentry == NULL)
        return false;

    dentry_t *parent_dir = dentry_parent(dentry);
    if (parent_dir->inode == NULL || parent_dir->inode->ops == NULL || parent_dir->inode->ops->mkdir == NULL)
    {
        dentry_unref(dentry);
        return false;
    }

    // TODO: use umask or something else
    const bool created = parent_dir->inode->ops->mkdir(parent_dir->inode, dentry, parent_dir->inode->perm);

    if (!created)
        mos_warn("failed to create directory '%s'", path);

    return created;
}

size_t vfs_list_dir(io_t *io, char *buf, size_t size)
{
    mos_debug(vfs, "vfs_list_dir(io=%p, buf=%p, size=%zu)", (void *) io, (void *) buf, size);
    file_t *file = container_of(io, file_t, io);
    if (unlikely(file->dentry->inode->type != FILE_TYPE_DIRECTORY))
    {
        mos_warn("not a directory");
        return 0;
    }

    dir_iterator_state_t state = {
        .dir_nth = file->offset,
        .buf = buf,
        .buf_capacity = size,
        .buf_written = 0,
    };

    size_t written = dentry_list(file->dentry, &state);
    file->offset = state.dir_nth;
    return written;
}

bool vfs_chdir(const char *path)
{
    mos_debug(vfs, "vfs_chdir('%s')", path);
    dentry_t *base = path_is_absolute(path) ? root_dentry : dentry_from_fd(FD_CWD);
    dentry_t *dentry = dentry_get(base, root_dentry, path, RESOLVE_EXPECT_EXIST | RESOLVE_EXPECT_DIR);
    if (dentry == NULL)
        return false;

    dentry_t *old_cwd = dentry_from_fd(FD_CWD);
    if (old_cwd)
        dentry_unref(old_cwd);

    current_process->working_directory = dentry;
    return true;
}

ssize_t vfs_getcwd(char *buf, size_t size)
{
    dentry_t *cwd = dentry_from_fd(FD_CWD);
    if (cwd == NULL)
        return -1;

    return dentry_path(cwd, root_dentry, buf, size);
}

// ! sysfs support

static bool vfs_sysfs_filesystems(sysfs_file_t *f)
{
    list_foreach(filesystem_t, fs, vfs_fs_list)
    {
        sysfs_printf(f, "%s\n", fs->name);
    }

    return true;
}

static bool vfs_sysfs_mountpoints(sysfs_file_t *f)
{
    char pathbuf[MOS_PATH_MAX_LENGTH];
    sysfs_printf(f, "%s: %s\n", "/", root_dentry->superblock->fs->name);
    list_foreach(mount_t, mp, vfs_mountpoint_list)
    {
        dentry_path(mp->mountpoint, root_dentry, pathbuf, sizeof(pathbuf));
        sysfs_printf(f, "%s: %s\n", pathbuf, mp->fs->name);
    }

    return true;
}

static const sysfs_item_t vfs_sysfs_items[] = {
    SYSFS_RO_ITEM("filesystems", vfs_sysfs_filesystems),
    SYSFS_RO_ITEM("mount", vfs_sysfs_mountpoints),
    SYSFS_END_ITEM,
};

SYSFS_AUTOREGISTER(vfs, vfs_sysfs_items);
