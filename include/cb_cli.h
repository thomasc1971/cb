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
int cli_run(int argc, char **argv);

/* Print usage/help for a command (or general help if cmd is NULL). */
void cli_print_help(const char *cmd);

#endif /* CB_CLI_H */
