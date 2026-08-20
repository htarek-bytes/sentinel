#!/bin/bash
# usage: orphan_reaping.sh <sentinel> <worker>
# an orphaned grandchild should be handed to sentinel, not to pid 1

sentinel=$1
worker=$2

# the inner shell starts the worker and then exits, which orphans it. the
# outer shell keeps sleeping so sentinel is still around to adopt it.
"$sentinel" bash -c "bash -c '$worker sleep >/dev/null 2>&1 &' ; sleep 8" >/dev/null 2>&1 &
sup=$!

sleep 2

orphan=$(pgrep -x worker | head -1)
if [ -z "$orphan" ]; then
    echo "the orphaned worker never started"
    kill -TERM "$sup" 2>/dev/null
    exit 1
fi

ppid=$(ps -o ppid= -p "$orphan" | tr -d ' ')

kill -TERM "$sup" 2>/dev/null
wait "$sup" 2>/dev/null
pkill -x worker 2>/dev/null

if [ "$ppid" != "$sup" ]; then
    echo "orphan $orphan was handed to pid $ppid, expected sentinel ($sup)"
    exit 1
fi
