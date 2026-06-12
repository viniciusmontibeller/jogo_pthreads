#define _DEFAULT_SOURCE

#include <pthread.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#define LARGURA 70
#define ALTURA 22
#define SOLO_Y (ALTURA - 2)
#define BASE_X (LARGURA / 2)
#define BASE_Y SOLO_Y

#define MAX_NAVES 100
#define MAX_FOGUETES 1000

/*
 * Jogo simples em C puro usando pthreads.
 *
 * Threads usadas:
 * - threadEntrada: le teclado sem bloquear o jogo
 * - threadCarregador: recarrega os lancadores
 * - threadGeradorNaves: cria as naves
 * - threadNave: uma thread para cada nave
 * - threadFoguete: uma thread para cada foguete disparado
 * - threadRenderizacao: atualiza a tela no terminal
 */

typedef enum {
    VERTICAL,
    DIAGONAL_ESQ,
    DIAGONAL_DIR,
    HORIZONTAL_ESQ,
    HORIZONTAL_DIR
} Direcao;

typedef struct {
    int id;
    int x;
    int y;
    bool viva;
    bool chegouSolo;
} Nave;

typedef struct {
    int id;
    int x;
    int y;
    int dx;
    int dy;
    bool ativo;
    char simbolo;
} Foguete;

typedef struct {
    int kLancadores;
    int totalNaves;
    int tempoRecargaMs;
    int velocidadeNaveMs;
    int intervaloNavesMs;
} Config;

pthread_mutex_t mutexJogo = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condEspacoVazio = PTHREAD_COND_INITIALIZER;

Config config;
Direcao direcaoAtual = VERTICAL;

typedef enum {
    RESULTADO_EM_ANDAMENTO,
    RESULTADO_VITORIA,
    RESULTADO_DERROTA,
    RESULTADO_SAIDA_USUARIO
} Resultado;

bool jogoAtivo = true;
Resultado resultadoFinal = RESULTADO_EM_ANDAMENTO;
int navesCriadas = 0;
int navesAbatidas = 0;
int navesNoSolo = 0;
int proximoIdFoguete = 1;

bool lancadores[50];
Nave naves[MAX_NAVES];
Foguete foguetes[MAX_FOGUETES];

pthread_t threadsNaves[MAX_NAVES];
pthread_t threadsFoguetes[MAX_FOGUETES];
int qtdThreadsNaves = 0;
int qtdThreadsFoguetes = 0;

struct termios terminalOriginal;

int msParaUs(int ms) {
    return ms * 1000;
}

void restaurarTerminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &terminalOriginal);
}

void configurarTerminal(void) {
    struct termios novo;
    int flags;

    tcgetattr(STDIN_FILENO, &terminalOriginal);
    atexit(restaurarTerminal);

    novo = terminalOriginal;
    novo.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &novo);

    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

char teclaLida(void) {
    char c;
    int resultado = (int)read(STDIN_FILENO, &c, 1);

    if (resultado == 1) {
        return c;
    }

    return '\0';
}

const char* nomeDirecao(Direcao d) {
    switch (d) {
        case VERTICAL:
            return "vertical |";
        case DIAGONAL_ESQ:
            return "diagonal esquerda \\";
        case DIAGONAL_DIR:
            return "diagonal direita /";
        case HORIZONTAL_ESQ:
            return "horizontal esquerda <-";
        case HORIZONTAL_DIR:
            return "horizontal direita ->";
        default:
            return "?";
    }
}

char simboloDirecao(Direcao d) {
    switch (d) {
        case VERTICAL:
            return '|';
        case DIAGONAL_ESQ:
            return '\\';
        case DIAGONAL_DIR:
            return '/';
        case HORIZONTAL_ESQ:
        case HORIZONTAL_DIR:
            return '-';
        default:
            return '|';
    }
}

void finalizarJogoComLock(Resultado resultado) {
    if (resultadoFinal == RESULTADO_EM_ANDAMENTO) {
        resultadoFinal = resultado;
    }

    jogoAtivo = false;
    pthread_cond_broadcast(&condEspacoVazio);
}

void verificarFimDeJogoComLock(void) {
    // so termina depois que TODAS as naves
    // vitoria: ao menos 50% das naves abatidas.
    if (navesCriadas < config.totalNaves) {
        return;
    }

    if (navesAbatidas + navesNoSolo < config.totalNaves) {
        return;
    }

    if (navesAbatidas * 2 >= config.totalNaves) {
        finalizarJogoComLock(RESULTADO_VITORIA);
    } else {
        finalizarJogoComLock(RESULTADO_DERROTA);
    }
}

