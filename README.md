Abaixo, um tutorial passo a passo para implementar um **simulador de escalonamento de CPU** que utiliza processos reais do Linux. O programa será escrito em C, usará `fork()`, sinais (`SIGSTOP`/`SIGCONT`), timers reais e um escalonador configurável (Round Robin). Cada passo inclui:

- **Conceito teórico**  
- **Código a ser adicionado**  
- **Teste prático** (para verificar o funcionamento daquela etapa)

Ao final, teremos um script completo e funcional.

---

## Pré‑requisitos

- Sistema Linux (qualquer distribuição)  
- Compilador GCC (`gcc`)  
- Conhecimentos básicos de terminal, processos e sinais  

---

## Passo 1 – Estrutura básica e criação dos processos filhos

### Teoria
O escalonador será o **processo pai**. Ele cria `N` processos filhos usando `fork()`. Cada filho executará uma função `trabalho_cpu()` que consome CPU continuamente.  
Para que o pai controle quem executa, cada filho começa **suspenso** (`SIGSTOP` logo após nascer). Assim, o pai precisa enviar `SIGCONT` para liberar um filho por vez.

### Código (adicione em `sched_sim.c`)

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <string.h>

#define NUM_PROCESSOS 3   // quantidade de filhos

pid_t filhos[NUM_PROCESSOS];   // armazena PIDs dos filhos

// Função que cada filho executará (trabalho pesado de CPU)
void trabalho_cpu() {
    volatile unsigned long x = 0;
    while (1) {
        for (unsigned long i = 0; i < 1000000; i++) {
            x += i * i;
        }
        // pequeno sleep para não aquecer demais a CPU (opcional)
        usleep(1000);
    }
}

int main() {
    printf("=== SIMULADOR DE ESCALONAMENTO (Round Robin) ===\n");

    for (int i = 0; i < NUM_PROCESSOS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            // Processo filho
            raise(SIGSTOP);               // começa parado
            trabalho_cpu();               // nunca retorna
            exit(0);
        } else if (pid > 0) {
            filhos[i] = pid;
            printf("Filho %d criado com PID %d\n", i, pid);
        } else {
            perror("fork");
            exit(1);
        }
    }

    // Aqui o pai continua... (próximos passos)

    // Por enquanto, aguarda um tempo e depois mata todos
    sleep(5);
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        kill(filhos[i], SIGKILL);
        waitpid(filhos[i], NULL, 0);
    }
    return 0;
}
```

### Teste do Passo 1
Compile e execute:

```bash
gcc sched_sim.c -o sched_sim
./sched_sim
```

Saída esperada:  
```
=== SIMULADOR DE ESCALONAMENTO (Round Robin) ===
Filho 0 criado com PID 1234
Filho 1 criado com PID 1235
Filho 2 criado com PID 1236
```

Em outro terminal, rode `ps aux | grep sched_sim`. Você verá o processo pai e três filhos, todos com estado **T** (suspended/stopped) – porque deram `raise(SIGSTOP)`.  
Após 5 segundos, o programa mata os filhos e termina.

✅ **Funcionamento**: os filhos existem e estão parados.

---

## Passo 2 – Escalonador simples (Round Robin sem timer)

### Teoria
O pai deve escolher um processo para executar e enviar `SIGCONT`. Depois de um certo tempo (quantum), ele envia `SIGSTOP` para esse processo e passa para o próximo. Inicialmente faremos sem timer: um loop onde cada filho executa por 1 segundo (medido com `sleep(1)`) e depois trocamos.

### Código a adicionar (substitua o `sleep(5)` e a seção final)

```c
    int atual = 0;
    // Coloca o primeiro processo para executar
    kill(filhos[atual], SIGCONT);
    printf("Processo %d (PID %d) iniciado\n", atual, filhos[atual]);

    for (int rodadas = 0; rodadas < 10; rodadas++) {   // 10 trocas
        sleep(1);   // quantum de 1 segundo (simulado)
        
        // Para o processo atual
        kill(filhos[atual], SIGSTOP);
        printf("Processo %d (PID %d) pausado\n", atual, filhos[atual]);
        
        // Próximo (Round Robin)
        atual = (atual + 1) % NUM_PROCESSOS;
        kill(filhos[atual], SIGCONT);
        printf("Processo %d (PID %d) retomado\n", atual, filhos[atual]);
    }

    // Finalização
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        kill(filhos[i], SIGKILL);
        waitpid(filhos[i], NULL, 0);
    }
