# Makefile for ext4_defrag
CC = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pedantic
LDFLAGS = 
TARGET = ext4_defrag
TARGET_GUI = ext4_defrag_gui

SRC = ext4_defrag.cpp
SRC_GUI = ext4_defrag_gui.cpp
OBJ = ext4_defrag.o
OBJ_GUI = ext4_defrag_gui.o

all: $(TARGET) $(TARGET_GUI)

$(TARGET): $(OBJ)
	$(CC) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(TARGET_GUI): $(OBJ_GUI)
	$(CC) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) -lncurses

$(OBJ): $(SRC)
	$(CC) $(CXXFLAGS) -c $< -o $@

$(OBJ_GUI): $(SRC_GUI)
	$(CC) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(OBJ_GUI) $(TARGET) $(TARGET_GUI)

.PHONY: all clean