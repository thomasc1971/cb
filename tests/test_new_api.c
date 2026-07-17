#include "config.h"
#include "cb_api.h"
#include "mock_server.h"
#include "test_helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MockServer server;

static void setup_server (MockResponse* responses, size_t count)
{
  memset (&server, 0, sizeof (server));
  if (mock_server_start (&server, responses, count) != 0) {
    fprintf (stderr, "Failed to start mock server\n");
    exit (1);
  }
}

static void teardown_server (void)
{
  mock_server_stop (&server);
}

static void make_client (ApiClient* a)
{
  char base_url[256];
  snprintf (base_url, sizeof (base_url), "http://127.0.0.1:%d/api/v1", server.port);
  api_client_init (a, base_url, "test-token");
}

static const char* RELEASE_JSON =
    "{\"id\":1,\"tag_name\":\"v1.0.0\",\"name\":\"First Release\","
    "\"body\":\"Initial release\",\"target_commitish\":\"main\","
    "\"draft\":false,\"prerelease\":false,\"hide_archive_links\":false,"
    "\"html_url\":\"https://codeberg.org/owner/repo/releases/tag/v1.0.0\","
    "\"tarball_url\":\"https://codeberg.org/owner/repo/archive/v1.0.0.tar.gz\","
    "\"zipball_url\":\"https://codeberg.org/owner/repo/archive/v1.0.0.zip\","
    "\"upload_url\":\"https://codeberg.org/owner/repo/releases/1/assets\","
    "\"url\":\"https://codeberg.org/api/v1/repos/owner/repo/releases/1\","
    "\"created_at\":\"2026-01-01T00:00:00Z\","
    "\"published_at\":\"2026-01-01T00:00:00Z\","
    "\"author\":{\"login\":\"thomasc\"},\"assets\":[]}";

static const char* ATTACHMENT_JSON =
    "{\"id\":10,\"name\":\"binary.tar.gz\","
    "\"browser_download_url\":\"https://codeberg.org/owner/repo/releases/download/v1.0.0/binary.tar.gz\","
    "\"uuid\":\"abc-123\",\"created_at\":\"2026-01-01T00:00:00Z\","
    "\"type\":\"addon\",\"size\":1024,\"download_count\":5}";

static void set_body (MockResponse* r, const char* s)
{
  size_t len = strlen (s);
  if (len >= sizeof (r->body))
    len = sizeof (r->body) - 1;
  memcpy (r->body, s, len);
  r->body[len] = '\0';
}

/* ===== Release list ===== */

