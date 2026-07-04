/* cb — Codeberg (Forgejo) Repository Management CLI
 * Copyright (C) 2026 Thomas Christensen
 *
 * This file is part of cb.
 *
 * cb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * cb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with cb.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "cb_version.h"
#include "cb_cli.h"
#include "cb_compat.h"
#include "cb_config.h"
#include "cb_json.h"
#include "cb_validate.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations */
static int require_owner_repo(const char *arg, char *owner, size_t owner_sz,
                              char *repo, size_t repo_sz, ApiClient *api);

/* ===== Flag parsing ===== */

typedef struct
{
    const char *name;  /* e.g. "--description" or "-d" */
    const char *alias; /* short alias, may be NULL */
    int takes_value;   /* 1 if flag takes a value, 0 if boolean */
} FlagDef;

static const FlagDef CREATE_FLAGS[] = {
    { "--private", NULL, 0 },
    { "--public", NULL, 0 },
    { "--description", "-d", 1 },
    { "--default-branch", "-b", 1 },
    { "--license", NULL, 1 },
    { "--gitignore", "-g", 1 },
    { "--auto-init", NULL, 0 },
    { "--template", NULL, 0 },
    { "--org", NULL, 1 },
    { "--object-format", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef EDIT_FLAGS[] = {
    { "--description", "-d", 1 },
    { "--website", "-w", 1 },
    { "--private", NULL, 0 },
    { "--public", NULL, 0 },
    { "--default-branch", "-b", 1 },
    { "--archived", NULL, 0 },
    { "--unarchived", NULL, 0 },
    { "--template", NULL, 0 },
    { "--no-template", NULL, 0 },
    { "--has-issues", NULL, 0 },
    { "--no-issues", NULL, 0 },
    { "--has-wiki", NULL, 0 },
    { "--no-wiki", NULL, 0 },
    { "--has-prs", NULL, 0 },
    { "--no-prs", NULL, 0 },
    { "--has-projects", NULL, 0 },
    { "--no-projects", NULL, 0 },
    { "--has-releases", NULL, 0 },
    { "--no-releases", NULL, 0 },
    { "--has-packages", NULL, 0 },
    { "--no-packages", NULL, 0 },
    { "--has-actions", NULL, 0 },
    { "--no-actions", NULL, 0 },
    { "--allow-merge", NULL, 0 },
    { "--no-merge", NULL, 0 },
    { "--allow-rebase", NULL, 0 },
    { "--no-rebase", NULL, 0 },
    { "--allow-squash", NULL, 0 },
    { "--no-squash", NULL, 0 },
    { "--allow-ff-only", NULL, 0 },
    { "--no-ff-only", NULL, 0 },
    { "--default-merge-style", NULL, 1 },
    { "--delete-branch-after-merge", NULL, 0 },
    { "--no-delete-branch-after-merge", NULL, 0 },
    { "--allow-maintainer-edit", NULL, 0 },
    { "--no-allow-maintainer-edit", NULL, 0 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef GLOBAL_FLAGS[] = {
    { "--json", NULL, 0 },
    { "--quiet", "-q", 0 },
    { "--base-url", NULL, 1 },
    { "--yes", NULL, 0 },
    { NULL, NULL, 0 }
};

static const FlagDef ORG_CREATE_FLAGS[] = {
    { "--description", "-d", 1 },
    { "--full-name", NULL, 1 },
    { "--email", NULL, 1 },
    { "--location", NULL, 1 },
    { "--website", NULL, 1 },
    { "--visibility", NULL, 1 },
    { "--repo-admin-change-team-access", NULL, 0 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

/* Help flag sentinel — checked before parse_flags in every handler. */
static int is_help_arg(const char *arg)
{
    return strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0;
}

/* Print all flags in a table as aligned "  --name, -alias  description" lines. */
static void print_flag_table(const FlagDef *table)
{
    for (int i = 0; table[i].name; i++) {
        if (strcmp(table[i].name, "--help") == 0)
            continue;
        if (table[i].alias)
            printf("  %s, %s\n", table[i].name, table[i].alias);
        else
            printf("  %s\n", table[i].name);
    }
}

/* Check if a string matches a flag definition (by name or alias). */
static int matches_flag(const char *arg, const FlagDef *def)
{
    if (strcmp(arg, def->name) == 0)
        return 1;
    if (def->alias && strcmp(arg, def->alias) == 0)
        return 1;
    return 0;
}

/* Find a flag definition in a table. Returns index or -1. */
static int find_flag(const char *arg, const FlagDef *table)
{
    for (int i = 0; table[i].name; i++) {
        if (matches_flag(arg, &table[i]))
            return i;
    }
    return -1;
}

/* Check if arg is a global flag. Returns 1 if yes. */
static int is_global_flag(const char *arg)
{
    return find_flag(arg, GLOBAL_FLAGS) >= 0;
}

/* ===== Help text ===== */

static void help_repo_create(void)
{
    printf("Usage: cb repo create <name> [flags]\n\n");
    printf("Create a new repository.\n\n");
    printf("Flags:\n");
    print_flag_table(CREATE_FLAGS);
    printf("  --help, -h              Show this help\n");
}

static void help_repo_delete(void)
{
    printf("Usage: cb repo delete <owner/repo> [--yes]\n\n");
    printf("Delete a repository. Requires --yes or interactive confirmation.\n\n");
    printf("Flags:\n");
    printf("  --yes                   Skip confirmation prompt\n");
    printf("  --help, -h              Show this help\n");
}

static void help_repo_rename(void)
{
    printf("Usage: cb repo rename <owner/repo> <new-name>\n\n");
    printf("Rename a repository.\n");
    printf("  --help, -h              Show this help\n");
}

static void help_repo_edit(void)
{
    printf("Usage: cb repo edit <owner/repo> [flags]\n\n");
    printf("Edit repository settings. Only provided flags are sent; unset\n");
    printf("fields are not modified.\n\n");
    printf("Flags:\n");
    print_flag_table(EDIT_FLAGS);
    printf("  --help, -h              Show this help\n");
}

static void help_repo_show(void)
{
    printf("Usage: cb repo show <owner/repo>\n\n");
    printf("Show repository details.\n\n");
    printf("Flags:\n");
    printf("  --json                  Output raw JSON\n");
    printf("  --help, -h              Show this help\n");
}

static void help_repo_list(void)
{
    printf("Usage: cb repo list [--user U | --org O]\n\n");
    printf("List repositories. With no flags, lists your own repos.\n\n");
    printf("Flags:\n");
    printf("  --user U                List repos for a specific user\n");
    printf("  --org O                 List repos for an organization\n");
    printf("  --help, -h              Show this help\n");
}

static void help_repo_transfer(void)
{
    printf("Usage: cb repo transfer <owner/repo> <new-owner> [--yes]\n\n");
    printf("Transfer a repository to a new owner. Requires --yes or\n");
    printf("interactive confirmation.\n\n");
    printf("Flags:\n");
    printf("  --yes                   Skip confirmation prompt\n");
    printf("  --help, -h              Show this help\n");
}

static void help_repo_topic(void)
{
    printf("Usage: cb repo topic <add|rm|list|set> ...\n\n");
    printf("Manage repository topics.\n\n");
    printf("Subcommands:\n");
    printf("  add    <owner/repo> <topic>          Add a topic\n");
    printf("  rm     <owner/repo> <topic>          Remove a topic\n");
    printf("  list   <owner/repo>                  List topics\n");
    printf("  set    <owner/repo> <t1,t2,...>      Replace all topics\n");
    printf("\nRun 'cb repo topic <subcommand> --help' for details.\n");
}

static void help_topic_add(void)
{
    printf("Usage: cb repo topic add <owner/repo> <topic>\n\n");
    printf("Add a topic to a repository.\n");
    printf("  --help, -h              Show this help\n");
}

static void help_topic_rm(void)
{
    printf("Usage: cb repo topic rm <owner/repo> <topic>\n\n");
    printf("Remove a topic from a repository.\n");
    printf("  --help, -h              Show this help\n");
}

static void help_topic_list(void)
{
    printf("Usage: cb repo topic list <owner/repo>\n\n");
    printf("List topics on a repository.\n");
    printf("  --help, -h              Show this help\n");
}

static void help_topic_set(void)
{
    printf("Usage: cb repo topic set <owner/repo> <topic1,topic2,...>\n\n");
    printf("Replace all topics on a repository with the given list.\n");
    printf("  --help, -h              Show this help\n");
}

static void help_repo(void)
{
    printf("Usage: cb repo <subcommand> [args] [flags]\n\n");
    printf("Repository management.\n\n");
    printf("Subcommands:\n");
    printf("  create     Create a new repository\n");
    printf("  delete     Delete a repository\n");
    printf("  rename     Rename a repository\n");
    printf("  edit       Edit repository settings\n");
    printf("  show       Show repository details\n");
    printf("  list       List repositories\n");
    printf("  transfer   Transfer ownership\n");
    printf("  topic      Manage topics (add, rm, list, set)\n");
    printf("\nRun 'cb repo <subcommand> --help' for details.\n");
}

static void help_org_create(void)
{
    printf("Usage: cb org create <name> [flags]\n\n");
    printf("Create a new organization.\n\n");
    printf("Flags:\n");
    print_flag_table(ORG_CREATE_FLAGS);
    printf("  --help, -h              Show this help\n");
}

static void help_org(void)
{
    printf("Usage: cb org <subcommand> [args] [flags]\n\n");
    printf("Organization management.\n\n");
    printf("Subcommands:\n");
    printf("  create     Create a new organization\n");
    printf("\nRun 'cb org <subcommand> --help' for details.\n");
}

/* ===== Output helpers ===== */

static void print_repo(const Repo *r, int json)
{
    if (json) {
        /* For JSON mode, we'd print the raw API response.
         * But since we already parsed it, we'll re-serialize what we have. */
        JsonValue *obj = json_object_new();
        if (r->name)
            json_object_set_string(obj, "name", r->name);
        if (r->full_name)
            json_object_set_string(obj, "full_name", r->full_name);
        if (r->description)
            json_object_set_string(obj, "description", r->description);
        if (r->html_url)
            json_object_set_string(obj, "html_url", r->html_url);
        if (r->default_branch)
            json_object_set_string(obj, "default_branch", r->default_branch);
        if (r->language)
            json_object_set_string(obj, "language", r->language);
        json_object_set_bool(obj, "private", r->private);
        json_object_set_bool(obj, "archived", r->archived);
        json_object_set_number(obj, "stars_count", r->stars);
        json_object_set_number(obj, "forks_count", r->forks);
        char *s = json_serialize(obj, true);
        printf("%s\n", s);
        free(s);
        json_free(obj);
    } else {
        printf("%s  %s  %s  %d stars  %d forks\n",
               r->full_name ? r->full_name : r->name,
               r->private ? "private" : "public",
               r->archived ? "archived" : "",
               r->stars, r->forks);
        if (r->description && r->description[0])
            printf("  %s\n", r->description);
        if (r->html_url)
            printf("  %s\n", r->html_url);
    }
}

static void print_repo_list(const Repo *repos, size_t count, int json)
{
    if (json) {
        JsonValue *arr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            if (repos[i].name)
                json_object_set_string(obj, "name", repos[i].name);
            if (repos[i].full_name)
                json_object_set_string(obj, "full_name", repos[i].full_name);
            if (repos[i].description)
                json_object_set_string(obj, "description", repos[i].description);
            if (repos[i].html_url)
                json_object_set_string(obj, "html_url", repos[i].html_url);
            json_object_set_bool(obj, "private", repos[i].private);
            json_object_set_bool(obj, "archived", repos[i].archived);
            json_object_set_number(obj, "stars_count", repos[i].stars);
            json_object_set_number(obj, "forks_count", repos[i].forks);
            json_array_push(arr, obj);
        }
        char *s = json_serialize(arr, true);
        printf("%s\n", s);
        free(s);
        json_free(arr);
    } else {
        for (size_t i = 0; i < count; i++) {
            printf("%-30s  %-10s  %-10s  %d stars  %d forks\n",
                   repos[i].full_name ? repos[i].full_name : repos[i].name,
                   repos[i].private ? "private" : "public",
                   repos[i].archived ? "archived" : "",
                   repos[i].stars, repos[i].forks);
        }
    }
}

static void print_topics(char **topics, size_t count, int json)
{
    if (json) {
        JsonValue *obj = json_object_new();
        JsonValue *arr = json_array_new();
        for (size_t i = 0; i < count; i++)
            json_array_push(arr, json_string_new(topics[i]));
        json_object_set(obj, "topics", arr);
        char *s = json_serialize(obj, false);
        printf("%s\n", s);
        free(s);
        json_free(obj);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("%s\n", topics[i]);
    }
}

/* ===== Error printing ===== */

static void print_api_error(int code, const char *msg)
{
    switch (code) {
    case API_ERR_AUTH:
        fprintf(stderr, "Error: Invalid or expired token. %s\n", msg);
        break;
    case API_ERR_SCOPE:
        fprintf(stderr, "Error: Token lacks required scope. %s\n", msg);
        fprintf(stderr, "Regenerate your token at the instance's settings/applications page.\n");
        break;
    case API_ERR_NOT_FOUND:
        fprintf(stderr, "Error: Not found. %s\n", msg);
        break;
    case API_ERR_QUOTA:
        fprintf(stderr, "Error: Quota exceeded. %s\n", msg);
        break;
    case API_ERR_CONFLICT:
        fprintf(stderr, "Error: Conflict. %s\n", msg);
        break;
    case API_ERR_VALIDATION:
        fprintf(stderr, "Error: Validation failed. %s\n", msg);
        break;
    case API_ERR_NETWORK:
        fprintf(stderr, "Error: Network error. %s\n", msg);
        break;
    case API_ERR_SERVER:
        fprintf(stderr, "Error: Server error. %s\n", msg);
        break;
    default:
        fprintf(stderr, "Error: %s\n", msg);
        break;
    }
}

/* ===== Confirmation prompt ===== */

static int confirm(const char *prompt)
{
    printf("%s [y/N] ", prompt);
    fflush(stdout);
    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin))
        return 0;
    return buf[0] == 'y' || buf[0] == 'Y';
}

/* ===== Global flag extraction ===== */

typedef struct
{
    int json;
    int quiet;
    int yes;
    int version;
    const char *base_url;
} CbGlobalFlags;

/* Extract global flags from argv. Returns new argc/argv with global flags removed.
 * Caller must free *out_argv. */
static int extract_global_flags(int argc, char **argv, CbGlobalFlags *gf,
                                char ***out_argv)
{
    memset(gf, 0, sizeof(*gf));

    char **new_argv = malloc((argc + 1) * sizeof(char *));
    if (!new_argv)
        return -1;
    int new_argc = 0;

    new_argv[new_argc++] = argv[0]; /* program name */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            gf->json = 1;
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            gf->quiet = 1;
        } else if (strcmp(argv[i], "--yes") == 0) {
            gf->yes = 1;
        } else if (strcmp(argv[i], "--base-url") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --base-url requires a value\n");
                free(new_argv);
                return -1;
            }
            gf->base_url = argv[++i];
        } else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            gf->version = 1;
        } else {
            new_argv[new_argc++] = argv[i];
        }
    }
    new_argv[new_argc] = NULL;
    *out_argv = new_argv;
    return new_argc;
}

/* Parse a flag table + positional args from argv.
 * Returns number of positional args in *positional, or -1 on error.
 * flag_values: array of strings, one per flag in the table (NULL if not set).
 * flag_bools: array of ints, one per flag (0 if not set).
 */
static int parse_flags(int argc, char **argv, const FlagDef *table,
                       const char ***positional_out,
                       const char ***flag_values_out,
                       int **flag_bools_out)
{
    int nflags = 0;
    while (table[nflags].name)
        nflags++;

    const char **flag_values = calloc(nflags, sizeof(char *));
    int *flag_bools = calloc(nflags, sizeof(int));
    const char **positional = malloc(argc * sizeof(char *));
    int npos = 0;

    for (int i = 0; i < argc; i++) {
        int idx = find_flag(argv[i], table);
        if (idx >= 0) {
            if (table[idx].takes_value) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: %s requires a value\n", argv[i]);
                    free(flag_values);
                    free(flag_bools);
                    free(positional);
                    return -1;
                }
                flag_values[idx] = argv[++i];
            } else {
                flag_bools[idx] = 1;
            }
        } else if (is_global_flag(argv[i])) {
            /* Global flags should have been extracted already, skip safely */
        } else if (argv[i][0] == '-' && argv[i][1] == '-') {
            fprintf(stderr, "Error: unknown flag %s\n", argv[i]);
            free(flag_values);
            free(flag_bools);
            free(positional);
            return -1;
        } else {
            positional[npos++] = argv[i];
        }
    }

    *positional_out = positional;
    *flag_values_out = flag_values;
    *flag_bools_out = flag_bools;
    return npos;
}

