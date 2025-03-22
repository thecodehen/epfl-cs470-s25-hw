TARGET_EXEC := simulate
BUILD_DIR := build
SRC_DIR := src

# finds all the .cpp files in the src directory
SRCS := $(shell find $(SRC_DIR) -name *.cpp)

# prepends BUILD_DIR and appends .o to every src file
OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)

INC_DIRS := $(shell find $(SRC_DIR) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

CXX := g++
CXXFLAGS := $(INC_FLAGS) -std=c++17

all: $(BUILD_DIR)/$(TARGET_EXEC)

# final build step
$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

# cpp sources
$(BUILD_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -r $(BUILD_DIR)