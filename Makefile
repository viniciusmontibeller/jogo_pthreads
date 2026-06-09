CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -pthread
TARGET = jogo_pthreads
SRC = jogo_pthreads.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)