static int find_flag_idx(const FlagDef *table, const char *name)
{
    for (int i = 0; table[i].name; i++) {
        if (strcmp(table[i].name, name) == 0)
            return i;
    }
    return -1;
}

/* ===== Command handlers ===== */

static int cmd_repo_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_repo_create();
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        help_repo_create();
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }

    const char *name = positional[0];
    char verr[256];
    if (validate_repo_name(name, verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }

    CreateRepoOpts opts = { 0 };
    opts.name = name;
    int idx;

    idx = find_flag_idx(CREATE_FLAGS, "--private");
    if (fb[idx]) {
        opts.private_set = 1;
        opts.private_val = 1;
    }
    idx = find_flag_idx(CREATE_FLAGS, "--public");
    if (fb[idx]) {
        opts.private_set = 1;
        opts.private_val = 0;
    }
    idx = find_flag_idx(CREATE_FLAGS, "--description");
    if (fv[idx]) {
        if (validate_description(fv[idx], verr, sizeof(verr)) != VALIDATE_OK) {
            fprintf(stderr, "Error: %s\n", verr);
            free(positional);
            free(fv);
            free(fb);
            return CLI_ERR;
        }
        opts.description = fv[idx];
    }
    idx = find_flag_idx(CREATE_FLAGS, "--default-branch");
    if (fv[idx])
        opts.default_branch = fv[idx];
    idx = find_flag_idx(CREATE_FLAGS, "--license");
    if (fv[idx])
        opts.license = fv[idx];
    idx = find_flag_idx(CREATE_FLAGS, "--gitignore");
    if (fv[idx])
        opts.gitignores = fv[idx];
    idx = find_flag_idx(CREATE_FLAGS, "--auto-init");
    if (fb[idx])
        opts.auto_init = 1;
    idx = find_flag_idx(CREATE_FLAGS, "--template");
    if (fb[idx])
        opts.template = 1;
    idx = find_flag_idx(CREATE_FLAGS, "--org");
    if (fv[idx])
        opts.org = fv[idx];
    idx = find_flag_idx(CREATE_FLAGS, "--object-format");
    if (fv[idx])
        opts.object_format = fv[idx];

    Repo r;
    int rc = api_repo_create(api, &opts, &r);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }

    if (!gf->quiet) {
        printf("Created %s (%s)\n", r.full_name ? r.full_name : name,
               r.private ? "private" : "public");
        if (r.html_url)
            printf("  %s\n", r.html_url);
    }
    repo_free(&r);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_repo_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_repo_delete();
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, GLOBAL_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        help_repo_delete();
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }

    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }

    if (!gf->yes) {
        char prompt[512];
        snprintf(prompt, sizeof(prompt), "Delete %s/%s?", owner, repo);
        if (!confirm(prompt)) {
            printf("Cancelled.\n");
            free(positional);
            free(fv);
            free(fb);
            return CLI_OK;
        }
    }

    int rc = api_repo_delete(api, owner, repo);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }

    if (!gf->quiet)
        printf("Deleted %s/%s\n", owner, repo);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_repo_rename(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_repo_rename();
            return CLI_OK;
        }
    }
    if (argc < 2) {
        help_repo_rename();
        return CLI_USAGE;
    }

    char owner[128], repo[128], verr[256];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;

    const char *new_name = argv[1];
    if (validate_repo_name(new_name, verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        return CLI_ERR;
    }

    EditRepoOpts opts = { 0 };
    opts.name_set = 1;
    opts.name = new_name;

    Repo r;
    int rc = api_repo_edit(api, owner, repo, &opts, &r);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }

    if (!gf->quiet)
        printf("Renamed %s/%s -> %s/%s\n", owner, repo, owner, new_name);
    repo_free(&r);
    return CLI_OK;
}

static int cmd_repo_edit(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_repo_edit();
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, EDIT_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        help_repo_edit();
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }

    char owner[128], repo[128], verr[256];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }

    EditRepoOpts opts = { 0 };
    int idx;

#define SET_STR(flag_name, set_field, val_field, validate_fn)          \
    idx = find_flag_idx(EDIT_FLAGS, flag_name);                        \
    if (fv[idx]) {                                                     \
        if (validate_fn(fv[idx], verr, sizeof(verr)) != VALIDATE_OK) { \
            fprintf(stderr, "Error: %s\n", verr);                      \
            free(positional);                                          \
            free(fv);                                                  \
            free(fb);                                                  \
            return CLI_ERR;                                            \
        }                                                              \
        opts.set_field##_set = 1;                                      \
        opts.val_field = fv[idx];                                      \
    }

#define SET_STR_NOVALIDATE(flag_name, set_field, val_field) \
    idx = find_flag_idx(EDIT_FLAGS, flag_name);             \
    if (fv[idx]) {                                          \
        opts.set_field##_set = 1;                           \
        opts.val_field = fv[idx];                           \
    }

#define SET_BOOL(flag_name, set_field, val_field, val) \
    idx = find_flag_idx(EDIT_FLAGS, flag_name);        \
    if (fb[idx]) {                                     \
        opts.set_field##_set = 1;                      \
        opts.val_field##_val = val;                    \
    }

    SET_STR("--description", desc, description, validate_description);
    SET_STR("--website", website, website, validate_website);
    SET_BOOL("--private", private, private, 1);
    SET_BOOL("--public", private, private, 0);
    SET_STR_NOVALIDATE("--default-branch", default_branch, default_branch);
    SET_BOOL("--archived", archived, archived, 1);
    SET_BOOL("--unarchived", archived, archived, 0);
    SET_BOOL("--template", template, template, 1);
    SET_BOOL("--no-template", template, template, 0);
    SET_BOOL("--has-issues", has_issues, has_issues, 1);
    SET_BOOL("--no-issues", has_issues, has_issues, 0);
    SET_BOOL("--has-wiki", has_wiki, has_wiki, 1);
    SET_BOOL("--no-wiki", has_wiki, has_wiki, 0);
    SET_BOOL("--has-prs", has_prs, has_prs, 1);
    SET_BOOL("--no-prs", has_prs, has_prs, 0);
    SET_BOOL("--has-projects", has_projects, has_projects, 1);
    SET_BOOL("--no-projects", has_projects, has_projects, 0);
    SET_BOOL("--has-releases", has_releases, has_releases, 1);
    SET_BOOL("--no-releases", has_releases, has_releases, 0);
    SET_BOOL("--has-packages", has_packages, has_packages, 1);
    SET_BOOL("--no-packages", has_packages, has_packages, 0);
    SET_BOOL("--has-actions", has_actions, has_actions, 1);
    SET_BOOL("--no-actions", has_actions, has_actions, 0);
    SET_BOOL("--allow-merge", allow_merge, allow_merge, 1);
    SET_BOOL("--no-merge", allow_merge, allow_merge, 0);
    SET_BOOL("--allow-rebase", allow_rebase, allow_rebase, 1);
    SET_BOOL("--no-rebase", allow_rebase, allow_rebase, 0);
    SET_BOOL("--allow-squash", allow_squash, allow_squash, 1);
    SET_BOOL("--no-squash", allow_squash, allow_squash, 0);
    SET_BOOL("--allow-ff-only", allow_ff_only, allow_ff_only, 1);
    SET_BOOL("--no-ff-only", allow_ff_only, allow_ff_only, 0);
    idx = find_flag_idx(EDIT_FLAGS, "--default-merge-style");
    if (fv[idx]) {
        if (validate_merge_style(fv[idx], verr, sizeof(verr)) != VALIDATE_OK) {
            fprintf(stderr, "Error: %s\n", verr);
            free(positional);
            free(fv);
            free(fb);
            return CLI_ERR;
        }
        opts.default_merge_style_set = 1;
        opts.default_merge_style = fv[idx];
    }
    SET_BOOL("--delete-branch-after-merge", delete_branch_after_merge, delete_branch_after_merge, 1);
    SET_BOOL("--no-delete-branch-after-merge", delete_branch_after_merge, delete_branch_after_merge, 0);
    SET_BOOL("--allow-maintainer-edit", allow_maintainer_edit, allow_maintainer_edit, 1);
    SET_BOOL("--no-allow-maintainer-edit", allow_maintainer_edit, allow_maintainer_edit, 0);

#undef SET_STR
#undef SET_STR_NOVALIDATE
#undef SET_BOOL

    Repo r;
    int rc = api_repo_edit(api, owner, repo, &opts, &r);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }

    if (!gf->quiet)
        printf("Updated %s/%s\n", owner, repo);
    repo_free(&r);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_repo_show(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_repo_show();
            return CLI_OK;
        }
    }
    if (argc < 1) {
        help_repo_show();
        return CLI_USAGE;
    }

    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;

    Repo r;
    int rc = api_repo_show(api, owner, repo, &r);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }

    print_repo(&r, gf->json);
    repo_free(&r);
    return CLI_OK;
}

static int cmd_repo_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    const char *owner = NULL;
    int is_org = 0;

    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_repo_list();
            return CLI_OK;
        }
        if (strcmp(argv[i], "--org") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --org requires a value\n");
                return CLI_USAGE;
            }
            owner = argv[++i];
            is_org = 1;
        } else if (strcmp(argv[i], "--user") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --user requires a value\n");
                return CLI_USAGE;
            }
            owner = argv[++i];
            is_org = 0;
        } else if (argv[i][0] == '-' && argv[i][1] == '-') {
            fprintf(stderr, "Error: unknown flag %s\n", argv[i]);
            return CLI_USAGE;
        } else {
            owner = argv[i];
        }
    }

    Repo *repos;
    size_t count;
    int rc = api_repo_list(api, owner, is_org, &repos, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }

    print_repo_list(repos, count, gf->json);
    repo_array_free(repos, count);
    return CLI_OK;
}

static int cmd_repo_transfer(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_repo_transfer();
            return CLI_OK;
        }
    }
    if (argc < 2) {
        help_repo_transfer();
        return CLI_USAGE;
    }

    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;

    const char *new_owner = argv[1];

    if (!gf->yes) {
        char prompt[512];
        snprintf(prompt, sizeof(prompt), "Transfer %s/%s to %s?", owner, repo, new_owner);
        if (!confirm(prompt)) {
            printf("Cancelled.\n");
            return CLI_OK;
        }
    }

    int rc = api_repo_transfer(api, owner, repo, new_owner, NULL, 0);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }

    if (!gf->quiet)
        printf("Transferred %s/%s -> %s/%s\n", owner, repo, new_owner, repo);
    return CLI_OK;
}

/* ===== Topic commands ===== */

static int cmd_topic_add(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_topic_add();
            return CLI_OK;
        }
    }
    if (argc < 2) {
        help_topic_add();
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int rc = api_topic_add(api, owner, repo, argv[1]);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Added topic '%s' to %s/%s\n", argv[1], owner, repo);
    return CLI_OK;
}

static int cmd_topic_rm(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_topic_rm();
            return CLI_OK;
        }
    }
    if (argc < 2) {
        help_topic_rm();
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int rc = api_topic_remove(api, owner, repo, argv[1]);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Removed topic '%s' from %s/%s\n", argv[1], owner, repo);
    return CLI_OK;
}

static int cmd_topic_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_topic_list();
            return CLI_OK;
        }
    }
    if (argc < 1) {
        help_topic_list();
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    char **topics;
    size_t count;
    int rc = api_topic_list(api, owner, repo, &topics, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_topics(topics, count, gf->json);
    topic_array_free(topics, count);
    return CLI_OK;
}

static int cmd_topic_set(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_topic_set();
            return CLI_OK;
        }
    }
    if (argc < 2) {
        help_topic_set();
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;

    /* Parse comma-separated topics */
    char *topics_str = strdup(argv[1]);
    size_t count = 1;
    for (char *p = topics_str; *p; p++)
        if (*p == ',')
            count++;

    const char **topics = malloc(count * sizeof(char *));
    size_t idx = 0;
    char *tok = strtok(topics_str, ",");
    while (tok) {
        /* trim leading spaces */
        while (*tok == ' ')
            tok++;
        topics[idx++] = tok;
        tok = strtok(NULL, ",");
    }
    count = idx;

    int rc = api_topic_set(api, owner, repo, topics, count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(topics);
        free(topics_str);
        return CLI_ERR;
    }

    if (!gf->quiet) {
        printf("Set topics: ");
        for (size_t i = 0; i < count; i++) {
            if (i > 0)
                printf(", ");
            printf("%s", topics[i]);
        }
        printf("\n");
    }

    free(topics);
    free(topics_str);
    return CLI_OK;
}

/* ===== Actions (CI/CD) help text ===== */

static void help_actions(void)
{
    printf("Usage: cb actions <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage CI/CD actions for a repository.\n\n");
    printf("Subcommands:\n");
    printf("  list       <owner/repo>                  List recent workflow runs\n");
    printf("  show       <owner/repo> <run-id>          Show details of a run\n");
    printf("  runners    <owner/repo>                   List available runners\n");
    printf("  dispatch   <owner/repo> <workflow>        Trigger a workflow\n");
    printf("  jobs       <owner/repo> <run-id>          List jobs in a run\n");
    printf("  log        <owner/repo> <run-id> [job]    Show log output for a run\n");
    printf("  secret     list|set|rm                    Manage secrets\n");
    printf("  var        list|show|set|rm               Manage variables\n");
    printf("\nRun 'cb actions <subcommand> --help' for details.\n");
}

static void help_actions_list(void)
{
    printf("Usage: cb actions list <owner/repo>\n\n");
    printf("List recent workflow runs.\n\n");
    printf("Flags:\n");
    printf("  --json                  Output raw JSON\n");
    printf("  --help, -h              Show this help\n");
}

static void help_actions_show(void)
{
    printf("Usage: cb actions show <owner/repo> <run-id>\n\n");
    printf("Show details of a specific workflow run.\n\n");
    printf("Flags:\n");
    printf("  --json                  Output raw JSON\n");
    printf("  --help, -h              Show this help\n");
}

static void help_actions_runners(void)
{
    printf("Usage: cb actions runners <owner/repo>\n\n");
    printf("List CI runners available to this repository.\n\n");
    printf("Flags:\n");
    printf("  --json                  Output raw JSON\n");
    printf("  --help, -h              Show this help\n");
}

static void help_actions_dispatch(void)
{
    printf("Usage: cb actions dispatch <owner/repo> <workflow-file>\n\n");
    printf("Trigger a workflow run.\n\n");
    printf("Flags:\n");
    printf("  --ref REF               Git ref to dispatch on (default: master)\n");
    printf("  --help, -h              Show this help\n");
}

static void help_actions_secret(void)
{
    printf("Usage: cb actions secret <list|set|rm> <owner/repo> [args]\n\n");
    printf("Manage repository action secrets.\n\n");
    printf("Subcommands:\n");
    printf("  list   <owner/repo>                  List secret names\n");
    printf("  set    <owner/repo> <name> --value V  Create or update a secret\n");
    printf("  rm     <owner/repo> <name>            Delete a secret\n");
    printf("\nRun 'cb actions secret <subcommand> --help' for details.\n");
}

static void help_actions_var(void)
{
    printf("Usage: cb actions var <list|show|set|rm> <owner/repo> [args]\n\n");
    printf("Manage repository action variables.\n\n");
    printf("Subcommands:\n");
    printf("  list   <owner/repo>                  List variables\n");
    printf("  show   <owner/repo> <name>           Show a variable's value\n");
    printf("  set    <owner/repo> <name> --value V  Create or update a variable\n");
    printf("  rm     <owner/repo> <name>            Delete a variable\n");
    printf("\nRun 'cb actions var <subcommand> --help' for details.\n");
}

/* ===== Actions output helpers ===== */

