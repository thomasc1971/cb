#include "config.h"
#include "cb_validate.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_valid_repo_name_simple (void)
{
  char err[256];
  ASSERT_EQ (validate_repo_name ("myproj", err, sizeof (err)), VALIDATE_OK);
}

static void test_valid_repo_name_with_dash_dot (void)
{
  char err[256];
  ASSERT_EQ (validate_repo_name ("my-project.123", err, sizeof (err)), VALIDATE_OK);
}

static void test_valid_repo_name_underscore (void)
{
  char err[256];
  ASSERT_EQ (validate_repo_name ("my_repo", err, sizeof (err)), VALIDATE_OK);
}

static void test_repo_name_with_spaces (void)
{
  char err[256];
  ASSERT_EQ (validate_repo_name ("my project", err, sizeof (err)), VALIDATE_ERR);
  ASSERT_TRUE (strlen (err) > 0);
}

static void test_repo_name_with_special_chars (void)
{
  char err[256];
  ASSERT_EQ (validate_repo_name ("my!project", err, sizeof (err)), VALIDATE_ERR);
}

static void test_repo_name_too_long (void)
{
  char err[256];
  char name[120];
  memset (name, 'a', 101);
  name[101] = '\0';
  ASSERT_EQ (validate_repo_name (name, err, sizeof (err)), VALIDATE_ERR);
}

static void test_repo_name_empty (void)
{
  char err[256];
  ASSERT_EQ (validate_repo_name ("", err, sizeof (err)), VALIDATE_ERR);
}

static void test_repo_name_null (void)
{
  char err[256];
  ASSERT_EQ (validate_repo_name (NULL, err, sizeof (err)), VALIDATE_ERR);
}

static void test_repo_name_exactly_100 (void)
{
  char err[256];
  char name[101];
  memset (name, 'a', 100);
  name[100] = '\0';
  ASSERT_EQ (validate_repo_name (name, err, sizeof (err)), VALIDATE_OK);
}

static void test_description_at_limit (void)
{
  char err[256];
  char desc[2049];
  memset (desc, 'x', 2048);
  desc[2048] = '\0';
  ASSERT_EQ (validate_description (desc, err, sizeof (err)), VALIDATE_OK);
}

static void test_description_over_limit (void)
{
  char err[256];
  char desc[2051];
  memset (desc, 'x', 2049);
  desc[2049] = '\0';
  ASSERT_EQ (validate_description (desc, err, sizeof (err)), VALIDATE_ERR);
}

static void test_description_null_ok (void)
{
  char err[256];
  /* NULL description is fine — it just means "no description" */
  ASSERT_EQ (validate_description (NULL, err, sizeof (err)), VALIDATE_OK);
}

static void test_website_at_limit (void)
{
  char err[256];
  char url[1025];
  memset (url, 'x', 1024);
  url[1024] = '\0';
  ASSERT_EQ (validate_website (url, err, sizeof (err)), VALIDATE_OK);
}

static void test_website_over_limit (void)
{
  char err[256];
  char url[1027];
  memset (url, 'x', 1025);
  url[1025] = '\0';
  ASSERT_EQ (validate_website (url, err, sizeof (err)), VALIDATE_ERR);
}

static void test_website_null_ok (void)
{
  char err[256];
  ASSERT_EQ (validate_website (NULL, err, sizeof (err)), VALIDATE_OK);
}

static void test_parse_owner_repo_with_slash (void)
{
  char owner[128], repo[128], err[256];
  int rc = validate_owner_repo ("thomasc/myproj", owner, sizeof (owner),
                                repo, sizeof (repo), err, sizeof (err));
  ASSERT_EQ (rc, VALIDATE_OK);
  ASSERT_STR_EQ (owner, "thomasc");
  ASSERT_STR_EQ (repo, "myproj");
}

