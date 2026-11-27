#!/bin/sh
# Runner for the whole test suite

set -u

COMPILER=${COMPILER:-"./comp"}
TESTDIR=${TESTDIR:-"./tests"}
TIMEOUT="${TIMEOUT:-10}"
BLESS=0
FORCE=0
VERBOSE=0
DRY=0

usage() {
    cat <<EOF >&2
Usage: $0 [options] [test ...]
Options:
  -b,--bless
        update expected output for matching tests
  -f,--force
        overwrite existing expected output when used with -b
  -v,--verbose
        show full output for matching tests
  -d,--dry
        dry run (show the name and status) the matching tests
  -h,--help
        display this help and exit
EOF
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        -b|--bless)
            BLESS=1
            shift
            ;;
        -f|--force)
            FORCE=1
            shift
            ;;
        -v|--verbose)
            VERBOSE=1
            shift
            ;;
        -d|--dry)
            DRY=1
            shift
            ;;
        -h|--help)
            usage 0
            ;;
        --)
            shift
            break
            ;;
        -*)
            usage 2
            ;;
        *)
            break
            ;;
    esac
done


[ -x "$COMPILER" ] || { printf 'error: compiler not executable: %s\n' "$COMPILER" >&2; exit 2; }
COMPILER="$(CDPATH= cd -- "$(dirname -- "$COMPILER")" && pwd)/$(basename -- "$COMPILER")"

[ -d "$TESTDIR" ] || { printf 'error: tests dir not found: %s\n' "$TESTDIR" >&2; exit 2; }
TESTDIR="$(CDPATH= cd -- "$TESTDIR" && pwd)"

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]
then
    C_GREEN=$(printf '\033[32m')
    C_RED=$(printf '\033[31m')
    C_YELLOW=$(printf '\033[33m')
    C_CYAN=$(printf '\033[36m')
    C_RESET=$(printf '\033[0m')
else
    C_GREEN=""
    C_RED=""
    C_YELLOW=""
    C_CYAN=""
    C_RESET=""
fi

print_result() {
    _color=$1
    _tag=$2
    _name=$3
    _extra=${4:-}
    printf '%s%s%s %s%s\n' "$_color" "$_tag" "$C_RESET" "$_name" "${_extra:+ $_extra}"
}

now_ms() {
    _t=$(date +%s%3N 2>/dev/null)
    case "$_t" in
        *[!0-9]*|"")
            printf '%s000\n' "$(date +%s)"
            ;;
        *)
            printf '%s\n' "$_t"
            ;;
    esac
}

match() {
    [ $# -eq 1 ] && return 0
    _name=$1
    shift
    for _p in "$@"; do
        case "$_name" in
            *"$_p"*)
                return 0
                ;;
        esac
    done
    return 1
}

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"

STDBUF=""
command -v stdbuf >/dev/null 2>&1 && STDBUF="stdbuf -oL"

WORKDIR=""
if [ "$DRY" -eq 0 ]
then
    WORKDIR=$(mktemp -d "${TMPDIR:-/tmp}/comp-tests.XXXXXX")
    trap 'rm -rf "$WORKDIR"' EXIT INT TERM
fi

PASS=0
FAIL=0
BLESSED=0
MISSING=0
SAVED=0
SKIP=0