static void print_action_run(const ActionRun *r, int json)
{
    if (json) {
        JsonValue *obj = json_object_new();
        if (r->title)
            json_object_set_string(obj, "title", r->title);
        if (r->status)
            json_object_set_string(obj, "status", r->status);
        if (r->event)
            json_object_set_string(obj, "event", r->event);
        if (r->workflow_id)
            json_object_set_string(obj, "workflow_id", r->workflow_id);
        if (r->prettyref)
            json_object_set_string(obj, "prettyref", r->prettyref);
        if (r->commit_sha)
            json_object_set_string(obj, "commit_sha", r->commit_sha);
        if (r->html_url)
            json_object_set_string(obj, "html_url", r->html_url);
        if (r->created)
            json_object_set_string(obj, "created", r->created);
        if (r->started)
            json_object_set_string(obj, "started", r->started);
        if (r->stopped)
            json_object_set_string(obj, "stopped", r->stopped);
        json_object_set(obj, "id", json_number_new((double)r->id));
        json_object_set(obj, "index_in_repo", json_number_new((double)r->index_in_repo));
        char *s = json_serialize(obj, false);
        printf("%s\n", s);
        free(s);
        json_free(obj);
        return;
    }
    printf("Run #%lld: %s\n", (long long)r->index_in_repo, r->title ? r->title : "");
    printf("  Status:    %s\n", r->status ? r->status : "?");
    printf("  Event:     %s\n", r->event ? r->event : "?");
    printf("  Workflow:  %s\n", r->workflow_id ? r->workflow_id : "?");
    printf("  Ref:       %s\n", r->prettyref ? r->prettyref : "?");
    printf("  Commit:    %s\n", r->commit_sha ? r->commit_sha : "?");
    printf("  Created:   %s\n", r->created ? r->created : "?");
    printf("  URL:       %s\n", r->html_url ? r->html_url : "?");
}

static void print_action_run_list(const ActionRun *arr, size_t count, int json)
{
    if (json) {
        JsonValue *root = json_object_new();
        JsonValue *runs = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set(obj, "id", json_number_new((double)arr[i].id));
            json_object_set(obj, "index_in_repo", json_number_new((double)arr[i].index_in_repo));
            if (arr[i].title)
                json_object_set_string(obj, "title", arr[i].title);
            if (arr[i].status)
                json_object_set_string(obj, "status", arr[i].status);
            if (arr[i].event)
                json_object_set_string(obj, "event", arr[i].event);
            if (arr[i].workflow_id)
                json_object_set_string(obj, "workflow_id", arr[i].workflow_id);
            if (arr[i].prettyref)
                json_object_set_string(obj, "prettyref", arr[i].prettyref);
            if (arr[i].commit_sha)
                json_object_set_string(obj, "commit_sha", arr[i].commit_sha);
            if (arr[i].html_url)
                json_object_set_string(obj, "html_url", arr[i].html_url);
            if (arr[i].created)
                json_object_set_string(obj, "created", arr[i].created);
            json_array_push(runs, obj);
        }
        json_object_set(root, "workflow_runs", runs);
        json_object_set(root, "total_count", json_number_new((double)count));
        char *s = json_serialize(root, false);
        printf("%s\n", s);
        free(s);
        json_free(root);
        return;
    }
    if (count == 0) {
        printf("No workflow runs found.\n");
        return;
    }
    printf("%-6s %-10s %-10s %-20s %-15s %s\n",
           "#", "Status", "Event", "Workflow", "Ref", "Created");
    for (size_t i = 0; i < count; i++) {
        printf("#%-5lld %-10s %-10s %-20s %-15s %s\n",
               (long long)arr[i].index_in_repo,
               arr[i].status ? arr[i].status : "?",
               arr[i].event ? arr[i].event : "?",
               arr[i].workflow_id ? arr[i].workflow_id : "?",
               arr[i].prettyref ? arr[i].prettyref : "?",
               arr[i].created ? arr[i].created : "?");
    }
}

static void print_action_runner_list(const ActionRunner *arr, size_t count, int json)
{
    if (json) {
        JsonValue *runs = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set(obj, "id", json_number_new((double)arr[i].id));
            if (arr[i].name)
                json_object_set_string(obj, "name", arr[i].name);
            if (arr[i].uuid)
                json_object_set_string(obj, "uuid", arr[i].uuid);
            if (arr[i].status)
                json_object_set_string(obj, "status", arr[i].status);
            if (arr[i].version)
                json_object_set_string(obj, "version", arr[i].version);
            json_array_push(runs, obj);
        }
        char *s = json_serialize(runs, false);
        printf("%s\n", s);
        free(s);
        json_free(runs);
        return;
    }
    if (count == 0) {
        printf("No runners found.\n");
        return;
    }
    printf("%-20s %-10s %-15s %s\n", "Name", "Status", "Version", "UUID");
    for (size_t i = 0; i < count; i++) {
        printf("%-20s %-10s %-15s %s\n",
               arr[i].name ? arr[i].name : "?",
               arr[i].status ? arr[i].status : "?",
               arr[i].version ? arr[i].version : "?",
               arr[i].uuid ? arr[i].uuid : "?");
    }
}

static void print_action_variable_list(const ActionVariable *arr, size_t count, int json)
{
    if (json) {
        JsonValue *vars = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            if (arr[i].name)
                json_object_set_string(obj, "name", arr[i].name);
            if (arr[i].data)
                json_object_set_string(obj, "data", arr[i].data);
            json_array_push(vars, obj);
        }
        char *s = json_serialize(vars, false);
        printf("%s\n", s);
        free(s);
        json_free(vars);
        return;
    }
    if (count == 0) {
        printf("No variables found.\n");
        return;
    }
    printf("%-30s %s\n", "Name", "Value");
    for (size_t i = 0; i < count; i++)
        printf("%-30s %s\n", arr[i].name ? arr[i].name : "?", arr[i].data ? arr[i].data : "?");
}

static void print_action_variable(const ActionVariable *v, int json)
{
    if (json) {
        JsonValue *obj = json_object_new();
        if (v->name)
            json_object_set_string(obj, "name", v->name);
        if (v->data)
            json_object_set_string(obj, "data", v->data);
        char *s = json_serialize(obj, false);
        printf("%s\n", s);
        free(s);
        json_free(obj);
        return;
    }
    printf("Name:  %s\n", v->name ? v->name : "?");
    printf("Value: %s\n", v->data ? v->data : "?");
}

static void print_action_secret_list(const ActionSecret *arr, size_t count, int json)
{
    if (json) {
        JsonValue *secrets = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            if (arr[i].name)
                json_object_set_string(obj, "name", arr[i].name);
            json_array_push(secrets, obj);
        }
        char *s = json_serialize(secrets, false);
        printf("%s\n", s);
        free(s);
        json_free(secrets);
        return;
    }
    if (count == 0) {
        printf("No secrets found.\n");
        return;
    }
    printf("%s\n", "Name");
    for (size_t i = 0; i < count; i++)
        printf("%s\n", arr[i].name ? arr[i].name : "?");
}

/* ===== Actions handlers ===== */

static int cmd_actions_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_actions_list();
            return CLI_OK;
        }
    }
    if (argc < 1) {
        help_actions_list();
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;

    ActionRun *runs = NULL;
    size_t count = 0;
    int rc = api_action_run_list(api, owner, repo, &runs, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_action_run_list(runs, count, gf->json);
    action_run_array_free(runs, count);
    return CLI_OK;
}

static int cmd_actions_show(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_actions_show();
            return CLI_OK;
        }
    }
    if (argc < 2) {
        help_actions_show();
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int64_t run_id = strtoll(argv[1], NULL, 10);
    if (run_id <= 0) {
        fprintf(stderr, "Error: invalid run ID '%s'\n", argv[1]);
        return CLI_ERR;
    }

    ActionRun run;
    int rc = api_action_run_show(api, owner, repo, run_id, &run);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_action_run(&run, gf->json);
    action_run_free(&run);
    return CLI_OK;
}

static int cmd_actions_runners(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_actions_runners();
            return CLI_OK;
        }
    }
    if (argc < 1) {
        help_actions_runners();
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;

    ActionRunner *runners = NULL;
    size_t count = 0;
    int rc = api_action_runner_list(api, owner, repo, &runners, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_action_runner_list(runners, count, gf->json);
    action_runner_array_free(runners, count);
    return CLI_OK;
}

static int cmd_actions_dispatch(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_actions_dispatch();
            return CLI_OK;
        }
    }
    if (argc < 2) {
        help_actions_dispatch();
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    const char *workflowfile = argv[1];

    const char *ref = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--ref") == 0 && i + 1 < argc) {
            ref = argv[++i];
        }
    }

    int rc = api_action_dispatch(api, owner, repo, workflowfile, ref);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Dispatched workflow '%s' on %s/%s (ref: %s)\n",
               workflowfile, owner, repo, ref ? ref : "master");
    return CLI_OK;
}

static int cmd_actions_secret_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb actions secret list <owner/repo>\n\n");
            printf("List action secrets (names only).\n");
            printf("  --json                  Output raw JSON\n");
            printf("  --help, -h              Show this help\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        printf("Usage: cb actions secret list <owner/repo>\n\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;

    ActionSecret *secrets = NULL;
    size_t count = 0;
    int rc = api_action_secret_list(api, owner, repo, &secrets, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_action_secret_list(secrets, count, gf->json);
    action_secret_array_free(secrets, count);
    return CLI_OK;
}

static int cmd_actions_secret_set(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    (void)gf;
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb actions secret set <owner/repo> <name> --value V\n\n");
            printf("Create or update a secret.\n");
            printf("  --value V               Secret value\n");
            printf("  --help, -h              Show this help\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        printf("Usage: cb actions secret set <owner/repo> <name> --value V\n\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    const char *name = argv[1];
    const char *value = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--value") == 0 && i + 1 < argc)
            value = argv[++i];
    }
    if (!value) {
        fprintf(stderr, "Error: --value is required\n");
        return CLI_ERR;
    }

    int rc = api_action_secret_set(api, owner, repo, name, value);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    printf("Secret '%s' set.\n", name);
    return CLI_OK;
}

static int cmd_actions_secret_rm(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb actions secret rm <owner/repo> <name> [--yes]\n\n");
            printf("Delete a secret.\n");
            printf("  --yes                   Skip confirmation\n");
            printf("  --help, -h              Show this help\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        printf("Usage: cb actions secret rm <owner/repo> <name> [--yes]\n\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    const char *name = argv[1];
    int yes = gf->yes;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--yes") == 0)
            yes = 1;
    }
    if (!yes) {
        char prompt[512];
        snprintf(prompt, sizeof(prompt), "Delete secret '%s' on %s/%s?", name, owner, repo);
        if (!confirm(prompt))
            return CLI_OK;
    }

    int rc = api_action_secret_delete(api, owner, repo, name);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    printf("Secret '%s' deleted.\n", name);
    return CLI_OK;
}

static int cmd_actions_var_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb actions var list <owner/repo>\n\n");
            printf("List action variables.\n");
            printf("  --json                  Output raw JSON\n");
            printf("  --help, -h              Show this help\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        printf("Usage: cb actions var list <owner/repo>\n\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;

    ActionVariable *vars = NULL;
    size_t count = 0;
    int rc = api_action_variable_list(api, owner, repo, &vars, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_action_variable_list(vars, count, gf->json);
    action_variable_array_free(vars, count);
    return CLI_OK;
}

static int cmd_actions_var_show(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb actions var show <owner/repo> <name>\n\n");
            printf("Show a variable's value.\n");
            printf("  --json                  Output raw JSON\n");
            printf("  --help, -h              Show this help\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        printf("Usage: cb actions var show <owner/repo> <name>\n\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    const char *name = argv[1];

    ActionVariable var;
    int rc = api_action_variable_show(api, owner, repo, name, &var);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_action_variable(&var, gf->json);
    action_variable_free(&var);
    return CLI_OK;
}

static int cmd_actions_var_set(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    (void)gf;
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb actions var set <owner/repo> <name> --value V\n\n");
            printf("Create or update a variable.\n");
            printf("  --value V               Variable value\n");
            printf("  --help, -h              Show this help\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        printf("Usage: cb actions var set <owner/repo> <name> --value V\n\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    const char *name = argv[1];
    const char *value = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--value") == 0 && i + 1 < argc)
            value = argv[++i];
    }
    if (!value) {
        fprintf(stderr, "Error: --value is required\n");
        return CLI_ERR;
    }

    int rc = api_action_variable_set(api, owner, repo, name, value);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    printf("Variable '%s' set.\n", name);
    return CLI_OK;
}

static int cmd_actions_var_rm(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb actions var rm <owner/repo> <name> [--yes]\n\n");
            printf("Delete a variable.\n");
            printf("  --yes                   Skip confirmation\n");
            printf("  --help, -h              Show this help\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        printf("Usage: cb actions var rm <owner/repo> <name> [--yes]\n\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    const char *name = argv[1];
    int yes = gf->yes;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--yes") == 0)
            yes = 1;
    }
    if (!yes) {
        char prompt[512];
        snprintf(prompt, sizeof(prompt), "Delete variable '%s' on %s/%s?", name, owner, repo);
        if (!confirm(prompt))
            return CLI_OK;
    }

    int rc = api_action_variable_delete(api, owner, repo, name);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    printf("Variable '%s' deleted.\n", name);
    return CLI_OK;
}

/* ===== Actions jobs & log handlers ===== */

static int cmd_actions_jobs(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb actions jobs <owner/repo> <run-id>\n\n");
            printf("List jobs in a workflow run.\n\n");
            printf("Flags:\n");
            printf("  --json                  Output raw JSON\n");
            printf("  --help, -h              Show this help\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        printf("Usage: cb actions jobs <owner/repo> <run-id>\n\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int run_id = atoi(argv[1]);
    if (run_id <= 0) {
        fprintf(stderr, "Error: invalid run ID '%s'\n", argv[1]);
        return CLI_ERR;
    }

    ActionJob *jobs = NULL;
    size_t count = 0;
    int rc = api_action_job_list(api, owner, repo, run_id, &jobs, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }

    if (gf->json) {
        JsonValue *arr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set(obj, "id", json_number_new((double)jobs[i].id));
            if (jobs[i].name)
                json_object_set_string(obj, "name", jobs[i].name);
            if (jobs[i].status)
                json_object_set_string(obj, "status", jobs[i].status);
            if (jobs[i].duration)
                json_object_set_string(obj, "duration", jobs[i].duration);
            json_array_push(arr, obj);
        }
        char *s = json_serialize(arr, false);
        printf("%s\n", s);
        free(s);
        json_free(arr);
    } else {
        if (count == 0) {
            printf("No jobs found.\n");
        } else {
            printf("%-6s %-20s %-10s %s\n", "Job", "Name", "Status", "Duration");
            for (size_t i = 0; i < count; i++) {
                printf("%-6zu %-20s %-10s %s\n", i,
                       jobs[i].name ? jobs[i].name : "?",
                       jobs[i].status ? jobs[i].status : "?",
                       jobs[i].duration ? jobs[i].duration : "?");
            }
        }
    }

    action_job_array_free(jobs, count);
    return CLI_OK;
}

static int cmd_actions_log(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    (void)gf;
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb actions log <owner/repo> <run-id> [job-index] [step-index]\n\n");
            printf("Show log output for a workflow run.\n");
            printf("If job-index is omitted, shows logs for job 0.\n");
            printf("If step-index is omitted, shows logs for all steps.\n");
            printf("  --help, -h              Show this help\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        printf("Usage: cb actions log <owner/repo> <run-id> [job-index] [step-index]\n\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int run_id = atoi(argv[1]);
    if (run_id <= 0) {
        fprintf(stderr, "Error: invalid run ID '%s'\n", argv[1]);
        return CLI_ERR;
    }
    int job_index = (argc > 2) ? atoi(argv[2]) : 0;
    if (job_index < 0) {
        fprintf(stderr, "Error: invalid job index\n");
        return CLI_ERR;
    }
    int step_filter = (argc > 3) ? atoi(argv[3]) : -1;

    /* Fetch job detail to get step list */
    ActionJobDetail detail;
    int rc = api_action_job_detail(api, owner, repo, run_id, job_index, &detail);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }

    /* Print job header */
    printf("Job %d: %s (%s, %s)\n\n", job_index,
           detail.job.name ? detail.job.name : "?",
           detail.job.status ? detail.job.status : "?",
           detail.job.duration ? detail.job.duration : "?");

    /* For each step (or just the requested one), fetch and print logs */
    for (size_t i = 0; i < detail.step_count; i++) {
        if (step_filter >= 0 && (int)i != step_filter)
            continue;

        printf("=== %s (%s, %s) ===\n",
               detail.steps[i].summary ? detail.steps[i].summary : "?",
               detail.steps[i].status ? detail.steps[i].status : "?",
               detail.steps[i].duration ? detail.steps[i].duration : "?");

        ActionLogLine *lines = NULL;
        size_t line_count = 0;
        rc = api_action_log_fetch(api, owner, repo, run_id, job_index, (int)i,
                                  &lines, &line_count);
        if (rc != API_OK) {
            print_api_error(rc, api->last_error);
            action_job_detail_free(&detail);
            return CLI_ERR;
        }
        for (size_t j = 0; j < line_count; j++)
            printf("%s\n", lines[j].message);

        action_log_lines_free(lines, line_count);
        if (i + 1 < detail.step_count)
            printf("\n");
    }

    action_job_detail_free(&detail);
    return CLI_OK;
}

/* ===== Actions command dispatch ===== */

static int cmd_actions(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_actions();
        return CLI_USAGE;
    }

    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;

    if (is_help_arg(sub)) {
        help_actions();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_actions_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "show") == 0)
        return cmd_actions_show(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "runners") == 0)
        return cmd_actions_runners(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "dispatch") == 0)
        return cmd_actions_dispatch(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "jobs") == 0)
        return cmd_actions_jobs(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "log") == 0)
        return cmd_actions_log(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "secret") == 0) {
        if (rest_argc < 1) {
            help_actions_secret();
            return CLI_USAGE;
        }
        const char *sec_sub = rest_argv[0];
        int sec_argc = rest_argc - 1;
        char **sec_argv = rest_argv + 1;
        if (is_help_arg(sec_sub)) {
            help_actions_secret();
            return CLI_OK;
        }
        if (strcmp(sec_sub, "list") == 0)
            return cmd_actions_secret_list(sec_argc, sec_argv, api, gf);
        if (strcmp(sec_sub, "set") == 0)
            return cmd_actions_secret_set(sec_argc, sec_argv, api, gf);
        if (strcmp(sec_sub, "rm") == 0)
            return cmd_actions_secret_rm(sec_argc, sec_argv, api, gf);
        fprintf(stderr, "Error: unknown secret subcommand '%s'\n", sec_sub);
        help_actions_secret();
        return CLI_USAGE;
    }
    if (strcmp(sub, "var") == 0) {
        if (rest_argc < 1) {
            help_actions_var();
            return CLI_USAGE;
        }
        const char *var_sub = rest_argv[0];
        int var_argc = rest_argc - 1;
        char **var_argv = rest_argv + 1;
        if (is_help_arg(var_sub)) {
            help_actions_var();
            return CLI_OK;
        }
        if (strcmp(var_sub, "list") == 0)
            return cmd_actions_var_list(var_argc, var_argv, api, gf);
        if (strcmp(var_sub, "show") == 0)
            return cmd_actions_var_show(var_argc, var_argv, api, gf);
        if (strcmp(var_sub, "set") == 0)
            return cmd_actions_var_set(var_argc, var_argv, api, gf);
        if (strcmp(var_sub, "rm") == 0)
            return cmd_actions_var_rm(var_argc, var_argv, api, gf);
        fprintf(stderr, "Error: unknown var subcommand '%s'\n", var_sub);
        help_actions_var();
        return CLI_USAGE;
    }

    fprintf(stderr, "Error: unknown actions subcommand '%s'\n", sub);
    help_actions();
    return CLI_USAGE;
}

