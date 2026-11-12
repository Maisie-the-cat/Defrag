CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
LDFLAGS ?= -pthread
LDLIBS ?= -lncurses

TARGET = defrag
SRCS = defrag_gui.cpp defrag_core.cpp
OBJS = $(SRCS:.cpp=.o)
TEST_TARGET = core_tests

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_TARGET): tests/core_tests.cpp defrag_core.cpp
	$(CXX) $(CXXFLAGS) -o $@ tests/core_tests.cpp $(LDFLAGS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_TARGET)

.PHONY: all clean test
