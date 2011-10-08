#!/bin/bash

cat "$1" | espeak -q 2>&1 | grep -F "word not found" | sed -e "s/word not found: //" | sort | uniq