void* threadNave(void* arg) {
    Nave* nave = (Nave*)arg;

    while (true) {
        usleep(msParaUs(config.velocidadeNaveMs));

        pthread_mutex_lock(&mutexJogo);

        if (!jogoAtivo || !nave->viva || nave->chegouSolo) {
            pthread_mutex_unlock(&mutexJogo);
            break;
        }

        nave->y++;

        if (nave->y >= SOLO_Y) {
            nave->viva = false;
            nave->chegouSolo = true;
            navesNoSolo++;
            verificarFimDeJogoComLock();
        }

        pthread_mutex_unlock(&mutexJogo);
    }

    return NULL;
}

void* threadGeradorNaves(void* arg) {
    (void)arg;

    while (true) {
        usleep(msParaUs(config.intervaloNavesMs));

        pthread_mutex_lock(&mutexJogo);

        if (!jogoAtivo || navesCriadas >= config.totalNaves || navesCriadas >= MAX_NAVES) {
            pthread_mutex_unlock(&mutexJogo);
            break;
        }

        Nave* nave = &naves[navesCriadas];
        nave->id = navesCriadas + 1;
        nave->x = 2 + rand() % (LARGURA - 4);
        nave->y = 0;
        nave->viva = true;
        nave->chegouSolo = false;

        pthread_create(&threadsNaves[qtdThreadsNaves], NULL, threadNave, nave);
        qtdThreadsNaves++;
        navesCriadas++;

        pthread_mutex_unlock(&mutexJogo);
    }

    return NULL;
}

void marcarColisaoSeExistirComLock(Foguete* foguete) {
    int i;

    for (i = 0; i < navesCriadas; i++) {
        Nave* nave = &naves[i];

        if (nave->viva && !nave->chegouSolo) {
            int distancia = abs(nave->x - foguete->x) + abs(nave->y - foguete->y);

            if (distancia <= 1) {
                nave->viva = false;
                foguete->ativo = false;
                navesAbatidas++;
                verificarFimDeJogoComLock();
                return;
            }
        }
    }
}

void* threadFoguete(void* arg) {
    Foguete* foguete = (Foguete*)arg;

    while (true) {
        usleep(msParaUs(80));

        pthread_mutex_lock(&mutexJogo);

        if (!jogoAtivo || !foguete->ativo) {
            pthread_mutex_unlock(&mutexJogo);
            break;
        }

        foguete->x += foguete->dx;
        foguete->y += foguete->dy;

        if (foguete->x < 0 || foguete->x >= LARGURA || foguete->y < 0 || foguete->y >= ALTURA) {
            foguete->ativo = false;
        } else {
            marcarColisaoSeExistirComLock(foguete);
        }

        pthread_mutex_unlock(&mutexJogo);
    }

    return NULL;
}

void definirMovimentoFoguete(Direcao d, int* dx, int* dy) {
    switch (d) {
        case VERTICAL:
            *dx = 0;
            *dy = -1;
            break;
        case DIAGONAL_ESQ:
            *dx = -1;
            *dy = -1;
            break;
        case DIAGONAL_DIR:
            *dx = 1;
            *dy = -1;
            break;
        case HORIZONTAL_ESQ:
            *dx = -1;
            *dy = 0;
            break;
        case HORIZONTAL_DIR:
            *dx = 1;
            *dy = 0;
            break;
    }
}

void dispararComLock(void) {
    int i;
    int indice = -1;
    int dx = 0;
    int dy = -1;
    Foguete* foguete;

    if (qtdThreadsFoguetes >= MAX_FOGUETES) {
        return;
    }

    for (i = 0; i < config.kLancadores; i++) {
        if (lancadores[i]) {
            indice = i;
            break;
        }
    }

    if (indice == -1) {
        return;
    }

    lancadores[indice] = false;
    pthread_cond_signal(&condEspacoVazio);

    definirMovimentoFoguete(direcaoAtual, &dx, &dy);

    foguete = &foguetes[qtdThreadsFoguetes];
    foguete->id = proximoIdFoguete++;
    foguete->x = BASE_X;
    foguete->y = BASE_Y - 1;
    foguete->dx = dx;
    foguete->dy = dy;
    foguete->ativo = true;
    foguete->simbolo = simboloDirecao(direcaoAtual);

    pthread_create(&threadsFoguetes[qtdThreadsFoguetes], NULL, threadFoguete, foguete);
    qtdThreadsFoguetes++;
}

