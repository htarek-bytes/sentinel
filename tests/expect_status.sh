#!/bin/bash
# usage: expect_status.sh <expected code> <command> [args...]
# runs the command and fails if the exit status is not what we expected

expected=$1
shift

"$@" >/dev/null 2>&1
actual=$?

if [ "$actual" -ne "$expected" ]; then
    echo "expected exit $expected, got $actual"
    echo "command was: $*"
    exit 1
fi