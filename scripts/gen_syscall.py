#!/usr/bin/env python

import io
import json
import os
from abc import ABC, abstractmethod
from sys import argv

MAX_SYSCALL_NARGS = 6


class Scope:
    def __init__(self):
        self.n = 0

    def __enter__(self):
        self.n += 1

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.n -= 1


def syscall_args(e):
    s = []
    # fd_t fd, void *buffer, size_t size, size_t offset
    for a in e["arguments"]:
        spacer = "" if a["type"].endswith("*") else " "
        s.append(a["type"] + spacer + a["arg"])
    if len(s) == 0:
        return "void"
    return ", ".join(s)


def syscall_is_noreturn(e):
    return e["return"] is None


def syscall_has_return_value(e):
    return (not syscall_is_noreturn(e)) and (e["return"] != "void")


def syscall_name_with_prefix(e):
    return "syscall_" + e["name"]


def syscall_format_return_type(e) -> str:
    if syscall_is_noreturn(e):
        return "noreturn void" + " "
    elif e["return"].endswith("*"):
        return e["return"]  # make * stick to the type
    else:
        return e["return"] + " "


def select_format(type: str) -> str:
    select_formats = {
        "fd_t": "%d",
        "size_t": "%zu",
        "off_t": "%zd",
        "int": "%d",
        "void *": "%p",
        "u8": "%u", "s8": "%d",
        "u16": "%u", "s16": "%d",
        "u32": "%u", "s32": "%d",
        "u64": "%llu", "s64": "%lld",
        "const char *": "'%s'",
        "char": "%c",
        "uid_t": "%d", "gid_t": "%d",
        "pid_t": "%d", "tid_t": "%d",
        "open_flags": "%x",
        "fstatat_flags": "%x",
        "thread_entry_t": "%p",
        "heap_control_op": "%d",
        "ptr_t": "\" PTR_FMT \"",
        "file_type_t": "%d",
        "mem_perm_t": "%d",
        "mmap_flags_t": "%d",
        "io_seek_whence_t": "%d",
        "signal_t": "%d",
        "bool": "%d",
    }
    if type in select_formats:
        return select_formats[type]
    if type.endswith("*"):
        return "%p"
    raise LookupError("Unknown type: %s" % type)


def select_format_type(type: str) -> str:
    if type == "const char *":
        return type
    if type.endswith("*") or type == "thread_entry_t":
        return "void *"
    return type


class BaseAbstractGenerator(ABC):
    def __init__(self):
        self.scope = Scope()

    def start(self, basedir: str):
        self.outfile = open(os.path.join(basedir, self.filename()), "w")
        self.gen("// SPDX-License-Identifier: GPL-3.0-or-later")
        self.gen("// " + self.description())
        self.gen("// This file was generated by scripts/gen_syscall.py")
        self.gen("")
        self.gen("#pragma once")
        self.gen("")

    def finish(self):
        self.outfile.close()

    def gen(self, s: str):
        for _ in range(self.scope.n):
            self.outfile.write("    ")
        self.outfile.write(s + "\n")

    def gen_includes(self, includes: list[str]):
        includes.sort()
        for inc in includes:
            self.gen("#include <%s>" % inc)
        self.gen("")

    @abstractmethod
    def filename(self):
        pass

    @abstractmethod
    def description(self):
        pass

    @abstractmethod
    def generate_prologue(self):
        pass

    @abstractmethod
    def generate_single(self, e):
        pass

    @abstractmethod
    def generate_epilogue(self):
        pass


class KernelDeclGenerator(BaseAbstractGenerator):
    def filename(self):
        return "decl.h"

    def description(self):
        return "Kernel syscall declarations"

    def generate_prologue(self):
        self.gen_includes(j["includes"])

    def generate_single(self, e):
        line = ""
        line += syscall_format_return_type(e)
        line += "impl_" + syscall_name_with_prefix(e)
        line += "(" + syscall_args(e) + ");"
        self.gen(line)

    def generate_epilogue(self):
        self.gen("#define define_syscall(name) impl_syscall_##name")


class UsermodeWrapperGenerator(BaseAbstractGenerator):
    def filename(self):
        return "usermode.h"

    def description(self):
        return "Usermode syscall wrappers"

    def generate_prologue(self):
        self.gen_includes(j["includes"])
        # also include the 'mos/platform_syscall.h' header
        self.gen("// platform syscall header")
        self.gen('#include <mos/platform_syscall.h>')
        self.gen('#include <mos/syscall/number.h>')
        self.gen("")
        self.gen("#ifdef __MOS_KERNEL__")
        self.gen("#error \"This file should not be included in the kernel!\"")
        self.gen("#endif")
        self.gen("")

    def generate_single(self, e):
        syscall_nargs = len(e["arguments"])
        syscall_conv_arg_to_reg_type = ", ".join(["SYSCALL_" + str(e["name"])] + ["(reg_t) %s" % arg["arg"] for arg in e["arguments"]])
        comments = e["comments"] if "comments" in e else []
        return_stmt = "return (" + e["return"] + ") " if syscall_has_return_value(e) else ""

        if len(comments) > 0:
            self.gen("/**")
            self.gen(" * %s" % e["name"])
            for comment in comments:
                self.gen(" * %s" % comment)
            self.gen(" */")

        self.gen("should_inline %s%s(%s)" % (syscall_format_return_type(e), syscall_name_with_prefix(e), syscall_args(e)))
        self.gen("{")
        with self.scope:
            self.gen("%splatform_syscall%d(%s);" % (return_stmt,
                                                    syscall_nargs,
                                                    syscall_conv_arg_to_reg_type))
            if syscall_is_noreturn(e):
                self.gen("__builtin_unreachable();")
        self.gen("}")

    def generate_epilogue(self):
        pass


