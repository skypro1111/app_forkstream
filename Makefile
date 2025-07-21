#
# Simple Makefile for app_forkstream module
#
# This is a standalone Makefile for development and testing purposes
# In production, the module would be integrated into Asterisk's build system
#

CC=gcc
CFLAGS=-Wall -Wextra -std=c99 -fPIC -shared
ASTERISK_INCLUDE_DIR=../asterisk_context_files
LDFLAGS=-shared

# Module name
MODULE=app_forkstream.so
SOURCE=app_forkstream.c
TEST_PROGRAM=test_args
TEST_SOURCE=tests/test_args.c
FRAMEHOOK_TEST=test_framehook
FRAMEHOOK_SOURCE=tests/test_framehook.c
HANGUP_TEST=test_hangup
HANGUP_SOURCE=tests/test_hangup.c
STRESS_TEST=stress_test
STRESS_SOURCE=tests/stress_test.c

# Default target
all: $(MODULE)

# Build the module
$(MODULE): $(SOURCE)
	$(CC) $(CFLAGS) -I$(ASTERISK_INCLUDE_DIR) $(SOURCE) -o $(MODULE) $(LDFLAGS)

# Build test program
$(TEST_PROGRAM): $(TEST_SOURCE)
	$(CC) -Wall -Wextra -std=c99 $(TEST_SOURCE) -o $(TEST_PROGRAM)

# Build framehook test program
$(FRAMEHOOK_TEST): $(FRAMEHOOK_SOURCE)
	$(CC) -Wall -Wextra -std=c99 $(FRAMEHOOK_SOURCE) -o $(FRAMEHOOK_TEST)

# Build hangup handler test program
$(HANGUP_TEST): $(HANGUP_SOURCE)
	$(CC) -Wall -Wextra -std=c99 $(HANGUP_SOURCE) -o $(HANGUP_TEST)

# Build stress test program
$(STRESS_TEST): $(STRESS_SOURCE)
	$(CC) -Wall -Wextra -std=c99 $(STRESS_SOURCE) -o $(STRESS_TEST)

# Run argument parsing tests
test: $(TEST_PROGRAM)
	./$(TEST_PROGRAM)

# Run framehook simulation test
test-framehook: $(FRAMEHOOK_TEST)
	./$(FRAMEHOOK_TEST)

# Run hangup handler test
test-hangup: $(HANGUP_TEST)
	./$(HANGUP_TEST)

# Run stress test
test-stress: $(STRESS_TEST)
	./$(STRESS_TEST)

# Run all tests
test-all: test test-framehook test-hangup
	@echo "All basic tests completed successfully!"

# Run comprehensive test suite including stress test
test-full: test-all test-stress
	@echo "Full test suite completed successfully!"

# Run UDP receiver for testing
udp-test:
	@echo "Starting UDP receiver on 127.0.0.1:8080..."
	@echo "Press Ctrl+C to stop"
	python3 tests/udp_receiver.py 127.0.0.1 8080

# Clean built files
clean:
	rm -f $(MODULE) $(TEST_PROGRAM) $(FRAMEHOOK_TEST) $(HANGUP_TEST) $(STRESS_TEST)

# Install (requires proper permissions)
install: $(MODULE)
	cp $(MODULE) /usr/lib/asterisk/modules/

.PHONY: all clean install test test-framehook test-hangup test-stress test-all test-full udp-test 