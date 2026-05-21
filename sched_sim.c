#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>

#define NUM_PROCESSOS 3
#define QUANTUM_MS 200

pid_t filhos[NUM_PROCESSOS];
long tempos_execucao[NUM_PROCESSOS];
int ativo[NUM_PROCESSOS];

int processo_atual = 0;
int trocar = 0;
struct timespec inicio_atual;

void handler_sigalrm(int sig) {
    struct timespec agora;
    clock_gettime(CLOCK_MONOTONIC, &agora);
    
    if (processo_atual >= 0 && processo_atual < NUM_PROCESSOS && ativo[processo_atual]) {
        long diff_us = (agora.tv_sec - inicio_atual.tv_sec) * 1000000 +
                       (agora.tv_nsec - inicio_atual.tv_nsec) / 1000;
        tempos_execucao[processo_atual] += diff_us;
        kill(filhos[processo_atual], SIGSTOP);
    }
    trocar = 1;
}

void configurar_timer(int ms) {
    struct itimerval timer;
    timer.it_value.tv_sec = ms / 1000;
    timer.it_value.tv_usec = (ms % 1000) * 1000;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
}

int proximo_processo(int atual) {
    // Round Robin entre processos ativos
    for (int i = 1; i <= NUM_PROCESSOS; i++) {
        int candidato = (atual + i) % NUM_PROCESSOS;
        if (ativo[candidato]) return candidato;
    }
    return -1; // nenhum ativo
}

void handler_sigchld(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < NUM_PROCESSOS; i++) {
            if (filhos[i] == pid) {
                ativo[i] = 0;
                printf("Filho %d (PID %d) terminou.\n", i, pid);
                // Se o processo atual morreu, força troca
                if (processo_atual == i) {
                    // O SIGSTOP não será necessário, apenas sinaliza troca
                    trocar = 1;
                }
                break;
            }
        }
    }
}

void trabalho_cpu() {
    volatile unsigned long x = 0;
    while (1) {
        for (unsigned long i = 0; i < 1000000; i++) {
            x += i * i;
        }
        usleep(1000);
    }
}

int main() {
    printf("=== SIMULADOR DE ESCALONAMENTO (Round Robin) ===\n");
    
    // Inicializa vetor ativo
    for (int i = 0; i < NUM_PROCESSOS; i++) ativo[i] = 1;
    
    // Cria filhos
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            raise(SIGSTOP);
            trabalho_cpu();
            exit(0);
        } else if (pid > 0) {
            filhos[i] = pid;
            printf("Filho %d criado com PID %d\n", i, pid);
        } else {
            perror("fork");
            exit(1);
        }
    }
    
    // Instala tratadores de sinais
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_sigalrm;
    sigaction(SIGALRM, &sa, NULL);
    
    sa.sa_handler = handler_sigchld;
    sigaction(SIGCHLD, &sa, NULL);
    
    // Máscara para proteger seções críticas
    sigset_t alarm_mask, old_mask;
    sigemptyset(&alarm_mask);
    sigaddset(&alarm_mask, SIGALRM);
    
    // Inicia primeiro processo
    processo_atual = 0;
    clock_gettime(CLOCK_MONOTONIC, &inicio_atual);
    kill(filhos[processo_atual], SIGCONT);
    printf("Processo %d (PID %d) iniciado\n", processo_atual, filhos[processo_atual]);
    configurar_timer(QUANTUM_MS);
    
    int rodadas = 0;
    const int MAX_RODADAS = 30;
    while (rodadas < MAX_RODADAS) {
        if (trocar) {
            sigprocmask(SIG_BLOCK, &alarm_mask, &old_mask);
            trocar = 0;
            
            int proximo = proximo_processo(processo_atual);
            if (proximo == -1) {
                printf("Nenhum processo ativo. Encerrando.\n");
                break;
            }
            
            printf("Troca: %d -> %d\n", processo_atual, proximo);
            processo_atual = proximo;
            
            // Inicia novo quantum
            clock_gettime(CLOCK_MONOTONIC, &inicio_atual);
            kill(filhos[processo_atual], SIGCONT);
            configurar_timer(QUANTUM_MS);
            rodadas++;
            
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
        }
        pause(); // aguarda sinal (SIGALRM ou SIGCHLD)
    }
    
    // Finalização
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        if (ativo[i]) {
            kill(filhos[i], SIGKILL);
            waitpid(filhos[i], NULL, 0);
        }
    }
    
    printf("\n--- Tempos totais de execução (ms) ---\n");
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        printf("Processo %d: %.2f ms\n", i, tempos_execucao[i] / 1000.0);
    }
    
    return 0;
}