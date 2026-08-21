#!/bin/bash
# usage: counters.sh <sentinel> <worker>
# the summary should tell a signal death apart from a plain nonzero exit

sentinel=$1
worker=$2

summary_for() {
    local mode=$1
    local log
    log=$(mktemp)
    "$sentinel" --restart "$worker" "$mode" > "$log" 2>&1 &
    local sup=$!
    sleep 4
    kill -TERM "$sup" 2>/dev/null
    wait "$sup" 2>/dev/null
    grep "restarts," "$log"
    rm -f "$log"
}

line=$(summary_for fail)
if [ -z "$line" ]; then
    echo "no summary line was printed"
    exit 1
fi
crashes=$(echo "$line" | sed -n 's/.* \([0-9]*\) crashes.*/\1/p')
failures=$(echo "$line" | sed -n 's/.* \([0-9]*\) failures.*/\1/p')
if [ "$crashes" != "0" ]; then
    echo "a nonzero exit should not count as a crash: $line"
    exit 1
fi
if [ "$failures" -lt 2 ]; then
    echo "expected at least two failures: $line"
    exit 1
fi

line=$(summary_for crash)
crashes=$(echo "$line" | sed -n 's/.* \([0-9]*\) crashes.*/\1/p')
failures=$(echo "$line" | sed -n 's/.* \([0-9]*\) failures.*/\1/p')
if [ "$failures" != "0" ]; then
    echo "a signal death should not count as a failure: $line"
    exit 1
fi
if [ "$crashes" -lt 2 ]; then
    echo "expected at least two crashes: $line"
    exit 1
fi