run_test() {
    dir=$1
    name=$(basename -- "$dir")
    exp="$dir/expected.txt"

    if [ "$DRY" -eq 1 ]
    then
        if [ -f "$exp" ]
        then
            c_rc=$(sed -n 's/^=== COMPILE EXIT: \([0-9]*\) ===$/\1/p' "$exp")
            r_rc=$(sed -n 's/^=== RUN EXIT: \([0-9]*\) ===$/\1/p' "$exp")
            print_result "$C_CYAN" "SAVED" "$name" "(compile=${c_rc:-?}${r_rc:+, run=$r_rc})"
            [ "$VERBOSE" -eq 1 ] && cat "$exp"
            SAVED=$((SAVED + 1))
        else
            print_result "$C_YELLOW" "MISSING" "$name"
            MISSING=$((MISSING + 1))
        fi
        return
    fi

    if [ "$BLESS" -eq 1 ] && [ "$FORCE" -eq 0 ] && [ -f "$exp" ]
    then
        SKIP=$((SKIP + 1))
        return
    fi

    out="$WORKDIR/$name"
    mkdir -p "$out"

    # Compile (suppress shell job-signal diagnostics on compiler crash)
    (
        cd "$dir" || exit 1
        if [ -f files ]
        then
            set -- $(grep -v '^[[:space:]]*#' files)
        else
            set --
            for f in *.lang *.c
            do
                [ -e "$f" ] && set -- "$@" "$f"
            done
        fi
        if [ $# -eq 0 ]
        then
            echo "runner: no .lang or .c files" >"$out/c.out"
            exit 99
        fi
        [ -f flags ] && set -- "$@" $(grep -v '^[[:space:]]*#' flags)
        printf 'cd %s && %s %s -o %s\n' "$dir" "$COMPILER" "$*" "$out/a.out" >"$out/cmd.txt"
        timeout "$TIMEOUT" $STDBUF "$COMPILER" "$@" -o "$out/a.out" >"$out/c.out" 2>&1
        exit $?
    ) 2>/dev/null
    c_rc=$?

    # Run (suppress shell job-signal diagnostics on binary crash)
    r_rc=""
    if [ "$c_rc" -eq 0 ] && [ -x "$out/a.out" ]
    then
        (
            cd "$dir" || exit 1
            if [ -f stdin ]
            then
                timeout "$TIMEOUT" $STDBUF "$out/a.out" < stdin >"$out/r.out" 2>&1
            else
                timeout "$TIMEOUT" $STDBUF "$out/a.out" < /dev/null >"$out/r.out" 2>&1
            fi
            exit $?
        ) 2>/dev/null
        r_rc=$?
    fi

    info="(compile=$c_rc${r_rc:+, run=$r_rc})"

    # Record output (strip temp/test paths so its portable)
    {
        printf '=== COMPILE EXIT: %d ===\n' "$c_rc"
        sed -e "s|$WORKDIR|<BUILD>|g" -e "s|$dir|<TEST>|g" \
            -e 's|/tmp/comp-[A-Za-z0-9_-]\{6\}\.ll|<TMP_IR>|g' "$out/c.out"
        if [ -n "$r_rc" ]
        then
            printf '=== RUN EXIT: %d ===\n' "$r_rc"
            sed -e "s|$WORKDIR|<BUILD>|g" -e "s|$dir|<TEST>|g" \
                -e 's|/tmp/comp-[A-Za-z0-9_-]\{6\}\.ll|<TMP_IR>|g' "$out/r.out"
        fi
    } > "$out/actual.txt"

    if [ "$BLESS" -eq 1 ]
    then
        if [ "$c_rc" -ge 128 ] || [ "$c_rc" -eq 124 ] || grep -qE 'Sanitizer' "$out/c.out"
        then
            print_result "$C_RED" "FAIL" "$name" "$info (refused to bless crash/timeout)"
            if [ "$VERBOSE" -eq 1 ]
            then
                [ -f "$out/cmd.txt" ] && printf 'cmd: %s\n' "$(cat "$out/cmd.txt")"
                cat "$out/actual.txt"
            fi
            FAIL=$((FAIL + 1))
            return
        fi
        cp "$out/actual.txt" "$exp"
        print_result "$C_CYAN" "BLESS" "$name" "$info"
        [ "$VERBOSE" -eq 1 ] && cat "$out/actual.txt"
        BLESSED=$((BLESSED + 1))
        return
    fi

    if [ ! -f "$exp" ]
    then
        print_result "$C_YELLOW" "MISSING" "$name" "$info"
        [ "$VERBOSE" -eq 1 ] && cat "$out/actual.txt"
        MISSING=$((MISSING + 1))
        return
    fi

    if diff --color=always -u -L "blessed/$name" -L "actual/$name" "$exp" "$out/actual.txt" > "$out/diff.txt"
    then
        print_result "$C_GREEN" "PASS" "$name" "$info"
        [ "$VERBOSE" -eq 1 ] && cat "$out/actual.txt"
        PASS=$((PASS + 1))
    else
        print_result "$C_RED" "FAIL" "$name" "$info"
        if [ "$VERBOSE" -eq 1 ]
        then
            [ -f "$out/cmd.txt" ] && printf 'cmd: %s\n' "$(cat "$out/cmd.txt")"
            cat "$out/diff.txt"
        fi
        FAIL=$((FAIL + 1))
    fi
}

START_MS=$(now_ms)

for d in "$TESTDIR"/*; do
    [ -d "$d" ] || continue
    name=$(basename -- "$d")
    case "$name" in
        .*)
            continue
            ;;
    esac

    if match "$name" "$@"
    then
        run_test "$d"
    else
        SKIP=$((SKIP + 1))
    fi
done

ELAPSED_MS=$(($(now_ms) - START_MS))
[ "$ELAPSED_MS" -lt 0 ] && ELAPSED_MS=0
TOTAL=$((PASS + FAIL + BLESSED + MISSING + SAVED))

printf '\ntotal: %d tests in %d.%03d seconds\n' "$TOTAL" $((ELAPSED_MS / 1000)) $((ELAPSED_MS % 1000))
if [ "$DRY" -eq 1 ]
then
    printf 'saved: %d, missing: %d, skip: %d\n' "$SAVED" "$MISSING" "$SKIP"
elif [ "$BLESS" -eq 1 ]
then
    printf 'bless: %d, fail: %d, skip: %d\n' "$BLESSED" "$FAIL" "$SKIP"
else
    printf 'pass: %d, fail: %d, missing: %d, skip: %d\n' "$PASS" "$FAIL" "$MISSING" "$SKIP"
fi

[ "$FAIL" -eq 0 ] && [ "$MISSING" -eq 0 ]
