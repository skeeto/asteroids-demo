CC      = cl /nologo
RC      = rc /nologo
CFLAGS  = /O2

asteroids.exe: asteroids.c icon.res
	$(CC) $(CFLAGS) asteroids.c icon.res

icon.rc:
	echo 1 ICON "asteroids.ico" >$@

clean:
	if exist icon.rc       del icon.rc
	if exist icon.res      del icon.res
	if exist asteroids.exe del asteroids.exe
