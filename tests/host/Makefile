# Frame codec host tests (stage 4): pure C++ — no Pico SDK needed.
# Run: make test   (or:  make build && ./test_frame_codec)

CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -I../../include -I../../src/protocol

TEST_SRC = test_frame_codec.cpp \
           ../../src/protocol/frame_codec.cpp

test_frame_codec: $(TEST_SRC) ../../src/protocol/frame_codec.h
	$(CXX) $(CXXFLAGS) -o $@ $(TEST_SRC)

.PHONY: test
test: test_frame_codec
	./test_frame_codec

.PHONY: clean
clean:
	rm -f test_frame_codec
