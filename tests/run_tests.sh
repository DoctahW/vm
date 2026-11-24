#!/bin/sh
# tests/run_tests.sh -- bateria do vm (F6): build sem warning nas 3 variantes, tabela
# de erros do plano (§3.7), entradas validas, correct.txt oficial byte a byte,
# contagens das 4 combinacoes + 256 quadros, threads = sequencial, invalidacao da
# TLB e ThreadSanitizer. Adaptado do run_tests.sh da atvd5, com auxilio de IA
# (secao Uso de IA).
# addresses.txt, BACKING_STORE.bin e correct.txt tem nome fixo no diretorio
# corrente, entao cada caso roda num diretorio temporario proprio: nada no
# projeto e sobrescrito.
# Uso: sh tests/run_tests.sh   (exit 0 so se tudo passar)
set -u

cd "$(dirname "$0")/.." || exit 1

raiz=$(pwd)
vm="$raiz/vm"
oficial="$raiz/tests/vetores/oficial"
tmp=$(mktemp -d)
trap 'chmod -R u+rwx "$tmp" 2>/dev/null; rm -rf "$tmp"' EXIT
ok=0
falha=0

passou() { echo "ok     $1"; ok=$((ok + 1)); }
falhou() { echo "FALHA  $1"; falha=$((falha + 1)); }

# monta $tmp/caso com addresses.txt (printf interpreta \n, \r, \t) e o backing store oficial
preparar() {
    rm -rf "$tmp/caso"
    mkdir "$tmp/caso"
    printf "$1" > "$tmp/caso/addresses.txt"
    cp "$oficial/BACKING_STORE.bin" "$tmp/caso"
}

# monta $tmp/caso com as entradas oficiais (copia, nunca redigita: R.13)
preparar_oficial() {
    rm -rf "$tmp/caso"
    mkdir "$tmp/caso"
    cp "$oficial/addresses.txt" "$oficial/BACKING_STORE.bin" "$tmp/caso"
}

# roda $bin (padrao: ./vm com threads) em $tmp/caso: stdout em $saida, stderr em
# $tmp/err, exit em $rc
bin=$vm
rodar() {
    saida=$(cd "$tmp/caso" && "$bin" "$@" 2>"$tmp/err"); rc=$?
}

# erro: exit != 0, stderr nao vazio, stdout vazio, nenhum correct.txt criado
esperar_erro() {
    desc=$1
    shift
    rodar "$@"
    if [ "$rc" -ne 0 ] && [ -s "$tmp/err" ] && [ -z "$saida" ] && [ ! -e "$tmp/caso/correct.txt" ]; then
        passou "$desc  ->  $(head -1 "$tmp/err")"
    else
        falhou "$desc  (rc=$rc, stdout='$saida')"
    fi
}

# como esperar_erro, e a mensagem tem que conter $2 (ex.: o numero da linha)
esperar_erro_com() {
    desc=$1
    trecho=$2
    shift 2
    rodar "$@"
    if [ "$rc" -ne 0 ] && [ -z "$saida" ] && [ ! -e "$tmp/caso/correct.txt" ] \
       && grep -qF -- "$trecho" "$tmp/err"; then
        passou "$desc  ->  $(head -1 "$tmp/err")"
    else
        falhou "$desc  (rc=$rc, esperava '$trecho' em: $(head -1 "$tmp/err"))"
    fi
}

# erro com correct.txt de uma execucao anterior: alem do esperar_erro, o arquivo
# antigo tem que continuar intacto (validar tudo antes de abrir a saida, E.2)
esperar_erro_preserva() {
    desc=$1
    shift
    printf 'execucao anterior\n' > "$tmp/caso/correct.txt"
    cp "$tmp/caso/correct.txt" "$tmp/antigo"
    rodar "$@"
    if [ "$rc" -ne 0 ] && [ -s "$tmp/err" ] && [ -z "$saida" ] \
       && cmp -s "$tmp/caso/correct.txt" "$tmp/antigo"; then
        passou "$desc  ->  $(head -1 "$tmp/err")"
    else
        falhou "$desc  (rc=$rc, stdout='$saida', correct.txt alterado?)"
    fi
}

