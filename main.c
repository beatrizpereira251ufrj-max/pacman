// pacman.c
// Trabalho Pratico PROG2 (2025/2) - Pac-Man
// - 20 linhas x 40 colunas
// - bloco = 40px
// - HUD = 40px
// - janela = 1600 x 840
// - portais '<' <-> '>' e 'T' (horizontal/vertical)
// - movimento discreto 4 blocos/s
// - salvar/carregar bin�rio
// Compilar (MSYS2 UCRT64): gcc pacman.c -o pacman.exe -lraylib -lopengl32 -lgdi32 -lwinmm

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "raylib.h"

#define LINHAS 20
#define COLUNAS 40
#define TAM_BLOCO 40
#define ALTURA_HUD 40

#define AREA_JOGAVEL (COLUNAS * TAM_BLOCO)
#define AREA_TOTAL  (LINHAS * TAM_BLOCO + ALTURA_HUD)

#define MAPA1_FILE "mapas/mapa1.txt"
#define MAPA2_FILE "mapas/mapa2.txt"
#define SAVE_FILE  "pacman_save.bin"

#define VEL_PAC 4.0f
#define STEP_PAC (1.0f / VEL_PAC)

#define VEL_FANT 4.0f
#define STEP_FANT (1.0f / VEL_FANT)

#define VEL_FANT_VULN 3.0f
#define STEP_FANT_VULN (1.0f / VEL_FANT_VULN)

#define DUR_POWER 8.0

typedef struct { int x,y; } Pos;

typedef struct {
    Pos pos;
    Pos inicio;
    int vidas;
    int pontuacao;
    int prox_dir; // 0 up,1 down,2 left,3 right
} Pacman;

typedef struct {
    Pos pos;
    int dir;        // 0 up,1 down,2 left,3 right
    bool vuln;
    double endVuln;
    double since;
    bool alive;
} Fantasma;

typedef enum { TELA_MENU, TELA_JOGO, TELA_PAUSA } Tela;

typedef struct {
    Pacman pac;
    char *mapa;         // LINHAS * COLUNAS
    Fantasma *fant;
    int nF;

    Tela tela;
    int nivel;
    int pellets;

    double lastPac;     // tempo do ultimo passo do Pacman
    bool power;
    double endPower;
} Estado;

// ---------------- helpers ----------------
static inline char map_get(char *m,int x,int y){ return m[y*COLUNAS + x]; }
static inline void map_set(char *m,int x,int y,char v){ m[y*COLUNAS + x] = v; }

bool pode_mover(char *m,int x,int y){
    if(x<0||x>=COLUNAS||y<0||y>=LINHAS) return false;
    return map_get(m,x,y) != '#';
}

int oposto(int d){ return d==0?1: d==1?0: d==2?3:2; }

// ---------------- leitura do mapa ----------------
// aceita: '#', '.', 'o', 'P', 'F'/'G' (fantasmas), '<', '>', 'T'
char *ler_mapa(const char *arquivo, Pacman *p, Fantasma **outF, int *outN, int *outPel){
    FILE *f = fopen(arquivo, "r");
    if(!f) return NULL;

    char *m = malloc(LINHAS * COLUNAS);
    if(!m){ fclose(f); return NULL; }

    char linha[COLUNAS + 8];
    Fantasma *fv = NULL;
    int nf = 0;
    int pellets = 0;

    for(int i=0;i<LINHAS;i++){
        if(fgets(linha, sizeof(linha), f)){
            linha[strcspn(linha, "\r\n")] = '\0';
            int len = (int)strlen(linha);
            for(int j=0;j<COLUNAS;j++){
                char c = (j < len) ? linha[j] : ' ';
                if(c == '<' || c == '>'){
                    map_set(m,j,i,c);
                }
                else if(c == 'F' || c == 'G'){
                    //. spawn fantasma
                    Fantasma *tmp = realloc(fv, sizeof(Fantasma)*(nf+1));
                    if(!tmp){ free(fv); free(m); fclose(f); return NULL; }
                    fv = tmp;
                    fv[nf].pos.x = j; fv[nf].pos.y = i;
                    fv[nf].dir = GetRandomValue(0,3);
                    fv[nf].vuln = false;
                    fv[nf].endVuln = 0;
                    fv[nf].since = 0;
                    fv[nf].alive = true;
                    nf++;
                    map_set(m,j,i,' ');
                }
                else if(c == 'P'){
                    p->pos.x = j; p->pos.y = i;
                    p->inicio.x = j; p->inicio.y = i;
                    map_set(m,j,i,' ');
                }
                else {
                    map_set(m,j,i,c);
                    if(c == '.' || c == 'o') pellets++;
                }
            }
        } else {
            // linha faltando -> preencher com espa�os
            for(int j=0;j<COLUNAS;j++) map_set(m,j,i,' ');
        }
    }

    fclose(f);
    *outF = fv; *outN = nf; *outPel = pellets;
    return m;
}
// ---------------- portais ----------------
// '<' -> '>' and '>' -> '<' global search
// 'T' -> classical (same row for horizontal, same col for vertical)
void usar_portal(char *m, Pos *p, int dir){
    char c = map_get(m, p->x, p->y);
    if(c == '<'){
        for(int y=0;y<LINHAS;y++) for(int x=0;x<COLUNAS;x++) if(map_get(m,x,y) == '>'){ p->x = x; p->y = y; return; }
    }
    if(c == '>'){
        for(int y=0;y<LINHAS;y++) for(int x=0;x<COLUNAS;x++) if(map_get(m,x,y) == '<'){ p->x = x; p->y = y; return; }
    }
    if(c == 'T'){
        if(dir == 2 || dir == 3){
            for(int x=0;x<COLUNAS;x++) if(x != p->x && map_get(m,x,p->y) == 'T'){ p->x = x; return; }
        } else {
            for(int y=0;y<LINHAS;y++) if(y != p->y && map_get(m,p->x,y) == 'T'){ p->y = y; return; }
        }
    }
}

