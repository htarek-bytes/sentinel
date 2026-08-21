#!/bin/bash
# usage: metrics.sh <sentinel> <worker>
# the endpoint should serve prometheus text format and the numbers should move

sentinel=$1
worker=$2
port=19099

"$sentinel" --restart --metrics-port "$port" "$worker" fail > /dev/null 2>&1 &
sup=$!

sleep 4
body=$(curl -s --max-time 5 "http://127.0.0.1:$port/metrics")
kill -TERM "$sup" 2>/dev/null
wait "$sup" 2>/dev/null

if [ -z "$body" ]; then
    echo "nothing came back from port $port"
    exit 1
fi

for name in sentinel_restarts_total sentinel_crashes_total \
            sentinel_failures_total sentinel_child_up; do
    if ! echo "$body" | grep -q "^# TYPE $name "; then
        echo "no TYPE line for $name"
        exit 1
    fi
    if ! echo "$body" | grep -q "^$name "; then
        echo "no sample for $name"
        exit 1
    fi
done

failures=$(echo "$body" | grep "^sentinel_failures_total " | awk '{print $2}')
if [ "$failures" -lt 2 ]; then
    echo "the failure counter should have moved by now, got $failures"
    exit 1
fi
