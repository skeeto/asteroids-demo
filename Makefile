.POSIX:
CROSS   =
CC      = $(CROSS)gcc
CFLAGS  = -std=c99 -DNDEBUG -ffast-math -Os
LDFLAGS = -s -mwindows
LDLIBS  = -lgdi32 -lopengl32 -ldsound
WINDRES = $(CROSS)windres

D_CFLAGS  = -Wall -Wextra -Wdouble-promotion -g -Og
D_LDFLAGS = -mconsole

asteroids.exe: asteroids.c icon.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ asteroids.c icon.o $(LDLIBS)

all: asteroids.exe debug.exe

debug.exe: asteroids.c
	$(CC) $(D_CFLAGS) $(D_LDFLAGS) -o $@ asteroids.c $(LDLIBS)

icon.o: asteroids.ico
	echo '1 ICON "asteroids.ico"' | $(WINDRES) -o $@

clean:
	rm -f debug.exe asteroids.exe icon.o
