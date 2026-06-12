# Jogo Bateria jogo com pthreads

Implementacao simples em C usando `pthread`, `pthread_mutex_t` e `pthread_cond_t`.

## Compilacao

```bash
make
```

Ou diretamente:

```bash
g++ -Wall -Wextra -std=c++11 -pthread jogo_pthreads.cpp -o jogo
```

## Execucao

```bash
./jogo_pthreads
```

## Controles

- `W`: direcao vertical
- `A`: diagonal para a esquerda
- `D`: diagonal para a direita
- `Z`: horizontal para a esquerda
- `C`: horizontal para a direita
- `Espaco`: disparar foguete
- `Q`: sair

## Dificuldades

- Facil: poucos lancadores, recarga lenta, menos naves.
- Medio: parametros intermediarios.
- Dificil: mais lancadores, recarga rapida, mais naves e naves mais rapidas.

## Uso de threads

- Thread de entrada do jogador.
- Thread do carregador da bateria.
- Thread geradora de naves.
- Uma thread para cada nave.
- Uma thread para cada foguete.
- Thread de renderizacao.

## Exclusao mutua e coordenacao

O estado global do jogo e protegido por `pthread_mutex_t mutexJogo`.

A thread do carregador usa `pthread_cond_t condEspacoVazio` para dormir quando todos os lancadores estao cheios e acordar quando o jogador dispara algum foguete.
