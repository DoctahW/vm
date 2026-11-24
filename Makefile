CC      = gcc
TARGET  = vm
SRC     = vm.c

CFLAGS  = -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -pthread
LDFLAGS = -pthread
LDLIBS  =

ifdef FRAMES
CFLAGS += -DN_QUADROS=$(FRAMES)
endif
ifdef SEM_THREAD
CFLAGS += -DSEM_THREAD
endif

OFICIAL = tests/vetores/oficial

.PHONY: all clean distclean rebuild tsan helgrind

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

rebuild: clean all

tsan: $(TARGET)_tsan
	cd $(OFICIAL) && ../../../$(TARGET)_tsan addresses.txt lru fifo

$(TARGET)_tsan: $(SRC)
	$(CC) $(CFLAGS) -O1 -g -fsanitize=thread -o $@ $< $(LDLIBS)

helgrind: $(TARGET)
	cd $(OFICIAL) && valgrind --tool=helgrind --error-exitcode=1 ../../../$(TARGET) addresses.txt lru fifo

clean:
	rm -f $(TARGET) $(TARGET)_tsan

distclean: clean
	rm -f correct.txt $(OFICIAL)/correct.txt