// ---------------- IA dos fantasmas ----------------
int escolher_dir(char *m,int x,int y,int atual){
    int op = oposto(atual);
    int cand[4]; int n=0;
    if(pode_mover(m,x,y-1) && 0!=op) cand[n++]=0;
    if(pode_mover(m,x,y+1) && 1!=op) cand[n++]=1;
    if(pode_mover(m,x-1,y) && 2!=op) cand[n++]=2;
    if(pode_mover(m,x+1,y) && 3!=op) cand[n++]=3;
    if(n==0){
        if(pode_mover(m,x,y-1)) cand[n++]=0;
        if(pode_mover(m,x,y+1)) cand[n++]=1;
        if(pode_mover(m,x-1,y)) cand[n++]=2;
        if(pode_mover(m,x+1,y)) cand[n++]=3;
    }
    if(n==0) return atual;
    return cand[GetRandomValue(0,n-1)];
}

void mover_fantasmas(Estado *E, float dt){
    for(int i=0;i<E->nF;i++){
        Fantasma *f = &E->fant[i];
        if(!f->alive) continue;
        float intervalo = f->vuln ? STEP_FANT_VULN : STEP_FANT;
        f->since += dt;
        if(f->since < intervalo) continue;
        f->since = 0;

        int dx=0, dy=0;
        if(f->dir==0) dy=-1;
        else if(f->dir==1) dy=1;
        else if(f->dir==2) dx=-1;
        else dx=1;

        int nx = f->pos.x + dx, ny = f->pos.y + dy;
        if(!pode_mover(E->mapa, nx, ny)){ //add função tem_fantasma_aqui (1.5)
            f->dir = escolher_dir(E->mapa, f->pos.x, f->pos.y, f->dir);
        }

        dx=dy=0;
        if(f->dir==0) dy=-1;
        else if(f->dir==1) dy=1;
        else if(f->dir==2) dx=-1;
        else dx=1;
        nx = f->pos.x + dx; ny = f->pos.y + dy;

        if(pode_mover(E->mapa, nx, ny)){
            f->pos.x = nx; f->pos.y = ny;
            usar_portal(E->mapa, &f->pos, f->dir);
        }

        if(f->vuln && GetTime() >= f->endVuln) f->vuln = false;
    }
}

// ---------------- interacao com pellets / power ----------------
void ao_mover_pac(Estado *E){
    char c = map_get(E->mapa, E->pac.pos.x, E->pac.pos.y);
    if(c == '.'){
        E->pac.pontuacao += 10;
        E->pellets--;
        map_set(E->mapa, E->pac.pos.x, E->pac.pos.y, ' ');
    } else if(c == 'o'){
        E->pac.pontuacao += 50;
        E->pellets--;
        map_set(E->mapa, E->pac.pos.x, E->pac.pos.y, ' ');
        E->power = true;
        E->endPower = GetTime() + DUR_POWER;
        for(int i=0;i<E->nF;i++){
            E->fant[i].vuln = true;
            E->fant[i].endVuln = GetTime() + DUR_POWER;
        }
    }
}

