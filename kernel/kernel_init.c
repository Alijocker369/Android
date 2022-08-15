// SPDX-License-Identifier: GPL-3.0-or-later

#include "mos/mm/paging.h"
#include "mos/platform/platform.h"
#include "mos/printk.h"

#ifdef MOS_KERNEL_RUN_TESTS
extern void mos_test_engine_run_tests();
#endif

void mos_start_kernel(mos_init_info_t *init_info)
{
    mos_init_kernel_mm();
    mos_platform.post_init(init_info);
    mos_platform.devices_setup(init_info);
    mos_platform.interrupt_enable();

    pr_info("Welcome to MOS!");
    pr_info("Boot Information:");
    pr_emph("cmdline: %s", init_info->cmdline);
    pr_emph("%-25s'%s'", "Kernel Version:", MOS_KERNEL_VERSION);
    pr_emph("%-25s'%s'", "Kernel Revision:", MOS_KERNEL_REVISION);
    pr_emph("%-25s'%s'", "Kernel builtin cmdline:", MOS_KERNEL_BUILTIN_CMDLINE);

    mos_warn("V2Ray 4.45.2 started");

#ifdef MOS_KERNEL_RUN_TESTS
    mos_test_engine_run_tests();
#endif
    while (1)
        ;
}