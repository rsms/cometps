INCDIRS = /opt/local/include .
LIBDIRS = /opt/local/lib
LIBS = event yaml
SOURCES = cometpsd.c yconf.c
EXECUTABLE = cometpsd

CFLAGS = -Wall $(addprefix -I, $(INCDIRS))
#CFLAGS += -std=c99
#CFLAGS += -O2 -DNDEBUG
LDLIBS = $(addprefix -l, $(LIBS) $(LIBS_$(notdir $*)))
LDFLAGS = $(addprefix -L, $(LIBDIRS)) $(LDLIBS)
OBJECTS = $(SOURCES:.c=.o)

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf *.o $(EXECUTABLE)

.PHONY: all clean