/* ===== New command flag tables ===== */

static const FlagDef RELEASE_LIST_FLAGS[] = {
    { "--draft", NULL, 0 },
    { "--pre-release", NULL, 0 },
    { "--limit", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef RELEASE_CREATE_FLAGS[] = {
    { "--tag", NULL, 1 },
    { "--name", NULL, 1 },
    { "--body", NULL, 1 },
    { "--target", NULL, 1 },
    { "--draft", NULL, 0 },
    { "--prerelease", NULL, 0 },
    { "--hide-archive-links", NULL, 0 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef RELEASE_EDIT_FLAGS[] = {
    { "--tag", NULL, 1 },
    { "--name", NULL, 1 },
    { "--body", NULL, 1 },
    { "--target", NULL, 1 },
    { "--draft", NULL, 0 },
    { "--no-draft", NULL, 0 },
    { "--prerelease", NULL, 0 },
    { "--no-prerelease", NULL, 0 },
    { "--hide-archive-links", NULL, 0 },
    { "--no-hide-archive-links", NULL, 0 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef TAG_CREATE_FLAGS[] = {
    { "--tag", NULL, 1 },
    { "--message", "-m", 1 },
    { "--target", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef BRANCH_CREATE_FLAGS[] = {
    { "--name", NULL, 1 },
    { "--from", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef ISSUE_LIST_FLAGS[] = {
    { "--state", NULL, 1 },
    { "--labels", NULL, 1 },
    { "--type", NULL, 1 },
    { "--milestone", NULL, 1 },
    { "--limit", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef ISSUE_CREATE_FLAGS[] = {
    { "--title", NULL, 1 },
    { "--body", NULL, 1 },
    { "--assignee", NULL, 1 },
    { "--label", NULL, 1 },
    { "--milestone", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef ISSUE_EDIT_FLAGS[] = {
    { "--title", NULL, 1 },
    { "--body", NULL, 1 },
    { "--state", NULL, 1 },
    { "--milestone", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef LABEL_CREATE_FLAGS[] = {
    { "--name", NULL, 1 },
    { "--color", NULL, 1 },
    { "--description", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef MILESTONE_CREATE_FLAGS[] = {
    { "--title", NULL, 1 },
    { "--description", NULL, 1 },
    { "--state", NULL, 1 },
    { "--due", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef PR_CREATE_FLAGS[] = {
    { "--title", NULL, 1 },
    { "--head", NULL, 1 },
    { "--base", NULL, 1 },
    { "--body", NULL, 1 },
    { "--label", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef COMMIT_LIST_FLAGS[] = {
    { "--sha", NULL, 1 },
    { "--path", NULL, 1 },
    { "--limit", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef KEY_ADD_FLAGS[] = {
    { "--title", NULL, 1 },
    { "--key", NULL, 1 },
    { "--read-only", NULL, 0 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef COLLAB_ADD_FLAGS[] = {
    { "--permission", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef FORK_CREATE_FLAGS[] = {
    { "--name", NULL, 1 },
    { "--org", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

static const FlagDef HOOK_CREATE_FLAGS[] = {
    { "--type", NULL, 1 },
    { "--url", NULL, 1 },
    { "--event", NULL, 1 },
    { "--content-type", NULL, 1 },
    { "--secret", NULL, 1 },
    { "--active", NULL, 0 },
    { "--inactive", NULL, 0 },
    { "--branch-filter", NULL, 1 },
    { "--help", "-h", 0 },
    { NULL, NULL, 0 }
};

/* ===== New command help text ===== */

static void help_release(void)
{
    printf("Usage: cb release <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage releases.\n\n");
    printf("Subcommands:\n");
    printf("  list          List releases\n");
    printf("  create        Create a release\n");
    printf("  show          Show a release by ID\n");
    printf("  latest        Show latest release\n");
    printf("  edit          Edit a release\n");
    printf("  delete        Delete a release\n");
    printf("  by-tag        Show release by tag name\n");
    printf("  delete-tag    Delete release by tag name\n");
    printf("  asset         Manage release assets (list, upload, show, edit, delete)\n");
    printf("\nRun 'cb release <subcommand> --help' for details.\n");
}

static void help_release_list(void)
{
    printf("Usage: cb release <owner/repo> list [flags]\n\n");
    printf("List releases.\n\n");
    printf("Flags:\n");
    print_flag_table(RELEASE_LIST_FLAGS);
    printf("  --help, -h              Show this help\n");
}

static void help_release_create(void)
{
    printf("Usage: cb release <owner/repo> create --tag <tag> [flags]\n\n");
    printf("Create a release.\n\n");
    printf("Flags:\n");
    print_flag_table(RELEASE_CREATE_FLAGS);
    printf("  --help, -h              Show this help\n");
}

static void help_release_edit(void)
{
    printf("Usage: cb release <owner/repo> edit <id> [flags]\n\n");
    printf("Edit a release. Only provided flags are sent.\n\n");
    printf("Flags:\n");
    print_flag_table(RELEASE_EDIT_FLAGS);
    printf("  --help, -h              Show this help\n");
}

static void help_release_asset(void)
{
    printf("Usage: cb release <owner/repo> asset <subcommand> ...\n\n");
    printf("Manage release assets.\n\n");
    printf("Subcommands:\n");
    printf("  list    <release-id>              List assets\n");
    printf("  upload  <release-id> --file <path> Upload a file\n");
    printf("  show    <release-id> <asset-id>   Show asset\n");
    printf("  edit    <release-id> <asset-id>   Edit asset\n");
    printf("  delete  <release-id> <asset-id>   Delete asset\n");
    printf("\nRun 'cb release asset <subcommand> --help' for details.\n");
}

static void help_tag(void)
{
    printf("Usage: cb tag <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage tags.\n\n");
    printf("Subcommands:\n");
    printf("  list      List tags\n");
    printf("  create    Create a tag\n");
    printf("  show      Show a tag\n");
    printf("  delete    Delete a tag\n");
    printf("\nRun 'cb tag <subcommand> --help' for details.\n");
}

static void help_branch(void)
{
    printf("Usage: cb branch <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage branches.\n\n");
    printf("Subcommands:\n");
    printf("  list      List branches\n");
    printf("  create    Create a branch\n");
    printf("  show      Show a branch\n");
    printf("  rename    Rename a branch\n");
    printf("  delete    Delete a branch\n");
    printf("\nRun 'cb branch <subcommand> --help' for details.\n");
}

static void help_issue(void)
{
    printf("Usage: cb issue <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage issues.\n\n");
    printf("Subcommands:\n");
    printf("  list      List issues\n");
    printf("  create    Create an issue\n");
    printf("  show      Show an issue\n");
    printf("  edit      Edit an issue\n");
    printf("  delete    Delete an issue\n");
    printf("  close     Close an issue (shorthand)\n");
    printf("  reopen    Reopen an issue (shorthand)\n");
    printf("  comment   Add a comment\n");
    printf("  label     Manage issue labels (add, set, rm, clear)\n");
    printf("  pin       Pin an issue\n");
    printf("  unpin     Unpin an issue\n");
    printf("  deadline  Set a due date\n");
    printf("\nRun 'cb issue <subcommand> --help' for details.\n");
}

static void help_label(void)
{
    printf("Usage: cb label <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage repository labels.\n\n");
    printf("Subcommands:\n");
    printf("  list      List labels\n");
    printf("  create    Create a label\n");
    printf("  show      Show a label\n");
    printf("  edit      Edit a label\n");
    printf("  delete    Delete a label\n");
    printf("\nRun 'cb label <subcommand> --help' for details.\n");
}

static void help_milestone(void)
{
    printf("Usage: cb milestone <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage milestones.\n\n");
    printf("Subcommands:\n");
    printf("  list      List milestones\n");
    printf("  create    Create a milestone\n");
    printf("  show      Show a milestone\n");
    printf("  edit      Edit a milestone\n");
    printf("  delete    Delete a milestone\n");
    printf("\nRun 'cb milestone <subcommand> --help' for details.\n");
}

static void help_pr(void)
{
    printf("Usage: cb pr <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage pull requests.\n\n");
    printf("Subcommands:\n");
    printf("  list      List pull requests\n");
    printf("  create    Create a pull request\n");
    printf("  show      Show a pull request\n");
    printf("  edit      Edit a pull request\n");
    printf("  merge     Merge a pull request\n");
    printf("  unmerge   Cancel scheduled auto-merge\n");
    printf("  close     Close a pull request (shorthand)\n");
    printf("  reopen    Reopen a pull request (shorthand)\n");
    printf("  files     List changed files\n");
    printf("  commits   List commits\n");
    printf("  diff      Show diff (or patch)\n");
    printf("  review    Manage reviews (list, create, request, unrequest)\n");
    printf("\nRun 'cb pr <subcommand> --help' for details.\n");
}

static void help_commit(void)
{
    printf("Usage: cb commit <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("View commits and commit statuses.\n\n");
    printf("Subcommands:\n");
    printf("  list      List commits\n");
    printf("  show      Show a commit\n");
    printf("  status    Show combined status for a ref\n");
    printf("  diff      Show diff (or patch)\n");
    printf("  compare   Compare two refs\n");
    printf("  note      Manage git notes (show, set, rm)\n");
    printf("\nRun 'cb commit <subcommand> --help' for details.\n");
}

static void help_content(void)
{
    printf("Usage: cb content <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("View and manage repository file contents.\n\n");
    printf("Subcommands:\n");
    printf("  list      List directory contents\n");
    printf("  show      Show file or directory contents\n");
    printf("  create    Create a file\n");
    printf("  update    Update a file\n");
    printf("  delete    Delete a file\n");
    printf("  raw       Get raw file content\n");
    printf("  archive   Download an archive\n");
    printf("\nRun 'cb content <subcommand> --help' for details.\n");
}

static void help_key(void)
{
    printf("Usage: cb key <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage deploy keys.\n\n");
    printf("Subcommands:\n");
    printf("  list      List deploy keys\n");
    printf("  add       Add a deploy key\n");
    printf("  show      Show a deploy key\n");
    printf("  delete    Delete a deploy key\n");
    printf("\nRun 'cb key <subcommand> --help' for details.\n");
}

static void help_collaborator(void)
{
    printf("Usage: cb collaborator <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage collaborators.\n\n");
    printf("Subcommands:\n");
    printf("  list      List collaborators\n");
    printf("  add       Add a collaborator\n");
    printf("  rm        Remove a collaborator\n");
    printf("  perms     Show collaborator permissions\n");
    printf("\nRun 'cb collaborator <subcommand> --help' for details.\n");
}

static void help_fork(void)
{
    printf("Usage: cb fork <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage forks.\n\n");
    printf("Subcommands:\n");
    printf("  list      List forks\n");
    printf("  create    Fork a repository\n");
    printf("\nRun 'cb fork <subcommand> --help' for details.\n");
}

static void help_hook(void)
{
    printf("Usage: cb hook <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage webhooks.\n\n");
    printf("Subcommands:\n");
    printf("  list      List webhooks\n");
    printf("  create    Create a webhook\n");
    printf("  show      Show a webhook\n");
    printf("  edit      Edit a webhook\n");
    printf("  delete    Delete a webhook\n");
    printf("  test      Test a webhook\n");
    printf("\nRun 'cb hook <subcommand> --help' for details.\n");
}

static void help_wiki(void)
{
    printf("Usage: cb wiki <owner/repo> <subcommand> [args] [flags]\n\n");
    printf("Manage wiki pages.\n\n");
    printf("Subcommands:\n");
    printf("  list        List wiki pages\n");
    printf("  create      Create a wiki page\n");
    printf("  show        Show a wiki page\n");
    printf("  edit        Edit a wiki page\n");
    printf("  delete      Delete a wiki page\n");
    printf("  revisions   Show page revisions\n");
    printf("\nRun 'cb wiki <subcommand> --help' for details.\n");
}

/* ===== New output helpers ===== */

/* (unused: parse_owner_repo_arg kept for future use) */

static int require_owner_repo(const char *arg, char *owner, size_t owner_sz,
                              char *repo, size_t repo_sz, ApiClient *api)
{
    char verr[256];
    if (validate_owner_repo(arg, owner, owner_sz, repo, repo_sz,
                            verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        return -1;
    }
    if (owner[0] == '\0') {
        static char cached_login[128] = { 0 };
        if (!cached_login[0] && api) {
            User u;
            memset(&u, 0, sizeof(u));
            if (api_user_get_current(api, &u) == API_OK && u.login)
                snprintf(cached_login, sizeof(cached_login), "%s", u.login);
            user_free(&u);
        }
        if (cached_login[0])
            snprintf(owner, owner_sz, "%s", cached_login);
        else {
            fprintf(stderr, "Error: please specify owner/repo\n");
            return -1;
        }
    }
    return 0;
}

static void print_release(const Release *r, int json)
{
    if (json) {
        JsonValue *obj = json_object_new();
        json_object_set_number(obj, "id", r->id);
        if (r->tag_name)
            json_object_set_string(obj, "tag_name", r->tag_name);
        if (r->name)
            json_object_set_string(obj, "name", r->name);
        if (r->body)
            json_object_set_string(obj, "body", r->body);
        if (r->target_commitish)
            json_object_set_string(obj, "target_commitish", r->target_commitish);
        json_object_set_bool(obj, "draft", r->draft);
        json_object_set_bool(obj, "prerelease", r->prerelease);
        if (r->html_url)
            json_object_set_string(obj, "html_url", r->html_url);
        if (r->created_at)
            json_object_set_string(obj, "created_at", r->created_at);
        if (r->published_at)
            json_object_set_string(obj, "published_at", r->published_at);
        char *s = json_serialize(obj, true);
        printf("%s\n", s);
        free(s);
        json_free(obj);
    } else {
        printf("#%lld  %s  %s%s%s\n", (long long)r->id,
               r->tag_name ? r->tag_name : "",
               r->draft ? " [draft]" : "",
               r->prerelease ? " [prerelease]" : "",
               r->name && r->name[0] ? "" : "");
        if (r->name && r->name[0])
            printf("  %s\n", r->name);
        if (r->body && r->body[0])
            printf("  %s\n", r->body);
        if (r->html_url)
            printf("  %s\n", r->html_url);
    }
}

static void print_release_list(const Release *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set_number(obj, "id", arr[i].id);
            if (arr[i].tag_name)
                json_object_set_string(obj, "tag_name", arr[i].tag_name);
            if (arr[i].name)
                json_object_set_string(obj, "name", arr[i].name);
            json_object_set_bool(obj, "draft", arr[i].draft);
            json_object_set_bool(obj, "prerelease", arr[i].prerelease);
            if (arr[i].published_at)
                json_object_set_string(obj, "published_at", arr[i].published_at);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++) {
            printf("#%-6lld %-20s %s%s\n", (long long)arr[i].id,
                   arr[i].tag_name ? arr[i].tag_name : "",
                   arr[i].draft ? " [draft]" : "",
                   arr[i].prerelease ? " [pre]" : "");
        }
    }
}

static void print_tag_list(const Tag *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            if (arr[i].name)
                json_object_set_string(obj, "name", arr[i].name);
            if (arr[i].id)
                json_object_set_string(obj, "id", arr[i].id);
            if (arr[i].message)
                json_object_set_string(obj, "message", arr[i].message);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("%s\n", arr[i].name ? arr[i].name : "");
    }
}

static void print_branch_list(const Branch *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            if (arr[i].name)
                json_object_set_string(obj, "name", arr[i].name);
            if (arr[i].commit_sha)
                json_object_set_string(obj, "commit_sha", arr[i].commit_sha);
            json_object_set_bool(obj, "protected", arr[i].protected);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++) {
            printf("%-30s %s%s\n", arr[i].name ? arr[i].name : "",
                   arr[i].protected ? "protected" : "",
                   arr[i].commit_sha ? arr[i].commit_sha : "");
        }
    }
}

static void print_issue(const Issue *is, int json)
{
    if (json) {
        JsonValue *obj = json_object_new();
        json_object_set_number(obj, "id", is->id);
        json_object_set_number(obj, "number", is->number);
        if (is->title)
            json_object_set_string(obj, "title", is->title);
        if (is->state)
            json_object_set_string(obj, "state", is->state);
        if (is->html_url)
            json_object_set_string(obj, "html_url", is->html_url);
        if (is->created_at)
            json_object_set_string(obj, "created_at", is->created_at);
        char *s = json_serialize(obj, true);
        printf("%s\n", s);
        free(s);
        json_free(obj);
    } else {
        printf("#%d %s  [%s]\n", is->number,
               is->title ? is->title : "", is->state ? is->state : "");
        if (is->body && is->body[0])
            printf("  %s\n", is->body);
        if (is->html_url)
            printf("  %s\n", is->html_url);
    }
}

static void print_issue_list(const Issue *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set_number(obj, "number", arr[i].number);
            if (arr[i].title)
                json_object_set_string(obj, "title", arr[i].title);
            if (arr[i].state)
                json_object_set_string(obj, "state", arr[i].state);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("#%-6d %-10s %s\n", arr[i].number,
                   arr[i].state ? arr[i].state : "",
                   arr[i].title ? arr[i].title : "");
    }
}

static void print_label_list(const Label *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set_number(obj, "id", arr[i].id);
            if (arr[i].name)
                json_object_set_string(obj, "name", arr[i].name);
            if (arr[i].color)
                json_object_set_string(obj, "color", arr[i].color);
            if (arr[i].description)
                json_object_set_string(obj, "description", arr[i].description);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("#%-6lld %-20s #%s  %s\n", (long long)arr[i].id,
                   arr[i].name ? arr[i].name : "",
                   arr[i].color ? arr[i].color : "",
                   arr[i].description ? arr[i].description : "");
    }
}

static void print_milestone_list(const Milestone *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set_number(obj, "id", arr[i].id);
            if (arr[i].title)
                json_object_set_string(obj, "title", arr[i].title);
            if (arr[i].state)
                json_object_set_string(obj, "state", arr[i].state);
            json_object_set_number(obj, "open_issues", arr[i].open_issues);
            json_object_set_number(obj, "closed_issues", arr[i].closed_issues);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("#%-6lld %-20s [%s] %d open / %d closed\n",
                   (long long)arr[i].id, arr[i].title ? arr[i].title : "",
                   arr[i].state ? arr[i].state : "",
                   arr[i].open_issues, arr[i].closed_issues);
    }
}

static void print_pr(const PullRequest *p, int json)
{
    if (json) {
        JsonValue *obj = json_object_new();
        json_object_set_number(obj, "id", p->id);
        json_object_set_number(obj, "number", p->number);
        if (p->title)
            json_object_set_string(obj, "title", p->title);
        if (p->state)
            json_object_set_string(obj, "state", p->state);
        json_object_set_bool(obj, "draft", p->draft);
        json_object_set_bool(obj, "merged", p->merged);
        if (p->head_ref)
            json_object_set_string(obj, "head", p->head_ref);
        if (p->base_ref)
            json_object_set_string(obj, "base", p->base_ref);
        if (p->html_url)
            json_object_set_string(obj, "html_url", p->html_url);
        char *s = json_serialize(obj, true);
        printf("%s\n", s);
        free(s);
        json_free(obj);
    } else {
        printf("#%d %s  [%s]%s%s  %s -> %s\n", p->number,
               p->title ? p->title : "", p->state ? p->state : "",
               p->draft ? " [draft]" : "",
               p->merged ? " [merged]" : "",
               p->head_ref ? p->head_ref : "?",
               p->base_ref ? p->base_ref : "?");
        if (p->html_url)
            printf("  %s\n", p->html_url);
    }
}

static void print_pr_list(const PullRequest *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set_number(obj, "number", arr[i].number);
            if (arr[i].title)
                json_object_set_string(obj, "title", arr[i].title);
            if (arr[i].state)
                json_object_set_string(obj, "state", arr[i].state);
            json_object_set_bool(obj, "draft", arr[i].draft);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("#%-6d %-10s %s%s\n", arr[i].number,
                   arr[i].state ? arr[i].state : "",
                   arr[i].title ? arr[i].title : "",
                   arr[i].draft ? " [draft]" : "");
    }
}

static void print_commit_list(const Commit *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            if (arr[i].sha)
                json_object_set_string(obj, "sha", arr[i].sha);
            if (arr[i].message)
                json_object_set_string(obj, "message", arr[i].message);
            if (arr[i].author_name)
                json_object_set_string(obj, "author_name", arr[i].author_name);
            if (arr[i].created)
                json_object_set_string(obj, "created", arr[i].created);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("%-12s %s  %s\n",
                   arr[i].sha ? arr[i].sha : "",
                   arr[i].author_name ? arr[i].author_name : "",
                   arr[i].message ? arr[i].message : "");
    }
}

static void print_content_entry_list(const ContentEntry *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            if (arr[i].type)
                json_object_set_string(obj, "type", arr[i].type);
            if (arr[i].name)
                json_object_set_string(obj, "name", arr[i].name);
            if (arr[i].path)
                json_object_set_string(obj, "path", arr[i].path);
            if (arr[i].sha)
                json_object_set_string(obj, "sha", arr[i].sha);
            json_object_set_number(obj, "size", arr[i].size);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("%-10s %s\n",
                   arr[i].type ? arr[i].type : "",
                   arr[i].name ? arr[i].name : "");
    }
}

static void print_deploykey_list(const DeployKey *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set_number(obj, "id", arr[i].id);
            if (arr[i].title)
                json_object_set_string(obj, "title", arr[i].title);
            if (arr[i].fingerprint)
                json_object_set_string(obj, "fingerprint", arr[i].fingerprint);
            json_object_set_bool(obj, "read_only", arr[i].read_only);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("#%-6lld %-20s %s  %s\n", (long long)arr[i].id,
                   arr[i].title ? arr[i].title : "",
                   arr[i].fingerprint ? arr[i].fingerprint : "",
                   arr[i].read_only ? "read-only" : "read-write");
    }
}

static void print_hook_list(const Hook *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set_number(obj, "id", arr[i].id);
            if (arr[i].type)
                json_object_set_string(obj, "type", arr[i].type);
            json_object_set_bool(obj, "active", arr[i].active);
            if (arr[i].url)
                json_object_set_string(obj, "url", arr[i].url);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("#%-6lld %-10s %s  %s\n", (long long)arr[i].id,
                   arr[i].type ? arr[i].type : "",
                   arr[i].active ? "active" : "inactive",
                   arr[i].url ? arr[i].url : "");
    }
}

static void print_wikipage_list(const WikiPage *arr, size_t count, int json)
{
    if (json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            if (arr[i].title)
                json_object_set_string(obj, "title", arr[i].title);
            if (arr[i].html_url)
                json_object_set_string(obj, "html_url", arr[i].html_url);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("%s\n", arr[i].title ? arr[i].title : "");
    }
}

/* ===== Release command handlers ===== */

static int cmd_release_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_release_list();
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, RELEASE_LIST_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        help_release_list();
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int draft = 0, prerelease = 0, limit = 0;
    int idx;
    idx = find_flag_idx(RELEASE_LIST_FLAGS, "--draft");
    if (fb[idx])
        draft = 1;
    idx = find_flag_idx(RELEASE_LIST_FLAGS, "--pre-release");
    if (fb[idx])
        prerelease = 1;
    idx = find_flag_idx(RELEASE_LIST_FLAGS, "--limit");
    if (fv[idx])
        limit = atoi(fv[idx]);
    Release *releases;
    size_t count;
    int rc = api_release_list(api, owner, repo, draft, prerelease, NULL, limit,
                              &releases, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    print_release_list(releases, count, gf->json);
    release_array_free(releases, count);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_release_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_release_create();
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, RELEASE_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        help_release_create();
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int idx;
    idx = find_flag_idx(RELEASE_CREATE_FLAGS, "--tag");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --tag is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    CreateReleaseOpts opts = { 0 };
    opts.tag_name = fv[idx];
    idx = find_flag_idx(RELEASE_CREATE_FLAGS, "--name");
    if (fv[idx])
        opts.name = fv[idx];
    idx = find_flag_idx(RELEASE_CREATE_FLAGS, "--body");
    if (fv[idx])
        opts.body = fv[idx];
    idx = find_flag_idx(RELEASE_CREATE_FLAGS, "--target");
    if (fv[idx])
        opts.target_commitish = fv[idx];
    idx = find_flag_idx(RELEASE_CREATE_FLAGS, "--draft");
    if (fb[idx]) {
        opts.draft_set = 1;
        opts.draft_val = 1;
    }
    idx = find_flag_idx(RELEASE_CREATE_FLAGS, "--prerelease");
    if (fb[idx]) {
        opts.prerelease_set = 1;
        opts.prerelease_val = 1;
    }
    idx = find_flag_idx(RELEASE_CREATE_FLAGS, "--hide-archive-links");
    if (fb[idx]) {
        opts.hide_archive_links_set = 1;
        opts.hide_archive_links_val = 1;
    }
    Release r;
    int rc = api_release_create(api, owner, repo, &opts, &r);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    print_release(&r, gf->json);
    release_free(&r);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_release_show(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb release <owner/repo> show <id>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: release show requires owner/repo and id\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int64_t id = atol(argv[1]);
    Release r;
    int rc = api_release_get(api, owner, repo, id, &r);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_release(&r, gf->json);
    release_free(&r);
    return CLI_OK;
}

static int cmd_release_latest(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb release <owner/repo> latest\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: release latest requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    Release r;
    int rc = api_release_get_latest(api, owner, repo, &r);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_release(&r, gf->json);
    release_free(&r);
    return CLI_OK;
}

static int cmd_release_edit(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_release_edit();
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, RELEASE_EDIT_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 2) {
        help_release_edit();
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int64_t id = atol(positional[1]);
    EditReleaseOpts opts = { 0 };
    int idx;
    idx = find_flag_idx(RELEASE_EDIT_FLAGS, "--tag");
    if (fv[idx]) {
        opts.tag_name_set = 1;
        opts.tag_name = fv[idx];
    }
    idx = find_flag_idx(RELEASE_EDIT_FLAGS, "--name");
    if (fv[idx]) {
        opts.name_set = 1;
        opts.name = fv[idx];
    }
    idx = find_flag_idx(RELEASE_EDIT_FLAGS, "--body");
    if (fv[idx]) {
        opts.body_set = 1;
        opts.body = fv[idx];
    }
    idx = find_flag_idx(RELEASE_EDIT_FLAGS, "--target");
    if (fv[idx]) {
        opts.target_commitish_set = 1;
        opts.target_commitish = fv[idx];
    }
    idx = find_flag_idx(RELEASE_EDIT_FLAGS, "--draft");
    if (fb[idx]) {
        opts.draft_set = 1;
        opts.draft_val = 1;
    }
    idx = find_flag_idx(RELEASE_EDIT_FLAGS, "--no-draft");
    if (fb[idx]) {
        opts.draft_set = 1;
        opts.draft_val = 0;
    }
    idx = find_flag_idx(RELEASE_EDIT_FLAGS, "--prerelease");
    if (fb[idx]) {
        opts.prerelease_set = 1;
        opts.prerelease_val = 1;
    }
    idx = find_flag_idx(RELEASE_EDIT_FLAGS, "--no-prerelease");
    if (fb[idx]) {
        opts.prerelease_set = 1;
        opts.prerelease_val = 0;
    }
    idx = find_flag_idx(RELEASE_EDIT_FLAGS, "--hide-archive-links");
    if (fb[idx]) {
        opts.hide_archive_links_set = 1;
        opts.hide_archive_links_val = 1;
    }
    idx = find_flag_idx(RELEASE_EDIT_FLAGS, "--no-hide-archive-links");
    if (fb[idx]) {
        opts.hide_archive_links_set = 1;
        opts.hide_archive_links_val = 0;
    }
    Release r;
    int rc = api_release_edit(api, owner, repo, id, &opts, &r);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Updated release #%lld\n", (long long)id);
    release_free(&r);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_release_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb release <owner/repo> delete <id> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: release delete requires owner/repo and id\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int64_t id = atol(argv[1]);
    if (!gf->yes && !confirm("Delete this release?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_release_delete(api, owner, repo, id);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted release #%lld\n", (long long)id);
    return CLI_OK;
}

static int cmd_release_by_tag(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb release <owner/repo> by-tag <tag>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: release by-tag requires owner/repo and tag\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    Release r;
    int rc = api_release_get_by_tag(api, owner, repo, argv[1], &r);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_release(&r, gf->json);
    release_free(&r);
    return CLI_OK;
}

static int cmd_release_delete_by_tag(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb release <owner/repo> delete-tag <tag> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: release delete-tag requires owner/repo and tag\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    if (!gf->yes && !confirm("Delete this release by tag?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_release_delete_by_tag(api, owner, repo, argv[1]);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted release for tag %s\n", argv[1]);
    return CLI_OK;
}

static int cmd_release_asset_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb release <owner/repo> asset list <release-id>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: asset list requires owner/repo and release-id\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int64_t release_id = atol(argv[1]);
    Attachment *assets;
    size_t count;
    int rc = api_release_asset_list(api, owner, repo, release_id, &assets, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (gf->json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            json_object_set_number(obj, "id", assets[i].id);
            if (assets[i].name)
                json_object_set_string(obj, "name", assets[i].name);
            json_object_set_number(obj, "size", assets[i].size);
            json_object_set_number(obj, "download_count", assets[i].download_count);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("#%-6lld %-30s %lld bytes  %lld downloads\n",
                   (long long)assets[i].id,
                   assets[i].name ? assets[i].name : "",
                   (long long)assets[i].size,
                   (long long)assets[i].download_count);
    }
    attachment_array_free(assets, count);
    return CLI_OK;
}

static int cmd_release_asset_edit(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb release <owner/repo> asset edit <release-id> <asset-id> --name <name>\n");
            return CLI_OK;
        }
    }
    if (argc < 3) {
        fprintf(stderr, "Error: asset edit requires owner/repo, release-id, and asset-id\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int64_t release_id = atol(argv[1]);
    int64_t asset_id = atol(argv[2]);

    static FlagDef flags[] = {
        { "--name", NULL, 1 },
        { NULL, NULL, 0 },
    };
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc - 3, argv + 3, flags, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    int name_idx = find_flag_idx(flags, "--name");
    if (!fv[name_idx]) {
        fprintf(stderr, "Error: --name is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }

    Attachment out;
    memset(&out, 0, sizeof(out));
    int rc = api_release_asset_edit(api, owner, repo, release_id, asset_id,
                                    fv[name_idx], &out);
    free(positional);
    free(fv);
    free(fb);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Renamed asset #%lld to %s\n", (long long)asset_id, out.name ? out.name : "");
    attachment_free(&out);
    return CLI_OK;
}

static int cmd_release_asset_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb release <owner/repo> asset delete <release-id> <asset-id> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 3) {
        fprintf(stderr, "Error: asset delete requires owner/repo, release-id, and asset-id\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int64_t release_id = atol(argv[1]);
    int64_t asset_id = atol(argv[2]);
    if (!gf->yes && !confirm("Delete this asset?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_release_asset_delete(api, owner, repo, release_id, asset_id);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted asset #%lld\n", (long long)asset_id);
    return CLI_OK;
}

/* ===== Release dispatch ===== */

static int cmd_release(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_release();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;

    if (is_help_arg(sub)) {
        help_release();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_release_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "create") == 0)
        return cmd_release_create(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "show") == 0)
        return cmd_release_show(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "latest") == 0)
        return cmd_release_latest(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "edit") == 0)
        return cmd_release_edit(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete") == 0)
        return cmd_release_delete(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "by-tag") == 0)
        return cmd_release_by_tag(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete-tag") == 0)
        return cmd_release_delete_by_tag(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "asset") == 0) {
        if (rest_argc < 1) {
            help_release_asset();
            return CLI_USAGE;
        }
        const char *asub = rest_argv[0];
        int a_argc = rest_argc - 1;
        char **a_argv = rest_argv + 1;
        if (is_help_arg(asub)) {
            help_release_asset();
            return CLI_OK;
        }
        if (strcmp(asub, "list") == 0)
            return cmd_release_asset_list(a_argc, a_argv, api, gf);
        if (strcmp(asub, "edit") == 0)
            return cmd_release_asset_edit(a_argc, a_argv, api, gf);
        if (strcmp(asub, "delete") == 0)
            return cmd_release_asset_delete(a_argc, a_argv, api, gf);
        fprintf(stderr, "Error: unknown asset subcommand '%s'\n", asub);
        help_release_asset();
        return CLI_USAGE;
    }
    fprintf(stderr, "Error: unknown release subcommand '%s'\n", sub);
    help_release();
    return CLI_USAGE;
}

/* ===== Tag command handlers ===== */

static int cmd_tag_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb tag <owner/repo> list [--limit N]\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: tag list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    Tag *tags;
    size_t count;
    int rc = api_tag_list(api, owner, repo, 0, &tags, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_tag_list(tags, count, gf->json);
    tag_array_free(tags, count);
    return CLI_OK;
}

static int cmd_tag_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb tag <owner/repo> create --tag <tag> [--message <msg>] [--target <ref>]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, TAG_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: tag create requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int idx;
    idx = find_flag_idx(TAG_CREATE_FLAGS, "--tag");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --tag is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    CreateTagOpts opts = { 0 };
    opts.tag_name = fv[idx];
    idx = find_flag_idx(TAG_CREATE_FLAGS, "--message");
    if (fv[idx])
        opts.message = fv[idx];
    idx = find_flag_idx(TAG_CREATE_FLAGS, "--target");
    if (fv[idx])
        opts.target = fv[idx];
    Tag t;
    int rc = api_tag_create(api, owner, repo, &opts, &t);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (gf->json) {
        JsonValue *obj = json_object_new();
        if (t.name)
            json_object_set_string(obj, "name", t.name);
        if (t.id)
            json_object_set_string(obj, "id", t.id);
        char *s = json_serialize(obj, true);
        printf("%s\n", s);
        free(s);
        json_free(obj);
    } else {
        printf("Created tag %s\n", t.name ? t.name : "");
    }
    tag_free(&t);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_tag_show(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb tag <owner/repo> show <tag>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: tag show requires owner/repo and tag name\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    Tag t;
    int rc = api_tag_get(api, owner, repo, argv[1], &t);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (gf->json) {
        JsonValue *obj = json_object_new();
        if (t.name)
            json_object_set_string(obj, "name", t.name);
        if (t.message)
            json_object_set_string(obj, "message", t.message);
        if (t.id)
            json_object_set_string(obj, "id", t.id);
        char *s = json_serialize(obj, true);
        printf("%s\n", s);
        free(s);
        json_free(obj);
    } else {
        printf("%s\n", t.name ? t.name : "");
        if (t.message && t.message[0])
            printf("  %s\n", t.message);
    }
    tag_free(&t);
    return CLI_OK;
}

static int cmd_tag_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb tag <owner/repo> delete <tag> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: tag delete requires owner/repo and tag name\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    if (!gf->yes && !confirm("Delete this tag?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_tag_delete(api, owner, repo, argv[1]);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted tag %s\n", argv[1]);
    return CLI_OK;
}

static int cmd_tag(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_tag();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_tag();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_tag_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "create") == 0)
        return cmd_tag_create(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "show") == 0)
        return cmd_tag_show(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete") == 0)
        return cmd_tag_delete(rest_argc, rest_argv, api, gf);
    fprintf(stderr, "Error: unknown tag subcommand '%s'\n", sub);
    help_tag();
    return CLI_USAGE;
}

/* ===== Branch command handlers ===== */

static int cmd_branch_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb branch <owner/repo> list\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: branch list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    Branch *branches;
    size_t count;
    int rc = api_branch_list(api, owner, repo, 0, &branches, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_branch_list(branches, count, gf->json);
    branch_array_free(branches, count);
    return CLI_OK;
}

static int cmd_branch_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb branch <owner/repo> create --name <name> [--from <ref>]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, BRANCH_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: branch create requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int idx;
    idx = find_flag_idx(BRANCH_CREATE_FLAGS, "--name");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --name is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    CreateBranchOpts opts = { 0 };
    opts.new_branch_name = fv[idx];
    idx = find_flag_idx(BRANCH_CREATE_FLAGS, "--from");
    if (fv[idx])
        opts.old_ref_name = fv[idx];
    Branch b;
    int rc = api_branch_create(api, owner, repo, &opts, &b);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (gf->json) {
        JsonValue *obj = json_object_new();
        if (b.name)
            json_object_set_string(obj, "name", b.name);
        char *s = json_serialize(obj, true);
        printf("%s\n", s);
        free(s);
        json_free(obj);
    } else {
        printf("Created branch %s\n", b.name ? b.name : "");
    }
    branch_free(&b);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_branch_show(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb branch <owner/repo> show <branch>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: branch show requires owner/repo and branch name\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    Branch b;
    int rc = api_branch_get(api, owner, repo, argv[1], &b);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (gf->json) {
        JsonValue *obj = json_object_new();
        if (b.name)
            json_object_set_string(obj, "name", b.name);
        if (b.commit_sha)
            json_object_set_string(obj, "commit_sha", b.commit_sha);
        json_object_set_bool(obj, "protected", b.protected);
        char *s = json_serialize(obj, true);
        printf("%s\n", s);
        free(s);
        json_free(obj);
    } else {
        printf("%s  %s%s\n", b.name ? b.name : "",
               b.protected ? "protected  " : "",
               b.commit_sha ? b.commit_sha : "");
    }
    branch_free(&b);
    return CLI_OK;
}

static int cmd_branch_rename(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb branch <owner/repo> rename <branch> --name <new-name>\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, BRANCH_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 2) {
        fprintf(stderr, "Error: branch rename requires owner/repo and branch name\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int idx = find_flag_idx(BRANCH_CREATE_FLAGS, "--name");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --name is required for rename\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    Branch b;
    int rc = api_branch_rename(api, owner, repo, positional[1], fv[idx], &b);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Renamed branch to %s\n", b.name ? b.name : fv[idx]);
    branch_free(&b);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_branch_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb branch <owner/repo> delete <branch> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: branch delete requires owner/repo and branch name\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    if (!gf->yes && !confirm("Delete this branch?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_branch_delete(api, owner, repo, argv[1]);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted branch %s\n", argv[1]);
    return CLI_OK;
}

static int cmd_branch(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_branch();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_branch();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_branch_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "create") == 0)
        return cmd_branch_create(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "show") == 0)
        return cmd_branch_show(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "rename") == 0)
        return cmd_branch_rename(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete") == 0)
        return cmd_branch_delete(rest_argc, rest_argv, api, gf);
    fprintf(stderr, "Error: unknown branch subcommand '%s'\n", sub);
    help_branch();
    return CLI_USAGE;
}

/* ===== Issue command handlers ===== */

static int cmd_issue_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb issue <owner/repo> list [--state open|closed|all] [--labels l1,l2] [--limit N]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, ISSUE_LIST_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: issue list requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    const char *state = NULL, *labels = NULL, *type = NULL;
    int limit = 0;
    int idx;
    idx = find_flag_idx(ISSUE_LIST_FLAGS, "--state");
    if (fv[idx])
        state = fv[idx];
    idx = find_flag_idx(ISSUE_LIST_FLAGS, "--labels");
    if (fv[idx])
        labels = fv[idx];
    idx = find_flag_idx(ISSUE_LIST_FLAGS, "--type");
    if (fv[idx])
        type = fv[idx];
    idx = find_flag_idx(ISSUE_LIST_FLAGS, "--limit");
    if (fv[idx])
        limit = atoi(fv[idx]);
    Issue *issues;
    size_t count;
    int rc = api_issue_list(api, owner, repo, state, labels, type, limit,
                            &issues, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    print_issue_list(issues, count, gf->json);
    issue_array_free(issues, count);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_issue_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb issue <owner/repo> create --title <title> [--body <body>] [--label <id>]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, ISSUE_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: issue create requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int idx;
    idx = find_flag_idx(ISSUE_CREATE_FLAGS, "--title");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --title is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    CreateIssueOpts opts = { 0 };
    opts.title = fv[idx];
    idx = find_flag_idx(ISSUE_CREATE_FLAGS, "--body");
    if (fv[idx])
        opts.body = fv[idx];
    idx = find_flag_idx(ISSUE_CREATE_FLAGS, "--assignee");
    if (fv[idx])
        opts.assignee = fv[idx];
    idx = find_flag_idx(ISSUE_CREATE_FLAGS, "--milestone");
    if (fv[idx])
        opts.milestone = atol(fv[idx]);
    Issue is;
    int rc = api_issue_create(api, owner, repo, &opts, &is);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    print_issue(&is, gf->json);
    issue_free(&is);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_issue_show(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb issue <owner/repo> show <number>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: issue show requires owner/repo and issue number\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    Issue is;
    int rc = api_issue_get(api, owner, repo, number, &is);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_issue(&is, gf->json);
    issue_free(&is);
    return CLI_OK;
}

static int cmd_issue_edit(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb issue <owner/repo> edit <number> [--title <title>] [--body <body>] [--state open|closed]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, ISSUE_EDIT_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 2) {
        fprintf(stderr, "Error: issue edit requires owner/repo and issue number\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int number = atoi(positional[1]);
    EditIssueOpts opts = { 0 };
    int idx;
    idx = find_flag_idx(ISSUE_EDIT_FLAGS, "--title");
    if (fv[idx]) {
        opts.title_set = 1;
        opts.title = fv[idx];
    }
    idx = find_flag_idx(ISSUE_EDIT_FLAGS, "--body");
    if (fv[idx]) {
        opts.body_set = 1;
        opts.body = fv[idx];
    }
    idx = find_flag_idx(ISSUE_EDIT_FLAGS, "--state");
    if (fv[idx]) {
        opts.state_set = 1;
        opts.state = fv[idx];
    }
    idx = find_flag_idx(ISSUE_EDIT_FLAGS, "--milestone");
    if (fv[idx]) {
        opts.milestone_set = 1;
        opts.milestone = atol(fv[idx]);
    }
    Issue is;
    int rc = api_issue_edit(api, owner, repo, number, &opts, &is);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Updated issue #%d\n", number);
    issue_free(&is);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_issue_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb issue <owner/repo> delete <number> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: issue delete requires owner/repo and issue number\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    if (!gf->yes && !confirm("Delete this issue?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_issue_delete(api, owner, repo, number);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted issue #%d\n", number);
    return CLI_OK;
}

static int cmd_issue_close(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb issue <owner/repo> close <number>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: issue close requires owner/repo and issue number\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    EditIssueOpts opts = { 0 };
    opts.state_set = 1;
    opts.state = "closed";
    Issue is;
    int rc = api_issue_edit(api, owner, repo, number, &opts, &is);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Closed issue #%d\n", number);
    issue_free(&is);
    return CLI_OK;
}

static int cmd_issue_reopen(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb issue <owner/repo> reopen <number>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: issue reopen requires owner/repo and issue number\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    EditIssueOpts opts = { 0 };
    opts.state_set = 1;
    opts.state = "open";
    Issue is;
    int rc = api_issue_edit(api, owner, repo, number, &opts, &is);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Reopened issue #%d\n", number);
    issue_free(&is);
    return CLI_OK;
}

static int cmd_issue_comment(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb issue <owner/repo> comment <number> --body <text>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: issue comment requires owner/repo and issue number\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    const char *body = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--body") == 0 && i + 1 < argc)
            body = argv[++i];
    }
    if (!body) {
        fprintf(stderr, "Error: --body is required\n");
        return CLI_USAGE;
    }
    int rc = api_issue_comment_create(api, owner, repo, number, body);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Comment added to issue #%d\n", number);
    return CLI_OK;
}

static int cmd_issue_label_add(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb issue <owner/repo> label add <number> <label_id> [<label_id>...]\n");
            return CLI_OK;
        }
    }
    if (argc < 3) {
        fprintf(stderr, "Error: issue label add requires owner/repo, issue number, and label IDs\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    size_t label_count = (size_t)(argc - 2);
    int64_t *labels = malloc(label_count * sizeof(int64_t));
    for (int i = 0; i < argc - 2; i++)
        labels[i] = atol(argv[i + 2]);
    int rc = api_issue_label_add(api, owner, repo, number, labels, label_count);
    free(labels);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Added labels to issue #%d\n", number);
    return CLI_OK;
}

static int cmd_issue_label_clear(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb issue <owner/repo> label clear <number>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: issue label clear requires owner/repo and issue number\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    int rc = api_issue_label_clear(api, owner, repo, number);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Cleared labels on issue #%d\n", number);
    return CLI_OK;
}

static int cmd_issue(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_issue();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_issue();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_issue_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "create") == 0)
        return cmd_issue_create(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "show") == 0)
        return cmd_issue_show(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "edit") == 0)
        return cmd_issue_edit(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete") == 0)
        return cmd_issue_delete(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "close") == 0)
        return cmd_issue_close(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "reopen") == 0)
        return cmd_issue_reopen(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "comment") == 0)
        return cmd_issue_comment(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "pin") == 0) {
        fprintf(stderr, "Error: issue pin not yet implemented\n");
        return CLI_ERR;
    }
    if (strcmp(sub, "unpin") == 0) {
        fprintf(stderr, "Error: issue unpin not yet implemented\n");
        return CLI_ERR;
    }
    if (strcmp(sub, "deadline") == 0) {
        fprintf(stderr, "Error: issue deadline not yet implemented\n");
        return CLI_ERR;
    }
    if (strcmp(sub, "label") == 0) {
        if (rest_argc < 1) {
            printf("Usage: cb issue <owner/repo> label <add|set|rm|clear> ...\n");
            return CLI_USAGE;
        }
        const char *lsub = rest_argv[0];
        int l_argc = rest_argc - 1;
        char **l_argv = rest_argv + 1;
        if (is_help_arg(lsub)) {
            printf("Usage: cb issue <owner/repo> label <add|set|rm|clear> ...\n");
            return CLI_OK;
        }
        if (strcmp(lsub, "add") == 0)
            return cmd_issue_label_add(l_argc, l_argv, api, gf);
        if (strcmp(lsub, "clear") == 0)
            return cmd_issue_label_clear(l_argc, l_argv, api, gf);
        fprintf(stderr, "Error: unknown issue label subcommand '%s'\n", lsub);
        return CLI_USAGE;
    }
    fprintf(stderr, "Error: unknown issue subcommand '%s'\n", sub);
    help_issue();
    return CLI_USAGE;
}

/* ===== Label command handlers ===== */

static int cmd_label_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb label <owner/repo> list\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: label list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    Label *labels;
    size_t count;
    int rc = api_label_list(api, owner, repo, &labels, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_label_list(labels, count, gf->json);
    label_array_free(labels, count);
    return CLI_OK;
}

static int cmd_label_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb label <owner/repo> create --name <name> --color <hex> [--description <desc>]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, LABEL_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: label create requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int idx;
    idx = find_flag_idx(LABEL_CREATE_FLAGS, "--name");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --name is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    idx = find_flag_idx(LABEL_CREATE_FLAGS, "--color");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --color is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    CreateLabelOpts opts = { 0 };
    idx = find_flag_idx(LABEL_CREATE_FLAGS, "--name");
    opts.name = fv[idx];
    idx = find_flag_idx(LABEL_CREATE_FLAGS, "--color");
    opts.color = fv[idx];
    idx = find_flag_idx(LABEL_CREATE_FLAGS, "--description");
    if (fv[idx])
        opts.description = fv[idx];
    Label l;
    int rc = api_label_create(api, owner, repo, &opts, &l);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Created label #%lld %s\n", (long long)l.id, l.name ? l.name : "");
    label_free(&l);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_label_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb label <owner/repo> delete <id> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: label delete requires owner/repo and label id\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int64_t id = atol(argv[1]);
    if (!gf->yes && !confirm("Delete this label?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_label_delete(api, owner, repo, id);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted label #%lld\n", (long long)id);
    return CLI_OK;
}

static int cmd_label(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_label();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_label();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_label_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "create") == 0)
        return cmd_label_create(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete") == 0)
        return cmd_label_delete(rest_argc, rest_argv, api, gf);
    fprintf(stderr, "Error: unknown label subcommand '%s'\n", sub);
    help_label();
    return CLI_USAGE;
}

/* ===== Milestone command handlers ===== */

static int cmd_milestone_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb milestone <owner/repo> list [--state open|closed|all]\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: milestone list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    const char *state = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--state") == 0 && i + 1 < argc)
            state = argv[++i];
    }
    Milestone *milestones;
    size_t count;
    int rc = api_milestone_list(api, owner, repo, state, &milestones, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_milestone_list(milestones, count, gf->json);
    milestone_array_free(milestones, count);
    return CLI_OK;
}

static int cmd_milestone_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb milestone <owner/repo> create --title <title> [--description <desc>] [--due <date>]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, MILESTONE_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: milestone create requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int idx;
    idx = find_flag_idx(MILESTONE_CREATE_FLAGS, "--title");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --title is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    CreateMilestoneOpts opts = { 0 };
    opts.title = fv[idx];
    idx = find_flag_idx(MILESTONE_CREATE_FLAGS, "--description");
    if (fv[idx])
        opts.description = fv[idx];
    idx = find_flag_idx(MILESTONE_CREATE_FLAGS, "--state");
    if (fv[idx])
        opts.state = fv[idx];
    idx = find_flag_idx(MILESTONE_CREATE_FLAGS, "--due");
    if (fv[idx])
        opts.due_on = fv[idx];
    Milestone m;
    int rc = api_milestone_create(api, owner, repo, &opts, &m);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Created milestone #%lld %s\n", (long long)m.id, m.title ? m.title : "");
    milestone_free(&m);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_milestone_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb milestone <owner/repo> delete <id> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: milestone delete requires owner/repo and milestone id\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int64_t id = atol(argv[1]);
    if (!gf->yes && !confirm("Delete this milestone?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_milestone_delete(api, owner, repo, id);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted milestone #%lld\n", (long long)id);
    return CLI_OK;
}

static int cmd_milestone(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_milestone();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_milestone();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_milestone_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "create") == 0)
        return cmd_milestone_create(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete") == 0)
        return cmd_milestone_delete(rest_argc, rest_argv, api, gf);
    fprintf(stderr, "Error: unknown milestone subcommand '%s'\n", sub);
    help_milestone();
    return CLI_USAGE;
}

/* ===== PR command handlers ===== */

static int cmd_pr_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb pr <owner/repo> list [--state open|closed|all] [--limit N]\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: pr list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    const char *state = NULL;
    int limit = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--state") == 0 && i + 1 < argc)
            state = argv[++i];
        else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
            limit = atoi(argv[++i]);
    }
    PullRequest *prs;
    size_t count;
    int rc = api_pr_list(api, owner, repo, state, limit, &prs, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_pr_list(prs, count, gf->json);
    pullrequest_array_free(prs, count);
    return CLI_OK;
}

static int cmd_pr_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb pr <owner/repo> create --title <title> --head <branch> [--base <branch>] [--body <body>]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, PR_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: pr create requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int idx;
    idx = find_flag_idx(PR_CREATE_FLAGS, "--title");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --title is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    idx = find_flag_idx(PR_CREATE_FLAGS, "--head");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --head is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    CreatePullRequestOpts opts = { 0 };
    idx = find_flag_idx(PR_CREATE_FLAGS, "--title");
    opts.title = fv[idx];
    idx = find_flag_idx(PR_CREATE_FLAGS, "--head");
    opts.head = fv[idx];
    idx = find_flag_idx(PR_CREATE_FLAGS, "--base");
    if (fv[idx])
        opts.base = fv[idx];
    idx = find_flag_idx(PR_CREATE_FLAGS, "--body");
    if (fv[idx])
        opts.body = fv[idx];
    PullRequest p;
    int rc = api_pr_create(api, owner, repo, &opts, &p);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    print_pr(&p, gf->json);
    pullrequest_free(&p);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_pr_show(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb pr <owner/repo> show <number>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: pr show requires owner/repo and PR number\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    PullRequest p;
    int rc = api_pr_get(api, owner, repo, number, &p);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_pr(&p, gf->json);
    pullrequest_free(&p);
    return CLI_OK;
}

static int cmd_pr_close(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb pr <owner/repo> close <number>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: pr close requires owner/repo and PR number\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    EditPullRequestOpts opts = { 0 };
    opts.state_set = 1;
    opts.state = "closed";
    PullRequest p;
    int rc = api_pr_edit(api, owner, repo, number, &opts, &p);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Closed PR #%d\n", number);
    pullrequest_free(&p);
    return CLI_OK;
}

static int cmd_pr_reopen(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb pr <owner/repo> reopen <number>\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: pr reopen requires owner/repo and PR number\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    EditPullRequestOpts opts = { 0 };
    opts.state_set = 1;
    opts.state = "open";
    PullRequest p;
    int rc = api_pr_edit(api, owner, repo, number, &opts, &p);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Reopened PR #%d\n", number);
    pullrequest_free(&p);
    return CLI_OK;
}

static int cmd_pr_merge(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb pr <owner/repo> merge <number> [--style merge|rebase|squash|rebase-merge] [--delete-branch] [--auto]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: pr merge requires owner/repo and PR number\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int number = atoi(argv[1]);
    MergePullRequestOpts opts = { 0 };
    opts.do_style = "merge";
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--style") == 0 && i + 1 < argc)
            opts.do_style = argv[++i];
        else if (strcmp(argv[i], "--delete-branch") == 0)
            opts.delete_branch_after_merge = 1;
        else if (strcmp(argv[i], "--auto") == 0)
            opts.merge_when_checks_succeed = 1;
    }
    if (!gf->yes && !confirm("Merge this pull request?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_pr_merge(api, owner, repo, number, &opts);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Merged PR #%d\n", number);
    return CLI_OK;
}

static int cmd_pr(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_pr();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_pr();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_pr_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "create") == 0)
        return cmd_pr_create(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "show") == 0)
        return cmd_pr_show(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "close") == 0)
        return cmd_pr_close(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "reopen") == 0)
        return cmd_pr_reopen(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "merge") == 0)
        return cmd_pr_merge(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "edit") == 0 || strcmp(sub, "unmerge") == 0 ||
        strcmp(sub, "files") == 0 || strcmp(sub, "commits") == 0 ||
        strcmp(sub, "diff") == 0 || strcmp(sub, "review") == 0) {
        fprintf(stderr, "Error: pr %s not yet implemented\n", sub);
        return CLI_ERR;
    }
    fprintf(stderr, "Error: unknown pr subcommand '%s'\n", sub);
    help_pr();
    return CLI_USAGE;
}

/* ===== Commit command handlers ===== */

static int cmd_commit_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb commit <owner/repo> list [--sha <ref>] [--path <path>] [--limit N]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, COMMIT_LIST_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: commit list requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    const char *sha = NULL, *path = NULL;
    int limit = 0;
    int idx;
    idx = find_flag_idx(COMMIT_LIST_FLAGS, "--sha");
    if (fv[idx])
        sha = fv[idx];
    idx = find_flag_idx(COMMIT_LIST_FLAGS, "--path");
    if (fv[idx])
        path = fv[idx];
    idx = find_flag_idx(COMMIT_LIST_FLAGS, "--limit");
    if (fv[idx])
        limit = atoi(fv[idx]);
    Commit *commits;
    size_t count;
    int rc = api_commit_list(api, owner, repo, sha, path, limit, &commits, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    print_commit_list(commits, count, gf->json);
    commit_array_free(commits, count);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_commit(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_commit();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_commit();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_commit_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "show") == 0 || strcmp(sub, "status") == 0 ||
        strcmp(sub, "diff") == 0 || strcmp(sub, "compare") == 0 ||
        strcmp(sub, "note") == 0) {
        fprintf(stderr, "Error: commit %s not yet implemented\n", sub);
        return CLI_ERR;
    }
    fprintf(stderr, "Error: unknown commit subcommand '%s'\n", sub);
    help_commit();
    return CLI_USAGE;
}

/* ===== Content command handlers ===== */

static int cmd_content_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb content <owner/repo> list [--ref <ref>]\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: content list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    const char *ref = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ref") == 0 && i + 1 < argc)
            ref = argv[++i];
    }
    ContentEntry *entries;
    size_t count;
    int rc = api_content_list(api, owner, repo, ref, &entries, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_content_entry_list(entries, count, gf->json);
    content_entry_array_free(entries, count);
    return CLI_OK;
}

static int cmd_content(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_content();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_content();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_content_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "show") == 0 || strcmp(sub, "create") == 0 ||
        strcmp(sub, "update") == 0 || strcmp(sub, "delete") == 0 ||
        strcmp(sub, "raw") == 0 || strcmp(sub, "archive") == 0) {
        fprintf(stderr, "Error: content %s not yet implemented\n", sub);
        return CLI_ERR;
    }
    fprintf(stderr, "Error: unknown content subcommand '%s'\n", sub);
    help_content();
    return CLI_USAGE;
}

