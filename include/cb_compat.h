#ifndef CB_COMPAT_H
#define CB_COMPAT_H

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_OPEN_MEMSTREAM
#define cb_open_memstream(bufp, sizep) open_memstream(bufp, sizep)
#define cb_close_memstream(f)          fclose(f)
#else
FILE *cb_open_memstream(char **bufp, size_t *sizep);
int cb_close_memstream(FILE *f);
#endif

#endif /* CB_COMPAT_H */
