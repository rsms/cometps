INCDIRS = /opt/local/include
LIBDIRS = /opt/local/lib
LIBS = event

CFLAGS  = -Wall -O2 -DNDEBUG $(addprefix -I, $(INCDIRS))
LDLIBS = $(addprefix -l, $(LIBS) $(LIBS_$(notdir $*)))
LDFLAGS = $(addprefix -L, $(LIBDIRS)) $(LDLIBS)
SOURCES = cometpsd.c
OBJECTS = $(SOURCES:.c=.o)
EXECUTABLE = cometpsd

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf *.o $(EXECUTABLE)

.PHONY: all clean