static void test_release_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", RELEASE_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/releases",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Release* releases;
  size_t count;
  int rc = api_release_list (&a, "thomasc", "myproj", 0, 0, NULL, 0,
                             &releases, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (releases[0].tag_name, "v1.0.0");
  ASSERT_STR_EQ (releases[0].name, "First Release");
  ASSERT_STR_EQ (releases[0].body, "Initial release");
  ASSERT_STR_EQ (releases[0].target_commitish, "main");
  ASSERT_FALSE (releases[0].draft);
  ASSERT_FALSE (releases[0].prerelease);
  ASSERT_EQ (releases[0].id, 1);

  release_array_free (releases, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_release_list_empty (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/releases",
    .status = 200,
    .body = "[]"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Release* releases;
  size_t count;
  int rc = api_release_list (&a, "thomasc", "myproj", 0, 0, NULL, 0,
                             &releases, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 0);

  release_array_free (releases, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_release_list_404 (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/missing/releases",
    .status = 404,
    .body = "{\"message\":\"repository not found\"}"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Release* releases;
  size_t count;
  int rc = api_release_list (&a, "thomasc", "missing", 0, 0, NULL, 0,
                             &releases, &count);
  ASSERT_EQ (rc, API_ERR_NOT_FOUND);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Release create ===== */

static void test_release_create_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/repos/thomasc/myproj/releases",
    .status = 201
  };
  set_body (&resp, RELEASE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  CreateReleaseOpts opts = {
    .tag_name = "v1.0.0",
    .name = "First Release",
    .body = "Initial release",
    .target_commitish = "main",
    .draft_set = 0,
    .prerelease_set = 0,
    .hide_archive_links_set = 0
  };
  Release r;
  int rc = api_release_create (&a, "thomasc", "myproj", &opts, &r);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (r.tag_name, "v1.0.0");
  ASSERT_STR_EQ (r.name, "First Release");

  release_free (&r);
  api_client_free (&a);
  teardown_server ();
}

static void test_release_create_missing_tag (void)
{
  ApiClient a;
  make_client (&a);
  CreateReleaseOpts opts = { 0 };
  Release r;
  int rc = api_release_create (&a, "thomasc", "myproj", &opts, &r);
  ASSERT_EQ (rc, API_ERR_VALIDATION);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Release get ===== */

static void test_release_get_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/releases/1",
    .status = 200
  };
  set_body (&resp, RELEASE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Release r;
  int rc = api_release_get (&a, "thomasc", "myproj", 1, &r);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (r.tag_name, "v1.0.0");

  release_free (&r);
  api_client_free (&a);
  teardown_server ();
}

static void test_release_get_latest (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/releases/latest",
    .status = 200
  };
  set_body (&resp, RELEASE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Release r;
  int rc = api_release_get_latest (&a, "thomasc", "myproj", &r);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (r.tag_name, "v1.0.0");

  release_free (&r);
  api_client_free (&a);
  teardown_server ();
}

static void test_release_get_by_tag (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/releases/tags/v1.0.0",
    .status = 200
  };
  set_body (&resp, RELEASE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Release r;
  int rc = api_release_get_by_tag (&a, "thomasc", "myproj", "v1.0.0", &r);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (r.tag_name, "v1.0.0");

  release_free (&r);
  api_client_free (&a);
  teardown_server ();
}

/* ===== Release edit ===== */

static void test_release_edit_success (void)
{
  MockResponse resp = {
    .method = "PATCH",
    .path = "/api/v1/repos/thomasc/myproj/releases/1",
    .status = 200
  };
  set_body (&resp, RELEASE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  EditReleaseOpts opts = {
    .body_set = 1,
    .body = "Updated body",
    .draft_set = 1,
    .draft_val = 1
  };
  Release r;
  int rc = api_release_edit (&a, "thomasc", "myproj", 1, &opts, &r);
  ASSERT_EQ (rc, API_OK);

  release_free (&r);
  api_client_free (&a);
  teardown_server ();
}

/* ===== Release delete ===== */

static void test_release_delete_success (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/repos/thomasc/myproj/releases/1",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_release_delete (&a, "thomasc", "myproj", 1);
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

static void test_release_delete_by_tag (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/repos/thomasc/myproj/releases/tags/v1.0.0",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_release_delete_by_tag (&a, "thomasc", "myproj", "v1.0.0");
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Release assets ===== */

static void test_release_asset_list (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", ATTACHMENT_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/releases/1/assets",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Attachment* assets;
  size_t count;
  int rc = api_release_asset_list (&a, "thomasc", "myproj", 1, &assets, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (assets[0].name, "binary.tar.gz");
  ASSERT_EQ (assets[0].size, 1024);
  ASSERT_EQ (assets[0].download_count, 5);

  attachment_array_free (assets, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_release_asset_get (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/releases/1/assets/10",
    .status = 200
  };
  set_body (&resp, ATTACHMENT_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Attachment asset;
  int rc = api_release_asset_get (&a, "thomasc", "myproj", 1, 10, &asset);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (asset.name, "binary.tar.gz");

  attachment_free (&asset);
  api_client_free (&a);
  teardown_server ();
}

static void test_release_asset_delete (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/repos/thomasc/myproj/releases/1/assets/10",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_release_asset_delete (&a, "thomasc", "myproj", 1, 10);
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Tags ===== */

static const char* TAG_JSON =
    "{\"name\":\"v1.0.0\",\"id\":\"abc123\","
    "\"message\":\"Release v1.0.0\","
    "\"tarball_url\":\"https://codeberg.org/owner/repo/archive/v1.0.0.tar.gz\","
    "\"zipball_url\":\"https://codeberg.org/owner/repo/archive/v1.0.0.zip\","
    "\"commit\":{\"sha\":\"def456\",\"url\":\"\"}}";

static void test_tag_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", TAG_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/tags",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Tag* tags;
  size_t count;
  int rc = api_tag_list (&a, "thomasc", "myproj", 0, &tags, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (tags[0].name, "v1.0.0");
  ASSERT_STR_EQ (tags[0].id, "abc123");

  tag_array_free (tags, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_tag_create_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/repos/thomasc/myproj/tags",
    .status = 201
  };
  set_body (&resp, TAG_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  CreateTagOpts opts = {
    .tag_name = "v1.0.0",
    .message = "Release v1.0.0",
    .target = "main"
  };
  Tag t;
  int rc = api_tag_create (&a, "thomasc", "myproj", &opts, &t);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (t.name, "v1.0.0");

  tag_free (&t);
  api_client_free (&a);
  teardown_server ();
}

static void test_tag_get_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/tags/v1.0.0",
    .status = 200
  };
  set_body (&resp, TAG_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Tag t;
  int rc = api_tag_get (&a, "thomasc", "myproj", "v1.0.0", &t);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (t.name, "v1.0.0");

  tag_free (&t);
  api_client_free (&a);
  teardown_server ();
}

static void test_tag_delete_success (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/repos/thomasc/myproj/tags/v1.0.0",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_tag_delete (&a, "thomasc", "myproj", "v1.0.0");
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Branches ===== */

static const char* BRANCH_JSON =
    "{\"name\":\"main\",\"protected\":false,"
    "\"effective_branch_protection_name\":\"\","
    "\"user_can_merge\":true,\"user_can_push\":true,"
    "\"commit\":{\"id\":\"abc123\",\"commit\":{\"message\":\"Initial commit\"}}}";

static void test_branch_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", BRANCH_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/branches",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Branch* branches;
  size_t count;
  int rc = api_branch_list (&a, "thomasc", "myproj", 0, &branches, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (branches[0].name, "main");
  ASSERT_FALSE (branches[0].protected);
  ASSERT_STR_EQ (branches[0].commit_sha, "abc123");

  branch_array_free (branches, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_branch_create_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/repos/thomasc/myproj/branches",
    .status = 201
  };
  set_body (&resp, BRANCH_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  CreateBranchOpts opts = {
    .new_branch_name = "feature",
    .old_ref_name = "main"
  };
  Branch b;
  int rc = api_branch_create (&a, "thomasc", "myproj", &opts, &b);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (b.name, "main");

  branch_free (&b);
  api_client_free (&a);
  teardown_server ();
}

static void test_branch_get_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/branches/main",
    .status = 200
  };
  set_body (&resp, BRANCH_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Branch b;
  int rc = api_branch_get (&a, "thomasc", "myproj", "main", &b);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (b.name, "main");

  branch_free (&b);
  api_client_free (&a);
  teardown_server ();
}

static void test_branch_delete_success (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/repos/thomasc/myproj/branches/old-branch",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_branch_delete (&a, "thomasc", "myproj", "old-branch");
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Issues ===== */

static const char* ISSUE_JSON =
    "{\"id\":42,\"number\":1,\"title\":\"Bug report\","
    "\"body\":\"Something is broken\",\"state\":\"open\","
    "\"html_url\":\"https://codeberg.org/owner/repo/issues/1\","
    "\"created_at\":\"2026-01-01T00:00:00Z\","
    "\"updated_at\":\"2026-01-01T00:00:00Z\","
    "\"closed_at\":null,\"due_date\":null,"
    "\"is_locked\":false,\"comments\":0,\"pin_order\":0}";

static void test_issue_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", ISSUE_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/issues",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Issue* issues;
  size_t count;
  int rc = api_issue_list (&a, "thomasc", "myproj", NULL, NULL, NULL, 0,
                           &issues, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (issues[0].title, "Bug report");
  ASSERT_STR_EQ (issues[0].state, "open");
  ASSERT_EQ (issues[0].number, 1);

  issue_array_free (issues, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_issue_create_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/repos/thomasc/myproj/issues",
    .status = 201
  };
  set_body (&resp, ISSUE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  CreateIssueOpts opts = {
    .title = "Bug report",
    .body = "Something is broken"
  };
  Issue i;
  int rc = api_issue_create (&a, "thomasc", "myproj", &opts, &i);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (i.title, "Bug report");

  issue_free (&i);
  api_client_free (&a);
  teardown_server ();
}

static void test_issue_get_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/issues/1",
    .status = 200
  };
  set_body (&resp, ISSUE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Issue i;
  int rc = api_issue_get (&a, "thomasc", "myproj", 1, &i);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (i.title, "Bug report");

  issue_free (&i);
  api_client_free (&a);
  teardown_server ();
}

static void test_issue_delete_success (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/repos/thomasc/myproj/issues/1",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_issue_delete (&a, "thomasc", "myproj", 1);
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Labels ===== */

static const char* LABEL_JSON =
    "{\"id\":5,\"name\":\"bug\",\"color\":\"ff0000\","
    "\"description\":\"Bug report\",\"exclusive\":false,\"is_archived\":false}";

static void test_label_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", LABEL_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/labels",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Label* labels;
  size_t count;
  int rc = api_label_list (&a, "thomasc", "myproj", &labels, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (labels[0].name, "bug");
  ASSERT_STR_EQ (labels[0].color, "ff0000");

  label_array_free (labels, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_label_get_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/labels/5",
    .status = 200
  };
  set_body (&resp, LABEL_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Label l;
  int rc = api_label_get (&a, "thomasc", "myproj", 5, &l);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ ((long long)l.id, 5LL);
  ASSERT_STR_EQ (l.name, "bug");
  ASSERT_STR_EQ (l.color, "ff0000");
  ASSERT_STR_EQ (l.description, "Bug report");

  label_free (&l);
  api_client_free (&a);
  teardown_server ();
}

static void test_label_create_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/repos/thomasc/myproj/labels",
    .status = 201
  };
  set_body (&resp, LABEL_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  CreateLabelOpts opts = {
    .name = "bug",
    .color = "ff0000",
    .description = "Bug report"
  };
  Label l;
  int rc = api_label_create (&a, "thomasc", "myproj", &opts, &l);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (l.name, "bug");

  label_free (&l);
  api_client_free (&a);
  teardown_server ();
}

static void test_label_delete_success (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/repos/thomasc/myproj/labels/5",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_label_delete (&a, "thomasc", "myproj", 5);
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Milestones ===== */

static const char* MILESTONE_JSON =
    "{\"id\":3,\"title\":\"v1.0\",\"description\":\"First release\","
    "\"state\":\"open\",\"due_on\":\"2026-12-31T00:00:00Z\","
    "\"created_at\":\"2026-01-01T00:00:00Z\","
    "\"updated_at\":\"2026-01-01T00:00:00Z\","
    "\"open_issues\":5,\"closed_issues\":2}";

static void test_milestone_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", MILESTONE_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/milestones",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Milestone* ms;
  size_t count;
  int rc = api_milestone_list (&a, "thomasc", "myproj", NULL, &ms, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (ms[0].title, "v1.0");
  ASSERT_EQ (ms[0].open_issues, 5);

  milestone_array_free (ms, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_milestone_get_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/milestones/3",
    .status = 200
  };
  set_body (&resp, MILESTONE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Milestone m;
  int rc = api_milestone_get (&a, "thomasc", "myproj", 3, &m);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ ((long long)m.id, 3LL);
  ASSERT_STR_EQ (m.title, "v1.0");
  ASSERT_STR_EQ (m.state, "open");
  ASSERT_EQ (m.open_issues, 5);
  ASSERT_EQ (m.closed_issues, 2);

  milestone_free (&m);
  api_client_free (&a);
  teardown_server ();
}

static void test_milestone_create_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/repos/thomasc/myproj/milestones",
    .status = 201
  };
  set_body (&resp, MILESTONE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  CreateMilestoneOpts opts = {
    .title = "v1.0",
    .description = "First release"
  };
  Milestone m;
  int rc = api_milestone_create (&a, "thomasc", "myproj", &opts, &m);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (m.title, "v1.0");

  milestone_free (&m);
  api_client_free (&a);
  teardown_server ();
}

static void test_milestone_delete_success (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/repos/thomasc/myproj/milestones/3",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_milestone_delete (&a, "thomasc", "myproj", 3);
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Pull Requests ===== */

static const char* PR_JSON =
    "{\"id\":99,\"number\":7,\"title\":\"Fix bug\","
    "\"body\":\"Fixes the thing\",\"state\":\"open\","
    "\"draft\":false,\"merged\":false,\"mergeable\":true,"
    "\"merged_at\":null,\"closed_at\":null,"
    "\"created_at\":\"2026-01-01T00:00:00Z\","
    "\"updated_at\":\"2026-01-01T00:00:00Z\","
    "\"html_url\":\"https://codeberg.org/owner/repo/pulls/7\","
    "\"diff_url\":\"https://codeberg.org/owner/repo/pulls/7.diff\","
    "\"patch_url\":\"https://codeberg.org/owner/repo/pulls/7.patch\","
    "\"merge_commit_sha\":\"\","
    "\"base\":{\"ref\":\"main\"},\"head\":{\"ref\":\"fix-bug\"}}";

static void test_pr_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", PR_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/pulls",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  PullRequest* prs;
  size_t count;
  int rc = api_pr_list (&a, "thomasc", "myproj", NULL, 0, &prs, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (prs[0].title, "Fix bug");
  ASSERT_STR_EQ (prs[0].base_ref, "main");
  ASSERT_STR_EQ (prs[0].head_ref, "fix-bug");

  pullrequest_array_free (prs, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_pr_create_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/repos/thomasc/myproj/pulls",
    .status = 201
  };
  set_body (&resp, PR_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  CreatePullRequestOpts opts = {
    .title = "Fix bug",
    .head = "fix-bug",
    .base = "main"
  };
  PullRequest p;
  int rc = api_pr_create (&a, "thomasc", "myproj", &opts, &p);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (p.title, "Fix bug");

  pullrequest_free (&p);
  api_client_free (&a);
  teardown_server ();
}

static void test_pr_get_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/pulls/7",
    .status = 200
  };
  set_body (&resp, PR_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  PullRequest p;
  int rc = api_pr_get (&a, "thomasc", "myproj", 7, &p);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (p.title, "Fix bug");

  pullrequest_free (&p);
  api_client_free (&a);
  teardown_server ();
}

/* ===== Commits ===== */

static const char* COMMIT_JSON =
    "{\"sha\":\"abc123def456\",\"created\":\"2026-01-01T00:00:00Z\","
    "\"html_url\":\"https://codeberg.org/owner/repo/commit/abc123\","
    "\"commit\":{\"message\":\"Initial commit\","
    "\"author\":{\"name\":\"Alice\",\"email\":\"alice@example.com\"},"
    "\"committer\":{\"name\":\"Alice\",\"email\":\"alice@example.com\"}},"
    "\"stats\":{\"additions\":10,\"deletions\":2,\"total\":12}}";

static void test_commit_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", COMMIT_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/commits",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Commit* commits;
  size_t count;
  int rc = api_commit_list (&a, "thomasc", "myproj", NULL, NULL, 0,
                            &commits, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (commits[0].sha, "abc123def456");
  ASSERT_STR_EQ (commits[0].message, "Initial commit");
  ASSERT_EQ (commits[0].additions, 10);

  commit_array_free (commits, count);
  api_client_free (&a);
  teardown_server ();
}

/* ===== Content ===== */

static const char* CONTENT_FILE_JSON =
    "{\"type\":\"file\",\"name\":\"README.md\",\"path\":\"README.md\","
    "\"sha\":\"abc123\",\"size\":42,\"encoding\":\"base64\","
    "\"content\":\"SGVsbG8gV29ybGQ=\","
    "\"download_url\":\"https://codeberg.org/owner/repo/raw/README.md\","
    "\"html_url\":\"https://codeberg.org/owner/repo/src/README.md\","
    "\"git_url\":\"https://codeberg.org/api/v1/repos/owner/repo/git/blobs/abc123\","
    "\"last_commit_sha\":\"def456\"}";

static const char* CONTENT_DIR_JSON =
    "[{\"type\":\"dir\",\"name\":\"src\",\"path\":\"src\","
    "\"sha\":\"abc123\",\"size\":0,"
    "\"download_url\":null,\"html_url\":null,\"git_url\":null,"
    "\"last_commit_sha\":\"def456\"}]";

static void test_content_list_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/contents",
    .status = 200
  };
  set_body (&resp, CONTENT_DIR_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  ContentEntry* entries;
  size_t count;
  int rc = api_content_list (&a, "thomasc", "myproj", NULL, &entries, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (entries[0].name, "src");
  ASSERT_STR_EQ (entries[0].type, "dir");

  content_entry_array_free (entries, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_content_get_file (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/contents/README.md",
    .status = 200
  };
  set_body (&resp, CONTENT_FILE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  ContentEntry e;
  int rc = api_content_get (&a, "thomasc", "myproj", "README.md", NULL, &e);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (e.name, "README.md");
  ASSERT_STR_EQ (e.type, "file");

  content_entry_free (&e);
  api_client_free (&a);
  teardown_server ();
}

/* ===== Deploy Keys ===== */

static const char* KEY_JSON =
    "{\"id\":7,\"title\":\"CI key\","
    "\"key\":\"ssh-rsa AAAA...\",\"fingerprint\":\"SHA256:abc\","
    "\"read_only\":true,\"created_at\":\"2026-01-01T00:00:00Z\"}";

static void test_key_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", KEY_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/keys",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  DeployKey* keys;
  size_t count;
  int rc = api_key_list (&a, "thomasc", "myproj", &keys, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (keys[0].title, "CI key");
  ASSERT_TRUE (keys[0].read_only);

  deploykey_array_free (keys, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_key_get_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/keys/7",
    .status = 200
  };
  set_body (&resp, KEY_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  DeployKey k;
  int rc = api_key_get (&a, "thomasc", "myproj", 7, &k);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ ((long long)k.id, 7LL);
  ASSERT_STR_EQ (k.title, "CI key");
  ASSERT_STR_EQ (k.fingerprint, "SHA256:abc");
  ASSERT_TRUE (k.read_only);

  deploykey_free (&k);
  api_client_free (&a);
  teardown_server ();
}

static void test_key_add_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/repos/thomasc/myproj/keys",
    .status = 201
  };
  set_body (&resp, KEY_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  CreateKeyOpts opts = {
    .title = "CI key",
    .key = "ssh-rsa AAAA...",
    .read_only = 1
  };
  DeployKey k;
  int rc = api_key_add (&a, "thomasc", "myproj", &opts, &k);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (k.title, "CI key");

  deploykey_free (&k);
  api_client_free (&a);
  teardown_server ();
}

/* ===== Collaborators ===== */

static const char* USER_JSON =
    "{\"id\":1,\"login\":\"alice\",\"full_name\":\"Alice\","
    "\"html_url\":\"https://codeberg.org/alice\"}";

static void test_collaborator_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", USER_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/collaborators",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  User* users;
  size_t count;
  int rc = api_collaborator_list (&a, "thomasc", "myproj", &users, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (users[0].login, "alice");

  user_array_free (users, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_collaborator_add (void)
{
  MockResponse resp = {
    .method = "PUT",
    .path = "/api/v1/repos/thomasc/myproj/collaborators/alice",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_collaborator_add (&a, "thomasc", "myproj", "alice", "write");
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Hooks ===== */

static const char* HOOK_JSON =
    "{\"id\":11,\"type\":\"gitea\",\"active\":true,"
    "\"config\":{\"url\":\"https://example.com/hook\",\"content_type\":\"json\"},"
    "\"branch_filter\":\"\",\"authorization_header\":\"\"}";

static void test_hook_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", HOOK_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/hooks",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Hook* hooks;
  size_t count;
  int rc = api_hook_list (&a, "thomasc", "myproj", &hooks, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (hooks[0].type, "gitea");
  ASSERT_TRUE (hooks[0].active);
  ASSERT_STR_EQ (hooks[0].url, "https://example.com/hook");

  hook_array_free (hooks, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_hook_create_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/repos/thomasc/myproj/hooks",
    .status = 201
  };
  set_body (&resp, HOOK_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  const char* events[] = { "push", NULL };
  CreateHookOpts opts = {
    .type = "gitea",
    .url = "https://example.com/hook",
    .content_type = "json",
    .events = events,
    .active = 1
  };
  Hook h;
  int rc = api_hook_create (&a, "thomasc", "myproj", &opts, &h);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (h.type, "gitea");

  hook_free (&h);
  api_client_free (&a);
  teardown_server ();
}

static void test_hook_delete_success (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/repos/thomasc/myproj/hooks/11",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_hook_delete (&a, "thomasc", "myproj", 11);
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Wiki ===== */

static const char* WIKI_JSON =
    "{\"title\":\"Home\",\"content_base64\":\"SGVsbG8=\","
    "\"html_url\":\"https://codeberg.org/owner/repo/wiki/Home\","
    "\"sub_url\":\"/owner/repo/wiki/Home\","
    "\"commit_count\":3,"
    "\"last_commit\":{\"sha\":\"abc123\"}}";

static void test_wiki_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", WIKI_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/wiki/pages",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  WikiPage* pages;
  size_t count;
  int rc = api_wiki_list (&a, "thomasc", "myproj", &pages, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (pages[0].title, "Home");

  wikipage_array_free (pages, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_wiki_get_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/wiki/page/Home",
    .status = 200
  };
  set_body (&resp, WIKI_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  WikiPage w;
  int rc = api_wiki_get (&a, "thomasc", "myproj", "Home", &w);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (w.title, "Home");

  wikipage_free (&w);
  api_client_free (&a);
  teardown_server ();
}

static void test_wiki_delete_success (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/repos/thomasc/myproj/wiki/page/OldPage",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_wiki_delete (&a, "thomasc", "myproj", "OldPage");
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Repo misc ===== */

static void test_repo_mirror_sync (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/repos/thomasc/myproj/mirror-sync",
    .status = 200
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_repo_mirror_sync (&a, "thomasc", "myproj");
  ASSERT_EQ (rc, API_OK);

  api_client_free (&a);
  teardown_server ();
}

static void test_repo_languages (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/repos/thomasc/myproj/languages",
    .status = 200,
    .body = "{\"C\":12345,\"Shell\":100}"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  char** langs;
  int64_t* bytes;
  size_t count;
  int rc = api_repo_languages (&a, "thomasc", "myproj", &langs, &bytes, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 2);

  for (size_t i = 0; i < count; i++)
    free (langs[i]);
  free (langs);
  free (bytes);
  api_client_free (&a);
  teardown_server ();
}

/* ===== User get current ===== */

static void test_user_get_current (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/user",
    .status = 200,
  };
  set_body (&resp,
            "{\"id\":1,\"login\":\"thomasc\",\"full_name\":\"Thomas C\","
            "\"email\":\"tc@example.com\",\"active\":true}");
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  User user;
  memset (&user, 0, sizeof (user));
  int rc = api_user_get_current (&a, &user);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (user.id, 1);
  ASSERT_STR_EQ (user.login, "thomasc");
  ASSERT_STR_EQ (user.full_name, "Thomas C");
  ASSERT_STR_EQ (user.email, "tc@example.com");

  user_free (&user);
  api_client_free (&a);
  teardown_server ();
}

/* ===== SSH key (user public key) tests ===== */

static const char* SSHKEY_LIST_JSON =
    "[{\"id\":1,\"title\":\"Laptop\",\"key\":\"ssh-ed25519 AAAAC3... laptop\","
    "\"fingerprint\":\"SHA256:abc123\",\"key_type\":\"ssh-ed25519\","
    "\"read_only\":false,\"url\":\"https://codeberg.org/api/v1/user/keys/1\","
    "\"created_at\":\"2026-07-10T12:00:00Z\"},"
    "{\"id\":2,\"title\":\"CI Runner\",\"key\":\"ssh-ed25519 AAAAC3... runner\","
    "\"fingerprint\":\"SHA256:def456\",\"key_type\":\"ssh-ed25519\","
    "\"read_only\":true,\"url\":\"https://codeberg.org/api/v1/user/keys/2\","
    "\"created_at\":\"2026-07-11T09:00:00Z\"}]";

static const char* SSHKEY_SINGLE_JSON =
    "{\"id\":1,\"title\":\"Laptop\",\"key\":\"ssh-ed25519 AAAAC3... laptop\","
    "\"fingerprint\":\"SHA256:abc123\",\"key_type\":\"ssh-ed25519\","
    "\"read_only\":false,\"url\":\"https://codeberg.org/api/v1/user/keys/1\","
    "\"created_at\":\"2026-07-10T12:00:00Z\"}";

static void test_user_key_list_success (void)
{
  MockResponse resp = {
    .method = "GET", .path = "/api/v1/user/keys", .status = 200
  };
  set_body (&resp, SSHKEY_LIST_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  PublicKey* keys;
  size_t count;
  int rc = api_user_key_list (&a, &keys, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ ((int)count, 2);
  ASSERT_EQ (keys[0].id, 1);
  ASSERT_STR_EQ (keys[0].title, "Laptop");
  ASSERT_STR_EQ (keys[0].fingerprint, "SHA256:abc123");
  ASSERT_FALSE (keys[0].read_only);
  ASSERT_EQ (keys[1].id, 2);
  ASSERT_STR_EQ (keys[1].title, "CI Runner");
  ASSERT_TRUE (keys[1].read_only);
  ASSERT_TRUE (mock_server_all_matched (&server));

  public_key_array_free (keys, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_user_key_list_empty (void)
{
  MockResponse resp = {
    .method = "GET", .path = "/api/v1/user/keys", .status = 200, .body = "[]"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  PublicKey* keys;
  size_t count;
  int rc = api_user_key_list (&a, &keys, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ ((int)count, 0);
  ASSERT_NULL (keys);
  ASSERT_TRUE (mock_server_all_matched (&server));

  public_key_array_free (keys, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_user_key_add_success (void)
{
  MockResponse resp = {
    .method = "POST", .path = "/api/v1/user/keys", .status = 201
  };
  set_body (&resp, SSHKEY_SINGLE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  PublicKey key;
  int rc = api_user_key_add (&a, "Laptop", "ssh-ed25519 AAAAC3... laptop", 0, &key);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (key.id, 1);
  ASSERT_STR_EQ (key.title, "Laptop");
  ASSERT_STR_EQ (key.fingerprint, "SHA256:abc123");
  ASSERT_FALSE (key.read_only);
  ASSERT_TRUE (mock_server_all_matched (&server));

  public_key_free (&key);
  api_client_free (&a);
  teardown_server ();
}

static void test_user_key_get_success (void)
{
  MockResponse resp = {
    .method = "GET", .path = "/api/v1/user/keys/1", .status = 200
  };
  set_body (&resp, SSHKEY_SINGLE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  PublicKey key;
  int rc = api_user_key_get (&a, 1, &key);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (key.id, 1);
  ASSERT_STR_EQ (key.title, "Laptop");
  ASSERT_STR_EQ (key.key_type, "ssh-ed25519");
  ASSERT_TRUE (mock_server_all_matched (&server));

  public_key_free (&key);
  api_client_free (&a);
  teardown_server ();
}

static void test_user_key_delete_success (void)
{
  MockResponse resp = {
    .method = "DELETE", .path = "/api/v1/user/keys/2", .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_user_key_delete (&a, 2);
  ASSERT_EQ (rc, API_OK);
  ASSERT_TRUE (mock_server_all_matched (&server));

  api_client_free (&a);
  teardown_server ();
}

static void test_user_key_list_401 (void)
{
  MockResponse resp = {
    .method = "GET", .path = "/api/v1/user/keys", .status = 401, .body = "{\"message\":\"invalid token\"}"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  PublicKey* keys;
  size_t count;
  int rc = api_user_key_list (&a, &keys, &count);
  ASSERT_EQ (rc, API_ERR_AUTH);
  ASSERT_TRUE (strlen (a.last_error) > 0);

  api_client_free (&a);
  teardown_server ();
}

static void test_user_key_get_404 (void)
{
  MockResponse resp = {
    .method = "GET", .path = "/api/v1/user/keys/999", .status = 404, .body = "{\"message\":\"key not found\"}"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  PublicKey key;
  int rc = api_user_key_get (&a, 999, &key);
  ASSERT_EQ (rc, API_ERR_NOT_FOUND);

  api_client_free (&a);
  teardown_server ();
}

/* ===== Package tests ===== */

static const char* PACKAGE_JSON =
    "{\"id\":1,\"name\":\"mylib\",\"type\":\"generic\","
    "\"version\":\"1.0.0\","
    "\"html_url\":\"https://codeberg.org/thomasc/-/packages/generic/mylib/1.0.0\","
    "\"created_at\":\"2024-01-01T00:00:00Z\","
    "\"creator\":{\"login\":\"thomasc\"},"
    "\"owner\":{\"login\":\"thomasc\"},"
    "\"repository\":{\"full_name\":\"thomasc/myrepo\"}}";

static const char* PACKAGE_FILE_JSON =
    "{\"id\":2,\"Size\":1024,\"name\":\"mylib-1.0.0.tar.gz\","
    "\"md5\":\"abc123\",\"sha1\":\"def456\","
    "\"sha256\":\"789abc\",\"sha512\":\"xyz\"}";

static void test_package_list_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", PACKAGE_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/packages/thomasc",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Package* pkgs;
  size_t count;
  int rc = api_package_list (&a, "thomasc", NULL, NULL, 0, &pkgs, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (pkgs[0].name, "mylib");
  ASSERT_STR_EQ (pkgs[0].type, "generic");
  ASSERT_STR_EQ (pkgs[0].version, "1.0.0");
  ASSERT_STR_EQ (pkgs[0].creator_login, "thomasc");
  ASSERT_STR_EQ (pkgs[0].owner_login, "thomasc");
  ASSERT_STR_EQ (pkgs[0].repo_full_name, "thomasc/myrepo");
  ASSERT_EQ (pkgs[0].id, 1);

  package_array_free (pkgs, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_package_list_empty (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/packages/thomasc",
    .status = 200,
    .body = "[]"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Package* pkgs;
  size_t count;
  int rc = api_package_list (&a, "thomasc", NULL, NULL, 0, &pkgs, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 0);

  api_client_free (&a);
  teardown_server ();
}

static void test_package_list_with_type_filter (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/packages/thomasc?type=generic",
    .status = 200,
    .body = "[]"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Package* pkgs;
  size_t count;
  int rc = api_package_list (&a, "thomasc", "generic", NULL, 0, &pkgs, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_TRUE (mock_server_all_matched (&server));

  api_client_free (&a);
  teardown_server ();
}

static void test_package_list_404 (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/packages/missing",
    .status = 404,
    .body = "{\"message\":\"user not found\"}"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Package* pkgs;
  size_t count;
  int rc = api_package_list (&a, "missing", NULL, NULL, 0, &pkgs, &count);
  ASSERT_EQ (rc, API_ERR_NOT_FOUND);

  api_client_free (&a);
  teardown_server ();
}

static void test_package_get_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/packages/thomasc/generic/mylib/1.0.0",
    .status = 200
  };
  set_body (&resp, PACKAGE_JSON);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Package pkg;
  int rc = api_package_get (&a, "thomasc", "generic", "mylib", "1.0.0", &pkg);
  ASSERT_EQ (rc, API_OK);
  ASSERT_STR_EQ (pkg.name, "mylib");
  ASSERT_STR_EQ (pkg.type, "generic");
  ASSERT_STR_EQ (pkg.version, "1.0.0");
  ASSERT_STR_EQ (pkg.creator_login, "thomasc");
  ASSERT_STR_EQ (pkg.owner_login, "thomasc");
  ASSERT_STR_EQ (pkg.repo_full_name, "thomasc/myrepo");

  package_free (&pkg);
  api_client_free (&a);
  teardown_server ();
}

static void test_package_get_404 (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/packages/thomasc/generic/notfound/1.0.0",
    .status = 404,
    .body = "{\"message\":\"package not found\"}"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  Package pkg;
  int rc = api_package_get (&a, "thomasc", "generic", "notfound", "1.0.0", &pkg);
  ASSERT_EQ (rc, API_ERR_NOT_FOUND);

  api_client_free (&a);
  teardown_server ();
}

static void test_package_delete_success (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/packages/thomasc/generic/mylib/1.0.0",
    .status = 204
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_package_delete (&a, "thomasc", "generic", "mylib", "1.0.0");
  ASSERT_EQ (rc, API_OK);
  ASSERT_TRUE (mock_server_all_matched (&server));

  api_client_free (&a);
  teardown_server ();
}

static void test_package_delete_404 (void)
{
  MockResponse resp = {
    .method = "DELETE",
    .path = "/api/v1/packages/thomasc/generic/notfound/1.0.0",
    .status = 404,
    .body = "{\"message\":\"package not found\"}"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_package_delete (&a, "thomasc", "generic", "notfound", "1.0.0");
  ASSERT_EQ (rc, API_ERR_NOT_FOUND);

  api_client_free (&a);
  teardown_server ();
}

static void test_package_files_success (void)
{
  char body[4096];
  snprintf (body, sizeof (body), "[%s]", PACKAGE_FILE_JSON);
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/packages/thomasc/generic/mylib/1.0.0/files",
    .status = 200
  };
  set_body (&resp, body);
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  PackageFile* files;
  size_t count;
  int rc = api_package_files (&a, "thomasc", "generic", "mylib", "1.0.0",
                              &files, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 1);
  ASSERT_STR_EQ (files[0].name, "mylib-1.0.0.tar.gz");
  ASSERT_EQ (files[0].size, 1024);
  ASSERT_STR_EQ (files[0].sha256, "789abc");

  package_file_array_free (files, count);
  api_client_free (&a);
  teardown_server ();
}

static void test_package_files_empty (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/v1/packages/thomasc/generic/mylib/1.0.0/files",
    .status = 200,
    .body = "[]"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  PackageFile* files;
  size_t count;
  int rc = api_package_files (&a, "thomasc", "generic", "mylib", "1.0.0",
                              &files, &count);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (count, 0);

  api_client_free (&a);
  teardown_server ();
}

static void test_package_link_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/packages/thomasc/generic/mylib/-/link/myrepo",
    .status = 201
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_package_link (&a, "thomasc", "generic", "mylib", "myrepo");
  ASSERT_EQ (rc, API_OK);
  ASSERT_TRUE (mock_server_all_matched (&server));

  api_client_free (&a);
  teardown_server ();
}

static void test_package_unlink_success (void)
{
  MockResponse resp = {
    .method = "POST",
    .path = "/api/v1/packages/thomasc/generic/mylib/-/unlink",
    .status = 201
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  int rc = api_package_unlink (&a, "thomasc", "generic", "mylib");
  ASSERT_EQ (rc, API_OK);
  ASSERT_TRUE (mock_server_all_matched (&server));

  api_client_free (&a);
  teardown_server ();
}

static void test_package_upload_success (void)
{
  MockResponse resp = {
    .method = "PUT",
    .path = "/api/packages/thomasc/generic/mylib/1.0.0/file.tar.gz",
    .status = 201
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  const char* data = "binary file content";
  int rc = api_package_upload (&a, "thomasc", "mylib", "1.0.0", "file.tar.gz",
                               data, strlen (data));
  ASSERT_EQ (rc, API_OK);
  ASSERT_TRUE (mock_server_all_matched (&server));

  api_client_free (&a);
  teardown_server ();
}

static void test_package_upload_conflict (void)
{
  MockResponse resp = {
    .method = "PUT",
    .path = "/api/packages/thomasc/generic/mylib/1.0.0/file.tar.gz",
    .status = 409,
    .body = "{\"message\":\"file already exists\"}"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  const char* data = "binary file content";
  int rc = api_package_upload (&a, "thomasc", "mylib", "1.0.0", "file.tar.gz",
                               data, strlen (data));
  ASSERT_EQ (rc, API_ERR_CONFLICT);

  api_client_free (&a);
  teardown_server ();
}

static void test_package_download_success (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/packages/thomasc/generic/mylib/1.0.0/file.tar.gz",
    .status = 200
  };
  set_body (&resp, "binary data!");
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  char* data;
  size_t len;
  int rc = api_package_download (&a, "thomasc", "mylib", "1.0.0", "file.tar.gz",
                                 &data, &len);
  ASSERT_EQ (rc, API_OK);
  ASSERT_EQ (len, 12);
  ASSERT_EQ (memcmp (data, "binary data!", 12), 0);

  free (data);
  api_client_free (&a);
  teardown_server ();
}

static void test_package_download_404 (void)
{
  MockResponse resp = {
    .method = "GET",
    .path = "/api/packages/thomasc/generic/notfound/1.0.0/file.tar.gz",
    .status = 404,
    .body = "{\"message\":\"package not found\"}"
  };
  setup_server (&resp, 1);

  ApiClient a;
  make_client (&a);
  char* data;
  size_t len;
  int rc = api_package_download (&a, "thomasc", "notfound", "1.0.0", "file.tar.gz",
                                 &data, &len);
  ASSERT_EQ (rc, API_ERR_NOT_FOUND);

  api_client_free (&a);
  teardown_server ();
}

int main (void)
{
  printf ("Running new API tests:\n");

  RUN_TEST (test_release_list_success);
  RUN_TEST (test_release_list_empty);
  RUN_TEST (test_release_list_404);
  RUN_TEST (test_release_create_success);
  RUN_TEST (test_release_create_missing_tag);
  RUN_TEST (test_release_get_success);
  RUN_TEST (test_release_get_latest);
  RUN_TEST (test_release_get_by_tag);
  RUN_TEST (test_release_edit_success);
  RUN_TEST (test_release_delete_success);
  RUN_TEST (test_release_delete_by_tag);
  RUN_TEST (test_release_asset_list);
  RUN_TEST (test_release_asset_get);
  RUN_TEST (test_release_asset_delete);

  RUN_TEST (test_tag_list_success);
  RUN_TEST (test_tag_create_success);
  RUN_TEST (test_tag_get_success);
  RUN_TEST (test_tag_delete_success);

  RUN_TEST (test_branch_list_success);
  RUN_TEST (test_branch_create_success);
  RUN_TEST (test_branch_get_success);
  RUN_TEST (test_branch_delete_success);

  RUN_TEST (test_issue_list_success);
  RUN_TEST (test_issue_create_success);
  RUN_TEST (test_issue_get_success);
  RUN_TEST (test_issue_delete_success);

  RUN_TEST (test_label_list_success);
  RUN_TEST (test_label_get_success);
  RUN_TEST (test_label_create_success);
  RUN_TEST (test_label_delete_success);

  RUN_TEST (test_milestone_list_success);
  RUN_TEST (test_milestone_get_success);
  RUN_TEST (test_milestone_create_success);
  RUN_TEST (test_milestone_delete_success);

  RUN_TEST (test_pr_list_success);
  RUN_TEST (test_pr_create_success);
  RUN_TEST (test_pr_get_success);

  RUN_TEST (test_commit_list_success);

  RUN_TEST (test_content_list_success);
  RUN_TEST (test_content_get_file);

  RUN_TEST (test_key_list_success);
  RUN_TEST (test_key_get_success);
  RUN_TEST (test_key_add_success);

  RUN_TEST (test_collaborator_list_success);
  RUN_TEST (test_collaborator_add);

  RUN_TEST (test_hook_list_success);
  RUN_TEST (test_hook_create_success);
  RUN_TEST (test_hook_delete_success);

  RUN_TEST (test_wiki_list_success);
  RUN_TEST (test_wiki_get_success);
  RUN_TEST (test_wiki_delete_success);

  RUN_TEST (test_repo_mirror_sync);
  RUN_TEST (test_repo_languages);

  RUN_TEST (test_user_get_current);

  RUN_TEST (test_user_key_list_success);
  RUN_TEST (test_user_key_list_empty);
  RUN_TEST (test_user_key_add_success);
  RUN_TEST (test_user_key_get_success);
  RUN_TEST (test_user_key_delete_success);
  RUN_TEST (test_user_key_list_401);
  RUN_TEST (test_user_key_get_404);

  RUN_TEST (test_package_list_success);
  RUN_TEST (test_package_list_empty);
  RUN_TEST (test_package_list_with_type_filter);
  RUN_TEST (test_package_list_404);
  RUN_TEST (test_package_get_success);
  RUN_TEST (test_package_get_404);
  RUN_TEST (test_package_delete_success);
  RUN_TEST (test_package_delete_404);
  RUN_TEST (test_package_files_success);
  RUN_TEST (test_package_files_empty);
  RUN_TEST (test_package_link_success);
  RUN_TEST (test_package_unlink_success);
  RUN_TEST (test_package_upload_success);
  RUN_TEST (test_package_upload_conflict);
  RUN_TEST (test_package_download_success);
  RUN_TEST (test_package_download_404);

  TEST_SUMMARY ();
}
