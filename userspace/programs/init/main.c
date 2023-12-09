// SPDX-License-Identifier: GPL-3.0-or-later

#include <argparse/libargparse.h>
#include <libconfig/libconfig.h>
#include <mos/syscall/usermode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const config_t *config;

static char *string_trim(char *in)
{
    if (in == NULL)
        return NULL;

    char *end;

    // Trim leading space
    while (*in == ' ')
        in++;

    if (*in == 0) // All spaces?
        return in;

    // Trim trailing space
    end = in + strlen(in) - 1;
    while (end > in && *end == ' ')
        end--;

    // Write new null terminator
    *(end + 1) = '\0';
    return in;
}

static pid_t start_device_manager(void)
{
    const char *dm_path = config_get(config, "device_manager.path");
    if (!dm_path)
        dm_path = "/initrd/drivers/device_manager";

    size_t dm_args_count;
    const char **dm_args = config_get_all(config, "device_manager.args", &dm_args_count);

    int argc = dm_args_count + 1;
    const char *argv[argc]; // +1 for the NULL
    argv[0] = dm_path;
    for (size_t i = 0; i < dm_args_count; i++)
        argv[i + 1] = dm_args[i];
    argv[argc] = NULL;

    // start the device manager
    pid_t pid = fork();
    if (pid == 0)
    {
        execv(dm_path, (char *const *) argv);
        exit(-1);
    }

    return pid;
}

static bool create_directories(void)
{
    size_t num_dirs;
    const char **dirs = config_get_all(config, "mkdir", &num_dirs);
    if (!dirs)
        return false;

    for (size_t i = 0; i < num_dirs; i++)
    {
        const char *dir = dirs[i];
        if (mkdir(dir, 0755) != 0)
            return false;
    }

    return true;
}

static bool create_symlinks(void)
{
    size_t num_symlinks;
    const char **symlinks = config_get_all(config, "symlink", &num_symlinks);
    if (!symlinks)
        return false;

    for (size_t i = 0; i < num_symlinks; i++)
    {
        // format: <Source> <Destination>
        const char *symlink = symlinks[i];

        char *dup = strdup(symlink);
        char *source = strtok(dup, " ");
        char *destination = strtok(NULL, " ");

        if (!source || !destination)
            return false; // invalid options

        source = string_trim(source);
        destination = string_trim(destination);

        if (link(source, destination) != 0)
            return false;
    }

    return true;
}

static bool mount_filesystems(void)
{
    size_t num_mounts;
    const char **mounts = config_get_all(config, "mount", &num_mounts);
    if (!mounts)
        return false;

    for (size_t i = 0; i < num_mounts; i++)
    {
        // format: <Device> <MountPoint> <Filesystem> <Options>
        const char *mount = mounts[i];

        char *dup = strdup(mount);
        char *device = strtok(dup, " ");
        char *mount_point = strtok(NULL, " ");
        char *filesystem = strtok(NULL, " ");
        char *options = strtok(NULL, " ");

        if (!device || !mount_point || !filesystem || !options)
            return false; // invalid options

        device = string_trim(device);
        mount_point = string_trim(mount_point);
        filesystem = string_trim(filesystem);
        options = string_trim(options);

        if (syscall_vfs_mount(device, mount_point, filesystem, options) != 0)
            return false;
    }

    return true;
}

#define DYN_ERROR_CODE (__COUNTER__ + 1)

static const argparse_arg_t longopts[] = {
    { "help", 'h', ARGPARSE_NONE, "show this help" },
    { "config", 'C', ARGPARSE_REQUIRED, "configuration file, default: /initrd/config/init.conf" },
    { "shell", 'S', ARGPARSE_REQUIRED, "shell to start, default: /initrd/programs/mossh" },
    { 0 },
};

int main(int argc, const char *argv[])
{
    sigaction(SIGCHLD, &(struct sigaction){ .sa_handler = SIG_IGN, .sa_flags = SA_RESTART }, NULL);
    sigaction(SIGTERM, &(struct sigaction){ .sa_handler = SIG_IGN, .sa_flags = SA_RESTART }, NULL);

    const char *config_file = "/initrd/config/init.conf";
    const char *shell = "/initrd/programs/mossh";
    argparse_state_t state;
    argparse_init(&state, argv);
    while (true)
    {
        const int option = argparse_long(&state, longopts, NULL);
        if (option == -1)
            break;

        switch (option)
        {
            case 'C': config_file = state.optarg; break;
            case 'S': shell = state.optarg; break;
            case 'h': argparse_usage(&state, longopts, "the init program"); return 0;
            default: break;
        }
    }

    if (getpid() != 1)
    {
        puts("init: not running as PID 1");

        for (int i = 0; i < argc; i++)
            printf("argv[%d] = %s\n", i, argv[i]);

        puts("Leaving init...");
        return DYN_ERROR_CODE;
    }

    config = config_parse_file(config_file);
    if (!config)
        return DYN_ERROR_CODE;

    if (!create_directories())
        return DYN_ERROR_CODE;

    if (!create_symlinks())
        return DYN_ERROR_CODE;

    if (!mount_filesystems())
        return DYN_ERROR_CODE;

    pid_t dm_pid = start_device_manager();
    if (dm_pid <= 0)
        return DYN_ERROR_CODE;

    // start the shell
    const char **shell_argv = malloc(sizeof(char *));
    int shell_argc = 1;
    shell_argv[0] = shell;

    const char *arg;
    argparse_init(&state, argv); // reset the options
    while ((arg = argparse_arg(&state)))
    {
        shell_argc++;
        shell_argv = realloc(shell_argv, shell_argc * sizeof(char *));
        shell_argv[shell_argc - 1] = arg;
    }
    shell_argv = realloc(shell_argv, (shell_argc + 1) * sizeof(char *));
    shell_argv[shell_argc] = NULL;

start_shell:;
    const pid_t shell_pid = fork();
    if (shell_pid == 0)
        if (execv(shell, (char **) shell_argv) <= 0)
            return DYN_ERROR_CODE;

    while (true)
    {
        pid_t pid = waitpid(-1, NULL, 0);
        if (pid == dm_pid)
        {
            puts("init: device manager exited, restarting...");
            dm_pid = start_device_manager();
        }
        else if (pid == shell_pid)
        {
            puts("init: shell exited, restarting...");
            goto start_shell;
        }
        else
        {
            printf("init: process %d exited\n", pid);
        }
    }

    return 0;
}