```

### Teste do Passo 2
Execute o programa. Você verá as mensagens a cada segundo indicando a troca.  
Em outro terminal, use `top -p PID1,PID2,PID3` (substitua pelos PIDs dos filhos). Observe que o consumo de CPU de cada filho sobe durante seu quantum e cai para zero quando parado.

✅ **Funcionamento**: o pai controla a alternância, mas o quantum é fixo (1 segundo) e pouco preciso.

---

## Passo 3 – Timer real (`setitimer`) com sinal `SIGALRM`

### Teoria
Para um controle mais realista e preciso, usamos `setitimer(ITIMER_REAL, ...)`. Ele envia `SIGALRM` ao processo pai após um intervalo (nosso quantum). O pai deve instalar um tratador para `SIGALRM` que, ao ser chamado, interrompa o processo atual e sinalize que é hora de trocar.

### Código a adicionar (antes de `main`, variáveis globais)

```c
int trocar = 0;           // flag sinalizada pelo timer
int processo_atual = 0;

void handler_sigalrm(int sig) {
    if (processo_atual >= 0 && processo_atual < NUM_PROCESSOS) {
        kill(filhos[processo_atual], SIGSTOP);   // pausa o atual
    }
    trocar = 1;           // avisa o loop principal
}

void configurar_timer(int ms) {
    struct itimerval timer;
    timer.it_value.tv_sec = ms / 1000;
    timer.it_value.tv_usec = (ms % 1000) * 1000;
    timer.it_interval.tv_sec = 0;     // dispara apenas uma vez
    timer.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer, NULL);
}
```

### Modifique o `main`: instalar o tratador e usar o timer

```c
    // Instala o tratador do SIGALRM
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_sigalrm;
    sigaction(SIGALRM, &sa, NULL);

    processo_atual = 0;
    kill(filhos[processo_atual], SIGCONT);
    printf("Processo %d (PID %d) iniciado\n", processo_atual, filhos[processo_atual]);
    
    // Configura o primeiro quantum (ex: 200 ms)
    configurar_timer(200);
    
    int rodadas = 0;
    while (rodadas < 20) {   // 20 trocas
        if (trocar) {
            trocar = 0;
            // Escolhe próximo (Round Robin)
            int proximo = (processo_atual + 1) % NUM_PROCESSOS;
            printf("Trocando %d -> %d\n", processo_atual, proximo);
            processo_atual = proximo;
            kill(filhos[processo_atual], SIGCONT);
            configurar_timer(200);   // reinicia o timer para o próximo quantum
            rodadas++;
        }
        pause();   // aguarda o próximo sinal (SIGALRM ou outro)
    }
    
    // Finalização: mata todos os filhos
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        kill(filhos[i], SIGKILL);
        waitpid(filhos[i], NULL, 0);
    }
```

### Teste do Passo 3
Compile e execute. Agora a troca ocorre a cada 200 ms (5 trocas por segundo). Para verificar a precisão, você pode aumentar o número de rodadas e usar o comando `time` no programa.  
Use `strace -e signal -p PID_DO_PAI` para confirmar que `SIGALRM` está sendo recebido.

✅ **Funcionamento**: o timer preempta o processo atual corretamente.

---

## Passo 4 – Medição do tempo de execução de cada processo

### Teoria
Queremos saber, ao final da simulação, quanto tempo real cada filho efetivamente executou (soma de seus quanta). Como o pai controla o início e a pausa de cada filho, podemos usar `clock_gettime(CLOCK_MONOTONIC, ...)` para marcar o instante em que enviamos `SIGCONT` e o instante em que enviamos `SIGSTOP` (dentro do tratador). A diferença é o tempo de CPU (tempo de parede) que o filho ocupou.

### Código a adicionar (variáveis globais)

```c
#include <time.h>

long tempos_execucao[NUM_PROCESSOS];   // em microssegundos
struct timespec inicio_atual;
```

### Modificações no tratador `handler_sigalrm`

```c
void handler_sigalrm(int sig) {
    struct timespec agora;
    clock_gettime(CLOCK_MONOTONIC, &agora);
    
    if (processo_atual >= 0 && processo_atual < NUM_PROCESSOS) {
        // Calcula o tempo desde o início deste processo
        long diff_us = (agora.tv_sec - inicio_atual.tv_sec) * 1000000 +
                       (agora.tv_nsec - inicio_atual.tv_nsec) / 1000;
        tempos_execucao[processo_atual] += diff_us;
        
        kill(filhos[processo_atual], SIGSTOP);
    }
    trocar = 1;
}
```

### No `main`, ao iniciar um processo (antes do `kill(SIGCONT)`)

```c
    clock_gettime(CLOCK_MONOTONIC, &inicio_atual);
    kill(filhos[processo_atual], SIGCONT);
```

### Ao retomar um processo (dentro do loop `if (trocar)`)

```c
            // Registra o início do próximo quantum
            clock_gettime(CLOCK_MONOTONIC, &inicio_atual);
            kill(filhos[processo_atual], SIGCONT);
