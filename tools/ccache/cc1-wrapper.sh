#!/bin/bash
# ccache wrapper for the preprocessed C compilation pipeline.
#
# The Makefile normally pipes preprocessed C into cc1 and reads assembly from
# stdout. Ccache cannot cache stdin/stdout cc1 calls, so this wrapper writes the
# input to a temporary .i file and lets the GCC driver produce assembly through
# ccache. Set CCACHE=0 on the make command line to use the original cc1 path.
set -e

compiler="${1:?compiler required}"
shift

ccache_bin="${CCACHE_BIN:-ccache}"
tmp_in="$(mktemp "${TMPDIR:-/tmp}/ccache-cc1-in-XXXXXX.i")"
tmp_out="$(mktemp "${TMPDIR:-/tmp}/ccache-cc1-out-XXXXXX.s")"
cleanup() {
    rm -f "$tmp_in" "$tmp_out"
}
trap cleanup EXIT HUP INT TERM

input_is_stdin=0
output_is_stdout=0
newargs=()
expect_output=0
for arg in "$@"; do
    if [ "$expect_output" = 1 ]; then
        if [ "$arg" = "-" ]; then
            output_is_stdout=1
            newargs+=("$tmp_out")
        else
            newargs+=("$arg")
        fi
        expect_output=0
    elif [ "$arg" = "-o" ]; then
        newargs+=("-o")
        expect_output=1
    elif [ "$arg" = "-" ]; then
        input_is_stdin=1
        newargs+=("$tmp_in")
    elif [ "$arg" = "-quiet" ]; then
        :
    else
        newargs+=("$arg")
    fi
done

if [ "$input_is_stdin" = 1 ]; then
    cat > "$tmp_in"
fi

if command -v "$ccache_bin" >/dev/null 2>&1; then
    "$ccache_bin" "$compiler" -S "${newargs[@]}"
else
    "$compiler" -S "${newargs[@]}"
fi

if [ "$output_is_stdout" = 1 ]; then
    cat "$tmp_out"
fi