/* ===== Key command handlers ===== */

static int cmd_key_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb key <owner/repo> list\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: key list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    DeployKey *keys;
    size_t count;
    int rc = api_key_list(api, owner, repo, &keys, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_deploykey_list(keys, count, gf->json);
    deploykey_array_free(keys, count);
    return CLI_OK;
}

static int cmd_key_add(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb key <owner/repo> add --title <title> --key <key> [--read-only]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, KEY_ADD_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: key add requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int idx;
    idx = find_flag_idx(KEY_ADD_FLAGS, "--title");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --title is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    idx = find_flag_idx(KEY_ADD_FLAGS, "--key");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --key is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    CreateKeyOpts opts = { 0 };
    idx = find_flag_idx(KEY_ADD_FLAGS, "--title");
    opts.title = fv[idx];
    idx = find_flag_idx(KEY_ADD_FLAGS, "--key");
    opts.key = fv[idx];
    idx = find_flag_idx(KEY_ADD_FLAGS, "--read-only");
    opts.read_only = fb[idx];
    DeployKey k;
    int rc = api_key_add(api, owner, repo, &opts, &k);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Added deploy key #%lld %s\n", (long long)k.id, k.title ? k.title : "");
    deploykey_free(&k);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_key_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb key <owner/repo> delete <id> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: key delete requires owner/repo and key id\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int64_t id = atol(argv[1]);
    if (!gf->yes && !confirm("Delete this deploy key?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_key_delete(api, owner, repo, id);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted deploy key #%lld\n", (long long)id);
    return CLI_OK;
}

static int cmd_key(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_key();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_key();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_key_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "add") == 0)
        return cmd_key_add(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete") == 0)
        return cmd_key_delete(rest_argc, rest_argv, api, gf);
    fprintf(stderr, "Error: unknown key subcommand '%s'\n", sub);
    help_key();
    return CLI_USAGE;
}

