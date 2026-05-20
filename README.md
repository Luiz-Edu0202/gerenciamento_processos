# gerenciamento_processos
Claro! Vou detalhar o projeto **"Monitor de recursos com alerta"** adaptado para o Windows, mantendo o espírito do original (cgroups no Linux). O objetivo é: **controlar e monitorar o uso de CPU e memória de um ou mais processos, aplicando limites e gerando alertas quando os recursos excederem um limiar.**

Aqui está um plano completo, com código e explicações.

---

## 🎯 Objetivo do Projeto

- Criar um **Job Object** (grupo de processos) no Windows.
- Definir limites para:
  - **CPU**: percentual máximo de uso (ex.: 30% de um core).
  - **Memória**: tamanho máximo do working set (ex.: 200 MB).
- Monitorar o consumo real desses processos em tempo real.
- Disparar alertas (log, e-mail, notificação) quando o consumo ultrapassar um limiar configurável.
- Registrar histórico de uso para análise posterior.

---

## 🛠️ Ferramentas e Tecnologias Utilizadas

| Componente | Tecnologia | Função |
|------------|------------|--------|
| **Criação do Job** | PowerShell + .NET (`[System.Diagnostics.Process]`, `Add-Type` com código C#) | Criar e gerenciar Job Objects |
| **Limite de CPU** | `JOBOBJECT_BASIC_LIMIT_INFORMATION` (via C#/P/Invoke) | Percentual máximo de CPU |
| **Limite de Memória** | `JOBOBJECT_EXTENDED_LIMIT_INFORMATION` (working set) | Tamanho máximo de memória física |
| **Monitoramento** | `Get-Counter` (Performance Counters) ou WMI (`Win32_PerfFormattedData_PerfProc_Process`) | Coleta de métricas em tempo real |
| **Alertas** | PowerShell script + `Write-EventLog` (log do Windows) ou `Send-MailMessage` | Notificação de eventos |
| **Agendamento** | Agendador de Tarefas do Windows | Executar o monitor automaticamente |
| **Log** | `Export-Csv`, arquivo texto com timestamp | Histórico para análise |

---

## 📝 Passo a Passo Detalhado

### 1. Criar um Job Object e atribuir processos

No Windows, um **Job Object** é um container que permite aplicar limites e políticas a um grupo de processos. Podemos criá-lo via PowerShell com código C# inline (P/Invoke) ou usando módulos prontos como `JobObject` da galeria do PowerShell.

Para simplificar, vou usar um script puro PowerShell que chama as APIs do Windows via `Add-Type` (compila C# on-the-fly). Isso dá controle total.

```powershell
# Criar Job Object
Add-Type @"
using System;
using System.Runtime.InteropServices;

public class JobObject {
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern IntPtr CreateJobObject(IntPtr lpJobAttributes, string lpName);

    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool SetInformationJobObject(IntPtr hJob, int JobObjectInfoClass, IntPtr lpJobObjectInfo, uint cbJobObjectInfoLength);

    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool AssignProcessToJobObject(IntPtr hJob, IntPtr hProcess);

    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool CloseHandle(IntPtr hObject);

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
        public long PerProcessUserTimeLimit;
        public long PerJobUserTimeLimit;
        public int LimitFlags;
        public UIntPtr MinimumWorkingSetSize;
        public UIntPtr MaximumWorkingSetSize;
        public int ActiveProcessLimit;
        public long Affinity;
        public int PriorityClass;
        public int SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
        public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
        public long IoReadBytes;
        public long IoWriteBytes;
        public long OtherTransferCount;
        public long ProcessMemoryLimit;
        public long JobMemoryLimit;
        public long PeakProcessMemoryUsed;
        public long PeakJobMemoryUsed;
    }

    public const int JOB_OBJECT_LIMIT_CPU_RATE = 0x00040000;
    public const int JOB_OBJECT_LIMIT_PROCESS_MEMORY = 0x00000100;
    public const int JOB_OBJECT_LIMIT_JOB_MEMORY = 0x00000200;
    public const int JOB_OBJECT_LIMIT_ACTIVE_PROCESS = 0x00000008;

    public static IntPtr CreateJob(string name) {
        return CreateJobObject(IntPtr.Zero, name);
    }

    public static bool SetCpuLimit(IntPtr hJob, int maxCpuPercent) {
        // Implementar limite de CPU via taxa (Windows 8+)
        // Para simplificar, usamos limite de tempo de usuário por processo
        var info = new JOBOBJECT_BASIC_LIMIT_INFORMATION();
        info.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY; // placeholder
        // Requer código adicional - veja texto explicativo
        return true;
    }

    public static bool SetMemoryLimit(IntPtr hJob, long maxMemoryBytes) {
        var extInfo = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
        extInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        extInfo.ProcessMemoryLimit = maxMemoryBytes;
        IntPtr ptr = Marshal.AllocHGlobal(Marshal.SizeOf(extInfo));
        Marshal.StructureToPtr(extInfo, ptr, false);
        bool result = SetInformationJobObject(hJob, 9, ptr, (uint)Marshal.SizeOf(extInfo)); // 9 = ExtendedLimitInformation
        Marshal.FreeHGlobal(ptr);
        return result;
    }

    public static bool AssignProcess(IntPtr hJob, int pid) {
        IntPtr hProcess = OpenProcess(0x1F0FFF, false, pid); // PROCESS_ALL_ACCESS
        bool result = AssignProcessToJobObject(hJob, hProcess);
        CloseHandle(hProcess);
        return result;
    }

    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);
}
"@
```

**Nota:** O código acima é um esqueleto. Para um projeto funcional, sugiro usar um módulo pronto da galeria PowerShell, como `PoshJobObject` (instale com `Install-Module -Name PoshJobObject`). Ele simplifica enormemente.

**Exemplo usando `PoshJobObject` (recomendado):**
```powershell
# Instalar módulo (uma vez)
Install-Module PoshJobObject -Force

# Criar job com nome "MeuMonitor"
$job = New-JobObject -Name "MeuMonitor"

# Definir limite de memória: 200 MB
Set-JobObjectMemoryLimit -JobObject $job -MaximumWorkingSetSizeMB 200

# Definir limite de CPU: 30% de uso total (em relação a todos os cores)
# O módulo oferece opções de CPU rate (Windows 8+)
Set-JobObjectCpuRate -JobObject $job -CpuRatePercent 30

# Adicionar um processo (ex.: notepad)
Start-Process notepad
$proc = Get-Process notepad
Add-ProcessToJobObject -JobObject $job -Process $proc
```

Agora o `notepad` está limitado a 30% de CPU e 200 MB de working set.

---

### 2. Monitorar o consumo de recursos do Job

Precisamos coletar métricas dos processos dentro do job. Podemos usar **Performance Counters**:

```powershell
# Coleta a cada 2 segundos
$jobName = "MeuMonitor"
$counterCpu = "\Process(notepad*)\% Processor Time"
$counterMem = "\Process(notepad*)\Working Set - Private (Bytes)"

# Loop de monitoramento
while ($true) {
    $cpu = (Get-Counter $counterCpu -ErrorAction SilentlyContinue).CounterSamples.CookedValue
    $mem = (Get-Counter $counterMem -ErrorAction SilentlyContinue).CounterSamples.CookedValue
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    
    # Se mais de um processo com mesmo nome, soma ou pega o primeiro
    if ($cpu -is [array]) { $cpu = ($cpu | Measure-Object -Sum).Sum }
    if ($mem -is [array]) { $mem = ($mem | Measure-Object -Sum).Sum }
    
    Write-Host "$timestamp - CPU: $([math]::Round($cpu,2))% - Memória: $([math]::Round($mem/1MB,2)) MB"
    
    # Disparar alerta se ultrapassar limiares
    if ($cpu -gt 25) { # alerta antes do limite (30%)
        Write-Warning "Alerta: CPU alta ($cpu%) no job $jobName"
        # Enviar e-mail, log, etc.
    }
    if ($mem -gt 190MB) {
        Write-Warning "Alerta: Memória próxima do limite ($([math]::Round($mem/1MB,2)) MB)"
    }
    
    Start-Sleep -Seconds 2
}
```

---

### 3. Disparar alertas personalizados

Você pode implementar ações como:

- **Evento no log do Windows:**
```powershell
Write-EventLog -LogName Application -Source "MonitorRecursos" -EventId 1001 -EntryType Warning -Message "CPU alta no job $jobName: $cpu%"
```
(Primeiro é necessário criar a fonte: `New-EventLog -LogName Application -Source "MonitorRecursos"`)

- **E-mail:**
```powershell
Send-MailMessage -To "admin@exemplo.com" -From "monitor@exemplo.com" -Subject "Alerta de recurso" -Body "CPU: $cpu%" -SmtpServer "smtp.exemplo.com"
```

- **Notificação no Windows (toast):** Usar `BurntToast` module.

---

### 4. Automatizar com o Agendador de Tarefas

Para rodar o monitor em segundo plano sempre que o sistema iniciar ou em intervalos regulares:

1. Salve o script de monitoramento como `MonitorJob.ps1`.
2. Abra o Agendador de Tarefas (`taskschd.msc`).
3. Crie uma tarefa:
   - **Gatilho:** Na inicialização do sistema ou a cada X minutos.
   - **Ação:** Iniciar programa `powershell.exe` com argumentos `-File "C:\caminho\MonitorJob.ps1" -WindowStyle Hidden`.
   - **Condições:** Marcar "Executar se o usuário estiver conectado ou não" e "Executar com privilégios máximos".

---

### 5. Registrar histórico em log

Dentro do loop de monitoramento, adicione:

```powershell
$logEntry = [PSCustomObject]@{
    Timestamp = $timestamp
    CPU_Percent = [math]::Round($cpu,2)
    Memory_MB = [math]::Round($mem/1MB,2)
}
$logEntry | Export-Csv -Path "C:\Logs\monitor.csv" -Append -NoTypeInformation
```

---

## 🧪 Testando o Projeto

1. **Crie um Job Object** com limite de CPU de 30% e memória de 200 MB.
2. **Adicione um processo pesado** (ex.: `stress` para Windows – use `StressCPUPortable` ou um loop infinito em PowerShell).
3. **Monitore** o consumo e veja se o processo nunca ultrapassa os limites (o sistema operacional aplica throttling).
4. **Force um pico** – tente alocar mais memória (ex.: um script em C# que aloca um array grande). O processo será impedido de alocar além do limite (erro de memória).
5. **Verifique os alertas** no log ou e-mail.

---

## ⚠️ Limitações e Considerações

- **Limite de CPU**: No Windows, o limite percentual via Job Object é efetivo apenas a partir do Windows 8/Server 2012. Versões anteriores exigem limites de tempo de usuário (mais complexos). O módulo `PoshJobObject` cuida disso.
- **Memória**: O limite de working set não é absoluto – o Windows pode permitir que o processo ultrapasse momentaneamente. Para um limite rígido, use `JobMemoryLimit` (memória total do job) que é mais estrito.
- **Privilégios**: Criar e gerenciar Job Objects requer privilégios de administrador na maioria das operações.
- **Processos já existentes**: Para adicionar processos que já estão rodando, você precisa de acesso adequado (às vezes, o processo pai pode impedir).

---

## 📚 Expandindo o Projeto

Após dominar o básico, você pode:

- Criar uma interface gráfica simples (WinForms/ WPF) que mostre gráficos de uso.
- Adicionar suporte a múltiplos jobs e processos dinâmicos.
- Integrar com o **Windows Admin Center** ou **InfluxDB + Grafana** para dashboards.
- Implementar **auto-scaling**: quando um job excede o limite, iniciar outro processo em um job separado.

---

## ✅ Conclusão

Com esse projeto, você terá na prática o controle de recursos de processos no Windows, similar ao `cgroups` do Linux, usando ferramentas nativas. Você aprenderá sobre **Job Objects**, **Performance Counters**, **PowerShell avançado**, **Agendador de Tarefas** e **alertas**.
---
Aqui está uma descrição detalhada do projeto **"Monitor de recursos com alerta (cgroups)"** para Linux, usando cgroups (v2, que é o padrão atual). O objetivo é criar um ambiente controlado onde você pode limitar, monitorar e receber alertas sobre o uso de CPU e memória de um grupo de processos.

---

## 🎯 Objetivo do Projeto

- Criar um **cgroup** (grupo de controle) para isolar um ou mais processos.
- Definir limites rígidos para:
  - **CPU**: percentual máximo de uso (ex.: 20% de um core).
  - **Memória**: limite máximo (ex.: 512 MB) com políticas de excedente (como matar o processo ou pausá-lo).
- Monitorar o consumo real desses processos em tempo real.
- Disparar alertas (log, e-mail, notificação no terminal) quando o consumo ultrapassar um limiar (ex.: 80% do limite).
- Registrar histórico para análise.

---

## 🛠️ Ferramentas e Tecnologias Utilizadas

| Componente | Comando/Ferramenta | Função |
|------------|--------------------|--------|
| **Criação e configuração do cgroup** | `cgcreate`, `cgset` (ou arquivos em `/sys/fs/cgroup/`) | Definir limites e parâmetros |
| **Atribuir processos ao cgroup** | `cgclassify`, `cgexec` | Mover processos existentes ou iniciar novos dentro do cgroup |
| **Monitoramento** | `cgget`, `systemd-cgtop`, `cat` em arquivos de estatísticas | Obter métricas de uso atual |
| **Alertas** | Script bash + `logger` (syslog), `mail`, `notify-send` | Enviar notificações |
| **Log** | `date`, `echo >> arquivo.csv` | Registrar histórico |
| **Automação** | `cron` ou `systemd timer` | Executar monitor periodicamente |
| **Visualização** | `systemd-cgtop`, `htop` (com cgroup support) | Monitoramento interativo |

---

## 📝 Passo a Passo Detalhado (cgroups v2)

### Pré‑requisitos
- Sistema com cgroups v2 habilitado (verifique com `mount | grep cgroup2`).  
  A maioria das distribuições modernas (Ubuntu 20.04+, Fedora, Debian 11+, RHEL 8+) já usa v2 por padrão.
- Pacotes utilitários: `cgroup-tools` (no Ubuntu/Debian: `sudo apt install cgroup-tools`) – fornece `cgcreate`, `cgset`, `cgget`, etc.

---

### 1. Criar um cgroup

Os cgroups v2 são organizados em uma hierarquia única montada em `/sys/fs/cgroup/`. Criamos um subdiretório para o nosso grupo:

```bash
# Criar um cgroup chamado "meulimite" sob o grupo raiz
sudo mkdir /sys/fs/cgroup/meulimite
```

Ou usando `cgcreate` (mais portável):

```bash
sudo cgcreate -g cpu,memory:meulimite
```

Isso cria automaticamente os arquivos de controle para CPU e memória.

---

### 2. Definir limites de recursos

#### a) Limite de CPU (percentual máximo)

Em cgroups v2, o limite de CPU é controlado pelos arquivos `cpu.max`.  
O formato é: `[quota] [período]`.  
- `quota` = tempo máximo de CPU em microssegundos por período.  
- `período` = janela de tempo padrão (100.000 µs = 100 ms).

Para limitar a **20% de um core** (ou 20% de um CPU), faça:  
`quota = 20% de 100.000 = 20.000`.

```bash
echo "20000 100000" | sudo tee /sys/fs/cgroup/meulimite/cpu.max
```

Com `cgset`:

```bash
sudo cgset -r cpu.max="20000 100000" meulimite
```

Para limitar a **100% de 2 cores** (ou seja, 200% de um core), use `200000 100000`.

#### b) Limite de memória

Defina o limite máximo em bytes. Exemplo: 512 MB.

```bash
echo "536870912" | sudo tee /sys/fs/cgroup/meulimite/memory.max
```

Com `cgset`:

```bash
sudo cgset -r memory.max=536870912 meulimite
```

Também é possível definir uma política de quando o limite for excedido (`memory.high` para pressão suave, `memory.max` para morte imediata). Vamos usar `memory.max` para limite rígido.

---

### 3. Executar processos dentro do cgroup

#### a) Iniciar um novo processo já dentro do cgroup

```bash
sudo cgexec -g cpu,memory:meulimite stress --cpu 2 --vm 1 --vm-bytes 400M --timeout 60
```

#### b) Mover um processo já existente

Encontre o PID do processo (ex.: `firefox`). Depois mova:

```bash
sudo cgclassify -g cpu,memory:meulimite <PID>
```

Verifique se o processo está no cgroup correto:

```bash
cat /proc/<PID>/cgroup
```

---

### 4. Monitorar o consumo em tempo real

#### a) Coletar métricas manualmente

Os arquivos de estatísticas ficam dentro do diretório do cgroup:

- **CPU**: `/sys/fs/cgroup/meulimite/cpu.stat` – contém `usage_usec` (tempo total de CPU usado) e `user_usec` / `system_usec`.
- **Memória**: `/sys/fs/cgroup/meulimite/memory.current` – uso atual em bytes.
- **Memória máxima já atingida**: `memory.peak`.

Exemplo de script simples:

```bash
#!/bin/bash
CGROUP_DIR="/sys/fs/cgroup/meulimite"

while true; do
    cpu_usage=$(awk '/usage_usec/ {print $2}' $CGROUP_DIR/cpu.stat)
    mem_current=$(cat $CGROUP_DIR/memory.current)
    mem_max=$(cat $CGROUP_DIR/memory.max)
    mem_percent=$(( (mem_current * 100) / mem_max ))
    
    echo "$(date) - CPU time (µs): $cpu_usage - Memória: $((mem_current/1024/1024)) MB / $((mem_max/1024/1024)) MB ($mem_percent%)"
    
    if [ $mem_percent -gt 80 ]; then
        echo "ALERTA: Memória acima de 80% do limite!"
        logger -t monitor-cgroup "Memória alta no cgroup meulimite: $mem_percent%"
    fi
    
    sleep 2
done
```

#### b) Usar `systemd-cgtop`

Para visualização interativa estilo `top`:

```bash
systemd-cgtop -d 2
```

Filtre para ver apenas o grupo `meulimite` com:

```bash
systemd-cgtop -d 2 -n meulimite
```

---

### 5. Disparar alertas automáticos

Além do script de monitoramento, podemos configurar alertas baseados em eventos usando **eBPF** ou simplesmente o `cron`.

#### Exemplo: alerta por e-mail quando a memória ultrapassa 80%

Crie um script `/usr/local/bin/monitor_cgroup.sh`:

```bash
#!/bin/bash
CGROUP="meulimite"
LIMIT_FILE="/sys/fs/cgroup/$CGROUP/memory.max"
CURRENT_FILE="/sys/fs/cgroup/$CGROUP/memory.current"
LIMIT=$(cat $LIMIT_FILE)
CURRENT=$(cat $CURRENT_FILE)
PERCENT=$(( (CURRENT * 100) / LIMIT ))

if [ $PERCENT -gt 80 ]; then
    echo "Alerta: cgroup $CGROUP está com $PERCENT% de memória usada ($((CURRENT/1024/1024)) MB / $((LIMIT/1024/1024)) MB)" | \
    mail -s "Alerta de recurso" seu-email@exemplo.com
fi
```

Adicione ao `crontab` do root (`sudo crontab -e`):

```cron
* * * * * /usr/local/bin/monitor_cgroup.sh
```

#### Outras formas de alerta:

- **Notificação desktop**: `notify-send "Alerta: memória alta"` (requer `DISPLAY` e DBUS configurados).
- **Log centralizado**: usar `logger` para enviar ao syslog; então configurar `rsyslog` para encaminhar para um servidor.
- **Webhook**: chamar uma URL com `curl` (para integração com Slack, Telegram, etc.).

---

### 6. Registrar histórico em arquivo CSV

Modifique o script de monitor para salvar métricas periodicamente:

```bash
LOG_FILE="/var/log/cgroup_meulimite.csv"
echo "timestamp,cpu_usec,mem_bytes,mem_percent" >> $LOG_FILE

while true; do
    cpu_usage=$(awk '/usage_usec/ {print $2}' /sys/fs/cgroup/meulimite/cpu.stat)
    mem_current=$(cat /sys/fs/cgroup/meulimite/memory.current)
    mem_max=$(cat /sys/fs/cgroup/meulimite/memory.max)
    mem_percent=$(( (mem_current * 100) / mem_max ))
    echo "$(date +%s),$cpu_usage,$mem_current,$mem_percent" >> $LOG_FILE
    sleep 10
done
```

Depois é possível gerar gráficos com `gnuplot` ou importar para planilha.

---

### 7. Automação com systemd (recomendado para serviços)

Em vez de usar `cgexec` manualmente, podemos criar um **service unit** do systemd que já aplica os limites de cgroup. O systemd gerencia cgroups automaticamente para cada unidade.

Crie o arquivo `/etc/systemd/system/meuprocesso-limitado.service`:

```ini
[Unit]
Description=Exemplo de serviço com limites de recursos

[Service]
ExecStart=/usr/bin/stress --cpu 1 --vm 1 --vm-bytes 300M
CPUQuota=20%          # Limita a 20% de CPU
MemoryMax=512M        # Limite de memória
MemoryHigh=400M       # Alerta suave aos 400 MB
Restart=always

[Install]
WantedBy=multi-user.target
```

Ative e inicie:

```bash
sudo systemctl daemon-reload
sudo systemctl enable meuprocesso-limitado.service
sudo systemctl start meuprocesso-limitado.service
```

Para monitorar, use:

```bash
systemd-cgtop
systemctl show meuprocesso-limitado.service -p MemoryCurrent,CPUUsageNSec
```

---

## 🧪 Testando o Projeto

1. **Crie o cgroup** e **defina limites baixos** (ex.: CPU 20%, memória 100 MB).
2. **Execute um programa que consome muitos recursos** (ex.: `stress` ou um script em loop).
3. **Observe**:
   - O processo não conseguirá usar mais que 20% de CPU (veja com `top`).
   - Se tentar alocar mais que 100 MB, será morto (ou pausado, dependendo da política).
4. **Monitore os alertas** – quando o uso de memória chegar a 80 MB, você deve ver uma mensagem (e‑mail, syslog, etc.).
5. **Verifique o histórico** no arquivo CSV.

---

## 📊 Explicação dos Arquivos e Conceitos do cgroup v2

| Arquivo | Descrição |
|---------|------------|
| `cpu.max` | Limite de CPU: `"quota período"`. Quota 0 significa sem acesso à CPU. |
| `cpu.stat` | Estatísticas: `usage_usec` (tempo total de CPU consumido em microssegundos). |
| `memory.max` | Limite máximo de memória em bytes. Se excedido, o OOM killer mata um processo. |
| `memory.current` | Uso atual de memória (incluindo cache, etc.). |
| `memory.events` | Contadores de eventos (como `high`, `max`, `oom`). |
| `cgroup.procs` | Lista de PIDs pertencentes ao cgroup. |

Para ver todos os parâmetros disponíveis:

```bash
ls /sys/fs/cgroup/meulimite/
```

---

## 🚀 Expandindo o Projeto

Após dominar o básico, você pode:

- **Usar `cgroups` via libcgroup** (programas em C ou Python com `python-cgroup`).
- **Implementar auto‑scaling**: ao atingir 80% de memória, iniciar um novo processo em outro cgroup.
- **Integrar com Prometheus + Grafana**: exportar métricas do `/sys/fs/cgroup` via node_exporter.
- **Criar uma interface web** (Flask + Bootstrap) para definir limites e visualizar gráficos em tempo real.
- **Simular contenção de recursos** usando `stress-ng` e observar como o cgroup isola o processo.

---

## ✅ Conclusão

Com este projeto você aprenderá na prática:

- Como o Linux **isola e limita recursos** com cgroups v2.
- A **monitorar e alertar** sobre uso de CPU/memória.
- A **automatizar** tarefas com scripts e cron ou systemd.
- Conceitos fundamentais para **contêineres** (Docker, LXC) e **orquestração** (Kubernetes).

Tudo isso usando ferramentas nativas do Linux, sem necessidade de software adicional além dos utilitários básicos.
