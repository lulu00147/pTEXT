#!/bin/sh
set -eu
cd "$(dirname "$0")"

sh run_reader_tests.sh
sh run_sanitizers.sh
sh run_libfuzzer.sh
