all:
	gcc -Wall caps2esc.c -o caps2esc -I/usr/include/libevdev-1.0 -levdev -ludev
