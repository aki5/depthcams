#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

typedef unsigned char uchar;
typedef unsigned short u16int;
typedef unsigned int u32int;
typedef unsigned long long u64int;

#define nelem(x) (int)(sizeof(x)/sizeof(x[0]))
