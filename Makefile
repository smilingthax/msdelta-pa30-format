OBJS=bitreader/bitreader.o bitreader/huffman.o getdeltainfo.o

CPPFLAGS=-Wall -Wextra -Ibitreader

DEPS=$(patsubst %.o,%.d,$(OBJS))

.PHONY: all clean
all: dump
ifneq "$(MAKECMDGOALS)" "clean"
  -include $(DEPS)
endif

clean:
	$(RM) dump $(OBJS) $(DEPS)

%.d: %.c
	@$(CC) $(CPPFLAGS) -MM -MT"$@" -MT"$*.o" -o $@ $<  2> /dev/null

dump: dump.c $(OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

