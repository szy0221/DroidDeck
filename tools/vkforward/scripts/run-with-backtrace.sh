#!/usr/bin/env bash
# Run a command and only ask gdb for a backtrace when it ends badly.
#
# The forwarding layer lives in a proot runtime we cannot attach a debugger to,
# so the only stack we will ever get for a crash inside the ICD is the one a
# runner captures. Running gdb unconditionally costs a clean run its exit code,
# because gdb has no inferior left to unwind once the program exits normally.
#
# usage: run-with-backtrace.sh <logfile> <command> [args...]

set -uo pipefail

LOG="$1"
shift

set +e
"$@" >"$LOG" 2>&1
CODE=$?

cat "$LOG"

if [ "$CODE" -eq 0 ]; then
    exit 0
fi

echo "=== command exited with $CODE, rerunning under gdb ==="
gdb -return-child-result -batch -ex run -ex "bt 25" -ex "x/6i \$pc" --args "$@"

# Report the original failure, not whatever gdb decided to return.
exit "$CODE"
