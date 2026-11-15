CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Isrc
LDFLAGS := -lssl -lcrypto -lpthread

TARGET := bin/usuarios
SRCS := src/main.cpp src/auth.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -rf bin build

.PHONY: all clean
