#!/bin/bash
# usage: backoff.sh <sentinel> <worker>
# the pause between restarts should double each time instead of staying flat

sentinel=$1
worker=$2

log=$(mktemp)
"$sentinel" --restart "$worker" fail > "$log" 2>&1 &
sup=$!

# 1 + 2 + 4 is seven seconds of waiting plus the runs themselves
sleep 8
kill -TERM "$sup" 2>/dev/null
wait "$sup" 2>/dev/null

delays=$(grep -o "in [0-9]*s" "$log" | grep -o "[0-9]*" | head -3 | tr '\n' ' ' | sed 's/ *$//')
rm -f "$log"

if [ "$delays" != "1 2 4" ]; then
    echo "expected the delay to double, got: '$delays'"
    exit 1
fi
