CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
INCLUDES := -Ithird_party -Isrc
BUILD    := build
TARGET   := $(BUILD)/fleet_sim
SRCS     := $(wildcard src/*.cpp)
OBJS     := $(SRCS:src/%.cpp=$(BUILD)/%.o)
DEPS     := $(OBJS:.o=.d)

.PHONY: all debug test run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

# Sanitizer build: catches UB / memory errors while you develop
debug: CXXFLAGS := -std=c++17 -O0 -g -Wall -Wextra -Wpedantic -fsanitize=address,undefined
debug: clean $(TARGET)

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	./tests/run_tests.sh $(TARGET)

clean:
	rm -rf $(BUILD)

-include $(DEPS)
