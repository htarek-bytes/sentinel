#!/bin/bash
# usage: restart.sh <sentinel> <worker>
# --restart should bring the child back after it dies, and leaving the flag
# off should keep the old run once behaviour

sentinel=$1
worker=$2

# with the flag
log=$(mktemp)
"$sentinel" --restart "$worker" fail > "$log" 2>&1 &
sup=$!

sleep 4
kill -TERM "$sup" 2>/dev/null
wait "$sup" 2>/dev/null

starts=$(grep -c "started" "$log")
rm -f "$log"

if [ "$starts" -lt 2 ]; then
    echo "with --restart the child should come back, but it started $starts time(s)"
    exit 1
fi

# without the flag
log=$(mktemp)
"$sentinel" "$worker" fail > "$log" 2>&1
starts=$(grep -c "started" "$log")
rm -f "$log"

if [ "$starts" -ne 1 ]; then
    echo "without --restart it should run once, but it started $starts time(s)"
    exit 1
fi