static void test_parse_owner_repo_no_slash (void)
{
  char owner[128], repo[128], err[256];
  int rc = validate_owner_repo ("myproj", owner, sizeof (owner),
                                repo, sizeof (repo), err, sizeof (err));
  ASSERT_EQ (rc, VALIDATE_OK);
  ASSERT_STR_EQ (owner, "");
  ASSERT_STR_EQ (repo, "myproj");
}

static void test_parse_owner_repo_with_dots (void)
{
  char owner[128], repo[128], err[256];
  int rc = validate_owner_repo ("org/repo.with.dots", owner, sizeof (owner),
                                repo, sizeof (repo), err, sizeof (err));
  ASSERT_EQ (rc, VALIDATE_OK);
  ASSERT_STR_EQ (owner, "org");
  ASSERT_STR_EQ (repo, "repo.with.dots");
}

static void test_parse_owner_repo_empty_repo (void)
{
  char owner[128], repo[128], err[256];
  int rc = validate_owner_repo ("thomasc/", owner, sizeof (owner),
                                repo, sizeof (repo), err, sizeof (err));
  ASSERT_EQ (rc, VALIDATE_ERR);
}

static void test_parse_owner_repo_empty_owner (void)
{
  char owner[128], repo[128], err[256];
  int rc = validate_owner_repo ("/myproj", owner, sizeof (owner),
                                repo, sizeof (repo), err, sizeof (err));
  ASSERT_EQ (rc, VALIDATE_ERR);
}

static void test_parse_owner_repo_null (void)
{
  char owner[128], repo[128], err[256];
  int rc = validate_owner_repo (NULL, owner, sizeof (owner),
                                repo, sizeof (repo), err, sizeof (err));
  ASSERT_EQ (rc, VALIDATE_ERR);
}

static void test_valid_merge_styles (void)
{
  char err[256];
  const char* valid[] = { "merge", "rebase", "rebase-merge", "squash",
                          "fast-forward-only", "manually-merged", "rebase-update-only" };
  for (size_t i = 0; i < sizeof (valid) / sizeof (valid[0]); i++) {
    ASSERT_EQ (validate_merge_style (valid[i], err, sizeof (err)), VALIDATE_OK);
  }
}

static void test_invalid_merge_style (void)
{
  char err[256];
  ASSERT_EQ (validate_merge_style ("invalid-style", err, sizeof (err)), VALIDATE_ERR);
}

static void test_null_merge_style (void)
{
  char err[256];
  ASSERT_EQ (validate_merge_style (NULL, err, sizeof (err)), VALIDATE_ERR);
}

static void test_validate_visibility_public (void)
{
  char err[256];
  ASSERT_EQ (validate_visibility ("public", err, sizeof (err)), VALIDATE_OK);
}

static void test_validate_visibility_limited (void)
{
  char err[256];
  ASSERT_EQ (validate_visibility ("limited", err, sizeof (err)), VALIDATE_OK);
}

static void test_validate_visibility_private (void)
{
  char err[256];
  ASSERT_EQ (validate_visibility ("private", err, sizeof (err)), VALIDATE_OK);
}

static void test_validate_visibility_null (void)
{
  char err[256];
  ASSERT_EQ (validate_visibility (NULL, err, sizeof (err)), VALIDATE_ERR);
}

static void test_validate_visibility_empty (void)
{
  char err[256];
  ASSERT_EQ (validate_visibility ("", err, sizeof (err)), VALIDATE_ERR);
}

static void test_validate_visibility_invalid (void)
{
  char err[256];
  ASSERT_EQ (validate_visibility ("hidden", err, sizeof (err)), VALIDATE_ERR);
  ASSERT_TRUE (strlen (err) > 0);
}

static void test_valid_org_name_simple (void)
{
  char err[256];
  ASSERT_EQ (validate_org_name ("myorg", err, sizeof (err)), VALIDATE_OK);
}

