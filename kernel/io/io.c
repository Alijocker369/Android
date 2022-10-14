// SPDX-License-Identifier: GPL-3.0-or-later

#include "mos/io/io.h"

#include "mos/printk.h"

void io_init(io_t *io, io_flags_t flags, size_t size, io_op_t *ops)
{
    io->flags = flags;
    io->size = size;
    io->ops = ops;
}

void io_ref(io_t *io)
{
    mos_debug("io_ref(%p)", (void *) io);
    io->refcount.atomic++;
}

void io_unref(io_t *io)
{
    mos_debug("io_unref(%p)", (void *) io);
    io->refcount.atomic--;
    if (io->refcount.atomic > 0)
        return;

    io_close(io);
}

size_t io_read(io_t *io, void *buf, size_t count)
{
    if (!(io->flags & IO_READABLE))
    {
        pr_info2("io_read: %p is not readable\n", (void *) io);
        return 0;
    }

    if (unlikely(!io->ops->read))
    {
        mos_warn_once("io_read: no read function");
        return 0;
    }
    return io->ops->read(io, buf, count);
}

size_t io_write(io_t *io, const void *buf, size_t count)
{
    if (!(io->flags & IO_WRITABLE))
    {
        pr_info2("io_write: %p is not writable\n", (void *) io);
        return 0;
    }
    if (unlikely(!io->ops->write))
    {
        mos_warn("io_write: no write function");
        return 0;
    }
    return io->ops->write(io, buf, count);
}

void io_close(io_t *io)
{
    if (unlikely(io->closed))
    {
        mos_warn("io_close: %p is already closed\n", (void *) io);
        return;
    }
    if (unlikely(!io->ops->close))
    {
        mos_warn("io_close: no close function");
        return;
    }
    io->refcount.atomic--;
    if (io->refcount.atomic != 0)
        mos_warn("io_close: %p still has %llu references\n", (void *) io, io->refcount.atomic);

    io->closed = true;
    io->ops->close(io);
}