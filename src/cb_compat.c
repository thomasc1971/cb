#include "cb_compat.h"

#ifndef HAVE_OPEN_MEMSTREAM

#include <stdlib.h>
#include <string.h>

/* Portable open_memstream replacement using tmpfile.
 * On close, the file contents are read into a malloc'd buffer
 * and the out-parameters are set. The buffer is null-terminated. */

typedef struct
{
    FILE *fp;
    char **bufp;
    size_t *sizep;
} cb_memstream_state;

/* We store the state alongside the FILE* using a small wrapper.
 * Since we can't attach data to a FILE*, we keep a static table.
 * This is safe because cb is single-threaded for serialization. */

static cb_memstream_state _cb_ms_state;

FILE *cb_open_memstream(char **bufp, size_t *sizep)
{
    *bufp = NULL;
    *sizep = 0;
    _cb_ms_state.fp = tmpfile();
    if (!_cb_ms_state.fp)
        return NULL;
    _cb_ms_state.bufp = bufp;
    _cb_ms_state.sizep = sizep;
    return _cb_ms_state.fp;
}

int cb_close_memstream(FILE *f)
{
    if (f != _cb_ms_state.fp)
        return fclose(f);

    long size;
    char *buf;

    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return -1;
    }
    buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    fseek(f, 0, SEEK_SET);
    if (size > 0)
        fread(buf, 1, (size_t)size, f);
    buf[size] = '\0';
    fclose(f);

    *_cb_ms_state.bufp = buf;
    *_cb_ms_state.sizep = (size_t)size;
    _cb_ms_state.fp = NULL;
    return 0;
}

#endif /* HAVE_OPEN_MEMSTREAM */
