.POSIX:
CROSS   =
CC      = $(CROSS)gcc -std=c99
CFLAGS  = -DNDEBUG -ffast-math -Os
LDFLAGS = -s
LDLIBS  = -lwinmm -lgdi32 -lopengl32 -ldsound
WINDRES = $(CROSS)windres

asteroids.exe: asteroids.c icon.o
	$(CC) $(CFLAGS) -mwindows $(LDFLAGS) -o $@ asteroids.c icon.o $(LDLIBS)

all: asteroids.exe

icon.o: asteroids.ico
	echo '1 ICON "asteroids.ico"' | $(WINDRES) -o $@

clean:
	rm -f asteroids.exe icon.o
