# ==============================================================================
# DualSense -> Beitong Adaptive Trigger & Haptics Engine
# ==============================================================================

CC := clang
CFLAGS := -O3 -Wall -Wextra -I.

all: test_engine test_bidi

test_engine: ds2bt_engine.c test_engine.c
	$(CC) $(CFLAGS) ds2bt_engine.c test_engine.c -o test_engine
	./test_engine

test_bidi: bidirectional_bridge.c test_bidi.c
	$(CC) $(CFLAGS) bidirectional_bridge.c test_bidi.c -o test_bidi
	./test_bidi

clean:
	rm -f test_engine test_bidi *.o
