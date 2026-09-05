CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2 -Iinclude -I.

SRCS = main.cpp \
       src/normalizer.cpp \
       src/classifier.cpp \
       src/parsers.cpp \
       src/ingestion.cpp \
       src/analyzer.cpp \
       src/detection.cpp \
       src/risk.cpp \
       src/alerts.cpp \
       src/recommendations.cpp \
       src/report.cpp \
       src/exporter.cpp \
       src/tests.cpp

BUILD_DIR = build
OBJS = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRCS))
TARGET = bin/log_analyzer

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

test: $(TARGET)
	./$(TARGET) --test

demo: $(TARGET)
	./$(TARGET) --demo

gui:
	@echo "Launching cross-platform GUI dashboard..."
	@if [ "$$(uname)" = "Darwin" ]; then open gui/index.html; \
	elif [ "$$(uname)" = "Linux" ]; then xdg-open gui/index.html; \
	else start gui/index.html; fi

clean:
	rm -rf $(BUILD_DIR) bin

.PHONY: all clean test demo gui