static void test_valid_org_name_with_dash_dot (void)
{
  char err[256];
  ASSERT_EQ (validate_org_name ("my-org.123", err, sizeof (err)), VALIDATE_OK);
}

static void test_valid_org_name_underscore (void)
{
  char err[256];
  ASSERT_EQ (validate_org_name ("my_org", err, sizeof (err)), VALIDATE_OK);
}

static void test_org_name_with_spaces (void)
{
  char err[256];
  ASSERT_EQ (validate_org_name ("my org", err, sizeof (err)), VALIDATE_ERR);
  ASSERT_TRUE (strlen (err) > 0);
}

static void test_org_name_with_special_chars (void)
{
  char err[256];
  ASSERT_EQ (validate_org_name ("my!org", err, sizeof (err)), VALIDATE_ERR);
}

static void test_org_name_too_long (void)
{
  char err[256];
  char name[120];
  memset (name, 'a', 101);
  name[101] = '\0';
  ASSERT_EQ (validate_org_name (name, err, sizeof (err)), VALIDATE_ERR);
}

static void test_org_name_empty (void)
{
  char err[256];
  ASSERT_EQ (validate_org_name ("", err, sizeof (err)), VALIDATE_ERR);
}

static void test_org_name_null (void)
{
  char err[256];
  ASSERT_EQ (validate_org_name (NULL, err, sizeof (err)), VALIDATE_ERR);
}

static void test_org_name_exactly_100 (void)
{
  char err[256];
  char name[101];
  memset (name, 'a', 100);
  name[100] = '\0';
  ASSERT_EQ (validate_org_name (name, err, sizeof (err)), VALIDATE_OK);
}

int main (int argc, char* argv[])
{
  test_parse_args (argc, argv);
  printf ("Running validation tests:\n");

  RUN_TEST (test_valid_repo_name_simple);
  RUN_TEST (test_valid_repo_name_with_dash_dot);
  RUN_TEST (test_valid_repo_name_underscore);
  RUN_TEST (test_repo_name_with_spaces);
  RUN_TEST (test_repo_name_with_special_chars);
  RUN_TEST (test_repo_name_too_long);
  RUN_TEST (test_repo_name_empty);
  RUN_TEST (test_repo_name_null);
  RUN_TEST (test_repo_name_exactly_100);
  RUN_TEST (test_description_at_limit);
  RUN_TEST (test_description_over_limit);
  RUN_TEST (test_description_null_ok);
  RUN_TEST (test_website_at_limit);
  RUN_TEST (test_website_over_limit);
  RUN_TEST (test_website_null_ok);
  RUN_TEST (test_parse_owner_repo_with_slash);
  RUN_TEST (test_parse_owner_repo_no_slash);
  RUN_TEST (test_parse_owner_repo_with_dots);
  RUN_TEST (test_parse_owner_repo_empty_repo);
  RUN_TEST (test_parse_owner_repo_empty_owner);
  RUN_TEST (test_parse_owner_repo_null);
  RUN_TEST (test_valid_merge_styles);
  RUN_TEST (test_invalid_merge_style);
  RUN_TEST (test_null_merge_style);

  RUN_TEST (test_validate_visibility_public);
  RUN_TEST (test_validate_visibility_limited);
  RUN_TEST (test_validate_visibility_private);
  RUN_TEST (test_validate_visibility_null);
  RUN_TEST (test_validate_visibility_empty);
  RUN_TEST (test_validate_visibility_invalid);

  RUN_TEST (test_valid_org_name_simple);
  RUN_TEST (test_valid_org_name_with_dash_dot);
  RUN_TEST (test_valid_org_name_underscore);
  RUN_TEST (test_org_name_with_spaces);
  RUN_TEST (test_org_name_with_special_chars);
  RUN_TEST (test_org_name_too_long);
  RUN_TEST (test_org_name_empty);
  RUN_TEST (test_org_name_null);
  RUN_TEST (test_org_name_exactly_100);

  TEST_SUMMARY ();
}
