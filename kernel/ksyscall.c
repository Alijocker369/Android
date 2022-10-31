// SPDX-License-Identifier: GPL-3.0-or-later

#include "mos/elf/elf.h"
#include "mos/filesystem/filesystem.h"
#include "mos/ksyscall/decl.h"
#include "mos/mos_global.h"
#include "mos/platform/platform.h"
#include "mos/printk.h"
#include "mos/tasks/process.h"
#include "mos/tasks/schedule.h"
#include "mos/tasks/task_type.h"
#include "mos/types.h"

void define_ksyscall(panic)(void)
{
    if (current_process->effective_uid == 0)
        mos_panic("Kernel panic called by syscall from process %d (%s), thread %d", current_process->pid, current_process->name, current_thread->tid);
    else
        mos_warn("only root can panic");
}

fd_t define_ksyscall(file_open)(const char *path, file_open_flags flags)
{
    if (path == NULL)
        return -1;

    file_t *f = vfs_open(path, flags);
    if (!f)
        return -1;
    return process_attach_fd(current_process, &f->io);
}

bool define_ksyscall(file_stat)(const char *path, file_stat_t *stat)
{
    if (path == NULL || stat == NULL)
        return false;

    return vfs_stat(path, stat);
}

size_t define_ksyscall(io_read)(fd_t fd, void *buf, size_t count, size_t offset)
{
    if (fd < 0 || buf == NULL)
        return 0;
    if (offset)
        mos_warn("offset is not supported yet");
    return io_read(current_process->files[fd], buf, count);
}

size_t define_ksyscall(io_write)(fd_t fd, const void *buf, size_t count, size_t offset)
{
    if (fd < 0 || buf == NULL)
        return 0;
    if (offset)
        mos_warn("offset is not supported yet");
    return io_write(current_process->files[fd], buf, count);
}

bool define_ksyscall(io_close)(fd_t fd)
{
    if (fd < 0)
        return false;
    process_detach_fd(current_process, fd);
    return true;
}

noreturn void define_ksyscall(exit)(u32 exit_code)
{
    int pid = current_process->pid;
    if (unlikely(pid == 1))
        mos_panic("init process exited with code %d", exit_code);

    pr_info("Kernel syscall exit called with code %d from pid %d", exit_code, pid);
    process_handle_exit(current_process, exit_code);
    jump_to_scheduler();
    MOS_UNREACHABLE();
}

void define_ksyscall(yield_cpu)(void)
{
    jump_to_scheduler();
}

pid_t define_ksyscall(fork)(void)
{
    process_t *parent = current_process;
    process_t *child = process_handle_fork(parent);
    if (child == NULL)
        return 0;

    return current_process == child ? 0 : child->pid; // return 0 for child, pid for parent
}

pid_t define_ksyscall(exec)(const char *path, const char *const argv[])
{
    MOS_UNUSED(path);
    MOS_UNUSED(argv);
    mos_warn("exec syscall not implemented yet");
    return -1;
}

pid_t define_ksyscall(get_pid)()
{
    MOS_ASSERT(current_process);
    return current_process->pid;
}

pid_t define_ksyscall(get_parent_pid)()
{
    MOS_ASSERT(current_process && current_process->parent);
    return current_process->parent->pid;
}

pid_t define_ksyscall(spawn)(const char *path, int argc, const char *const argv[])
{
    MOS_UNUSED(argc);
    MOS_UNUSED(argv);
    process_t *process = process_create_from_elf(path, current_process, current_process->effective_uid);
    if (process == NULL)
        return -1;

    return process->pid;
}