class SyscallNumberGenerator(BaseAbstractGenerator):
    def filename(self):
        return "number.h"

    def description(self):
        return "Syscall number definitions"

    def generate_epilogue(self):
        pass

    def generate_prologue(self):
        self.gen("// expand to 1 if syscall is defined, 0 otherwise")
        self.gen("#define SYSCALL_DEFINED(name) (SYSCALL_##name > 0)")
        self.gen("")
        pass

    def generate_single(self, e):
        self.gen("#define SYSCALL_%s %d" % (e["name"], e["number"]))
        self.gen("#define SYSCALL_NAME_%d %s" % (e["number"], e["name"]))


class SyscallDispatcherGenerator(BaseAbstractGenerator):
    def filename(self):
        return "dispatcher.h"

    def description(self):
        return "Syscall dispatcher"

    def generate_prologue(self):
        self.gen_includes(j["includes"])
        self.gen("// syscall implementation declarations and syscall numbers")
        self.gen('#include <mos/syscall/decl.h>')
        self.gen('#include <mos/syscall/number.h>')
        self.gen("")
        self.gen("// mos_debug macro support")
        self.gen('#include "mos/printk.h"')
        self.gen("")
        self.gen("should_inline reg_t dispatch_syscall(const reg_t number, %s)" % (", ".join(["reg_t arg%d" % (i + 1) for i in range(MAX_SYSCALL_NARGS)])))
        self.gen("{")
        with self.scope:
            for i in range(MAX_SYSCALL_NARGS):
                self.gen("MOS_UNUSED(arg%d);" % (i + 1))
            self.gen("")
            self.gen("reg_t ret = 0;")
            self.gen("")
            self.gen("switch (number)")
            self.gen("{")

    def generate_single(self, e):
        with self.scope:  # function scope
            with self.scope:  # switch scope
                nargs = len(e["arguments"])
                syscall_arg_casted = ", ".join(["(%s) arg%d" % (e["arguments"][i]["type"], i + 1) for i in range(nargs)])
                retval_assign = "ret = (reg_t) " if syscall_has_return_value(e) else ""

                self.gen("case SYSCALL_%s:" % e["name"])
                self.gen("{")
                with self.scope:
                    fmt = 'mos_debug(syscall, "%s(' % e["name"]
                    fmt += ", ".join(["%s=%s" % (e["arguments"][i]["arg"], select_format(e["arguments"][i]["type"])) for i in range(nargs)])
                    fmt += ")\""

                    if e["arguments"]:
                        fmt += ", "
                        fmt += ", ".join(["(%s) arg%d" % (select_format_type(e["arguments"][i]["type"]), i + 1) for i in range(nargs)])
                    fmt += ");"
                    self.gen(fmt)
                    self.gen("%simpl_%s(%s);" % (retval_assign, syscall_name_with_prefix(e), syscall_arg_casted))
                    self.gen("break;")
                self.gen("}")

    def generate_epilogue(self):
        with self.scope:
            self.gen("}")
            self.gen("")
            self.gen("return ret;")
        self.gen("}")


generators: list[BaseAbstractGenerator] = [
    KernelDeclGenerator(),
    UsermodeWrapperGenerator(),
    SyscallNumberGenerator(),
    SyscallDispatcherGenerator(),
]


if __name__ != "__main__":
    print("This script is not meant to be imported.")
    exit(1)

if len(argv) != 3:
    print("Usage:")
    print("  gen_syscall.py <syscall-json> <output-directory>")
    print("")
    print("This script generates several files from the syscall json file.")
    for g in generators:
        print("  %-15s  %s" % (g.filename(), g.description()))
    exit(1)

input_json = argv[1]
output_dir = argv[2]

if not os.path.exists(output_dir) or not os.path.isdir(output_dir):
    print("Output directory does not exist: %s" % output_dir)
    exit(1)

if not os.path.exists(input_json) or not os.path.isfile(input_json):
    print("Input json file does not exist: %s" % input_json)
    exit(1)

with open(input_json, "r") as f:
    j = json.load(f)

    for g in generators:
        g.start(output_dir)
        g.generate_prologue()
        for e in j["syscalls"]:
            g.generate_single(e)
            g.gen("")
        g.generate_epilogue()
        g.finish()
