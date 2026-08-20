#!/bin/bash
# usage: signal_forwarding.sh <sentinel> <worker>
# SIGTERM the supervisor and check the child goes down with it

sentinel=$1
worker=$2

"$sentinel" "$worker" sleep >/dev/null 2>&1 &
sup=$!

sleep 1

# pgrep -P finds children by parent pid, no name matching needed
child=$(pgrep -P "$sup" | head -1)
if [ -z "$child" ]; then
    echo "sentinel $sup never spawned a child"
    kill -9 "$sup" 2>/dev/null
    exit 1
fi

kill -TERM "$sup"
wait "$sup"
rc=$?

if [ "$rc" -ne 143 ]; then
    echo "sentinel exited $rc, expected 143 (128 + SIGTERM)"
    kill -9 "$child" 2>/dev/null
    exit 1
fi

if kill -0 "$child" 2>/dev/null; then
    echo "child $child survived, it should have been signalled"
    kill -9 "$child" 2>/dev/null
    exit 1
fi