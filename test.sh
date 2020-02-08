#!/bin/bash
set -e

ls tests/ | grep '\.cpp$' | while read f; do
	echo "$ ./icpp tests/$f"
	./icpp tests/$f | md5sum -c tests/md5sum/${f%.cpp}.md5sum
done

echo '$ ./icpp tests/007-argc-argv.cpp abc def "123 xyz"'
./icpp tests/007-argc-argv.cpp abc def "123 xyz" | md5sum -c tests/md5sum/007-argc-argv.with-args.md5sum

echo "all passed."