# valido: exit 0, stdout e stderr vazios, correct.txt criado
esperar_ok() {
    desc=$1
    shift
    rodar "$@"
    if [ "$rc" -eq 0 ] && [ -z "$saida" ] && [ ! -s "$tmp/err" ] && [ -s "$tmp/caso/correct.txt" ]; then
        passou "$desc"
    else
        falhou "$desc  (rc=$rc, stdout='$saida', err='$(head -1 "$tmp/err")')"
    fi
}

# compara o arquivo $1 com o esperado $2; na falha mostra o diff com cat -A
# ('$' = fim de linha, deixa espaco e \r visiveis)
comparar() {
    desc=$1
    obtido=$2
    esperado=$3
    if [ -e "$obtido" ] && cmp -s "$obtido" "$esperado"; then
        passou "$desc"
    else
        falhou "$desc"
        if [ -e "$obtido" ]; then
            cat -A "$esperado" > "$tmp/esperado_A"
            cat -A "$obtido" > "$tmp/obtido_A"
            echo "       diff esperado obtido (primeiras 10 linhas):"
            diff "$tmp/esperado_A" "$tmp/obtido_A" | head -10 | sed 's/^/         /'
        fi
    fi
}

# exit 0, stdout/stderr vazios e correct.txt igual (cmp) ao arquivo $2
esperar_resultado() {
    desc=$1
    esperado=$2
    shift 2
    rodar "$@"
    if [ "$rc" -ne 0 ] || [ -n "$saida" ] || [ -s "$tmp/err" ]; then
        falhou "$desc  (rc=$rc, stdout='$saida', err='$(head -1 "$tmp/err")')"
        return
    fi
    comparar "$desc" "$tmp/caso/correct.txt" "$esperado"
}

# grava as 5 linhas de estatistica esperadas em $tmp/esperado
estatisticas() {
    printf 'Number of Translated Addresses = %s\nPage Faults = %s\nPage Fault Rate = %s\nTLB Hits = %s\nTLB Hit Rate = %s\n' \
        "$1" "$2" "$3" "$4" "$5" > "$tmp/esperado"
}

# so as estatisticas (tail -5) do correct.txt comparadas com $tmp/esperado
esperar_estatisticas() {
    desc=$1
    shift
    rodar "$@"
    if [ "$rc" -ne 0 ] || [ -n "$saida" ] || [ -s "$tmp/err" ]; then
        falhou "$desc  (rc=$rc, err='$(head -1 "$tmp/err")')"
        return
    fi
    tail -5 "$tmp/caso/correct.txt" > "$tmp/estat"
    comparar "$desc  ($(sed -n 2p "$tmp/estat"), $(sed -n 4p "$tmp/estat"))" "$tmp/estat" "$tmp/esperado"
}

# make rebuild com variaveis ($@); sem warning; o binario vai para $tmp/$nome
construir() {
    nome=$1
    shift
    if make rebuild "$@" > "$tmp/build" 2>&1; then
        if grep -qi 'warning' "$tmp/build"; then
            falhou "make rebuild $* com warning"
            cat "$tmp/build"
        else
            passou "make rebuild $* sem warning"
        fi
        cp "$vm" "$tmp/$nome"
    else
        cat "$tmp/build"
        echo "make rebuild $* FALHOU"
        exit 1
    fi
}

echo "== build"
construir vm_seq SEM_THREAD=1
construir vm_256 FRAMES=256
construir vm_thr          # por ultimo: ./vm do projeto fica o build padrao (com threads)
make clean > /dev/null 2>&1 && [ ! -e "$vm" ] && passou "make clean remove vm" || falhou "make clean"
make > /dev/null 2>&1 && [ -x "$vm" ] && passou "make sem alvo gera vm" || { echo "make FALHOU"; exit 1; }

