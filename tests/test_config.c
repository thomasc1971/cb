#include "config.h"
#include "cb_compat.h"
#include "cb_config.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char tmpdir[256];

static void write_config_file (const char *content)
{
  char path[512];
  snprintf (path, sizeof (path), "%s/config", tmpdir);
  FILE *f = fopen (path, "w");
  ASSERT_NOT_NULL (f);
  fputs (content, f);
  fclose (f);
}

static void test_env_token (void)
{
  cb_setenv ("CB_TOKEN", "env-token-123", 1);
  cb_unsetenv ("CB_BASE_URL");

  Config cfg;
  char err[256] = { 0 };
  int rc = config_load (&cfg, NULL, err, sizeof (err));
  ASSERT_EQ (rc, 0);
  ASSERT_TRUE (cfg.token_set);
  ASSERT_STR_EQ (cfg.token, "env-token-123");
  ASSERT_STR_EQ (cfg.base_url, DEFAULT_BASE_URL);

  cb_unsetenv ("CB_TOKEN");
}

static void test_env_base_url (void)
{
  cb_setenv ("CB_TOKEN", "tok", 1);
  cb_setenv ("CB_BASE_URL", "https://example.com/api/v1", 1);

  Config cfg;
  char err[256] = { 0 };
  int rc = config_load (&cfg, NULL, err, sizeof (err));
  ASSERT_EQ (rc, 0);
  ASSERT_STR_EQ (cfg.base_url, "https://example.com/api/v1");

  cb_unsetenv ("CB_TOKEN");
  cb_unsetenv ("CB_BASE_URL");
}

static void test_config_file_token (void)
{
  cb_setenv ("CB_TOKEN", "", 1); /* empty env so file takes over */
  cb_unsetenv ("CB_BASE_URL");
  write_config_file ("token = \"file-token-abc\"\n");

  char path[512];
  snprintf (path, sizeof (path), "%s/config", tmpdir);

  Config cfg;
  char err[256] = { 0 };
  int rc = config_load (&cfg, path, err, sizeof (err));
  ASSERT_EQ (rc, 0);
  ASSERT_TRUE (cfg.token_set);
  ASSERT_STR_EQ (cfg.token, "file-token-abc");

  cb_unsetenv ("CB_TOKEN");
}

static void test_config_file_base_url (void)
{
  cb_unsetenv ("CB_TOKEN");
  cb_unsetenv ("CB_BASE_URL");
  write_config_file ("token = \"tok\"\nbase_url = \"https://forge.example.org/api/v1\"\n");

  char path[512];
  snprintf (path, sizeof (path), "%s/config", tmpdir);

  Config cfg;
  char err[256] = { 0 };
  int rc = config_load (&cfg, path, err, sizeof (err));
  ASSERT_EQ (rc, 0);
  ASSERT_STR_EQ (cfg.base_url, "https://forge.example.org/api/v1");
}

static void test_cli_overrides_env (void)
{
  cb_setenv ("CB_TOKEN", "tok", 1);
  cb_setenv ("CB_BASE_URL", "https://env.example.com/api/v1", 1);

  Config cfg;
  char err[256] = { 0 };
  config_load (&cfg, NULL, err, sizeof (err));
  config_apply_cli_override (&cfg, "https://cli.example.com/api/v1");
  ASSERT_STR_EQ (cfg.base_url, "https://cli.example.com/api/v1");

  cb_unsetenv ("CB_TOKEN");
  cb_unsetenv ("CB_BASE_URL");
}

static void test_env_overrides_file (void)
{
  cb_setenv ("CB_TOKEN", "env-tok", 1);
  cb_setenv ("CB_BASE_URL", "https://env.example.com/api/v1", 1);
  write_config_file ("token = \"file-tok\"\nbase_url = \"https://file.example.com/api/v1\"\n");

  char path[512];
  snprintf (path, sizeof (path), "%s/config", tmpdir);

  Config cfg;
  char err[256] = { 0 };
  int rc = config_load (&cfg, path, err, sizeof (err));
  ASSERT_EQ (rc, 0);
  ASSERT_STR_EQ (cfg.token, "env-tok");
  ASSERT_STR_EQ (cfg.base_url, "https://env.example.com/api/v1");

  cb_unsetenv ("CB_TOKEN");
  cb_unsetenv ("CB_BASE_URL");
}

static void test_default_base_url (void)
{
  cb_setenv ("CB_TOKEN", "tok", 1);
  cb_unsetenv ("CB_BASE_URL");

  Config cfg;
  char err[256] = { 0 };
  int rc = config_load (&cfg, NULL, err, sizeof (err));
  ASSERT_EQ (rc, 0);
  ASSERT_STR_EQ (cfg.base_url, DEFAULT_BASE_URL);

  cb_unsetenv ("CB_TOKEN");
}

