RGB_LIB_DISTRIBUTION ?= /home/user/rpi-rgb-led-matrix
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a

CXX ?= g++
CXXFLAGS=-Wall -Wextra -O3 -std=c++17 -Wno-unused-parameter -Isrc -I$(RGB_INCDIR)
LDFLAGS=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread -lcurl

BIN=muni-display
SRCS=$(shell find src -name '*.cpp')
OBJS=$(SRCS:.cpp=.o)

all: $(BIN)

$(BIN): $(OBJS) $(RGB_LIBRARY)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(RGB_LIBRARY):
	$(MAKE) -C $(RGB_LIBDIR)/lib

clean:
	find src -name '*.o' -delete
	rm -f $(BIN)

.PHONY: all clean
