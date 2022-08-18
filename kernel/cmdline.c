// SPDX-License-Identifier: GPL-3.0-or-later

#include "mos/cmdline.h"

#include "lib/string.h"
#include "mos/mm/kmalloc.h"
#include "mos/printk.h"

cmdline_t *mos_cmdline = NULL;

#define copy_string(dst, src, len) dst = kmalloc(len + 1), strncpy(dst, src, len)

cmdline_t *mos_cmdline_parse(const char *cmdline)
{
    cmdline_t *cmd = kmalloc(sizeof(cmdline_t));
    cmd->args_count = 0;

    while (*cmdline)
    {
        cmdline_arg_t *arg = kmalloc(sizeof(cmdline_arg_t));
        arg->param_count = 0;
        cmd->arguments[cmd->args_count++] = arg;

        // an arg name ends with an '=' (has parameters) or a space
        {
            const char *start = cmdline;
            while (*cmdline && !((*cmdline) == ' ' || (*cmdline) == '='))
                cmdline++;
            copy_string(arg->arg_name, start, cmdline - start);
        }

        // the option has parameters
        if (*cmdline && *cmdline++ != '=')
            continue;

        while (*cmdline && *cmdline != ' ')
        {
            cmdline_param_t *parameter = kmalloc(sizeof(cmdline_param_t));
            arg->params[arg->param_count++] = parameter;

            if (strncmp(cmdline, "true", 4) == 0)
            {
                parameter->param_type = CMDLINE_PARAM_TYPE_BOOL;
                parameter->val.boolean = true;
                cmdline += 4;
            }
            else if (strncmp(cmdline, "false", 5) == 0)
            {
                parameter->param_type = CMDLINE_PARAM_TYPE_BOOL;
                parameter->val.boolean = false;
                cmdline += 5;
            }
            else
            {
                // an option ends with a space or a comma
                const char *param_start = cmdline;
                while (*cmdline && *cmdline != ' ' && *cmdline != ',')
                    cmdline++;

                copy_string(parameter->val.string, param_start, cmdline - param_start);
                parameter->param_type = CMDLINE_PARAM_TYPE_STRING;
            }

            MOS_ASSERT(*cmdline == ' ' || *cmdline == ',' || *cmdline == '\0');

            // we have another option
            if (*cmdline == ',')
                cmdline++;

            // we have no other options, continue with another parameter
            if (*cmdline == ' ')
            {
                cmdline++;
                break;
            }
        }
    }

    return cmd;
}

cmdline_arg_t *mos_cmdline_get_arg(const char *option_name)
{
    MOS_ASSERT(mos_cmdline);
    for (u32 i = 0; i < mos_cmdline->args_count; i++)
    {
        if (strcmp(mos_cmdline->arguments[i]->arg_name, option_name) == 0)
            return mos_cmdline->arguments[i];
    }
    return NULL;
}
