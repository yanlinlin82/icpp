ls tests/ | grep '\.cpp$' | while read f; do echo tests/$f; ./icpp tests/$f | md5sum -c tests/md5sum/${f%.cpp}.md5sum || exit 1; done; echo "all passed."
