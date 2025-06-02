#include <iostream>             // Entrada e saída padrão
#include <fstream>              // Leitura de arquivos
#include <vector>               // Vetores dinâmicos
#include <string>               // Strings
#include <queue>                // Filas para gerenciar processos
#include <thread>               // Threads para execução paralela
#include <mutex>                // Mutex para exclusão mútua
#include <condition_variable>   // Condicional para sincronização entre threads
#include <chrono>               // Controle de tempo

using namespace std;


// Estrutura que representa um processo com seus atributos
struct Processo {
    string id;
    int chegada;
    int exec1;
    bool bloqueia;
    int espera;
    int exec2;

    int exec1_restante;
    int exec2_restante;
    int espera_restante;
    enum Estado {PRONTO, EXECUTANDO, BLOQUEADO, TERMINADO} estado;

    int tempo_espera;
    int tempo_cpu;
    int trocas_contexto;
    int tempo_finalizacao;

    Processo()
        : exec1_restante(0), exec2_restante(0), espera_restante(0), estado(PRONTO),
          tempo_espera(0), tempo_cpu(0), trocas_contexto(0), tempo_finalizacao(0) {}
};


// Filas de processos prontos e bloqueados
queue<Processo*> fila_prontos;
queue<Processo*> fila_bloqueados;

// Variáveis para sincronização
mutex mtx_fila;
condition_variable cv_fila;
condition_variable cv_tempo;

int tempo_global = 0;          // Relógio global da simulação
bool fim_simulacao = false;    // Flag para finalizar a simulação


// Lê processos do arquivo de entrada
void ler_processos(const string& nome_arquivo, vector<Processo>& processos) {
    ifstream arquivo(nome_arquivo);
    if (!arquivo.is_open()) {
        cout << "\nErro ao abrir " << nome_arquivo << "\n";
        return;
    }

    Processo p;
    while (arquivo >> p.id >> p.chegada >> p.exec1 >> p.bloqueia >> p.espera >> p.exec2) {
        p.exec1_restante = p.exec1;
        p.exec2_restante = p.exec2;
        p.espera_restante = p.espera;
        p.estado = Processo::PRONTO;
        processos.push_back(p);
    }
}


// Thread que controla o relógio da simulação, incrementando o tempo global
void thread_relogio(int tempo_max, vector<Processo>& processos) {
    while (true) {
        this_thread::sleep_for(chrono::milliseconds(1000)); // Espera 1 segundo (1 unidade de tempo)

        {
            lock_guard<mutex> lock(mtx_fila);
            tempo_global++;
            cout << "\nTempo global agora: " << tempo_global << "\n";

            // Atualiza tempo de espera de todos processos PRONTOS
            for (auto& p : processos) {
                if (p.estado == Processo::PRONTO) {
                    p.tempo_espera++;
                }
            }

            if (tempo_global >= tempo_max) {
                fim_simulacao = true;
            }
        }

        cv_tempo.notify_all();
        cv_fila.notify_all();

        if (fim_simulacao) break;
    }
}


// Thread que insere processos na fila de prontos conforme chegam
void thread_chegada(vector<Processo>& processos) {
    size_t i = 0;
    while (true) {
        unique_lock<mutex> lock(mtx_fila);
        cv_fila.wait(lock, [&]() { return (i < processos.size() && tempo_global >= processos[i].chegada) || fim_simulacao; });

        if (fim_simulacao) break;

        while (i < processos.size() && tempo_global >= processos[i].chegada) {
            fila_prontos.push(&processos[i]);
            cout << "\n>>> Processo " << processos[i].id << " chegou no tempo " << tempo_global << "\n";
            i++;
        }
        lock.unlock();
        cv_fila.notify_all();
    }
}

const int QUANTUM = 4;  // Tempo máximo de execução contínua (quantum)