void* threadEntrada(void* arg) {
    (void)arg;

    while (true) {
        char c;
        bool ativo;

        usleep(msParaUs(40));

        pthread_mutex_lock(&mutexJogo);
        ativo = jogoAtivo;
        pthread_mutex_unlock(&mutexJogo);

        if (!ativo) {
            break;
        }

        c = teclaLida();
        if (c == '\0') {
            continue;
        }

        pthread_mutex_lock(&mutexJogo);

        if (!jogoAtivo) {
            pthread_mutex_unlock(&mutexJogo);
            break;
        }

        if (c == 'q' || c == 'Q') {
            finalizarJogoComLock(RESULTADO_SAIDA_USUARIO);
        } else if (c == 'w' || c == 'W') {
            direcaoAtual = VERTICAL;
        } else if (c == 'a' || c == 'A') {
            direcaoAtual = DIAGONAL_ESQ;
        } else if (c == 'd' || c == 'D') {
            direcaoAtual = DIAGONAL_DIR;
        } else if (c == 'z' || c == 'Z') {
            direcaoAtual = HORIZONTAL_ESQ;
        } else if (c == 'c' || c == 'C') {
            direcaoAtual = HORIZONTAL_DIR;
        } else if (c == ' ') {
            dispararComLock();
        }

        pthread_mutex_unlock(&mutexJogo);
    }

    return NULL;
}

bool bateriaCheiaComLock(void) {
    int i;

    for (i = 0; i < config.kLancadores; i++) {
        if (!lancadores[i]) {
            return false;
        }
    }

    return true;
}

void* threadCarregador(void* arg) {
    (void)arg;

    while (true) {
        int i;

        pthread_mutex_lock(&mutexJogo);

        while (jogoAtivo && bateriaCheiaComLock()) {
            pthread_cond_wait(&condEspacoVazio, &mutexJogo);
        }

        if (!jogoAtivo) {
            pthread_mutex_unlock(&mutexJogo);
            break;
        }

        pthread_mutex_unlock(&mutexJogo);

        usleep(msParaUs(config.tempoRecargaMs));

        pthread_mutex_lock(&mutexJogo);

        if (!jogoAtivo) {
            pthread_mutex_unlock(&mutexJogo);
            break;
        }

        for (i = 0; i < config.kLancadores; i++) {
            if (!lancadores[i]) {
                lancadores[i] = true;
                break;
            }
        }

        pthread_mutex_unlock(&mutexJogo);
    }

    return NULL;
}

void limparTela(void) {
    printf("\033[2J\033[H");
}

void* threadRenderizacao(void* arg) {
    (void)arg;

    while (true) {
        char tela[ALTURA][LARGURA + 1];
        int x;
        int y;
        int i;
        int foguetesDisponiveis = 0;
        bool ativo;
        int abatidas;
        int solo;
        int criadas;
        const char* dir;

        usleep(msParaUs(100));

        pthread_mutex_lock(&mutexJogo);

        for (y = 0; y < ALTURA; y++) {
            for (x = 0; x < LARGURA; x++) {
                tela[y][x] = ' ';
            }
            tela[y][LARGURA] = '\0';
        }

        for (x = 0; x < LARGURA; x++) {
            tela[SOLO_Y + 1][x] = '=';
        }

        for (i = 0; i < navesCriadas; i++) {
            Nave* nave = &naves[i];
            if (nave->viva && nave->y >= 0 && nave->y < ALTURA && nave->x >= 0 && nave->x < LARGURA) {
                tela[nave->y][nave->x] = 'V';
            }
        }

        for (i = 0; i < qtdThreadsFoguetes; i++) {
            Foguete* foguete = &foguetes[i];
            if (foguete->ativo && foguete->y >= 0 && foguete->y < ALTURA && foguete->x >= 0 && foguete->x < LARGURA) {
                tela[foguete->y][foguete->x] = foguete->simbolo;
            }
        }

        tela[BASE_Y][BASE_X] = 'A';
        tela[BASE_Y - 1][BASE_X] = simboloDirecao(direcaoAtual);

        for (i = 0; i < config.kLancadores; i++) {
            if (lancadores[i]) {
                foguetesDisponiveis++;
            }
        }

        ativo = jogoAtivo;
        abatidas = navesAbatidas;
        solo = navesNoSolo;
        criadas = navesCriadas;
        dir = nomeDirecao(direcaoAtual);

        pthread_mutex_unlock(&mutexJogo);

        limparTela();
        printf("BATERIA ANTIAEREA - pthreads em C\n");
        printf("Teclas: W vertical | A diagonal esquerda | D diagonal direita | Z horiz. esquerda | C horiz. direita | ESPACO dispara | Q sai\n");
        printf("Direcao: %s | Foguetes na bateria: %d/%d | Abatidas: %d | Solo: %d | Naves criadas: %d/%d\n\n",
               dir,
               foguetesDisponiveis,
               config.kLancadores,
               abatidas,
               solo,
               criadas,
               config.totalNaves);

        for (y = 0; y < ALTURA; y++) {
            printf("%s\n", tela[y]);
        }

        fflush(stdout);

        if (!ativo) {
            break;
        }
    }

    return NULL;
}

