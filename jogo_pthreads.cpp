#include <pthread.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <string>

using namespace std;

const int LARGURA = 70;
const int ALTURA = 22;
const int SOLO_Y = ALTURA - 2;
const int BASE_X = LARGURA / 2;
const int BASE_Y = SOLO_Y;

struct Nave {
    int id;
    int x;
    int y;
    bool viva;
    bool chegouSolo;
};

struct Foguete {
    int id;
    int x;
    int y;
    int dx;
    int dy;
    bool ativo;
    char simbolo;
};

enum Direcao {
    VERTICAL,
    DIAGONAL_ESQ,
    DIAGONAL_DIR,
    HORIZONTAL_ESQ,
    HORIZONTAL_DIR
};

struct Config {
    int kLancadores;
    int totalNaves;
    int tempoRecargaMs;
    int velocidadeNaveMs;
    int intervaloNavesMs;
};

pthread_mutex_t mutexJogo = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condEspacoVazio = PTHREAD_COND_INITIALIZER;

vector<bool> lancadores;
vector<Nave*> naves;
vector<Foguete*> foguetes;
vector<pthread_t> threadsNaves;
vector<pthread_t> threadsFoguetes;

Config config;
Direcao direcaoAtual = VERTICAL;

bool jogoAtivo = true;
int navesCriadas = 0;
int navesAbatidas = 0;
int navesNoSolo = 0;
int proximoIdFoguete = 1;

termios terminalOriginal;

int msParaUs(int ms) {
    return ms * 1000;
}

void restaurarTerminal() {
    tcsetattr(STDIN_FILENO, TCSANOW, &terminalOriginal);
}

void configurarTerminal() {
    tcgetattr(STDIN_FILENO, &terminalOriginal);
    atexit(restaurarTerminal);

    termios novo = terminalOriginal;
    novo.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &novo);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

char teclaLida() {
    char c;
    int resultado = read(STDIN_FILENO, &c, 1);
    if (resultado == 1) return c;
    return '\0';
}

string nomeDirecao(Direcao d) {
    switch (d) {
        case VERTICAL: return "vertical |";
        case DIAGONAL_ESQ: return "diagonal esquerda \\";
        case DIAGONAL_DIR: return "diagonal direita /";
        case HORIZONTAL_ESQ: return "horizontal esquerda <-";
        case HORIZONTAL_DIR: return "horizontal direita ->";
    }
    return "?";
}

char simboloDirecao(Direcao d) {
    switch (d) {
        case VERTICAL: return '|';
        case DIAGONAL_ESQ: return '\\';
        case DIAGONAL_DIR: return '/';
        case HORIZONTAL_ESQ: return '-';
        case HORIZONTAL_DIR: return '-';
    }
    return '|';
}

void verificarFimDeJogoComLock() {
    int metade = config.totalNaves / 2;
    bool deveEncerrar = false;

    if (navesAbatidas >= metade + (config.totalNaves % 2 == 0 ? 0 : 1)) {
        deveEncerrar = true;
    }

    if (navesNoSolo > metade) {
        deveEncerrar = true;
    }

    if (deveEncerrar) {
        jogoAtivo = false;
        pthread_cond_broadcast(&condEspacoVazio);
    }
}

