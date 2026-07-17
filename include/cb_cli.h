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

#ifndef CB_CLI_H
#define CB_CLI_H

#include "cb_api.h"
#include "cb_config.h"

/* CLI exit codes */
#define CLI_OK    0
#define CLI_ERR   1
#define CLI_USAGE 2

/* Parse command line arguments and execute the command.
 * argc/argv: the full program arguments (argv[0] is program name)
 * Returns exit code (0=success, 1=error, 2=usage error).
 */
int cli_run (int argc, char **argv);

/* Print usage/help for a command (or general help if cmd is NULL). */
void cli_print_help (const char *cmd);

#endif /* CB_CLI_H */
