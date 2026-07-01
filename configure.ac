AC_INIT([cb], [0.1.0], [bugs@example.com])
AC_CONFIG_MACRO_DIRS([m4])
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE([foreign -Wall subdir-objects])
AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_HEADERS([config.h])
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AR
AC_PROG_RANLIB

# Require C11
AX_CHECK_COMPILE_FLAG([-std=c11], [CFLAGS="$CFLAGS -std=c11"])

# --- libtls ---
# Fedora: dnf install libretls-devel
# Debian: apt install libretls-dev
# MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-libressl
#   (libretls not packaged for MSYS2; LibreSSL provides libtls)
#   Set PKG_CONFIG_PATH=/ucrt64/lib/libressl/pkgconfig if needed.
PKG_CHECK_MODULES([LIBTLS], [libtls])

# --- pthread ---
AX_PTHREAD
AS_IF([test -z "$PTHREAD_LIBS" && test -z "$PTHREAD_CFLAGS"], [
  AC_MSG_CHECKING([if pthreads is in default runtime])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <pthread.h>]],
                                  [[pthread_create(0,0,0,0);]])], [
    AC_MSG_RESULT([yes])
    PTHREAD_CFLAGS=""
    PTHREAD_LIBS=""
  ], [AC_MSG_ERROR([pthreads not found])])
])

# --- Networking libraries ---
# MSYS2 UCRT64 provides POSIX sockets in the default runtime.
# Native MinGW would need -lws2_32; check for it as a fallback.
AC_CHECK_LIB([ws2_32], [WSAStartup], [LIBS="-lws2_32 $LIBS"])

# --- POSIX socket headers (not available on MSYS2 UCRT64 / native MinGW) ---
# cb_http.c uses getaddrinfo, socket, connect, poll — these require POSIX
# headers that are absent from the UCRT64 toolchain (which uses Winsock2).
# A Winsock2 port of cb_http.c is needed to build on pure Windows.
AC_CHECK_HEADERS([netdb.h poll.h sys/socket.h], [],
  [AC_MSG_WARN([$ac_header not found — cb_http.c requires POSIX socket headers])])

# --- open_memstream (POSIX, not available on MSYS2/MinGW) ---
AC_CHECK_FUNCS([open_memstream])

# --- ASan/UBSan (opt-in) ---
AC_ARG_ENABLE([asan],
  [AS_HELP_STRING([--enable-asan], [enable ASan+UBSan (default: no)])],
  [have_asan=$enableval], [have_asan=no])
AS_IF([test "x$have_asan" = xyes], [
  AX_CHECK_COMPILE_FLAG([-fsanitize=address,undefined], [
    CFLAGS="$CFLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -O0 -g3"
    LDFLAGS="$LDFLAGS -fsanitize=address,undefined"
  ], [AC_MSG_ERROR([--enable-asan requested but compiler does not support it])])
])

# --- -Werror (default on, allow disable) ---
AC_ARG_ENABLE([werror],
  [AS_HELP_STRING([--disable-werror], [do not treat warnings as errors])],
  [], [enable_werror=yes])
AS_IF([test "x$enable_werror" != xno], [CFLAGS="$CFLAGS -Werror"])

# Also pass -Wall -Wextra unconditionally
CFLAGS="$CFLAGS -Wall -Wextra"

AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])
AC_OUTPUT
