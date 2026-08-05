.PHONY: all
all:
	gcc -O2 -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=implicit-int -o code main.c buddy.c
