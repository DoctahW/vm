#include <stdio.h>

#define ARQ_BACKING "BACKING_STORE.bin"
#define ARQ_SAIDA   "correct.txt"

#define TAM_PAGINA 256
#define N_PAGINAS  256
#define TAM_TLB    16

// sobrescrito por make FRAMES=256
#ifndef N_QUADROS
#define N_QUADROS 128
#endif

int main(void) {
    return 0;
}
