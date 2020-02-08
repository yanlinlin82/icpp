#!/bin/bash
set -e
ls tests/ | grep '\.cpp$' | while read f; do
	echo tests/$f
	./icpp tests/$f | md5sum -c tests/md5sum/${f%.cpp}.md5sum
done
echo "all passed."
