// SPDX-License-Identifier: GPL-3.0-or-later

#include "mos/printk.h"

#include "lib/stdio.h"
#include "lib/string.h"
#include "lib/structures/list.h"
#include "lib/sync/spinlock.h"
#include "mos/cmdline.h"
#include "mos/device/console.h"

#include <stdarg.h>

static console_t *printk_console;

void printk_setup_console()
{
    cmdline_arg_t *kcon_arg = mos_cmdline_get_arg("kmsg_console");

    if (!kcon_arg || kcon_arg->params_count <= 0)
        return;

    if (kcon_arg->params_count > 1)
        pr_warn("too many parameters for kmsg_console, using first one");

    cmdline_param_t *kcon_param = kcon_arg->params[0];
    if (unlikely(kcon_param->param_type != CMDLINE_PARAM_TYPE_STRING))
    {
        pr_warn("kmsg_console parameter is not a string, ignoring");
        return;
    }

    const char *kcon_name = kcon_param->val.string;

    console_t *console = console_get(kcon_name);
    if (console)
    {
        pr_emph("Selected console '%s' for future printk", kcon_name);
        printk_console = console;
        return;
    }

    console = console_get_by_prefix(kcon_name);
    if (console)
    {
        pr_emph("Selected console '%s' for future printk (prefix-based)", console->name);
        printk_console = console;
        return;
    }

    mos_warn("No console found for printk based on given name or prefix '%s'", kcon_name);
    printk_console = NULL;
}

static inline void deduce_level_color(int loglevel, standard_color_t *fg, standard_color_t *bg)
{
    *bg = Black;
    switch (loglevel)
    {
        case MOS_LOG_INFO2: *fg = DarkGray; break;
        case MOS_LOG_INFO: *fg = Gray; break;
        case MOS_LOG_EMPH: *fg = Cyan; break;
        case MOS_LOG_WARN: *fg = Brown; break;
        case MOS_LOG_EMERG: *fg = Red; break;
        case MOS_LOG_FATAL: *fg = White, *bg = Red; break;
        default: break; // do not change the color
    }
}

static void print_to_console(console_t *con, int loglevel, const char *message, size_t len)
{
    if (!con)
        return;

    spinlock_acquire(&con->lock);

    standard_color_t prev_fg, prev_bg;
    standard_color_t fg = White, bg = Black;
    deduce_level_color(loglevel, &fg, &bg);

    if (con->caps & CONSOLE_CAP_COLOR)
    {
        con->get_color(con, &prev_fg, &prev_bg);
        con->set_color(con, fg, bg);
    }

    console_write(con, message, len);

    if (con->caps & CONSOLE_CAP_COLOR)
        con->set_color(con, prev_fg, prev_bg);

    spinlock_release(&con->lock);
}

static void lvprintk(int loglevel, const char *fmt, va_list args)
{
    char message[PRINTK_BUFFER_SIZE] = { 0 };
    vsnprintf(message, PRINTK_BUFFER_SIZE, fmt, args);
    const size_t len = strlen(message);

    if (likely(printk_console))
    {
        print_to_console(printk_console, loglevel, message, len);
    }
    else
    {
        list_foreach(console_t, con, consoles)
        {
            print_to_console(con, loglevel, message, len);
        }
    }
}

void lprintk(int loglevel, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    lvprintk(loglevel, format, args);
    va_end(args);
}

void printk(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    lvprintk(MOS_LOG_INFO, format, args);
    va_end(args);
}
