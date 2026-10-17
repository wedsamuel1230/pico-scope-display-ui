# Host tests: pure C++ — no Pico SDK needed.
# Run: make test   (or:  make <target> && ./<target>)

CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror \
            -I../../include -I../../src/protocol -I../../src/capture

FRAME_SRC   = test_frame_codec.cpp \
              ../../src/protocol/frame_codec.cpp

CAPTURE_SRC = test_capture.cpp \
              ../../src/capture/deinterleave.cpp \
              ../../src/capture/trigger.cpp

test_frame_codec: $(FRAME_SRC) ../../src/protocol/frame_codec.h
	$(CXX) $(CXXFLAGS) -o $@ $(FRAME_SRC)

test_capture: $(CAPTURE_SRC) ../../src/capture/deinterleave.h \
              ../../src/capture/trigger.h ../../include/scope_types.h
	$(CXX) $(CXXFLAGS) -o $@ $(CAPTURE_SRC)

.PHONY: test
test: test_frame_codec test_capture
	./test_frame_codec
	./test_capture

.PHONY: clean
clean:
	rm -f test_frame_codec test_capture
