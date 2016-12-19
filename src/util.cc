#include <stdio.h>
#include <stdlib.h>
#include "shared.h"

void panic(const char *str){
	fprintf(stderr, "panic: %s\n", str);
	exit(-2);
}
