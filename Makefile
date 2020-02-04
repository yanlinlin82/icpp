.PHONY: all clean

all: icpp

clean:
	@rm -fv debug

icpp: icpp.cpp
	g++ -Wall -std=c++17 $< -o $@
