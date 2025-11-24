CXX     = g++
CXXFLAGS = -Wall -Wextra -std=c++17 -O2

TARGET  = marker
SRC     = src/marker.cpp

RUBRIC  = data/rubric.txt

.PHONY: all clean run reset_rubric

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC)

# Regenerate the base rubric file
reset_rubric:
	@mkdir -p data
	@echo "Resetting rubric to base values..."
	@printf "1, A\n2, B\n3, C\n4, D\n5, E\n" > $(RUBRIC)

run: $(TARGET)
	./$(TARGET) 3 data/rubric.txt data/exams

clean: 
	rm -f $(TARGET)
	$(MAKE) reset_rubric
