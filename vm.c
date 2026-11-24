#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef SEM_THREAD
#include <pthread.h>
#endif

#define ARQ_BACKING "BACKING_STORE.bin"
#define ARQ_SAIDA   "correct.txt"

#define TAM_PAGINA 256
#define N_PAGINAS  256
#define TAM_TLB    16
#define MAX_ENDERECO (N_PAGINAS * TAM_PAGINA - 1)
#define TAM_BACKING  ((long)N_PAGINAS * TAM_PAGINA)

// sobrescrito por make FRAMES=256
#ifndef N_QUADROS
#define N_QUADROS 128
#endif

// erros
// mesmo padrão de erro do ProcessFlow, Scheduler e banker
typedef enum ErroParse {
    OK = 0,
    ERR_NAO_NUMERICO,
    ERR_NEGATIVO,
    ERR_FORA_FAIXA,
    ERR_VALOR_EXTRA,
    ERR_SEM_ENDERECOS,
    ERR_LEITURA,
    ERR_MEMORIA
} ErroParse;

static const char *erro_msg(ErroParse e) {
    switch (e) {
        case OK:                return "sem erro";
        case ERR_NAO_NUMERICO:  return "valor nao e um inteiro valido";
        case ERR_NEGATIVO:      return "endereco nao pode ser negativo";
        case ERR_FORA_FAIXA:    return "endereco fora do espaco de enderecos (0 a 65535)";
        case ERR_VALOR_EXTRA:   return "mais de um valor na linha (um endereco por linha)";
        case ERR_SEM_ENDERECOS: return "o arquivo nao tem nenhum endereco";
        case ERR_LEITURA:       return "falha de leitura do arquivo";
        case ERR_MEMORIA:       return "memoria insuficiente";
    }
    return "motivo desconhecido";
}

// parser

typedef enum { ALG_FIFO, ALG_LRU } Algoritmo;

typedef struct {
    int *v;
    int n;
} ListaEnderecos;

