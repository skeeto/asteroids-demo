# Asteroids Clone for Windows

This game is a simple Asteroids clone primarily intended to demonstrate
the capabilities and flexibility of [w64devkit][]. It has real-time
graphics (OpenGL), sound (DirectSound), and gamepad support (XInput).
Anyone running Windows is about a minute away from building this program
from source, without the need to install tools. It's easy for anyone to
modify and adapt, or to serve as a starting point for their own projects.

![](https://i.imgur.com/Eaa3O8R.png)

Other than the operating system and trivially-obtained compiler toolchain,
there are no build dependencies. There are also no run-time dependencies,
so distribution of the game .exe is trivial.

For an introduction and overview of the game's source code, see [Nolan
Prescott's excellent guide][guide].

## Build

Download a [w64devkit][] release, unzip anywhere, run `w64devkit.exe` to
bring up a console window, navigate to this source directory (`cd`), and
run `make`. This compiles a ready-to-play ~50kB `asteroids.exe`.

To hack on it, create a debug build by customizing `CFLAGS` and `LDFLAGS`:

    $ export LDFLAGS=""
    $ export CFLAGS="-ggdb3 -Wall -Wextra -Wdouble-promotion"
    $ CFLAGS="$CFLAGS -fsanitize=undefined -fsanitize-undefined-trap-on-error"
    $ make -e
    $ gdb ./asteroids.exe

This disables optimization, maximizes debug information, enables run-time
instrumentation, and provides linting.

## Gameplay

Lives are unlimited but every death halves your score. Each time the
asteroids are cleared the game slightly increases in difficulty.

Keyboard: Arrows keys for turning and thrust. Spacebar to shoot.

Gamepad: X or Y for thrust, and A or B to shoot. Shoulder buttons, D-pad,
or left thumbstick to turn.

## Linux and such

While the game depends explicitly on Windows, it runs comfortably on other
x86 systems via Wine. To build using a cross-compiler:

    make CROSS=x86_64-w64-mingw32-

Then run with Wine:

    wine64 ./asteroids.exe


[guide]: https://idle.nprescott.com/2021/understanding-asteroids.html
[w64devkit]: https://github.com/skeeto/w64devkit