/* ===== Collaborator command handlers ===== */

static int cmd_collaborator_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb collaborator <owner/repo> list\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: collaborator list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    User *users;
    size_t count;
    int rc = api_collaborator_list(api, owner, repo, &users, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (gf->json) {
        JsonValue *jarr = json_array_new();
        for (size_t i = 0; i < count; i++) {
            JsonValue *obj = json_object_new();
            if (users[i].login)
                json_object_set_string(obj, "login", users[i].login);
            json_object_set_number(obj, "id", users[i].id);
            json_array_push(jarr, obj);
        }
        char *s = json_serialize(jarr, true);
        printf("%s\n", s);
        free(s);
        json_free(jarr);
    } else {
        for (size_t i = 0; i < count; i++)
            printf("%s\n", users[i].login ? users[i].login : "");
    }
    user_array_free(users, count);
    return CLI_OK;
}

static int cmd_collaborator_add(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb collaborator <owner/repo> add <username> [--permission read|write|admin]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, COLLAB_ADD_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 2) {
        fprintf(stderr, "Error: collaborator add requires owner/repo and username\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    const char *permission = "write";
    int idx = find_flag_idx(COLLAB_ADD_FLAGS, "--permission");
    if (fv[idx])
        permission = fv[idx];
    int rc = api_collaborator_add(api, owner, repo, positional[1], permission);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Added collaborator %s with %s permission\n", positional[1], permission);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_collaborator_rm(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb collaborator <owner/repo> rm <username> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: collaborator rm requires owner/repo and username\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    if (!gf->yes && !confirm("Remove this collaborator?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_collaborator_remove(api, owner, repo, argv[1]);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Removed collaborator %s\n", argv[1]);
    return CLI_OK;
}

static int cmd_collaborator(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_collaborator();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_collaborator();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_collaborator_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "add") == 0)
        return cmd_collaborator_add(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "rm") == 0)
        return cmd_collaborator_rm(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "perms") == 0) {
        fprintf(stderr, "Error: collaborator perms not yet implemented\n");
        return CLI_ERR;
    }
    fprintf(stderr, "Error: unknown collaborator subcommand '%s'\n", sub);
    help_collaborator();
    return CLI_USAGE;
}

