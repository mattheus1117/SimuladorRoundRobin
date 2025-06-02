# Simulador Round Robin com Multithreading
Simulador de escalonamento Round Robin com 2 núcleos, suporte a processos bloqueados e execução paralela usando threads em C++.

## Como compilar e executar
g++ -Wall -Wextra -g3 main.cpp -o simulador -pthread

./simulador


## Descrição
Simula execução de processos com chegada, execução, bloqueio e espera.

Usa threads para relógio, chegada, núcleos de CPU e gerenciamento de bloqueados.

Exibe métricas finais: tempo de espera, turnaround, uso de CPU e trocas de contexto.