void* threadNave(void* arg) {
    Nave* nave = (Nave*) arg;

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

void* threadGeradorNaves(void*) {
    while (true) {
        usleep(msParaUs(config.intervaloNavesMs));

        pthread_mutex_lock(&mutexJogo);

        if (!jogoAtivo || navesCriadas >= config.totalNaves) {
            pthread_mutex_unlock(&mutexJogo);
            break;
        }

        Nave* nave = new Nave;
        nave->id = navesCriadas + 1;
        nave->x = 2 + rand() % (LARGURA - 4);
        nave->y = 0;
        nave->viva = true;
        nave->chegouSolo = false;
        naves.push_back(nave);
        navesCriadas++;

        pthread_t tid;
        pthread_create(&tid, NULL, threadNave, nave);
        threadsNaves.push_back(tid);

        pthread_mutex_unlock(&mutexJogo);
    }

    return NULL;
}

void marcarColisaoSeExistirComLock(Foguete* foguete) {
    for (Nave* nave : naves) {
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
    Foguete* foguete = (Foguete*) arg;

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

void definirMovimentoFoguete(Direcao d, int& dx, int& dy) {
    switch (d) {
        case VERTICAL:
            dx = 0; dy = -1; break;
        case DIAGONAL_ESQ:
            dx = -1; dy = -1; break;
        case DIAGONAL_DIR:
            dx = 1; dy = -1; break;
        case HORIZONTAL_ESQ:
            dx = -1; dy = 0; break;
        case HORIZONTAL_DIR:
            dx = 1; dy = 0; break;
    }
}

void dispararComLock() {
    int indice = -1;
    for (int i = 0; i < (int)lancadores.size(); i++) {
        if (lancadores[i]) {
            indice = i;
            break;
        }
    }

    if (indice == -1) return;

    lancadores[indice] = false;
    pthread_cond_signal(&condEspacoVazio);

    int dx, dy;
    definirMovimentoFoguete(direcaoAtual, dx, dy);

    Foguete* foguete = new Foguete;
    foguete->id = proximoIdFoguete++;
    foguete->x = BASE_X;
    foguete->y = BASE_Y - 1;
    foguete->dx = dx;
    foguete->dy = dy;
    foguete->ativo = true;
    foguete->simbolo = simboloDirecao(direcaoAtual);
    foguetes.push_back(foguete);

    pthread_t tid;
    pthread_create(&tid, NULL, threadFoguete, foguete);
    threadsFoguetes.push_back(tid);
}

void* threadEntrada(void*) {
    while (true) {
        usleep(msParaUs(40));

        // a thread precisa verificar se o jogo acabou mesmo sem nenhuma tecla
        pthread_mutex_lock(&mutexJogo);
        bool ativo = jogoAtivo;
        pthread_mutex_unlock(&mutexJogo);

        if (!ativo) break;

        char c = teclaLida();
        if (c == '\0') continue;

        pthread_mutex_lock(&mutexJogo);

        if (!jogoAtivo) {
            pthread_mutex_unlock(&mutexJogo);
            break;
        }

        if (c == 'q' || c == 'Q') {
            jogoAtivo = false;
            pthread_cond_broadcast(&condEspacoVazio);
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

bool bateriaCheiaComLock() {
    for (bool carregado : lancadores) {
        if (!carregado) return false;
    }
    return true;
}

void* threadCarregador(void*) {
    while (true) {
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

        for (int i = 0; i < (int)lancadores.size(); i++) {
            if (!lancadores[i]) {
                lancadores[i] = true;
                break;
            }
        }

        pthread_mutex_unlock(&mutexJogo);
    }

    return NULL;
}

void limparTela() {
    cout << "\033[2J\033[H";
}

void* threadRenderizacao(void*) {
    while (true) {
        usleep(msParaUs(100));

        pthread_mutex_lock(&mutexJogo);

        vector<string> tela(ALTURA, string(LARGURA, ' '));

        for (int x = 0; x < LARGURA; x++) tela[SOLO_Y + 1][x] = '=';

        for (Nave* nave : naves) {
            if (nave->viva && nave->y >= 0 && nave->y < ALTURA && nave->x >= 0 && nave->x < LARGURA) {
                tela[nave->y][nave->x] = 'V';
            }
        }

        for (Foguete* foguete : foguetes) {
            if (foguete->ativo && foguete->y >= 0 && foguete->y < ALTURA && foguete->x >= 0 && foguete->x < LARGURA) {
                tela[foguete->y][foguete->x] = foguete->simbolo;
            }
        }

        tela[BASE_Y][BASE_X] = 'A';
        tela[BASE_Y - 1][BASE_X] = simboloDirecao(direcaoAtual);

        int foguetesDisponiveis = 0;
        for (bool carregado : lancadores) if (carregado) foguetesDisponiveis++;

        bool ativo = jogoAtivo;
        int abatidas = navesAbatidas;
        int solo = navesNoSolo;
        int criadas = navesCriadas;
        string dir = nomeDirecao(direcaoAtual);

        pthread_mutex_unlock(&mutexJogo);

        limparTela();
        cout << "BATERIA ANTIAEREA - pthreads\n";
        cout << "Teclas: W vertical | A diagonal esquerda | D diagonal direita | Z horiz. esquerda | C horiz. direita | ESPACO dispara | Q sai\n";
        cout << "Direcao: " << dir
             << " | Foguetes na bateria: " << foguetesDisponiveis << "/" << config.kLancadores
             << " | Abatidas: " << abatidas
             << " | Solo: " << solo
             << " | Naves criadas: " << criadas << "/" << config.totalNaves << "\n\n";

        for (const string& linha : tela) {
            cout << linha << "\n";
        }

        cout.flush();

        if (!ativo) break;
    }

    return NULL;
}

void escolherDificuldade() {
    cout << "Escolha a dificuldade:\n";
    cout << "1 - Facil\n";
    cout << "2 - Medio\n";
    cout << "3 - Dificil\n";
    cout << "Opcao: ";

    int op;
    cin >> op;

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
}

void encerrarThreadsPrincipais(pthread_t entrada, pthread_t carregador, pthread_t gerador, pthread_t render) {
    pthread_join(entrada, NULL);
    pthread_join(carregador, NULL);
    pthread_join(gerador, NULL);
    pthread_join(render, NULL);
}

void liberarMemoria() {
    for (Nave* nave : naves) delete nave;
    for (Foguete* foguete : foguetes) delete foguete;
}

int main() {
    srand(time(NULL));

    escolherDificuldade();
    lancadores.assign(config.kLancadores, true);

    configurarTerminal();

    pthread_t tidEntrada, tidCarregador, tidGerador, tidRender;

    pthread_create(&tidEntrada, NULL, threadEntrada, NULL);
    pthread_create(&tidCarregador, NULL, threadCarregador, NULL);
    pthread_create(&tidGerador, NULL, threadGeradorNaves, NULL);
    pthread_create(&tidRender, NULL, threadRenderizacao, NULL);

    while (true) {
        usleep(msParaUs(200));
        pthread_mutex_lock(&mutexJogo);
        bool ativo = jogoAtivo;
        pthread_mutex_unlock(&mutexJogo);
        if (!ativo) break;
    }

    pthread_mutex_lock(&mutexJogo);
    jogoAtivo = false;
    pthread_cond_broadcast(&condEspacoVazio);
    pthread_mutex_unlock(&mutexJogo);

    encerrarThreadsPrincipais(tidEntrada, tidCarregador, tidGerador, tidRender);

    for (pthread_t tid : threadsNaves) pthread_join(tid, NULL);
    for (pthread_t tid : threadsFoguetes) pthread_join(tid, NULL);

    restaurarTerminal();
    limparTela();

    cout << "Fim de jogo!\n";
    cout << "Naves abatidas: " << navesAbatidas << " de " << config.totalNaves << "\n";
    cout << "Naves que chegaram ao solo: " << navesNoSolo << " de " << config.totalNaves << "\n";

    if (navesAbatidas >= (config.totalNaves + 1) / 2) {
        cout << "Resultado: VITORIA DO JOGADOR!\n";
    } else {
        cout << "Resultado: DERROTA DO JOGADOR!\n";
    }

    liberarMemoria();
    return 0;
}