/* ===== Fork command handlers ===== */

static int cmd_fork_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb fork <owner/repo> list\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: fork list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    Repo *forks;
    size_t count;
    int rc = api_fork_list(api, owner, repo, &forks, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_repo_list(forks, count, gf->json);
    repo_array_free(forks, count);
    return CLI_OK;
}

static int cmd_fork_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb fork <owner/repo> create [--name <repo-name>] [--org <organization>]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, FORK_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: fork create requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    const char *name = NULL, *org = NULL;
    int idx;
    idx = find_flag_idx(FORK_CREATE_FLAGS, "--name");
    if (fv[idx])
        name = fv[idx];
    idx = find_flag_idx(FORK_CREATE_FLAGS, "--org");
    if (fv[idx])
        org = fv[idx];
    Repo r;
    int rc = api_fork_create(api, owner, repo, name, org, &r);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Forked repository to %s\n", r.full_name ? r.full_name : "");
    repo_free(&r);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_fork(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_fork();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_fork();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_fork_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "create") == 0)
        return cmd_fork_create(rest_argc, rest_argv, api, gf);
    fprintf(stderr, "Error: unknown fork subcommand '%s'\n", sub);
    help_fork();
    return CLI_USAGE;
}

/* ===== Hook command handlers ===== */

static int cmd_hook_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb hook <owner/repo> list\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: hook list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    Hook *hooks;
    size_t count;
    int rc = api_hook_list(api, owner, repo, &hooks, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_hook_list(hooks, count, gf->json);
    hook_array_free(hooks, count);
    return CLI_OK;
}

