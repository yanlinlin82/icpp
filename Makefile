.PHONY: all clean test

all: icpp

clean:
	@rm -fv debug

test:
	bash tests/run.sh

icpp: icpp.cpp
	g++ -Wall -std=c++17 $< -o $@