// Thread que simula a execução dos processos no núcleo do processador
void thread_nucleo(int id) {
    while (true) {
        Processo* proc;
        {
            unique_lock<mutex> lock(mtx_fila);
            cv_fila.wait(lock, [&] { return !fila_prontos.empty() || fim_simulacao; });

            if (fim_simulacao && fila_prontos.empty()) break;

            proc = fila_prontos.front();
            fila_prontos.pop();
            proc->estado = Processo::EXECUTANDO;
            proc->trocas_contexto++;  // Conta troca de contexto
        }

        cout << "\nNucleo " << id << " executando processo " << proc->id << " no tempo " << tempo_global << "\n";

        int tempo_executado = 0;
        bool finalizou = false;

        while (tempo_executado < QUANTUM && !fim_simulacao && !finalizou) {
            unique_lock<mutex> lock(mtx_fila);
            cv_tempo.wait_for(lock, chrono::milliseconds(100)); // Espera relógio avançar 1 unidade (100ms)

            if (fim_simulacao) break;

            if (proc->exec1_restante > 0) {
                proc->exec1_restante--;
                tempo_executado++;
                proc->tempo_cpu++;
                cout << "\nProcesso " << proc->id << " executando exec1. Tempo restante: " << proc->exec1_restante << "\n";

                if (proc->exec1_restante == 0 && proc->bloqueia) {
                    // Processo bloqueia após exec1
                    proc->estado = Processo::BLOQUEADO;
                    proc->espera_restante = proc->espera;
                    fila_bloqueados.push(proc);
                    cout << "\nProcesso " << proc->id << " bloqueado.\n";
                    finalizou = true;
                }
            }
            else if (proc->exec2_restante > 0) {
                proc->exec2_restante--;
                tempo_executado++;
                proc->tempo_cpu++;
                cout << "\nProcesso " << proc->id << " executando exec2. Tempo restante: " << proc->exec2_restante << "\n";

                if (proc->exec2_restante == 0) {
                    proc->estado = Processo::TERMINADO;
                    proc->tempo_finalizacao = tempo_global + 1;
                    cout << "\nProcesso " << proc->id << " terminado.\n";
                    finalizou = true;
                }
            }
            else {
                // Processo já terminou as execuções
                proc->estado = Processo::TERMINADO;
                proc->tempo_finalizacao = tempo_global + 1;
                cout << "\nProcesso " << proc->id << " terminado.\n";
                finalizou = true;
            }
        }

        if (!finalizou) {
            // Quantum acabou mas processo não terminou, volta para fila pronta
            if (proc->estado == Processo::EXECUTANDO) {
                proc->estado = Processo::PRONTO;
                lock_guard<mutex> lock(mtx_fila);
                fila_prontos.push(proc);
            }
        }

        cv_fila.notify_all();
    }
}


// Thread que gerencia a fila de processos bloqueados, decrementando tempo de espera
void thread_bloqueados() {
    while (true) {
        unique_lock<mutex> lock(mtx_fila);

        if (fila_bloqueados.empty()) {
            cv_fila.wait(lock, [] { return fim_simulacao || !fila_bloqueados.empty(); });
            if (fim_simulacao) break;
        } 

        if (!fila_bloqueados.empty()) {
            Processo* proc = fila_bloqueados.front();
            fila_bloqueados.pop();

            if (proc->espera_restante > 0) {
                proc->espera_restante--;
                cout << "\nProcesso " << proc->id << " esperando bloqueado. Tempo restante: " << proc->espera_restante << "\n";
            }

            if (proc->espera_restante <= 0) {
                // Processo termina espera e volta para fila pronta
                proc->estado = Processo::PRONTO;
                fila_prontos.push(proc);
                cout << "\nProcesso " << proc->id << " desbloqueado e voltou para fila pronta.\n";
            } else {
                // Continua bloqueado
                fila_bloqueados.push(proc);
            }
        }

        lock.unlock();
        cv_fila.notify_all();

        this_thread::sleep_for(chrono::milliseconds(1000)); // Aguarda 1 unidade de tempo antes de processar novamente
    }
}



int main() {
    vector<Processo> processos;
    ler_processos("entrada.txt", processos);

    int tempo_max = 12;  // Tempo máximo para simulação

    // Cria threads para relógio, chegada, núcleos e bloqueados
    thread t_relogio(thread_relogio, tempo_max, ref(processos));
    thread t_chegada(thread_chegada, ref(processos));
    thread t_nucleo1(thread_nucleo, 1);
    thread t_nucleo2(thread_nucleo, 2);
    thread t_bloqueados(thread_bloqueados);

    // Aguarda todas as threads terminarem
    t_relogio.join();
    t_chegada.join();
    t_nucleo1.join();
    t_nucleo2.join();
    t_bloqueados.join();

    // Exibe resultados finais
    cout << "\n=== Resultados Finais ===\n";
    for (const auto& p : processos) {
        int turnaround = p.tempo_finalizacao - p.chegada;
        cout << "Processo " << p.id << ":\n";
        cout << "  Tempo de espera: " << p.tempo_espera << "\n";
        cout << "  Turnaround: " << turnaround << "\n";
        cout << "  Uso da CPU: " << p.tempo_cpu << "\n";
        cout << "  Trocas de contexto: " << p.trocas_contexto << "\n\n";
    }

    cout << "\nSimulacao finalizada\n";

    return 0;
}