void escolherDificuldade(void) {
    int op;

    printf("Escolha a dificuldade:\n");
    printf("1 - Facil\n");
    printf("2 - Medio\n");
    printf("3 - Dificil\n");
    printf("Opcao: ");
    fflush(stdout);

    if (scanf("%d", &op) != 1) {
        op = 2;
    }

    if (op == 1) {
        config.kLancadores = 3;
        config.totalNaves = 12;
        config.tempoRecargaMs = 1300;
        config.velocidadeNaveMs = 550;
        config.intervaloNavesMs = 900;
    } else if (op == 2) {
        config.kLancadores = 5;
        config.totalNaves = 18;
        config.tempoRecargaMs = 850;
        config.velocidadeNaveMs = 430;
        config.intervaloNavesMs = 700;
    } else {
        config.kLancadores = 8;
        config.totalNaves = 24;
        config.tempoRecargaMs = 500;
        config.velocidadeNaveMs = 320;
        config.intervaloNavesMs = 500;
    }

    if (config.kLancadores > 50) {
        config.kLancadores = 50;
    }
}

void iniciarLancadores(void) {
    int i;

    for (i = 0; i < config.kLancadores; i++) {
        lancadores[i] = true;
    }
}

void encerrarThreadsPrincipais(pthread_t entrada, pthread_t carregador, pthread_t gerador, pthread_t render) {
    pthread_join(entrada, NULL);
    pthread_join(carregador, NULL);
    pthread_join(gerador, NULL);
    pthread_join(render, NULL);
}

void aguardarThreadsCriadas(void) {
    int i;

    for (i = 0; i < qtdThreadsNaves; i++) {
        pthread_join(threadsNaves[i], NULL);
    }

    for (i = 0; i < qtdThreadsFoguetes; i++) {
        pthread_join(threadsFoguetes[i], NULL);
    }
}

int main(void) {
    pthread_t tidEntrada;
    pthread_t tidCarregador;
    pthread_t tidGerador;
    pthread_t tidRender;

    srand((unsigned int)time(NULL));

    escolherDificuldade();
    iniciarLancadores();
    configurarTerminal();

    pthread_create(&tidEntrada, NULL, threadEntrada, NULL);
    pthread_create(&tidCarregador, NULL, threadCarregador, NULL);
    pthread_create(&tidGerador, NULL, threadGeradorNaves, NULL);
    pthread_create(&tidRender, NULL, threadRenderizacao, NULL);

    while (true) {
        bool ativo;

        usleep(msParaUs(200));

        pthread_mutex_lock(&mutexJogo);
        ativo = jogoAtivo;
        pthread_mutex_unlock(&mutexJogo);

        if (!ativo) {
            break;
        }
    }

    pthread_mutex_lock(&mutexJogo);
    jogoAtivo = false;
    pthread_cond_broadcast(&condEspacoVazio);
    pthread_mutex_unlock(&mutexJogo);

    encerrarThreadsPrincipais(tidEntrada, tidCarregador, tidGerador, tidRender);
    aguardarThreadsCriadas();

    restaurarTerminal();
    limparTela();

    printf("Fim de jogo!\n");
    printf("Naves abatidas: %d de %d\n", navesAbatidas, config.totalNaves);
    printf("Naves que chegaram ao solo: %d de %d\n", navesNoSolo, config.totalNaves);

    if (resultadoFinal == RESULTADO_VITORIA) {
        printf("Resultado: VITORIA DO JOGADOR!\n");
    } else if (resultadoFinal == RESULTADO_DERROTA) {
        printf("Resultado: DERROTA DO JOGADOR!\n");
    } else if (resultadoFinal == RESULTADO_SAIDA_USUARIO) {
        printf("Resultado: JOGO ENCERRADO PELO USUARIO.\n");
    } else {
        printf("Resultado: ENCERRADO SEM RESULTADO DEFINIDO.\n");
    }

    return 0;
}
