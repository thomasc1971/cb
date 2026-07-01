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

#include "cb_cli.h"
#include "cb_config.h"
#include "cb_json.h"
#include "cb_validate.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    const char *base_url;
} GlobalFlags;

/* Extract global flags from argv. Returns new argc/argv with global flags removed.
 * Caller must free *out_argv. */
static int extract_global_flags(int argc, char **argv, GlobalFlags *gf,
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

static int cmd_repo_create(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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

static int cmd_repo_delete(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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

    char owner[128], repo[128], verr[256];
    if (validate_owner_repo(positional[0], owner, sizeof(owner),
                            repo, sizeof(repo), verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    /* owner defaults to empty — TODO: fill with current user */
    if (owner[0] == '\0') {
        fprintf(stderr, "Error: please specify owner/repo (owner is required for delete)\n");
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

static int cmd_repo_rename(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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
    if (validate_owner_repo(argv[0], owner, sizeof(owner),
                            repo, sizeof(repo), verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        return CLI_ERR;
    }
    if (owner[0] == '\0') {
        fprintf(stderr, "Error: please specify owner/repo\n");
        return CLI_ERR;
    }

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

static int cmd_repo_edit(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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
    if (validate_owner_repo(positional[0], owner, sizeof(owner),
                            repo, sizeof(repo), verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        free(positional);
        free(fv);
        free(fb);
        return CLI_ERR;
    }
    if (owner[0] == '\0') {
        fprintf(stderr, "Error: please specify owner/repo\n");
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

static int cmd_repo_show(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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

    char owner[128], repo[128], verr[256];
    if (validate_owner_repo(argv[0], owner, sizeof(owner),
                            repo, sizeof(repo), verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        return CLI_ERR;
    }
    if (owner[0] == '\0') {
        fprintf(stderr, "Error: please specify owner/repo\n");
        return CLI_ERR;
    }

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

static int cmd_repo_list(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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

static int cmd_repo_transfer(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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

    char owner[128], repo[128], verr[256];
    if (validate_owner_repo(argv[0], owner, sizeof(owner),
                            repo, sizeof(repo), verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        return CLI_ERR;
    }
    if (owner[0] == '\0') {
        fprintf(stderr, "Error: please specify owner/repo\n");
        return CLI_ERR;
    }

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

static int cmd_topic_add(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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
    char owner[128], repo[128], verr[256];
    if (validate_owner_repo(argv[0], owner, sizeof(owner),
                            repo, sizeof(repo), verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        return CLI_ERR;
    }
    if (owner[0] == '\0') {
        fprintf(stderr, "Error: please specify owner/repo\n");
        return CLI_ERR;
    }
    int rc = api_topic_add(api, owner, repo, argv[1]);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Added topic '%s' to %s/%s\n", argv[1], owner, repo);
    return CLI_OK;
}

static int cmd_topic_rm(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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
    char owner[128], repo[128], verr[256];
    if (validate_owner_repo(argv[0], owner, sizeof(owner),
                            repo, sizeof(repo), verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        return CLI_ERR;
    }
    if (owner[0] == '\0') {
        fprintf(stderr, "Error: please specify owner/repo\n");
        return CLI_ERR;
    }
    int rc = api_topic_remove(api, owner, repo, argv[1]);
    if (rc != API_OK) {
        print_api_error(rc, api->last_error);
        return CLI_ERR;
    }
    if (!gf->quiet)
        printf("Removed topic '%s' from %s/%s\n", argv[1], owner, repo);
    return CLI_OK;
}

static int cmd_topic_list(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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
    char owner[128], repo[128], verr[256];
    if (validate_owner_repo(argv[0], owner, sizeof(owner),
                            repo, sizeof(repo), verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        return CLI_ERR;
    }
    if (owner[0] == '\0') {
        fprintf(stderr, "Error: please specify owner/repo\n");
        return CLI_ERR;
    }
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

static int cmd_topic_set(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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
    char owner[128], repo[128], verr[256];
    if (validate_owner_repo(argv[0], owner, sizeof(owner),
                            repo, sizeof(repo), verr, sizeof(verr)) != VALIDATE_OK) {
        fprintf(stderr, "Error: %s\n", verr);
        return CLI_ERR;
    }
    if (owner[0] == '\0') {
        fprintf(stderr, "Error: please specify owner/repo\n");
        return CLI_ERR;
    }

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

/* ===== Command dispatch ===== */

static int cmd_repo(int argc, char **argv, ApiClient *api, GlobalFlags *gf)
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
        printf("  repo    Repository management (create, delete, rename, edit, show, list, transfer, topic)\n");
        printf("\nGlobal flags:\n");
        printf("  --json          Output raw JSON\n");
        printf("  --quiet, -q     Suppress non-essential output\n");
        printf("  --base-url URL  Override API base URL\n");
        printf("  --yes           Skip confirmation prompts\n");
        printf("  --help, -h      Show this help\n");
        printf("\nRun 'cb repo --help' for subcommand details.\n");
        return;
    }
}

int cli_run(int argc, char **argv)
{
    /* Extract global flags first */
    GlobalFlags gf;
    char **filtered_argv = NULL;
    int filtered_argc = extract_global_flags(argc, argv, &gf, &filtered_argv);
    if (filtered_argc < 0) {
        return CLI_USAGE;
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

    if (strcmp(cmd, "repo") != 0) {
        fprintf(stderr, "Error: unknown command '%s'\n", cmd);
        free(filtered_argv);
        return CLI_USAGE;
    }

    /* Load config */
    Config cfg;
    char err[256];
    /* Try to find config file */
    const char *home = getenv("HOME");
    char config_path[512] = { 0 };
    if (home)
        snprintf(config_path, sizeof(config_path), "%s/.config/cb/config", home);

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

    /* Run the repo command */
    int rc = cmd_repo(filtered_argc - 2, filtered_argv + 2, &api, &gf);

    api_client_free(&api);
    free(filtered_argv);
    return rc;
}
