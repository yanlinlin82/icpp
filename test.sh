for f in tests/*.cpp; do echo $f; ./icpp $f | md5sum -c ${f/cpp/md5sum} || exit 1; echo; done; echo "all passed."
