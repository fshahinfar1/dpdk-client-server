/* vim: set et ts=2 sw=2: */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zipf.h"

int main(int argc, char *argv[])
{
  int print = 1;
  if (argc > 1) {
    print = 0;
  }
  struct zipfgen *z;
  z = new_zipfgen(1000000, /* skewness: */ 0);
  if (print) {
    for (int i = 0; i < 1000*1000; i++) {
      int t = z->gen(z) - 1;
      fprintf(stdout, "%d\n", t);
    }
  } else {
    volatile long long x = 0;
    for (int i = 0; i < 1000*1000; i++) {
      x += z->gen(z) - 1;
    }
  }
  free_zipfgen(z);
  return 0;
}