echo "== argumentos"
preparar '16916\n'
esperar_erro "#1 sem argumentos"
esperar_erro "#1 so o arquivo"                addresses.txt
esperar_erro "#1 arquivo + pagina"            addresses.txt lru
esperar_erro "#1 4 argumentos"                addresses.txt lru fifo extra
esperar_erro "#2 pagina LRU (maiuscula)"      addresses.txt LRU fifo
esperar_erro "#2 pagina opt"                  addresses.txt opt fifo
esperar_erro "#2 pagina vazia"                addresses.txt '' fifo
esperar_erro "#3 tlb xyz"                     addresses.txt lru xyz
esperar_erro "#3 tlb FIFO (maiuscula)"        addresses.txt lru FIFO

echo "== addresses.txt"
preparar '16916\n'; rm "$tmp/caso/addresses.txt"
esperar_erro "#4 inexistente"                 addresses.txt lru fifo
if [ "$(id -u)" -ne 0 ]; then
    preparar '16916\n'; chmod 000 "$tmp/caso/addresses.txt"
    esperar_erro "#5 sem permissao de leitura" addresses.txt lru fifo
fi
preparar '16916\n'; rm "$tmp/caso/addresses.txt"; mkdir "$tmp/caso/addresses.txt"
esperar_erro "#6 e um diretorio"              addresses.txt lru fifo
preparar '';                    esperar_erro "#7 vazio"                    addresses.txt lru fifo
preparar '\n  \n\t\n\r\n';      esperar_erro "#7 so linhas em branco"      addresses.txt lru fifo
preparar 'abc\n';               esperar_erro_com "#8 abc"          "linha 1"  addresses.txt lru fifo
preparar '10\n12a\n';           esperar_erro_com "#8 12a na linha 2" "linha 2" addresses.txt lru fifo
preparar '4.5\n';               esperar_erro_com "#8 decimal"      "linha 1"  addresses.txt lru fifo
preparar '+\n';                 esperar_erro_com "#8 so sinal"     "linha 1"  addresses.txt lru fifo
preparar '+5\n';                esperar_erro_com "#8 +5"           "linha 1"  addresses.txt lru fifo
preparar '10\n\n-3\n';          esperar_erro_com "#8 -3 na linha 3 (branco conta)" "linha 3" addresses.txt lru fifo
preparar '1\n2\000\n';          esperar_erro_com "#8 byte nulo"    "linha 2"  addresses.txt lru fifo
preparar '65535\n65536\n';      esperar_erro_com "#9 65536"        "linha 2"  addresses.txt lru fifo
preparar '99999999999\n';       esperar_erro_com "#9 overflow"     "linha 1"  addresses.txt lru fifo
preparar '123 456\n';           esperar_erro_com "#10 dois valores" "linha 1" addresses.txt lru fifo
preparar '123\t456\n';          esperar_erro_com "#10 dois valores (tab)" "linha 1" addresses.txt lru fifo
preparar_oficial; printf 'x\n' >> "$tmp/caso/addresses.txt"
esperar_erro_com "#8 erro so na linha 1001 do oficial" "linha 1001" addresses.txt lru fifo

echo "== BACKING_STORE.bin"
preparar '16916\n'; rm "$tmp/caso/BACKING_STORE.bin"
esperar_erro "#11 ausente"                    addresses.txt lru fifo
preparar '16916\n'; head -c 65535 "$oficial/BACKING_STORE.bin" > "$tmp/caso/BACKING_STORE.bin"
esperar_erro "#12 65535 bytes"                addresses.txt lru fifo
preparar '16916\n'; printf 'x' >> "$tmp/caso/BACKING_STORE.bin"
esperar_erro "#12 65537 bytes"                addresses.txt lru fifo
preparar '16916\n'; rm "$tmp/caso/BACKING_STORE.bin"; mkdir "$tmp/caso/BACKING_STORE.bin"
esperar_erro "#12 e um diretorio"             addresses.txt lru fifo

