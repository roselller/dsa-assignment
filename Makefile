# C++ Programming Forum
#
#   make            build everything
#   make test       build and run the unit tests and end-to-end scripts
#   make asan       run `make test` again with AddressSanitizer + UBSan
#   make run-web    start the web server on http://127.0.0.1:8080
#   make run        start the terminal client
#   make clean

ifeq ($(origin CXX),default)
CXX := clang++
endif

BUILD    ?= build
SAN      ?=
OPT      ?= -O2
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic $(OPT) -g -pthread -MMD -MP -Isrc -isystem third_party $(SAN)
LDFLAGS  := -pthread $(SAN)

# Code shared by both front-ends and the tests
LIB_SRC := src/Sha256.cpp src/StringSearch.cpp src/Comment.cpp src/Account.cpp src/Storage.cpp src/Forum.cpp
LIB_OBJ := $(LIB_SRC:%.cpp=$(BUILD)/%.o)

# Executables; each is only built once its main file exists
BINS :=
ifneq ($(wildcard src/web_main.cpp),)
BINS += $(BUILD)/forum-web
endif
ifneq ($(wildcard src/Terminal.cpp),)
BINS += $(BUILD)/forum
endif

TEST_SRC := $(wildcard tests/test_*.cpp)
TEST_BIN := $(TEST_SRC:tests/%.cpp=$(BUILD)/tests/%)
E2E      := $(wildcard tests/*_e2e.sh)

.PHONY: all test asan run run-web clean
.SECONDARY:
all: $(BINS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/tests/test_%: $(BUILD)/tests/test_%.o $(LIB_OBJ)
	$(CXX) -o $@ $^ $(LDFLAGS)

test: $(TEST_BIN) $(BINS)
	@set -e; for t in $(TEST_BIN); do echo "== $$t"; $$t; done
	@set -e; for s in $(E2E); do echo "== $$s"; BUILD=$(BUILD) bash $$s; done

asan:
	$(MAKE) BUILD=build-asan OPT=-O1 SAN="-fsanitize=address,undefined -fno-omit-frame-pointer" test

run-web: $(BUILD)/forum-web
	$(BUILD)/forum-web --data data --web web

run: $(BUILD)/forum
	$(BUILD)/forum --data data

clean:
	rm -rf build build-asan

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
