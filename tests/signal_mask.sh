#!/bin/bash
# usage: signal_mask.sh <sentinel>
# exec keeps the signal mask, so the child needs a clean one. if it does not
# get one then whatever sentinel launches cannot be interrupted at all.

sentinel=$1

out=$("$sentinel" grep -m1 '^SigBlk' /proc/self/status 2>/dev/null)
mask=$(echo "$out" | awk '/SigBlk/ {print $2}')

if [ -z "$mask" ]; then
    echo "could not read SigBlk out of the child"
    echo "got: $out"
    exit 1
fi

if [ "$mask" != "0000000000000000" ]; then
    echo "child inherited a blocked mask: $mask"
    echo "SIGINT is bit 2 and SIGTERM is bit 15, so 4002 means both are blocked"
    exit 1
fi