// ---------------- colisoes ----------------
void checar_colisoes(Estado *E){
    for(int i=0;i<E->nF;i++){
        Fantasma *f = &E->fant[i];
        if(!f->alive) continue;
        if(f->pos.x == E->pac.pos.x && f->pos.y == E->pac.pos.y){
            if(f->vuln){
                f->alive = false;
                E->pac.pontuacao += 100;
            } else {
                E->pac.vidas--;
                E->pac.pontuacao -= 200;
                if(E->pac.pontuacao < 0) E->pac.pontuacao = 0;

                // reposicionar Pac-Man (centro)
                E->pac.pos = E->pac.inicio; 
                E->pac.prox_dir = -1;

                // CORRE��O CR�TICA: reset do temporizador de movimento
                // para evitar o travamento que voc� descreveu
                E->lastPac = GetTime();

                // desliga power mode se houver
                E->power = false;

                // se zerou vidas, reinicia jogo no mapa1
                if(E->pac.vidas <= 0){
                    free(E->mapa);
                    free(E->fant);
                    Pacman ptmp = E->pac;
                    Fantasma *fv = NULL; int nf = 0, pel = 0;
                    char *m = ler_mapa(MAPA1_FILE, &ptmp, &fv, &nf, &pel);
                    if(m){
                        E->mapa = m;
                        E->pac = ptmp;
                        E->fant = fv;
                        E->nF = nf;
                        E->pellets = pel;
                        E->power = false;
                        E->lastPac = GetTime();
                        E->nivel = 1;
                    } else {
                        // fallback m�nimo se mapa1 n�o encontrado
                        E->pac.vidas = 3;
                        E->lastPac = GetTime();
                    }
                }
            }
        }
    }
}

// ---------------- desenho ----------------
void desenhar_mapa(Estado *E){
    for(int i=0;i<LINHAS;i++){
        for(int j=0;j<COLUNAS;j++){
            int x = j * TAM_BLOCO;
            int y = i * TAM_BLOCO;
            char c = map_get(E->mapa, j, i);
            switch(c){
                case '#': DrawRectangle(x,y,TAM_BLOCO,TAM_BLOCO, BLUE); break;
                case '.': DrawCircle(x+TAM_BLOCO/2, y+TAM_BLOCO/2, 4, WHITE); break;
                case 'o': DrawCircle(x+TAM_BLOCO/2, y+TAM_BLOCO/2, TAM_BLOCO/2 - 8, GREEN); break;
                case '<': case '>': DrawRectangle(x,y,TAM_BLOCO,TAM_BLOCO, PURPLE); break;
                case 'T': DrawRectangle(x,y,TAM_BLOCO,TAM_BLOCO, MAGENTA); break;
                default: DrawRectangleLines(x,y,TAM_BLOCO,TAM_BLOCO, Fade(BLACK, 0.04f)); break; //desenhar fundo preto (1.4)
            }
        }
    }
}

void desenhar_HUD(Estado *E){
    DrawRectangle(0, LINHAS*TAM_BLOCO, AREA_JOGAVEL, ALTURA_HUD, BLACK);
    char buf[128];
    sprintf(buf, "Vidas: %d", E->pac.vidas);
    DrawText(buf, 8, LINHAS*TAM_BLOCO + 6, 20, WHITE);
    sprintf(buf, "Pontuacao: %06d", E->pac.pontuacao);
    DrawText(buf, 220, LINHAS*TAM_BLOCO + 6, 20, WHITE);
    sprintf(buf, "Nivel: %d", E->nivel);
    DrawText(buf, 520, LINHAS*TAM_BLOCO + 6, 20, WHITE);
    sprintf(buf, "Pellets: %d", E->pellets);
    DrawText(buf, 760, LINHAS*TAM_BLOCO + 6, 20, YELLOW);
    if(E->power) DrawText("POWER", 980, LINHAS*TAM_BLOCO + 6, 20, ORANGE);
}

