// SPDX-License-Identifier: GPL-3.0-or-later

#include "mos/tasks/thread.h"

#include "lib/string.h"
#include "lib/structures/hashmap.h"
#include "lib/structures/stack.h"
#include "mos/kconfig.h"
#include "mos/mm/kmalloc.h"
#include "mos/mm/memops.h"
#include "mos/platform/platform.h"
#include "mos/printk.h"
#include "mos/tasks/process.h"
#include "mos/tasks/task_type.h"
#include "mos/x86/tasks/context.h"

#define THREAD_HASHTABLE_SIZE 512

hashmap_t *thread_table;

static hash_t hashmap_thread_hash(const void *key)
{
    return (hash_t){ .hash = *(tid_t *) key };
}

static int hashmap_thread_equal(const void *key1, const void *key2)
{
    return *(tid_t *) key1 == *(tid_t *) key2;
}

static tid_t new_thread_id()
{
    static tid_t next = 1;
    return (tid_t){ next++ };
}

thread_t *thread_allocate(process_t *owner, thread_flags_t tflags)
{
    thread_t *t = kmalloc(sizeof(thread_t));
    t->magic[0] = 'T';
    t->magic[1] = 'H';
    t->magic[2] = 'R';
    t->magic[3] = 'D';
    t->tid = new_thread_id();
    t->owner = owner;
    t->status = THREAD_STATUS_CREATED;
    t->flags = tflags;

    return t;
}

void thread_init()
{
    thread_table = kmalloc(sizeof(hashmap_t));
    memset(thread_table, 0, sizeof(hashmap_t));
    hashmap_init(thread_table, THREAD_HASHTABLE_SIZE, hashmap_thread_hash, hashmap_thread_equal);
}

void thread_deinit()
{
    hashmap_deinit(thread_table);
    kfree(thread_table);
}

thread_t *thread_new(process_t *owner, thread_flags_t tflags, thread_entry_t entry, void *arg)
{
    thread_t *t = thread_allocate(owner, tflags);

    // Kernel stack
    const vmblock_t kstack_blk = platform_mm_alloc_pages(owner->pagetable, MOS_STACK_PAGES_KERNEL, PGALLOC_HINT_USERSPACE, VM_READ | VM_WRITE);
    stack_init(&t->kernel_stack, (void *) kstack_blk.vaddr, kstack_blk.npages * MOS_PAGE_SIZE);
    process_attach_mmap(owner, kstack_blk, VMTYPE_KSTACK, false);

    if (tflags & THREAD_FLAG_USERMODE)
    {
        // allcate stack for the thread
        const vmblock_t ustack_blk = platform_mm_alloc_pages(owner->pagetable, MOS_STACK_PAGES_USER, PGALLOC_HINT_USERSPACE, VM_READ | VM_WRITE | VM_USER);
        stack_init(&t->stack, (void *) ustack_blk.vaddr, MOS_STACK_PAGES_USER * MOS_PAGE_SIZE);
        process_attach_mmap(owner, ustack_blk, VMTYPE_STACK, false);

        // map the stack into current kernel's address space, making a proxy stack
        const vmblock_t ustack_proxy = mm_map_proxy_space(owner->pagetable, ustack_blk.vaddr, ustack_blk.npages);
        downwards_stack_t proxy_stack;
        stack_init(&proxy_stack, (void *) ustack_proxy.vaddr, ustack_proxy.npages * MOS_PAGE_SIZE);
        platform_context_setup(t, &proxy_stack, entry, arg);

        t->stack.head -= proxy_stack.top - proxy_stack.head;
        mm_unmap_proxy_space(ustack_proxy);
    }

    hashmap_put(thread_table, &t->tid, t);
    process_attach_thread(owner, t);

    return t;
}

thread_t *thread_get(tid_t tid)
{
    return hashmap_get(thread_table, &tid);
}

void thread_handle_exit(thread_t *t)
{
    if (!thread_is_valid(t))
        return;

    mos_warn("TODO");
    t->status = THREAD_STATUS_DEAD;
}
