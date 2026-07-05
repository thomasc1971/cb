#!/bin/sh
# gen-completions.sh — Generate shell completions for cb from --help-spec output.
#
# Usage: gen-completions.sh <cb-binary> <output-dir>
#
# Reads the TSV spec from `cb --help-spec` and emits:
#   <output-dir>/cb.bash   — Bash completions
#   <output-dir>/cb.zsh    — Zsh completions
#   <output-dir>/cb.fish   — Fish completions

set -eu

CB="${1:?usage: gen-completions.sh <cb-binary> <output-dir>}"
OUTDIR="${2:?usage: gen-completions.sh <cb-binary> <output-dir>}"

SPEC=$("$CB" --help-spec)
mkdir -p "$OUTDIR"

# Extract data into temp files
CMDS=$(echo "$SPEC" | awk -F'\t' '$1 == "CMD" { print $2 }' | tr '\n' ' ' | sed 's/ $//')
SUBS_FILE=$(mktemp)
FLAGS_FILE=$(mktemp)
echo "$SPEC" | awk -F'\t' '$1 == "SUB" { print $2 "\t" $3 }' > "$SUBS_FILE"
echo "$SPEC" | awk -F'\t' '$1 == "FLAG" { print $2 "\t" $3 "\t" $4 }' > "$FLAGS_FILE"

# ---- Bash ----
{
    echo '# cb bash completion (auto-generated — do not edit)'
    echo '# Regenerate with: make completions'
    echo ''
    echo '_cb()'
    echo '{'
    echo '    local cur prev words cword'
    echo '    _init_completion || return'
    echo ''
    printf '    if [ $cword -eq 1 ]; then\n'
    printf '        COMPREPLY=($(compgen -W "%s --help -h --version -v" -- "$cur"))\n' "$CMDS"
    printf '        return 0\n'
    printf '    fi\n'
    printf '\n'
    printf '    case "$cur" in\n'
    printf '        --*) COMPREPLY=($(compgen -W "--json --quiet -q --base-url --yes --help -h --version -v" -- "$cur")); return 0;;\n'
    printf '    esac\n'
    printf '\n'
    printf '    local cmd="${words[1]}"\n'
    printf '    local subcmd="${words[2]}"\n'
    printf '\n'

    # Per-command subcommand completion at position 2
    awk -F'\t' '
        {
            c = $1
            s = $2
            if (!(c in list)) {
                list[c] = s
            } else {
                list[c] = list[c] " " s
            }
        }
        END {
            for (c in list) {
                printf "    [ \"$cmd\" = \"%s\" ] && [ $cword -eq 2 ] && {\n", c
                printf "        COMPREPLY=($(compgen -W \"%s --help -h\" -- \"$cur\"))\n", list[c]
                printf "        return 0\n"
                printf "    }\n"
            }
        }
    ' "$SUBS_FILE"

    printf '\n'

    # Per-subcommand flag completion at position >= 3
    awk -F'\t' '
        {
            c = $1
            s = $2
            f = $3
            key = c " " s
            if (!(key in list)) {
                list[key] = f
            } else {
                list[key] = list[key] " " f
            }
        }
        END {
            for (k in list) {
                n = split(k, parts, " ")
                printf "    [ \"$cmd\" = \"%s\" ] && [ \"$subcmd\" = \"%s\" ] && [ $cword -ge 3 ] && {\n", parts[1], parts[2]
                printf "        COMPREPLY=($(compgen -W \"%s\" -- \"$cur\"))\n", list[k]
                printf "        return 0\n"
                printf "    }\n"
            }
        }
    ' "$FLAGS_FILE"

    printf '\n'
    printf '    return 0\n'
    printf '}\n'
    printf 'complete -F _cb cb\n'
} > "$OUTDIR/cb.bash"

# ---- Zsh ----
{
    echo '#compdef cb'
    echo '# cb zsh completion (auto-generated — do not edit)'
    echo '# Regenerate with: make completions'
    echo ''
    echo '_cb() {'
    echo '    local -a commands'
    echo '    commands=('

    echo "$CMDS" | tr ' ' '\n' | sed "s/^/        '/;s/$/'/"

    echo '    )'
    echo ''
    echo '    if (( CURRENT == 2 )); then'
    echo '        _describe command commands'
    echo '        _values "global flags" --json --quiet -q --base-url --yes --help -h --version -v'
    echo '        return'
    echo '    fi'
    echo ''
    echo '    local cmd=$words[2]'
    echo '    local subcmd=$words[3]'
    echo ''

    # Subcommand completion
    awk -F'\t' '
        {
            c = $1
            s = $2
            if (!(c in list)) {
                list[c] = s
            } else {
                list[c] = list[c] " " s
            }
        }
        END {
            for (c in list) {
                printf "    (( CURRENT == 3 )) && [[ $cmd == %s ]] && {\n", c
                printf "        _values \"subcommand\" %s --help -h\n", list[c]
                printf "        return\n"
                printf "    }\n"
            }
        }
    ' "$SUBS_FILE"

    echo ''

    # Flag completion
    awk -F'\t' '
        {
            c = $1
            s = $2
            f = $3
            key = c " " s
            if (!(key in list)) {
                list[key] = f
            } else {
                list[key] = list[key] " " f
            }
        }
        END {
            for (k in list) {
                n = split(k, parts, " ")
                printf "    (( CURRENT >= 3 )) && [[ $cmd == %s ]] && [[ $subcmd == %s ]] && {\n", parts[1], parts[2]
                printf "        _values \"flags\" %s --help -h\n", list[k]
                printf "        return\n"
                printf "    }\n"
            }
        }
    ' "$FLAGS_FILE"

    echo '}'
    echo '_cb "$@"'
} > "$OUTDIR/cb.zsh"

# ---- Fish ----
{
    echo '# cb fish completion (auto-generated — do not edit)'
    echo '# Regenerate with: make completions'
    echo ''

    # Top-level commands
    echo "$CMDS" | tr ' ' '\n' | awk '{ printf "complete -c cb -n \"__fish_use_subcommand\" -a %s\n", $1 }'

    # Global flags
    echo "$SPEC" | awk -F'\t' '
        $1 == "GFLAG" {
            flag = $2
            gsub(/^--/, "", flag)
            printf "complete -c cb -n \"__fish_use_subcommand\" -l %s\n", flag
        }
    '

    # Subcommands
    awk -F'\t' '
        { printf "complete -c cb -n \"__fish_seen_subcommand_from %s\" -a %s\n", $1, $2 }
    ' "$SUBS_FILE"

    # Per-subcommand flags
    awk -F'\t' '
        {
            c = $1
            s = $2
            f = $3
            gsub(/^--/, "", f)
            printf "complete -c cb -n \"__fish_seen_subcommand_from %s; and __fish_seen_subcommand_from %s\" -l %s\n", c, s, f
        }
    ' "$FLAGS_FILE"
} > "$OUTDIR/cb.fish"

rm -f "$SUBS_FILE" "$FLAGS_FILE"

echo "Generated completions in $OUTDIR/"
echo "  $OUTDIR/cb.bash"
echo "  $OUTDIR/cb.zsh"
echo "  $OUTDIR/cb.fish"
