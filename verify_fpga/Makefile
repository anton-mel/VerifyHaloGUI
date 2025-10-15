CXX = clang++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -mmacosx-version-min=14.0 -I. -I../asic-sender

# Target executable
TARGET = halo_verifier

# Source files
SOURCES = main.cpp halo_verifier.cpp
OBJECTS = $(SOURCES:.cpp=.o)

# Library path and linking
LDFLAGS = -L../asic-sender -lokFrontPanel -Wl,-rpath,@loader_path

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	./$(TARGET) 2437001CWG halo_seizure.bit
