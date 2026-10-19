RGB_LIB_DISTRIBUTION ?= /home/user/rpi-rgb-led-matrix
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a

CXXFLAGS=-Wall -Wextra -O3 -std=c++17 -Wno-unused-parameter -MMD -MP -Isrc -I$(RGB_INCDIR)
LDFLAGS=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread -lcurl

BIN=muni-display
SRCS=$(shell find src -name '*.cpp')
OBJS=$(SRCS:.cpp=.o)
DEPS=$(OBJS:.o=.d)

all: $(BIN)

$(BIN): $(OBJS) $(RGB_LIBRARY)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(RGB_LIBRARY):
	$(MAKE) -C $(RGB_LIBDIR)

# Host-side unit tests for the pure logic; needs neither the matrix lib nor curl.
TEST_BIN=tests/run_tests
TEST_SRCS=tests/test_main.cpp src/util/time_utils.cpp src/render/special_dates.cpp \
	src/render/special_icons.gen.cpp src/data/laundry_state.cpp

test:
	$(CXX) -Wall -Wextra -std=c++17 -Isrc -o $(TEST_BIN) $(TEST_SRCS)
	./$(TEST_BIN)

clean:
	find src -name '*.o' -delete -o -name '*.d' -delete
	rm -f $(BIN) $(TEST_BIN)

.PHONY: all clean test

-include $(DEPS)
