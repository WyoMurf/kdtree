CC = gcc
CFLAGS = -std=gnu89 -O2 -Wall -Wno-unused-function -Wno-unused-variable \
         -Wno-unused-but-set-variable -Wno-format -Wno-pointer-to-int-cast \
         -Wno-int-to-pointer-cast -Wno-implicit-int \
         -Wno-implicit-function-declaration -Wno-return-type
LDFLAGS = -lm

# Static linking on Windows to avoid DLL issues
ifeq ($(OS),Windows_NT)
  LDFLAGS += -static
endif

TESTS = kd_test_soft kd_test_hard

.PHONY: all test clean

all: $(TESTS)

kd_test_soft: kd.c kd_test_soft.c kd.h
	$(CC) $(CFLAGS) -o $@ kd.c kd_test_soft.c $(LDFLAGS)

kd_test_hard: kd.c kd_test_hard.c kd.h
	$(CC) $(CFLAGS) -o $@ kd.c kd_test_hard.c $(LDFLAGS)

# Run both tests in parallel with exit code checking
test: $(TESTS)
	@echo "=== Running tests in parallel ==="
	@./kd_test_soft$(EXEEXT) & PID1=$$!; \
	 ./kd_test_hard$(EXEEXT) & PID2=$$!; \
	 FAIL=0; \
	 wait $$PID1 || FAIL=1; \
	 wait $$PID2 || FAIL=1; \
	 if [ $$FAIL -ne 0 ]; then echo "=== TESTS FAILED ==="; exit 1; fi
	@echo "=== All tests passed ==="

clean:
	rm -f kd_test_soft kd_test_hard kd_test_soft.exe kd_test_hard.exe kd_test.exe *.o out.txt
