<div align="center">

# vm

Gerenciador de memória virtual em C, com a TLB buscada por 16 threads.

[Rodando](#rodando) • [Entrada e saída](#entrada-e-saída) • [Como funciona](#como-funciona) • [Erros](#erros) • [Testes](#testes) • [Estrutura](#estrutura)

</div>

---

Implementação 6 de Infraestrutura de Software (00016), CESAR School. É o *Designing a
Virtual Memory Manager* do Silberschatz (10ª ed., p. P-51) com 128 quadros,
substituição de página e de TLB `fifo` ou `lru` e uma thread por entrada na busca da
TLB. Com os arquivos do professor, `./vm addresses.txt lru fifo` gera um `correct.txt`
idêntico byte a byte ao oficial.

# Rodando

Precisa de um compilador C11, GNU Make e POSIX threads. Testado em Linux (Omarchy,
kernel 7.2.15, x86_64) com GCC 16.2.1.

```sh
make
./vm addresses.txt lru fifo    # arquivo de endereços, página, TLB
```

O `BACKING_STORE.bin` (65536 bytes) precisa estar no diretório atual, e a saída vai
para `correct.txt` no mesmo lugar. Na execução normal o programa não imprime nada e
sai com 0.

- `make clean` apaga `vm` e `vm_tsan`; `make distclean` apaga também o `correct.txt`.
- `make rebuild FRAMES=256` compila com 256 quadros e `make rebuild SEM_THREAD=1` usa a
  busca sequencial na TLB. Os 3 argumentos continuam os mesmos.
- `make tsan` e `make helgrind` rodam o exemplo oficial sob ThreadSanitizer e Helgrind.

# Entrada e saída

Um endereço lógico por linha, de 0 a 65535: página nos bits 8 a 15, deslocamento nos
bits 0 a 7. Espaço em volta do número, `\r\n`, linha em branco e última linha sem `\n`
são aceitos (o `addresses.txt` oficial termina em `12107 `, com espaço).

```
Virtual address: 16916 TLB: 0 Physical address: 20 Value: 0
...
Number of Translated Addresses = 1000
Page Faults = 539
Page Fault Rate = 0.539
TLB Hits = 54
TLB Hit Rate = 0.054
```

`TLB:` é o índice da entrada usada (onde a página foi achada, ou onde entrou no miss) e
`Value` é o byte lido como `signed char`. Com o `addresses.txt` oficial:

| Quadros | Página | TLB | Page faults | TLB hits |
|---|---|---|---|---|
| 128 | fifo | fifo | 538 | 54 |
| 128 | fifo | lru | 538 | 55 |
| 128 | lru | fifo | 539 | 54 |
| 128 | lru | lru | 539 | 55 |
| 256 | lru | fifo / lru | 244 | 54 / 55 |

# Como funciona

- Os quadros enchem de 0 a 127. Depois disso, o FIFO de página é um ponteiro circular
  sobre os quadros, e o LRU tira o quadro com o acesso mais antigo (hit de TLB também
  conta como acesso).
- Quando uma página sai da memória, a entrada dela na TLB é invalidada.
- `tlb_busca` cria 16 threads por consulta. Cada uma compara só a sua entrada e escreve
  só no próprio argumento; a TLB só muda depois do `pthread_join`, então não há mutex.
- Argumentos, o arquivo de endereços inteiro e o `BACKING_STORE.bin` são validados
  antes de abrir o `correct.txt`.

# Erros

Entrada inválida escreve em `stderr`, sai com 1 e deixa o `correct.txt` como estava.

```
$ ./vm addresses.txt LRU fifo
Erro: algoritmo de substituicao de pagina invalido: 'LRU' (use fifo ou lru)
$ ./vm a.txt lru fifo           # a.txt com -3 na linha 2
Erro: a.txt: linha 2 ('-3'): endereco nao pode ser negativo
$ ./vm addresses.txt lru fifo   # BACKING_STORE.bin com 100 bytes
Erro: 'BACKING_STORE.bin' invalido: tem 100 bytes, esperado 65536
```

Também dão erro: número de argumentos diferente de 3, arquivo de endereços inexistente,
sem permissão ou que é um diretório, arquivo sem nenhum endereço, texto, decimal,
sinal, dois números na mesma linha, endereço acima de 65535 (o livro mascara os 16
bits baixos; o `vm` recusa o arquivo) e falha ao gravar o `correct.txt`.

# Testes

Os arquivos do professor ficam em `tests/vetores/oficial/`, com o oficial renomeado
para `correct_esperado.txt` (assim nenhum `./vm` rodado ali apaga a referência).

```sh
cd tests/vetores/oficial
../../../vm addresses.txt lru fifo && cmp correct.txt correct_esperado.txt && echo igual
cd ../../..
sh tests/run_tests.sh           # 64 casos; exit 0 se todos passarem
```

A bateria roda cada caso num diretório temporário e cobre os erros acima, o oficial
byte a byte com e sem threads, as contagens da tabela, threads = sequencial, a
invalidação da TLB e o ThreadSanitizer. As execuções gravadas estão no
`evidencias.log` e o relatório, no `jems2.pdf`.

# Estrutura

```
.
├── vm.c               # o programa inteiro: parser, backing store, TLB, memória e saída
├── Makefile
├── evidencias.log     # sessões gravadas com script
├── jems2.pdf          # relatório
└── tests/
    ├── run_tests.sh   # bateria de 64 casos
    ├── logrc          # bashrc das sessões gravadas
    └── vetores/oficial/
```

O `.tar` da entrega leva só `vm.c`, `Makefile`, `README.md` e `evidencias.log`.

Infraestrutura de Software (00016) • CESAR School • 2026.2