```

### Ao final da simulação, imprimir os tempos

```c
    printf("\n--- Tempos totais de execução (ms) ---\n");
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        printf("Processo %d: %.2f ms\n", i, tempos_execucao[i] / 1000.0);
    }
```

### Teste do Passo 4
Execute a simulação por muitas rodadas (ex.: 100 trocas). A soma dos tempos de todos os processos deve ser aproximadamente igual ao tempo total decorrido (ex.: se rodou 10 segundos, a soma dos tempos será ~10 segundos, pois somente um processo executa por vez). Pequenas diferenças ocorrem devido ao tempo gasto pelo pai e às latências dos sinais.

✅ **Funcionamento**: o pai mede com precisão o tempo real de CPU de cada filho.

---

## Passo 5 – Escalonador configurável (outros algoritmos)

### Teoria
Podemos implementar diferentes políticas alterando a lógica de escolha do próximo processo. Vamos criar uma função `proximo_processo()` que, por enquanto, implementa Round Robin. Depois você pode modificar para prioridades fixas, prioridades dinâmicas, etc.

### Código (adicione antes de `main`)

```c
int proximo_processo(int atual) {
    // Round Robin simples
    return (atual + 1) % NUM_PROCESSOS;
}
```

### No loop de troca, substitua a lógica fixa por:

```c
            int proximo = proximo_processo(processo_atual);
```

### Exemplo de política por prioridade fixa (valores menores = maior prioridade)

```c
int prioridades[NUM_PROCESSOS] = {2, 0, 1};   // processo 1 tem maior prioridade

int proximo_prioridade(int atual) {
    int melhor = -1;
    for (int i = 0; i < NUM_PROCESSOS; i++) {
        if (i == atual) continue;
        if (melhor == -1 || prioridades[i] < prioridades[melhor])
            melhor = i;
    }
    return melhor;
}
```

### Teste do Passo 5
Troque a função chamada e observe a ordem de execução. Para prioridades fixas, o processo de maior prioridade (menor número) executará com mais frequência. Você pode ajustar os quanta ou medir o número de vezes que cada processo foi escalonado.

---

## Passo 6 – Tratamento adequado de sinais e término dos filhos

### Teoria
Os sinais `SIGSTOP` e `SIGCONT` são seguros, mas o `SIGALRM` pode chegar enquanto o pai está no meio de uma troca, causando condições de corrida. Além disso, se um filho terminar prematuramente, o pai deve removê‑lo da fila para não tentar parar/continuar um processo inexistente.

### Código para mascarar `SIGALRM` durante a troca (use `sigprocmask`)

No início do `main`, crie uma máscara que bloqueia `SIGALRM`:

```c
    sigset_t alarm_mask, old_mask;
    sigemptyset(&alarm_mask);
    sigaddset(&alarm_mask, SIGALRM);
```

Dentro do loop, ao entrar na seção crítica (quando `trocar == 1`), bloqueie:

```c
        if (trocar) {
            sigprocmask(SIG_BLOCK, &alarm_mask, &old_mask);
            trocar = 0;
            // ... (cálculo do próximo, kill, configurar_timer)
            sigprocmask(SIG_SETMASK, &old_mask, NULL);
        }
```

### Tratamento de `SIGCHLD` (filho terminou)

Instale um tratador para `SIGCHLD` que:

- Chama `waitpid(-1, NULL, WNOHANG)` para coletar o status.
- Marca aquele PID como inválido (ex.: array `ativo[]`).
- Se o processo atual morreu, força uma troca imediata.

Exemplo simplificado (adicione variável `int ativo[NUM_PROCESSOS]`).

### Teste do Passo 6
- Execute o programa e, em outro terminal, envie `SIGKILL` para um dos filhos (`kill -9 PID_FILHO`). O pai deve detectar e remover o filho da escala, continuando com os demais.  
- Para testar a máscara, rode o programa sob `strace` e veja se não ocorrem `SIGALRM` durante o trecho crítico.

---

## Código completo final (Round Robin + medição + máscara)

Abaixo o script completo, reunindo todos os passos. Copie, compile e execute.

```c
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
```

### Compilação e execução

```bash
gcc sched_sim.c -o sched_sim -Wall
./sched_sim
```

---

## Possíveis aprimoramentos (desafios)

- **Políticas de prioridade dinâmica** (ex.: o processo que mais espera ganha prioridade).  
- **Simulação de operações de E/S** (substituir o loop de CPU por `read` de um pipe vazio; o pai deve bloquear o processo até que dados cheguem).  
- **Uso de `sched_setaffinity`** para fixar todos os processos em um único núcleo e tornar a simulação mais determinística.  
- **Interface via `ncurses`** mostrando em tempo real a fila e os tempos.
