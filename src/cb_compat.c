#include "config.h"
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

FILE *cb_open_memstream (char **bufp, size_t *sizep)
{
  *bufp = NULL;
  *sizep = 0;
  _cb_ms_state.fp = tmpfile ();
  if (!_cb_ms_state.fp)
    return NULL;
  _cb_ms_state.bufp = bufp;
  _cb_ms_state.sizep = sizep;
  return _cb_ms_state.fp;
}

int cb_close_memstream (FILE *f)
{
  if (f != _cb_ms_state.fp)
    return fclose (f);

  long size;
  char *buf;

  fseek (f, 0, SEEK_END);
  size = ftell (f);
  if (size < 0) {
    fclose (f);
    return -1;
  }
  buf = malloc ((size_t)size + 1);
  if (!buf) {
    fclose (f);
    return -1;
  }
  fseek (f, 0, SEEK_SET);
  if (size > 0)
    fread (buf, 1, (size_t)size, f);
  buf[size] = '\0';
  fclose (f);

  *_cb_ms_state.bufp = buf;
  *_cb_ms_state.sizep = (size_t)size;
  _cb_ms_state.fp = NULL;
  return 0;
}

#endif /* HAVE_OPEN_MEMSTREAM */

/* --- Winsock2 helpers --- */

#ifdef _WIN32

int cb_wsa_startup (void)
{
  WSADATA wsa;
  static int initialized = 0;
  if (initialized)
    return 0;
  if (WSAStartup (MAKEWORD (2, 2), &wsa) != 0)
    return -1;
  initialized = 1;
  return 0;
}

void cb_wsa_cleanup (void)
{
  WSACleanup ();
}

const char *cb_wsa_strerror (int err)
{
  static char buf[256];
  switch (err) {
  case WSAECONNREFUSED:
    return "Connection refused";
  case WSAETIMEDOUT:
    return "Connection timed out";
  case WSAECONNRESET:
    return "Connection reset by peer";
  case WSAEHOSTUNREACH:
    return "Host unreachable";
  case WSAENETUNREACH:
    return "Network unreachable";
  case WSAECONNABORTED:
    return "Connection aborted";
  case WSAESHUTDOWN:
    return "Socket has been shut down";
  case WSAEINTR:
    return "Interrupted system call";
  default:
    snprintf (buf, sizeof (buf), "Winsock error %d", err);
    return buf;
  }
}

#endif /* _WIN32 */

/* --- setenv/unsetenv compat --- */

#ifdef _WIN32

int cb_setenv (const char *name, const char *value, int overwrite)
{
  if (!overwrite && getenv (name))
    return 0;
  return _putenv_s (name, value);
}

int cb_unsetenv (const char *name)
{
  _putenv_s (name, "");
  SetEnvironmentVariableA (name, NULL);
  return 0;
}

#else

int cb_setenv (const char *name, const char *value, int overwrite)
{
  return setenv (name, value, overwrite);
}

int cb_unsetenv (const char *name)
{
  return unsetenv (name);
}

#endif

/* --- Config directory --- */

#ifdef _WIN32

const char *cb_config_dir (void)
{
  const char *xdg = getenv ("XDG_CONFIG_HOME");
  if (xdg && xdg[0])
    return xdg;
  const char *dir = getenv ("APPDATA");
  if (dir && dir[0])
    return dir;
  const char *userprofile = getenv ("USERPROFILE");
  if (userprofile && userprofile[0]) {
    static char buf[MAX_PATH];
    snprintf (buf, sizeof (buf), "%s\\AppData\\Roaming", userprofile);
    return buf;
  }
  return NULL;
}

#else

const char *cb_config_dir (void)
{
  const char *xdg = getenv ("XDG_CONFIG_HOME");
  if (xdg && xdg[0])
    return xdg;
  const char *home = getenv ("HOME");
  if (home && home[0]) {
    static char buf[512];
    snprintf (buf, sizeof (buf), "%s/.config", home);
    return buf;
  }
  return NULL;
}

#endif

/* --- Base64 encoding/decoding --- */

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode (const unsigned char *data, size_t len)
{
  size_t out_len = 4 * ((len + 2) / 3);
  char *out = malloc (out_len + 1);
  if (!out)
    return NULL;

  size_t i, j;
  for (i = 0, j = 0; i + 2 < len; i += 3) {
    unsigned int v = ((unsigned int)data[i] << 16) | ((unsigned int)data[i + 1] << 8) | (unsigned int)data[i + 2];
    out[j++] = b64_table[(v >> 18) & 0x3F];
    out[j++] = b64_table[(v >> 12) & 0x3F];
    out[j++] = b64_table[(v >> 6) & 0x3F];
    out[j++] = b64_table[v & 0x3F];
  }
  if (i < len) {
    unsigned int v = (unsigned int)data[i] << 16;
    if (i + 1 < len)
      v |= (unsigned int)data[i + 1] << 8;
    out[j++] = b64_table[(v >> 18) & 0x3F];
    out[j++] = b64_table[(v >> 12) & 0x3F];
    out[j++] = (i + 1 < len) ? b64_table[(v >> 6) & 0x3F] : '=';
    out[j++] = '=';
  }
  out[j] = '\0';
  return out;
}

static int b64_val (char c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

unsigned char *base64_decode (const char *str, size_t *out_len)
{
  size_t len = strlen (str);
  if (len == 0) {
    *out_len = 0;
    unsigned char *out = malloc (1);
    if (out)
      out[0] = '\0';
    return out;
  }

  unsigned char *out = malloc (len);
  if (!out)
    return NULL;

  size_t i, j;
  for (i = 0, j = 0; i + 3 < len; i += 4) {
    int a = b64_val (str[i]);
    int b = b64_val (str[i + 1]);
    int c = (str[i + 2] == '=') ? 0 : b64_val (str[i + 2]);
    int d = (str[i + 3] == '=') ? 0 : b64_val (str[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0) {
      free (out);
      return NULL;
    }
    unsigned int v = ((unsigned int)a << 18) | ((unsigned int)b << 12) | ((unsigned int)c << 6) | (unsigned int)d;
    out[j++] = (unsigned char)((v >> 16) & 0xFF);
    if (str[i + 2] != '=')
      out[j++] = (unsigned char)((v >> 8) & 0xFF);
    if (str[i + 3] != '=')
      out[j++] = (unsigned char)(v & 0xFF);
  }
  *out_len = j;
  return out;
}

/* --- URL encoding --- */

char *url_encode (const char *str)
{
  if (!str)
    return NULL;
  size_t len = strlen (str);
  char *out = malloc (len * 3 + 1);
  if (!out)
    return NULL;
  size_t j = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~') {
      out[j++] = (char)c;
    } else {
      snprintf (out + j, 4, "%%%02X", c);
      j += 3;
    }
  }
  out[j] = '\0';
  return out;
}