static void test_missing_token (void)
{
  cb_unsetenv ("CB_TOKEN");
  cb_unsetenv ("CB_BASE_URL");

  Config cfg;
  char err[256] = { 0 };
  int rc = config_load (&cfg, NULL, err, sizeof (err));
  ASSERT_EQ (rc, -1);
  ASSERT_TRUE (strlen (err) > 0);
  ASSERT_FALSE (cfg.token_set);
}

static void test_malformed_config_line_skipped (void)
{
  cb_unsetenv ("CB_TOKEN");
  cb_unsetenv ("CB_BASE_URL");
  write_config_file ("# this is a comment\nmalformed line without equals\ntoken = \"good-token\"\n");

  char path[512];
  snprintf (path, sizeof (path), "%s/config", tmpdir);

  Config cfg;
  char err[256] = { 0 };
  int rc = config_load (&cfg, path, err, sizeof (err));
  ASSERT_EQ (rc, 0);
  ASSERT_STR_EQ (cfg.token, "good-token");
}

static void test_config_with_whitespace (void)
{
  cb_unsetenv ("CB_TOKEN");
  cb_unsetenv ("CB_BASE_URL");
  write_config_file ("  token   =   \"ws-token\"  \n  base_url   =   \"https://ws.example.com/api/v1\"  \n");

  char path[512];
  snprintf (path, sizeof (path), "%s/config", tmpdir);

  Config cfg;
  char err[256] = { 0 };
  int rc = config_load (&cfg, path, err, sizeof (err));
  ASSERT_EQ (rc, 0);
  ASSERT_STR_EQ (cfg.token, "ws-token");
  ASSERT_STR_EQ (cfg.base_url, "https://ws.example.com/api/v1");
}

static void test_parse_url_https (void)
{
  char host[256], prefix[256];
  int port, use_tls;
  int rc = config_parse_url ("https://codeberg.org/api/v1", host, sizeof (host),
                             &port, &use_tls, prefix, sizeof (prefix));
  ASSERT_EQ (rc, 0);
  ASSERT_STR_EQ (host, "codeberg.org");
  ASSERT_EQ (port, 443);
  ASSERT_TRUE (use_tls);
  ASSERT_STR_EQ (prefix, "/api/v1");
}

static void test_parse_url_http (void)
{
  char host[256], prefix[256];
  int port, use_tls;
  int rc = config_parse_url ("http://localhost:8080/api/v1", host, sizeof (host),
                             &port, &use_tls, prefix, sizeof (prefix));
  ASSERT_EQ (rc, 0);
  ASSERT_STR_EQ (host, "localhost");
  ASSERT_EQ (port, 8080);
  ASSERT_FALSE (use_tls);
  ASSERT_STR_EQ (prefix, "/api/v1");
}

static void test_parse_url_no_path (void)
{
  char host[256], prefix[256];
  int port, use_tls;
  int rc = config_parse_url ("https://example.com", host, sizeof (host),
                             &port, &use_tls, prefix, sizeof (prefix));
  ASSERT_EQ (rc, 0);
  ASSERT_STR_EQ (host, "example.com");
  ASSERT_EQ (port, 443);
  ASSERT_STR_EQ (prefix, "");
}

static void test_parse_url_invalid (void)
{
  char host[256], prefix[256];
  int port, use_tls;
  int rc = config_parse_url ("not-a-url", host, sizeof (host),
                             &port, &use_tls, prefix, sizeof (prefix));
  ASSERT_EQ (rc, -1);
}

int main (int argc, char *argv[])
{
  test_parse_args (argc, argv);

  /* Create temp dir for config files */
  const char *tmp = getenv ("TMPDIR");
  if (!tmp)
    tmp = getenv ("TEMP");
  if (!tmp)
    tmp = "/tmp";
  snprintf (tmpdir, sizeof (tmpdir), "%s/cb-test-%d", tmp, cb_getpid ());
  cb_mkdir (tmpdir, 0755);

  printf ("Running config tests:\n");

  RUN_TEST (test_env_token);
  RUN_TEST (test_env_base_url);
  RUN_TEST (test_config_file_token);
  RUN_TEST (test_config_file_base_url);
  RUN_TEST (test_cli_overrides_env);
  RUN_TEST (test_env_overrides_file);
  RUN_TEST (test_default_base_url);
  RUN_TEST (test_missing_token);
  RUN_TEST (test_malformed_config_line_skipped);
  RUN_TEST (test_config_with_whitespace);
  RUN_TEST (test_parse_url_https);
  RUN_TEST (test_parse_url_http);
  RUN_TEST (test_parse_url_no_path);
  RUN_TEST (test_parse_url_invalid);

  /* Cleanup */
  char path[512];
  snprintf (path, sizeof (path), "%s/config", tmpdir);
  cb_unlink (path);
  cb_rmdir (tmpdir);

  TEST_SUMMARY ();
}