// ---------------- salvar / carregar ----------------
void salvar(Estado *E){
    FILE *f = fopen(SAVE_FILE, "wb");
    if(!f) return;
    fwrite(&E->nivel, sizeof(int), 1, f);
    fwrite(&E->pac, sizeof(Pacman), 1, f);
    fwrite(&E->pellets, sizeof(int), 1, f);
    fwrite(E->mapa, 1, LINHAS * COLUNAS, f);
    fwrite(&E->nF, sizeof(int), 1, f);
    for(int i=0;i<E->nF;i++) fwrite(&E->fant[i], sizeof(Fantasma), 1, f);
    fclose(f);
}

bool carregar(Estado *E){
    FILE *f = fopen(SAVE_FILE, "rb");
    if(!f) return false;
    fread(&E->nivel, sizeof(int), 1, f);
    fread(&E->pac, sizeof(Pacman), 1, f);
    fread(&E->pellets, sizeof(int), 1, f);
    // mapa deve j� existir alocado
    if(!E->mapa){
        E->mapa = malloc(LINHAS * COLUNAS);
        if(!E->mapa){ fclose(f); return false; }
    }
    fread(E->mapa, 1, LINHAS * COLUNAS, f);
    int nf = 0;
    fread(&nf, sizeof(int), 1, f);
    free(E->fant);
    E->fant = malloc(sizeof(Fantasma) * nf);
    E->nF = nf;
    for(int i=0;i<nf;i++) fread(&E->fant[i], sizeof(Fantasma), 1, f);
    fclose(f);
    E->lastPac = GetTime();
    return true;
}

// ---------------- novo jogo / carregar niveis ----------------
void novo_jogo(Estado *E, const char *arquivo){
    E->nivel = 1;
    E->pac.vidas = 3;
    E->pac.pontuacao = 0;
    E->pac.prox_dir = -1;
    E->pellets = 0;
    E->power = false;
    E->lastPac = GetTime();

    free(E->mapa); E->mapa = NULL;
    free(E->fant); E->fant = NULL;
    E->nF = 0;

    Fantasma *fv = NULL; int nf = 0, pel = 0;
    Pacman ptmp = E->pac;
    char *m = ler_mapa(arquivo, &ptmp, &fv, &nf, &pel);
    if(!m){
        // fallback: map simples com paredes externas e pellets
        m = malloc(LINHAS * COLUNAS);
        for(int i=0;i<LINHAS;i++){
            for(int j=0;j<COLUNAS;j++){
                if(i==0||i==LINHAS-1||j==0||j==COLUNAS-1) map_set(m,j,i,'#');
                else map_set(m,j,i,'.');
            }
        }
        ptmp.pos.x = COLUNAS/2; ptmp.pos.y = LINHAS/2;
        fv = malloc(sizeof(Fantasma)*4);
        for(int k=0;k<4;k++){
            fv[k].pos.x = 1 + k*2; fv[k].pos.y = 1;
            fv[k].dir = GetRandomValue(0,3); fv[k].vuln = false; fv[k].endVuln = 0; fv[k].since = 0; fv[k].alive = true;
        }
        nf = 4; pel = 0;
        for(int i=0;i<LINHAS;i++) for(int j=0;j<COLUNAS;j++) if(map_get(m,j,i)=='.') pel++;
    }

    E->mapa = m;
    E->pac = ptmp;
    E->fant = fv;
    E->nF = nf;
    E->pellets = pel;
}

