// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers.h"
#include "lib/stdio.h"
#include "lib/sync/mutex.h"
#include "lib/sync/spinlock.h"
#include "librpc/rpc_client.h"
#include "libuserspace.h"
#include "mos/device/dm_types.h"
#include "mos/syscall/usermode.h"
#include "mos/types.h"
#include "mos/x86/delays.h"

#define N_THREADS  10
#define N_WORKLOAD 50000

typedef struct
{
    void (*acquire)(void);
    void (*release)(void);
} lock_t;

// clang-format off
static spinlock_t s_lock = SPINLOCK_INIT;
static inline void s_acquire(void) { spinlock_acquire(&s_lock); }
static inline void s_release(void) { spinlock_release(&s_lock); }

static mutex_t m_lock = MUTEX_INIT;
static inline void m_acquire(void) { mutex_acquire(&m_lock); }
static inline void m_release(void) { mutex_release(&m_lock); }

static inline void no_acquire(void) { }
static inline void no_release(void) { }

static const lock_t spinlock = { .acquire =  s_acquire, .release = s_release };
static const lock_t mutex = { .acquire = m_acquire, .release = m_release };
static const lock_t no_lock = { .acquire = no_acquire, .release = no_release };
// clang-format on

static void time_consuming_work(void)
{
    for (u32 i = 0; i < 100; i++)
    {
        volatile u32 j = 0;
        j++; // prevent compiler from optimizing this loop away
    }
}

static u64 counter = 0;
static tid_t threads[N_THREADS] = { 0 };

static void thread_do_work(void *arg)
{
    const lock_t *lock = (const lock_t *) arg;
    printf("Thread %ld started!\n", syscall_get_tid());
    lock->acquire();
    for (size_t i = 0; i < N_WORKLOAD; i++)
    {
        u64 current_count = counter;
        time_consuming_work();
        current_count++;
        counter = current_count;
    };
    lock->release();
    printf("Thread %ld finished!\n", syscall_get_tid());
}

static void run_single_test(const char *name, const lock_t *lock)
{
    set_console_color(Yellow, Black);
    print_to_console("%-10s: test started!\n", name);
    counter = 0;

    const u64 started = rdtsc(); // record a start timestamp
    {
        for (u32 i = 0; i < N_THREADS; i++)
            threads[i] = start_thread("thread", thread_do_work, (void *) lock);

        for (u32 i = 0; i < N_THREADS; i++)
            syscall_wait_for_thread(threads[i]);
    }
    const u64 finished = rdtsc(); // record a finish timestamp

    const u64 expected = N_WORKLOAD * N_THREADS;
    if (counter != expected)
    {
        set_console_color(Red, Black);
        print_to_console("%-10s: FAIL: counter value: %llu, where it should be %llu\n", name, counter, expected);
    }
    else
    {
        set_console_color(Green, Black);
        print_to_console("%-10s: SUCCESS: counter value: %llu\n", name, counter);
    }

    const u64 elapsed = (finished - started) / 1000000; // in millions of cycles

    print_to_console("%-10s: elapsed: %llu million cycles\n", name, elapsed);

    set_console_color(White, Black);
    print_to_console("\n");
}

int main(int argc, char **argv)
{
    MOS_UNUSED(argc);
    MOS_UNUSED(argv);
    open_console();

    run_single_test("No Lock", &no_lock);
    run_single_test("Spinlock", &spinlock);
    run_single_test("Mutex", &mutex);

    while (1)
        ;
    return 0;
}