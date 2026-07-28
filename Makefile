CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pedantic
LDFLAGS = -lncurses

TARGET = defrag
SRCS = defrag_gui.cpp defrag_core.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