echo "== correct.txt (saida)"
if [ "$(id -u)" -ne 0 ]; then
    preparar '16916\n'; chmod a-w "$tmp/caso"
    esperar_erro "#13 diretorio sem escrita"  addresses.txt lru fifo
    chmod u+w "$tmp/caso"
fi
if [ -c /dev/full ]; then
    preparar '16916\n'; ln -s /dev/full "$tmp/caso/correct.txt"
    rodar addresses.txt lru fifo
    if [ "$rc" -ne 0 ] && [ -s "$tmp/err" ] && [ -z "$saida" ]; then
        passou "#13 disco cheio (correct.txt -> /dev/full)  ->  $(head -1 "$tmp/err")"
    else
        falhou "#13 disco cheio (correct.txt -> /dev/full)  (rc=$rc, stdout='$saida')"
    fi
fi
preparar '16916\n';                      esperar_erro_preserva "#14 erro no argv"   addresses.txt lru
preparar '16916\n';                      esperar_erro_preserva "#14 algoritmo invalido" addresses.txt lru xyz
preparar_oficial; printf '65536\n' >> "$tmp/caso/addresses.txt"
esperar_erro_preserva "#14 erro na ultima linha do addresses.txt" addresses.txt lru fifo
preparar '16916\n'; rm "$tmp/caso/BACKING_STORE.bin"
esperar_erro_preserva "#14 BACKING_STORE.bin ausente" addresses.txt lru fifo

echo "== entradas validas"
preparar_oficial;                        esperar_ok "oficial (ultima linha '12107 ')" addresses.txt lru fifo
preparar '16916\r\n62493\r\n';           esperar_ok "CRLF"                    addresses.txt lru fifo
preparar '16916\n62493';                 esperar_ok "sem \\n no fim"          addresses.txt lru fifo
preparar '\n 16916\t\n\n  62493  \n\n';  esperar_ok "linhas em branco e espacos" addresses.txt fifo lru
preparar '16916\n';                      esperar_ok "1 endereco"              addresses.txt fifo fifo

echo "== correct.txt byte a byte"
preparar_oficial
esperar_resultado "V1 oficial lru fifo (threads) = correct_esperado.txt" "$oficial/correct_esperado.txt" addresses.txt lru fifo
bin="$tmp/vm_seq"
preparar_oficial
esperar_resultado "V1 oficial lru fifo (SEM_THREAD) = correct_esperado.txt" "$oficial/correct_esperado.txt" addresses.txt lru fifo
bin=$vm
preparar_oficial
sed 's/$/\r/' "$oficial/addresses.txt" > "$tmp/caso/addresses.txt"
esperar_resultado "V1 oficial em CRLF = correct_esperado.txt" "$oficial/correct_esperado.txt" addresses.txt lru fifo
preparar_oficial
printf '%s' "$(cat "$oficial/addresses.txt")" > "$tmp/caso/addresses.txt"
esperar_resultado "V1 oficial sem \\n no fim = correct_esperado.txt" "$oficial/correct_esperado.txt" addresses.txt lru fifo

# pagina 0 e 255 (limites do espaco), Value com sinal (ultimo byte do backing e
# negativo) e um hit de TLB na volta para o endereco 0
v0=$(od -An -td1 -N1 "$oficial/BACKING_STORE.bin" | tr -d ' ')
vfim=$(od -An -td1 -j 65535 -N1 "$oficial/BACKING_STORE.bin" | tr -d ' ')
preparar '0\n65535\n0\n'
printf 'Virtual address: 0 TLB: 0 Physical address: 0 Value: %s\nVirtual address: 65535 TLB: 1 Physical address: 511 Value: %s\nVirtual address: 0 TLB: 0 Physical address: 0 Value: %s\n' \
    "$v0" "$vfim" "$v0" > "$tmp/esperado_limites"
