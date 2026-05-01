RGB_LIB_DISTRIBUTION ?= /home/user/rpi-rgb-led-matrix
RGB_INCDIR=$(RGB_LIB_DISTRIBUTION)/include
RGB_LIBDIR=$(RGB_LIB_DISTRIBUTION)/lib
RGB_LIBRARY_NAME=rgbmatrix
RGB_LIBRARY=$(RGB_LIBDIR)/lib$(RGB_LIBRARY_NAME).a

CXX ?= g++
CXXFLAGS=-Wall -Wextra -O3 -std=c++17 -Wno-unused-parameter
LDFLAGS=-L$(RGB_LIBDIR) -l$(RGB_LIBRARY_NAME) -lrt -lm -lpthread -lcurl

BIN=muni-display
SRCS=main.cpp http.cpp xbm.cpp
OBJS=$(SRCS:.cpp=.o)

all: $(BIN)

$(BIN): $(OBJS) $(RGB_LIBRARY)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) -I$(RGB_INCDIR) $(CXXFLAGS) -c -o $@ $<

$(RGB_LIBRARY):
	$(MAKE) -C $(RGB_LIBDIR)/lib

clean:
	rm -f $(OBJS) $(BIN)

.PHONY: all clean