static int cmd_hook_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb hook <owner/repo> create --type <type> --url <url> [--event <event>] [--active]\n");
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, HOOK_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        fprintf(stderr, "Error: hook create requires owner/repo\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(positional[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0) {
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    int idx;
    idx = find_flag_idx(HOOK_CREATE_FLAGS, "--type");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --type is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    idx = find_flag_idx(HOOK_CREATE_FLAGS, "--url");
    if (!fv[idx]) {
        fprintf(stderr, "Error: --url is required\n");
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }
    CreateHookOpts opts = { 0 };
    idx = find_flag_idx(HOOK_CREATE_FLAGS, "--type");
    opts.type = fv[idx];
    idx = find_flag_idx(HOOK_CREATE_FLAGS, "--url");
    opts.url = fv[idx];
    idx = find_flag_idx(HOOK_CREATE_FLAGS, "--content-type");
    if (fv[idx])
        opts.content_type = fv[idx];
    idx = find_flag_idx(HOOK_CREATE_FLAGS, "--secret");
    if (fv[idx])
        opts.secret = fv[idx];
    idx = find_flag_idx(HOOK_CREATE_FLAGS, "--active");
    if (fb[idx])
        opts.active = 1;
    idx = find_flag_idx(HOOK_CREATE_FLAGS, "--inactive");
    if (fb[idx])
        opts.active = 0;
    idx = find_flag_idx(HOOK_CREATE_FLAGS, "--branch-filter");
    if (fv[idx])
        opts.branch_filter = fv[idx];
    Hook h;
    int rc = api_hook_create(api, owner, repo, &opts, &h);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Created webhook #%lld\n", (long long)h.id);
    hook_free(&h);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_hook_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb hook <owner/repo> delete <id> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: hook delete requires owner/repo and hook id\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    int64_t id = atol(argv[1]);
    if (!gf->yes && !confirm("Delete this webhook?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_hook_delete(api, owner, repo, id);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted webhook #%lld\n", (long long)id);
    return CLI_OK;
}

static int cmd_hook(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_hook();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_hook();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_hook_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "create") == 0)
        return cmd_hook_create(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete") == 0)
        return cmd_hook_delete(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "show") == 0 || strcmp(sub, "edit") == 0 ||
        strcmp(sub, "test") == 0) {
        fprintf(stderr, "Error: hook %s not yet implemented\n", sub);
        return CLI_ERR;
    }
    fprintf(stderr, "Error: unknown hook subcommand '%s'\n", sub);
    help_hook();
    return CLI_USAGE;
}

/* ===== Wiki command handlers ===== */

static int cmd_wiki_list(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb wiki <owner/repo> list\n");
            return CLI_OK;
        }
    }
    if (argc < 1) {
        fprintf(stderr, "Error: wiki list requires owner/repo\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    WikiPage *pages;
    size_t count;
    int rc = api_wiki_list(api, owner, repo, &pages, &count);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    print_wikipage_list(pages, count, gf->json);
    wikipage_array_free(pages, count);
    return CLI_OK;
}

static int cmd_wiki_delete(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            printf("Usage: cb wiki <owner/repo> delete <pageName> [--yes]\n");
            return CLI_OK;
        }
    }
    if (argc < 2) {
        fprintf(stderr, "Error: wiki delete requires owner/repo and page name\n");
        return CLI_USAGE;
    }
    char owner[128], repo[128];
    if (require_owner_repo(argv[0], owner, sizeof(owner),
                           repo, sizeof(repo), api) != 0)
        return CLI_ERR;
    if (!gf->yes && !confirm("Delete this wiki page?")) {
        printf("Cancelled.\n");
        return CLI_OK;
    }
    int rc = api_wiki_delete(api, owner, repo, argv[1]);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Deleted wiki page %s\n", argv[1]);
    return CLI_OK;
}

static int cmd_wiki(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_wiki();
        return CLI_USAGE;
    }
    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;
    if (is_help_arg(sub)) {
        help_wiki();
        return CLI_OK;
    }
    if (strcmp(sub, "list") == 0)
        return cmd_wiki_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete") == 0)
        return cmd_wiki_delete(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "create") == 0 || strcmp(sub, "show") == 0 ||
        strcmp(sub, "edit") == 0 || strcmp(sub, "revisions") == 0) {
        fprintf(stderr, "Error: wiki %s not yet implemented\n", sub);
        return CLI_ERR;
    }
    fprintf(stderr, "Error: unknown wiki subcommand '%s'\n", sub);
    help_wiki();
    return CLI_USAGE;
}

/* ===== Org commands ===== */

static int cmd_org_create(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    for (int i = 0; i < argc; i++) {
        if (is_help_arg(argv[i])) {
            help_org_create();
            return CLI_OK;
        }
    }
    const char **positional;
    const char **fv;
    int *fb;
    int npos = parse_flags(argc, argv, ORG_CREATE_FLAGS, &positional, &fv, &fb);
    if (npos < 0)
        return CLI_USAGE;
    if (npos < 1) {
        help_org_create();
        free(positional);
        free(fv);
        free(fb);
        return CLI_USAGE;
    }

    const char *name = positional[0];
    char verr[256];
    if (validate_org_name(name, verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }

    CreateOrgOpts opts = { 0 };
    opts.username = name;
    int idx;

    idx = find_flag_idx(ORG_CREATE_FLAGS, "--description");
    if (fv[idx]) {
        if (validate_description(fv[idx], verr, sizeof(verr)) != VALIDATE_OK) {
            fprintf(stderr, "Error: %s\n", verr);
            free(positional);
            free(fv);
            free(fb);
            return CLI_ERR;
        }
        opts.description = fv[idx];
    }
    idx = find_flag_idx(ORG_CREATE_FLAGS, "--full-name");
    if (fv[idx])
        opts.full_name = fv[idx];
    idx = find_flag_idx(ORG_CREATE_FLAGS, "--email");
    if (fv[idx])
        opts.email = fv[idx];
    idx = find_flag_idx(ORG_CREATE_FLAGS, "--location");
    if (fv[idx])
        opts.location = fv[idx];
    idx = find_flag_idx(ORG_CREATE_FLAGS, "--website");
    if (fv[idx]) {
        if (validate_website(fv[idx], verr, sizeof(verr)) != VALIDATE_OK) {
            fprintf(stderr, "Error: %s\n", verr);
            free(positional);
            free(fv);
            free(fb);
            return CLI_ERR;
        }
        opts.website = fv[idx];
    }
    idx = find_flag_idx(ORG_CREATE_FLAGS, "--visibility");
    if (fv[idx]) {
        if (validate_visibility(fv[idx], verr, sizeof(verr)) != VALIDATE_OK) {
            fprintf(stderr, "Error: %s\n", verr);
            free(positional);
            free(fv);
            free(fb);
            return CLI_ERR;
        }
        opts.visibility = fv[idx];
    }
    idx = find_flag_idx(ORG_CREATE_FLAGS, "--repo-admin-change-team-access");
    if (fb[idx])
        opts.repo_admin_change_team_access = 1;

    Organization o;
    int rc = api_org_create(api, &opts, &o);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }

    if (!gf->quiet) {
        printf("Created %s (%s)\n", o.name ? o.name : name,
               o.visibility ? o.visibility : "public");
        if (o.avatar_url)
            printf("  %s\n", o.avatar_url);
    }
    org_free(&o);
    free(positional);
    free(fv);
    free(fb);
    return CLI_OK;
}

static int cmd_org(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_org();
        return CLI_USAGE;
    }

    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;

    if (is_help_arg(sub)) {
        help_org();
        return CLI_OK;
    }
    if (strcmp(sub, "create") == 0)
        return cmd_org_create(rest_argc, rest_argv, api, gf);

    fprintf(stderr, "Error: unknown org subcommand '%s'\n", sub);
    help_org();
    return CLI_USAGE;
}

/* ===== Command dispatch ===== */

static int cmd_repo(int argc, char **argv, ApiClient *api, CbGlobalFlags *gf)
{
    if (argc < 1) {
        help_repo();
        return CLI_USAGE;
    }

    const char *sub = argv[0];
    int rest_argc = argc - 1;
    char **rest_argv = argv + 1;

    if (is_help_arg(sub)) {
        help_repo();
        return CLI_OK;
    }
    if (strcmp(sub, "create") == 0)
        return cmd_repo_create(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "delete") == 0)
        return cmd_repo_delete(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "rename") == 0)
        return cmd_repo_rename(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "edit") == 0)
        return cmd_repo_edit(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "show") == 0)
        return cmd_repo_show(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "list") == 0)
        return cmd_repo_list(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "transfer") == 0)
        return cmd_repo_transfer(rest_argc, rest_argv, api, gf);
    if (strcmp(sub, "topic") == 0) {
        if (rest_argc < 1) {
            help_repo_topic();
            return CLI_USAGE;
        }
        const char *topic_sub = rest_argv[0];
        int topic_argc = rest_argc - 1;
        char **topic_argv = rest_argv + 1;
        if (is_help_arg(topic_sub)) {
            help_repo_topic();
            return CLI_OK;
        }
        if (strcmp(topic_sub, "add") == 0)
            return cmd_topic_add(topic_argc, topic_argv, api, gf);
        if (strcmp(topic_sub, "rm") == 0)
            return cmd_topic_rm(topic_argc, topic_argv, api, gf);
        if (strcmp(topic_sub, "list") == 0)
            return cmd_topic_list(topic_argc, topic_argv, api, gf);
        if (strcmp(topic_sub, "set") == 0)
            return cmd_topic_set(topic_argc, topic_argv, api, gf);
        fprintf(stderr, "Error: unknown topic subcommand '%s'\n", topic_sub);
        help_repo_topic();
        return CLI_USAGE;
    }

    fprintf(stderr, "Error: unknown repo subcommand '%s'\n", sub);
    help_repo();
    return CLI_USAGE;
}

void cli_print_help(const char *cmd)
{
    if (!cmd) {
        printf("cb — Codeberg (Forgejo) repository management CLI\n\n");
        printf("Usage: cb [global flags] <command> [subcommand] [args] [flags]\n\n");
        printf("Commands:\n");
        printf("  repo           Repository management (create, delete, rename, edit, show, list, transfer, topic)\n");
        printf("  actions        CI/CD actions (list runs, show run, runners, dispatch, secrets, variables)\n");
        printf("  release        Manage releases (list, create, show, edit, delete, assets)\n");
        printf("  tag            Manage tags (list, create, show, delete)\n");
        printf("  branch         Manage branches (list, create, show, rename, delete)\n");
        printf("  issue          Manage issues (list, create, show, edit, delete, comment, labels)\n");
        printf("  label          Manage repository labels (list, create, show, edit, delete)\n");
        printf("  milestone      Manage milestones (list, create, show, edit, delete)\n");
        printf("  pr             Manage pull requests (list, create, show, merge, close)\n");
        printf("  commit         View commits and statuses (list, show, status, compare)\n");
        printf("  content        View and manage file contents (list, show, create, update, delete, raw)\n");
        printf("  key            Manage deploy keys (list, add, show, delete)\n");
        printf("  collaborator   Manage collaborators (list, add, rm, perms)\n");
        printf("  fork           Manage forks (list, create)\n");
        printf("  hook           Manage webhooks (list, create, show, edit, delete, test)\n");
        printf("  org            Organization management (create)\n");
        printf("  wiki           Manage wiki pages (list, create, show, edit, delete, revisions)\n");
        printf("\nGlobal flags:\n");
        printf("  --json          Output raw JSON\n");
        printf("  --quiet, -q     Suppress non-essential output\n");
        printf("  --base-url URL  Override API base URL\n");
        printf("  --yes           Skip confirmation prompts\n");
        printf("  --version, -v   Show version\n");
        printf("  --help, -h      Show this help\n");
        printf("\nRun 'cb repo --help' for subcommand details.\n");
        return;
    }
}

int cli_run(int argc, char **argv)
{
    /* Extract global flags first */
    CbGlobalFlags gf;
    char **filtered_argv = NULL;
    int filtered_argc = extract_global_flags(argc, argv, &gf, &filtered_argv);
    if (filtered_argc < 0) {
        return CLI_USAGE;
    }

    if (gf.version) {
        printf("cb %s\n", CB_VERSION);
        printf("Copyright (C) 2026 Thomas Christensen.\n");
        printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
        printf("This is free software: you are free to change and redistribute it.\n");
        printf("There is NO WARRANTY, to the extent permitted by law.\n");
        free(filtered_argv);
        return CLI_OK;
    }

    if (filtered_argc < 2) {
        cli_print_help(NULL);
        free(filtered_argv);
        return CLI_USAGE;
    }

    const char *cmd = filtered_argv[1];
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        cli_print_help(NULL);
        free(filtered_argv);
        return CLI_OK;
    }

    if (strcmp(cmd, "repo") != 0 && strcmp(cmd, "actions") != 0 &&
        strcmp(cmd, "release") != 0 && strcmp(cmd, "tag") != 0 &&
        strcmp(cmd, "branch") != 0 && strcmp(cmd, "issue") != 0 &&
        strcmp(cmd, "label") != 0 && strcmp(cmd, "milestone") != 0 &&
        strcmp(cmd, "pr") != 0 && strcmp(cmd, "commit") != 0 &&
        strcmp(cmd, "content") != 0 && strcmp(cmd, "key") != 0 &&
        strcmp(cmd, "collaborator") != 0 && strcmp(cmd, "fork") != 0 &&
        strcmp(cmd, "hook") != 0 && strcmp(cmd, "org") != 0 &&
        strcmp(cmd, "wiki") != 0) {
        fprintf(stderr, "Error: unknown command '%s'\n", cmd);
        free(filtered_argv);
        return CLI_USAGE;
    }

    /* Load config */
    Config cfg;
    char err[256];
    /* Try to find config file */
    const char *cfg_dir = cb_config_dir();
    char config_path[512] = { 0 };
    if (cfg_dir) {
#ifdef _WIN32
        snprintf(config_path, sizeof(config_path), "%s\\cb\\config", cfg_dir);
#else
        snprintf(config_path, sizeof(config_path), "%s/cb/config", cfg_dir);
#endif
    }

    if (config_load(&cfg, config_path[0] ? config_path : NULL, err, sizeof(err)) != 0) {
        fprintf(stderr, "Error: %s\n", err);
        free(filtered_argv);
        return CLI_ERR;
    }

    config_apply_cli_override(&cfg, gf.base_url);

    /* Init API client */
    ApiClient api;
    if (api_client_init(&api, cfg.base_url, cfg.token) != 0) {
        fprintf(stderr, "Error: %s\n", api.last_error);
        free(filtered_argv);
        return CLI_ERR;
    }

    /* Run the command */
    int rc;
    if (strcmp(cmd, "actions") == 0)
        rc = cmd_actions(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "release") == 0)
        rc = cmd_release(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "tag") == 0)
        rc = cmd_tag(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "branch") == 0)
        rc = cmd_branch(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "issue") == 0)
        rc = cmd_issue(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "label") == 0)
        rc = cmd_label(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "milestone") == 0)
        rc = cmd_milestone(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "pr") == 0)
        rc = cmd_pr(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "commit") == 0)
        rc = cmd_commit(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "content") == 0)
        rc = cmd_content(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "key") == 0)
        rc = cmd_key(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "collaborator") == 0)
        rc = cmd_collaborator(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "fork") == 0)
        rc = cmd_fork(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "hook") == 0)
        rc = cmd_hook(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "org") == 0)
        rc = cmd_org(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else if (strcmp(cmd, "wiki") == 0)
        rc = cmd_wiki(filtered_argc - 2, filtered_argv + 2, &api, &gf);
    else
        rc = cmd_repo(filtered_argc - 2, filtered_argv + 2, &api, &gf);

    api_client_free(&api);
    free(filtered_argv);
    return rc;
}
