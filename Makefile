FLAGS=$(shell pkg-config --libs --cflags libevdev libudev)

all:
	gcc -Wall caps2esc.c -o caps2esc ${FLAGS}
