CXX = g++
CXXFLAGS = -Wall -std=c++17 `pkg-config --cflags gtk+-3.0` -I.
LDFLAGS = `pkg-config --libs gtk+-3.0` -lsqlite3

Aureus: tome.cpp
	$(CXX) tome.cpp -o Aureus $(CXXFLAGS) $(LDFLAGS)
	@echo "=== Vibe Journal (now Aureus base) built! Run ./Aureus ==="

clean:
	rm -f Aureus