static int eh_espaco(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *aparar(char *texto) {
    while (eh_espaco(*texto)) {
        texto++;
    }
    char *fim = texto + strlen(texto);
    while (fim > texto && eh_espaco(fim[-1])) {
        fim--;
    }
    *fim = '\0';
    return texto;
}

// para_nao_neg_int da atvd5, restrito a 0..maximo.
static ErroParse para_nao_neg_int(const char *texto, int maximo, int *saida) {
    // strtol aceitaria "+5"; endereco e so digito
    if (*texto == '+') {
        return ERR_NAO_NUMERICO;
    }

    char *fim;
    errno = 0;
    long valor = strtol(texto, &fim, 10);

    if (fim == texto) {
        return ERR_NAO_NUMERICO;
    }
    if (*fim != '\0') {
        return eh_espaco(*fim) ? ERR_VALOR_EXTRA : ERR_NAO_NUMERICO;
    }
    if (valor < 0) {
        return ERR_NEGATIVO;
    }
    if (errno == ERANGE || valor > maximo) {
        return ERR_FORA_FAIXA;
    }

    *saida = (int)valor;
    return OK;
}

// devolve o bloco com espaco para mais um item de tam_item bytes (a capacidade
// dobra); NULL se faltar memoria, e nesse caso o bloco antigo continua valido
static void *cresce(void *bloco, int *capacidade, int usados, size_t tam_item) {
    if (usados < *capacidade) {
        return bloco;
    }
    if (*capacidade > INT_MAX / 2) {
        return NULL;
    }
    int nova = *capacidade ? *capacidade * 2 : 1024;
    if ((size_t)nova > SIZE_MAX / tam_item) {
        return NULL;
    }
    void *novo = realloc(bloco, (size_t)nova * tam_item);
    if (novo == NULL) {
        return NULL;
    }
    *capacidade = nova;
    return novo;
}

static ErroParse fim_de_leitura(FILE *arquivo) {
    if (feof(arquivo)) {
        return OK;
    }
    if (ferror(arquivo)) {
        return ERR_LEITURA;
    }
    return ERR_MEMORIA;
}

static bool le_algoritmo(const char *texto, Algoritmo *saida) {
    if (strcmp(texto, "fifo") == 0) {
        *saida = ALG_FIFO;
        return true;
    }
    if (strcmp(texto, "lru") == 0) {
        *saida = ALG_LRU;
        return true;
    }
    return false;
}

static ErroParse le_enderecos(FILE *arquivo, ListaEnderecos *out, char *erro, size_t erro_sz) {
    out->v = NULL;
    out->n = 0;

    char *linha = NULL;
    size_t linha_cap = 0;
    ssize_t lido;
    int *enderecos = NULL;
    int capacidade = 0;
    int n = 0;
    int nlinha = 0;
    ErroParse resultado = OK;

    while ((lido = getline(&linha, &linha_cap, arquivo)) != -1) {
        nlinha++;

        if (strlen(linha) != (size_t)lido) {
            resultado = ERR_NAO_NUMERICO;
            snprintf(erro, erro_sz, "linha %d: %s (byte nulo)", nlinha, erro_msg(resultado));
            break;
        }

        char *conteudo = aparar(linha);
        if (*conteudo == '\0') {
            continue;
        }

        int endereco;
        resultado = para_nao_neg_int(conteudo, MAX_ENDERECO, &endereco);
        if (resultado != OK) {
            snprintf(erro, erro_sz, "linha %d ('%.40s'): %s", nlinha, conteudo, erro_msg(resultado));
            break;
        }

        int *maior = cresce(enderecos, &capacidade, n, sizeof *enderecos);
        if (maior == NULL) {
            resultado = ERR_MEMORIA;
            snprintf(erro, erro_sz, "linha %d: %s", nlinha, erro_msg(resultado));
            break;
        }
        enderecos = maior;
        enderecos[n++] = endereco;
    }

    if (resultado == OK) {
        int erro_leitura = errno;
        resultado = fim_de_leitura(arquivo);
        if (resultado == ERR_LEITURA) {
            snprintf(erro, erro_sz, "linha %d: %s: %s", nlinha + 1, erro_msg(resultado),
                     strerror(erro_leitura));
        } else if (resultado != OK) {
            snprintf(erro, erro_sz, "linha %d: %s", nlinha + 1, erro_msg(resultado));
        }
    }

    if (resultado == OK && n == 0) {
        resultado = ERR_SEM_ENDERECOS;
        snprintf(erro, erro_sz, "%s", erro_msg(resultado));
    }

    free(linha);
    if (resultado != OK) {
        free(enderecos);
        return resultado;
    }
    out->v = enderecos;
    out->n = n;
    return OK;
}

// backing store

// abre e confere que e um arquivo regular de exatamente 65536 bytes; o FILE*
// fica aberto para as leituras de pagina sob demanda
static FILE *abre_backing_store(void) {
    FILE *arquivo = fopen(ARQ_BACKING, "rb");
    if (arquivo == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s': %s\n", ARQ_BACKING, strerror(errno));
        return NULL;
    }

    struct stat info;
    if (fstat(fileno(arquivo), &info) != 0) {
        fprintf(stderr, "Erro: nao foi possivel consultar '%s': %s\n", ARQ_BACKING, strerror(errno));
        fclose(arquivo);
        return NULL;
    }
    if (!S_ISREG(info.st_mode)) {
        fprintf(stderr, "Erro: '%s' invalido: nao e um arquivo regular\n", ARQ_BACKING);
        fclose(arquivo);
        return NULL;
    }
    if (info.st_size != TAM_BACKING) {
        fprintf(stderr, "Erro: '%s' invalido: tem %lld bytes, esperado %ld\n",
                ARQ_BACKING, (long long)info.st_size, TAM_BACKING);
        fclose(arquivo);
        return NULL;
    }
    return arquivo;
}

// tlb

typedef struct {
    int pagina;
    int quadro;
    bool valida;
    unsigned long uso;
} EntradaTLB;

typedef struct {
    EntradaTLB entradas[TAM_TLB];
    int prox_fifo;
} Tlb;

static void tlb_inicializa(Tlb *tlb) {
    for (int i = 0; i < TAM_TLB; i++) {
        tlb->entradas[i] = (EntradaTLB){ .pagina = -1, .quadro = -1, .valida = false, .uso = 0 };
    }
    tlb->prox_fifo = 0;
}

#ifdef SEM_THREAD

// busca sequencial (make SEM_THREAD=1)
static int tlb_busca(const Tlb *tlb, int pagina) {
    for (int i = 0; i < TAM_TLB; i++) {
        if (tlb->entradas[i].valida && tlb->entradas[i].pagina == pagina) {
            return i;
        }
    }
    return -1;
}

#else

typedef struct {
    const EntradaTLB *entrada;
    int pagina;
    bool achou;
} ArgBusca;

static void *compara_entrada(void *arg) {
    ArgBusca *busca = arg;
    busca->achou = busca->entrada->valida && busca->entrada->pagina == busca->pagina;
    return NULL;
}

// uma thread por entrada da tlb. cada thread so le a propria entrada e so
// escreve no proprio ArgBusca, entao nenhum dado compartilhado e escrito durante
// a busca e nao ha mutex; a tlb so e alterada depois do join (barreira).
// se alguma criacao falhar, a thread principal compara aquela entrada (mesmo
// resultado), como o criada[i] do mandelbrot
static int tlb_busca(const Tlb *tlb, int pagina) {
    pthread_t threads[TAM_TLB];
    ArgBusca args[TAM_TLB];
    bool criada[TAM_TLB];

    for (int i = 0; i < TAM_TLB; i++) {
        args[i] = (ArgBusca){ .entrada = &tlb->entradas[i], .pagina = pagina, .achou = false };
        criada[i] = pthread_create(&threads[i], NULL, compara_entrada, &args[i]) == 0;
    }
    for (int i = 0; i < TAM_TLB; i++) {
        if (criada[i]) {
            pthread_join(threads[i], NULL);
        } else {
            compara_entrada(&args[i]);
        }
    }

    for (int i = 0; i < TAM_TLB; i++) {
        if (args[i].achou) {
            return i;
        }
    }
    return -1;
}

#endif

// fifo: ponteiro circular puro, mesmo que haja entrada invalidada (buraco);
// lru: primeira entrada invalida, senao a de menor uso
static int tlb_insere(Tlb *tlb, Algoritmo alg, int pagina, int quadro, unsigned long relogio) {
    int indice;
    if (alg == ALG_FIFO) {
        indice = tlb->prox_fifo;
        tlb->prox_fifo = (indice + 1) % TAM_TLB;
    } else {
        indice = -1;
        for (int i = 0; i < TAM_TLB && indice < 0; i++) {
            if (!tlb->entradas[i].valida) {
                indice = i;
            }
        }
        if (indice < 0) {
            indice = 0;
            for (int i = 1; i < TAM_TLB; i++) {
                if (tlb->entradas[i].uso < tlb->entradas[indice].uso) {
                    indice = i;
                }
            }
        }
    }

    tlb->entradas[indice] = (EntradaTLB){
        .pagina = pagina, .quadro = quadro, .valida = true, .uso = relogio
    };
    return indice;
}

static void tlb_invalida(Tlb *tlb, int pagina) {
    for (int i = 0; i < TAM_TLB; i++) {
        if (tlb->entradas[i].valida && tlb->entradas[i].pagina == pagina) {
            tlb->entradas[i].valida = false;
        }
    }
}

// memoria virtual

// estado de tamanho fixo, sem alocacao dinamica: vive na pilha do main
// (memoria fisica + tabela, quadros e tlb: ~35 KiB com 128 quadros, ~68 KiB
// com 256); so o vetor de enderecos e dinamico
typedef struct {
    int tabela[N_PAGINAS];
    int dono[N_QUADROS];
    signed char memoria[N_QUADROS][TAM_PAGINA];
    unsigned long uso_quadro[N_QUADROS];
    int prox_livre;
    int prox_vitima_fifo;
    Tlb tlb;
    unsigned long relogio;
    long faults;
    long hits;
} MemoriaVirtual;

static void inicializa_memoria(MemoriaVirtual *mv) {
    for (int p = 0; p < N_PAGINAS; p++) {
        mv->tabela[p] = -1;
    }
    for (int q = 0; q < N_QUADROS; q++) {
        mv->dono[q] = -1;
        mv->uso_quadro[q] = 0;
    }
    memset(mv->memoria, 0, sizeof mv->memoria);
    mv->prox_livre = 0;
    mv->prox_vitima_fifo = 0;
    tlb_inicializa(&mv->tlb);
    mv->relogio = 0;
    mv->faults = 0;
    mv->hits = 0;
}

static bool carrega_pagina(FILE *backing, int pagina, signed char *destino) {
    if (fseek(backing, (long)pagina * TAM_PAGINA, SEEK_SET) != 0) {
        fprintf(stderr, "Erro: falha ao posicionar '%s' na pagina %d: %s\n",
                ARQ_BACKING, pagina, strerror(errno));
        return false;
    }
    if (fread(destino, 1, TAM_PAGINA, backing) != TAM_PAGINA) {
        if (ferror(backing)) {
            fprintf(stderr, "Erro: falha ao ler '%s' na pagina %d: %s\n",
                    ARQ_BACKING, pagina, strerror(errno));
        } else {
            fprintf(stderr, "Erro: '%s' terminou antes da pagina %d\n", ARQ_BACKING, pagina);
        }
        return false;
    }
    return true;
}

// fifo: ponteiro circular (os quadros enchem em ordem, logo a ordem de chegada
// das paginas depois de cheia e a circular); lru: menor uso_quadro
static int escolhe_vitima(MemoriaVirtual *mv, Algoritmo alg) {
    if (alg == ALG_FIFO) {
        int quadro = mv->prox_vitima_fifo;
        mv->prox_vitima_fifo = (quadro + 1) % N_QUADROS;
        return quadro;
    }

    int quadro = 0;
    for (int q = 1; q < N_QUADROS; q++) {
        if (mv->uso_quadro[q] < mv->uso_quadro[quadro]) {
            quadro = q;
        }
    }
    return quadro;
}

static bool traduz(MemoriaVirtual *mv, int endereco, Algoritmo alg_pagina, Algoritmo alg_tlb,
                   FILE *backing, int *indice_tlb, int *fisico, int *valor) {
    mv->relogio++;

    int pagina = endereco >> 8;
    int deslocamento = endereco & (TAM_PAGINA - 1);
    int quadro;
    int indice = tlb_busca(&mv->tlb, pagina);

    if (indice >= 0) {
        mv->hits++;
        quadro = mv->tlb.entradas[indice].quadro;
        mv->tlb.entradas[indice].uso = mv->relogio;
    } else {
        quadro = mv->tabela[pagina];

        if (quadro < 0) {
            mv->faults++;

            if (mv->prox_livre < N_QUADROS) {
                quadro = mv->prox_livre++;
            } else {
                quadro = escolhe_vitima(mv, alg_pagina);
                int despejada = mv->dono[quadro];
                if (despejada >= 0) {
                    mv->tabela[despejada] = -1;
                    tlb_invalida(&mv->tlb, despejada);
                }
            }

            if (!carrega_pagina(backing, pagina, mv->memoria[quadro])) {
                return false;
            }
            mv->tabela[pagina] = quadro;
            mv->dono[quadro] = pagina;
        }

        indice = tlb_insere(&mv->tlb, alg_tlb, pagina, quadro, mv->relogio);
    }

    // todo acesso conta para o lru de pagina, inclusive hit de tlb
    mv->uso_quadro[quadro] = mv->relogio;
    *indice_tlb = indice;
    *fisico = quadro * TAM_PAGINA + deslocamento;
    *valor = mv->memoria[quadro][deslocamento];
    return true;
}

static bool escreve_saida(const char *caminho, const ListaEnderecos *enderecos,
                          Algoritmo alg_pagina, Algoritmo alg_tlb, FILE *backing,
                          MemoriaVirtual *mv) {
    FILE *saida = fopen(caminho, "w");
    if (saida == NULL) {
        fprintf(stderr, "Erro: nao foi possivel criar '%s': %s\n", caminho, strerror(errno));
        return false;
    }

    bool ok = true;
    for (int i = 0; i < enderecos->n && ok; i++) {
        int indice_tlb;
        int fisico;
        int valor;
        if (!traduz(mv, enderecos->v[i], alg_pagina, alg_tlb, backing,
                    &indice_tlb, &fisico, &valor)) {
            ok = false;
            break;
        }
        if (fprintf(saida, "Virtual address: %d TLB: %d Physical address: %d Value: %d\n",
                    enderecos->v[i], indice_tlb, fisico, valor) < 0) {
            ok = false;
        }
    }

    if (ok) {
        double taxa_faltas = (double)mv->faults / enderecos->n;
        double taxa_hits = (double)mv->hits / enderecos->n;
        if (fprintf(saida, "Number of Translated Addresses = %d\n", enderecos->n) < 0
                || fprintf(saida, "Page Faults = %ld\n", mv->faults) < 0
                || fprintf(saida, "Page Fault Rate = %.3f\n", taxa_faltas) < 0
                || fprintf(saida, "TLB Hits = %ld\n", mv->hits) < 0
                || fprintf(saida, "TLB Hit Rate = %.3f\n", taxa_hits) < 0) {
            ok = false;
        }
    }

    bool falha_gravacao = ferror(saida) != 0;
    if (fclose(saida) != 0) {
        falha_gravacao = true;
    }
    if (falha_gravacao) {
        fprintf(stderr, "Erro: falha ao gravar '%s': %s\n", caminho, strerror(errno));
        return false;
    }
    return ok;
}

// main

static bool carrega_algoritmos(char **argv, Algoritmo *pagina, Algoritmo *tlb) {
    if (!le_algoritmo(argv[2], pagina)) {
        fprintf(stderr, "Erro: algoritmo de substituicao de pagina invalido: '%s' (use fifo ou lru)\n",
                argv[2]);
        return false;
    }
    if (!le_algoritmo(argv[3], tlb)) {
        fprintf(stderr, "Erro: algoritmo de substituicao da TLB invalido: '%s' (use fifo ou lru)\n",
                argv[3]);
        return false;
    }
    return true;
}

static bool carrega_enderecos(const char *caminho, ListaEnderecos *enderecos) {
    FILE *arquivo = fopen(caminho, "r");
    if (arquivo == NULL) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s': %s\n", caminho, strerror(errno));
        return false;
    }

    char msg[256];
    ErroParse resultado = le_enderecos(arquivo, enderecos, msg, sizeof msg);
    fclose(arquivo);
    if (resultado != OK) {
        fprintf(stderr, "Erro: %s: %s\n", caminho, msg);
        return false;
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <arquivo de enderecos> <pagina: fifo|lru> <tlb: fifo|lru>\n"
                        "Exemplo: %s addresses.txt lru fifo\n", argv[0], argv[0]);
        return 1;
    }

    Algoritmo alg_pagina;
    Algoritmo alg_tlb;
    ListaEnderecos enderecos = {0};
    FILE *backing = NULL;

    // valida tudo antes de abrir correct.txt
    bool ok = carrega_algoritmos(argv, &alg_pagina, &alg_tlb)
           && carrega_enderecos(argv[1], &enderecos)
           && (backing = abre_backing_store()) != NULL;

    if (ok) {
        MemoriaVirtual mv;
        inicializa_memoria(&mv);
        ok = escreve_saida(ARQ_SAIDA, &enderecos, alg_pagina, alg_tlb, backing, &mv);
    }

    if (backing != NULL) {
        fclose(backing);
    }
    free(enderecos.v);
    return ok ? 0 : 1;
}
