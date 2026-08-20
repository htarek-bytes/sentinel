#!/bin/bash
# usage: sigkill_escalation.sh <sentinel> <worker>
# the child ignores SIGTERM, so sentinel should wait out the grace period
# and then kill it with a signal it cannot catch

sentinel=$1
worker=$2

"$sentinel" "$worker" stubborn >/dev/null 2>&1 &
sup=$!

sleep 1

child=$(pgrep -P "$sup" | head -1)
if [ -z "$child" ]; then
    echo "sentinel $sup never spawned a child"
    kill -9 "$sup" 2>/dev/null
    exit 1
fi

kill -TERM "$sup"

# stubborn mode ignores SIGTERM, so the child should still be here
sleep 2
if ! kill -0 "$child" 2>/dev/null; then
    echo "child died on SIGTERM, so this is not testing the escalation"
    exit 1
fi

wait "$sup"
rc=$?

if [ "$rc" -ne 137 ]; then
    echo "sentinel exited $rc, expected 137 (128 + SIGKILL)"
    kill -9 "$child" 2>/dev/null
    exit 1
fi

if kill -0 "$child" 2>/dev/null; then
    echo "child $child survived SIGKILL somehow"
    kill -9 "$child" 2>/dev/null
    exit 1
fi