// ---------------- main ----------------
int main(void){
    InitWindow(AREA_JOGAVEL, AREA_TOTAL, "Pac-Man - PROG2");
    SetTargetFPS(60);
    srand((unsigned)time(NULL));

    Estado E;
    memset(&E,0,sizeof(Estado));
    E.tela = TELA_MENU;
    E.nivel = 1;
    E.lastPac = GetTime();

    novo_jogo(&E, MAPA1_FILE);

    double last = GetTime();
    while(!WindowShouldClose()){
        double now = GetTime(); // tempo frame
        float dt = (float)(now - last); // delta time
        last = now;

        BeginDrawing();
        ClearBackground(RAYWHITE);

        if(E.tela == TELA_MENU){
            DrawText("Pac-Man - Menu\nN: novo\nC: carregar\nQ: sair\n", 260, 220, 22, BLACK);
            if(IsKeyPressed(KEY_N)){ novo_jogo(&E, MAPA1_FILE); E.tela = TELA_JOGO; }
            if(IsKeyPressed(KEY_C)){ if(carregar(&E)) E.tela = TELA_JOGO; }
            if(IsKeyPressed(KEY_Q)) break;
        }
        else if(E.tela == TELA_JOGO){
            // ---------------------------------------------------------
            //1.1 Responsividade: Nao ter Delay perceptivel
            //1.1.1: input do buffer
            if(IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))        E.pac.prox_dir = 0;
            else if(IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) E.pac.prox_dir = 1;
            else if(IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) E.pac.prox_dir = 2;
            else if(IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) E.pac.prox_dir = 3;

            // else E.pac.prox_dir = -1; (parar ao soltar a tela)


            //1.1.2: movimentacao dentro do tempo permitido
            double pac_now = GetTime();
            if(pac_now - E.lastPac >= STEP_PAC){
                
                int dir = E.pac.prox_dir; // Pega a direção que foi salva no buffer
                
                if(dir != -1){
                    int nx = E.pac.pos.x;
                    int ny = E.pac.pos.y;

                    // Calcula a proxima posição baseada no buffer
                    if(dir == 0) ny--;
                    else if(dir == 1) ny++;
                    else if(dir == 2) nx--;
                    else if(dir == 3) nx++;

                    // Verifica se pode mover para lá
                    if(pode_mover(E.mapa, nx, ny)){
                        E.pac.pos.x = nx;
                        E.pac.pos.y = ny;
                        usar_portal(E.mapa, &E.pac.pos, dir);
                        ao_mover_pac(&E);
                    }
                }
                
                E.lastPac = pac_now;
            }
            //1.1 Responsividade: Nao ter Delay perceptivel
            // ---------------------------------------------------------

            // mover fantasmas
            mover_fantasmas(&E, dt);

            // expirar power
            if(E.power && GetTime() >= E.endPower){
                E.power = false;
                for(int i=0;i<E.nF;i++) if(E.fant[i].vuln && GetTime() >= E.fant[i].endVuln) E.fant[i].vuln = false;
            }

            // colisoes
            checar_colisoes(&E);

            // trocar de fase quando pellets zerarem
           if(E.pellets <= 0){
                E.nivel++;
                
                char proximo_mapa[64]; // para o nome do proximo arquivo
                
                snprintf(proximo_mapa, sizeof(proximo_mapa), "mapas/mapa%d.txt", E.nivel);
                // carrega o proximo mapa
                free(E.mapa); free(E.fant);
                Pacman ptmp = E.pac; Fantasma *fv = NULL; int nf=0, pel=0;
                // ler o arquivo 
                char *m = ler_mapa(proximo_mapa, &ptmp, &fv, &nf, &pel);
                
                if(m){
                    //carrega o proximo nivel
                    E.mapa = m; E.pac = ptmp; E.fant = fv; E.nF = nf; E.pellets = pel;
                    
                    E.pac.pos = E.pac.inicio;
                    E.pac.prox_dir = -1;
                    
                    E.power = false;
                    
                    E.lastPac = GetTime();
                    
                } else {
                    // não existe proximo
                    novo_jogo(&E, MAPA1_FILE);
                    
                    // pontuação bônus por zerar
                    E.pac.pontuacao += 5000;
                }
            }


            // desenhar mapa, entidades e HUD
            desenhar_mapa(&E);

            // desenhar fantasmas
            for(int i=0;i<E.nF;i++){
                Fantasma *f = &E.fant[i];
                if(!f->alive) continue;
                Color cor = f->vuln ? SKYBLUE : RED;
                DrawCircle(f->pos.x*TAM_BLOCO + TAM_BLOCO/2, f->pos.y*TAM_BLOCO + TAM_BLOCO/2, TAM_BLOCO/2 - 4, cor);
            }

            // desenhar pacman
            DrawCircle(E.pac.pos.x*TAM_BLOCO + TAM_BLOCO/2, E.pac.pos.y*TAM_BLOCO + TAM_BLOCO/2, TAM_BLOCO/2 - 2, YELLOW);

            desenhar_HUD(&E);

            if(IsKeyPressed(KEY_TAB)) E.tela = TELA_PAUSA;
        }
        else if(E.tela == TELA_PAUSA){
            DrawText("PAUSADO (S: salvar, V: voltar, M: menu)", 260, 220, 22, BLACK);
            if(IsKeyPressed(KEY_S)) salvar(&E);
            if(IsKeyPressed(KEY_V)) E.tela = TELA_JOGO;
            if(IsKeyPressed(KEY_M)) E.tela = TELA_MENU;
        }

        EndDrawing();
    }

    // cleanup
    free(E.mapa);
    free(E.fant);
    CloseWindow();
    return 0;
}