estatisticas 3 2 0.667 1 0.333
cat "$tmp/esperado" >> "$tmp/esperado_limites"
esperar_resultado "limites 0/65535, Value com sinal ($vfim), hit de TLB" "$tmp/esperado_limites" addresses.txt lru lru

echo "== contagens (plano §3.4)"
preparar_oficial
estatisticas 1000 538 0.538 54 0.054; esperar_estatisticas "V2 fifo fifo" addresses.txt fifo fifo
estatisticas 1000 538 0.538 55 0.055; esperar_estatisticas "V2 fifo lru"  addresses.txt fifo lru
estatisticas 1000 539 0.539 54 0.054; esperar_estatisticas "V2 lru fifo"  addresses.txt lru fifo
estatisticas 1000 539 0.539 55 0.055; esperar_estatisticas "V2 lru lru"   addresses.txt lru lru
bin="$tmp/vm_256"
estatisticas 1000 244 0.244 54 0.054; esperar_estatisticas "V2 256 quadros, lru fifo" addresses.txt lru fifo
estatisticas 1000 244 0.244 55 0.055; esperar_estatisticas "V2 256 quadros, lru lru"  addresses.txt lru lru
bin=$vm

echo "== threads = sequencial"
for pagina in fifo lru; do
    for tlb in fifo lru; do
        preparar_oficial
        bin="$tmp/vm_seq"; rodar addresses.txt $pagina $tlb
        mv "$tmp/caso/correct.txt" "$tmp/seq.txt"
        bin=$vm
        esperar_resultado "V3 $pagina $tlb: threads = SEM_THREAD" "$tmp/seq.txt" addresses.txt $pagina $tlb
    done
done

echo "== invalidacao da TLB (V4)"
# fifo lru: a pagina 0 (quadro 0) e acessada entre todas as outras, entao fica
# quente na TLB LRU; a pagina 128 enche a memoria e o FIFO de pagina despeja a 0.
# O 0 seguinte (linha 257) tem que ser page fault no quadro 1 (fisico 256); se a
# TLB nao fosse invalidada, seria hit no quadro 0, que agora guarda a pagina 128.
preparar ''
{ echo 0; for k in $(seq 1 128); do echo $((k * 256)); echo 0; done; } > "$tmp/caso/addresses.txt"
for b in "$vm" "$tmp/vm_seq"; do
    bin=$b
    rodar addresses.txt fifo lru
    linha=$(sed -n 257p "$tmp/caso/correct.txt" 2>/dev/null)
    faltas=$(grep 'Page Faults' "$tmp/caso/correct.txt" 2>/dev/null)
    if [ "$rc" -eq 0 ] && [ "$faltas" = "Page Faults = 130" ] \
       && echo "$linha" | grep -q '^Virtual address: 0 TLB: [0-9]* Physical address: 256 Value: '; then
        passou "V4 $(basename "$b"): pagina despejada sai da TLB ($faltas; $linha)"
    else
        falhou "V4 $(basename "$b"): rc=$rc, '$faltas', linha 257 '$linha'"
    fi
done
bin=$vm

echo "== ThreadSanitizer"
if make vm_tsan > "$tmp/build_tsan" 2>&1; then
    bin="$raiz/vm_tsan"
    preparar_oficial
    rodar addresses.txt lru fifo
    if [ "$rc" -eq 0 ] && ! grep -q 'ThreadSanitizer' "$tmp/err" \
       && cmp -s "$tmp/caso/correct.txt" "$oficial/correct_esperado.txt"; then
        passou "tsan: sem data race, saida = oficial"
    else
        falhou "tsan (rc=$rc): $(grep -m1 'WARNING' "$tmp/err")"
    fi
    bin=$vm
    rm -f "$raiz/vm_tsan"
else
    echo "aviso  -fsanitize=thread indisponivel neste compilador (pulado)"
fi

echo
echo "resultado: $ok ok, $falha falha"
[ "$falha" -eq 0 ]
