#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "process.h"
#include "queue.h"
#include "scheduler.h"

int num_algorithms() {
  return sizeof(algorithmsNames) / sizeof(char *);
}

int num_modalities() {
  return sizeof(modalitiesNames) / sizeof(char *);
}

size_t initFromCSVFile(char* filename, Process** procTable){
    FILE* f = fopen(filename,"r");
    
    size_t procTableSize = 10;
    
    *procTable = malloc(procTableSize * sizeof(Process));
    Process * _procTable = *procTable;

    if(f == NULL){
      perror("initFromCSVFile():::Error Opening File:::");   
      exit(1);             
    }

    char* line = NULL;
    size_t buffer_size = 0;
    size_t nprocs= 0;
    while( getline(&line,&buffer_size,f)!=-1){
        if(line != NULL){
            Process p = initProcessFromTokens(line,";");

            if (nprocs==procTableSize-1){
                procTableSize=procTableSize+procTableSize;
                _procTable=realloc(_procTable, procTableSize * sizeof(Process));
            }

            _procTable[nprocs]=p;

            nprocs++;
        }
    }
   free(line);
   fclose(f);
   return nprocs;
}

size_t getTotalCPU(Process *procTable, size_t nprocs){
    size_t total=0;
    for (int p=0; p<nprocs; p++ ){
        total += (size_t) procTable[p].burst;
    }
    return total;
}

int getCurrentBurst(Process* proc, int current_time){
    int burst = 0;
    for(int t=0; t<current_time; t++){
        if(proc->lifecycle[t] == Running){
            burst++;
        }
    }
    return burst;
}

int run_dispatcher(Process *procTable, size_t nprocs, int algorithm, int modality, int quantum){

    //Process * _proclist;

    qsort(procTable,nprocs,sizeof(Process),compareArrival);

    init_queue();
    size_t duration = getTotalCPU(procTable, nprocs) +1;

    for (int p=0; p<nprocs; p++ ){
        procTable[p].lifecycle = malloc( duration * sizeof(int));
        for(int t=0; t<duration; t++){
            procTable[p].lifecycle[t]=-1;
        }
        procTable[p].waiting_time = 0;
        procTable[p].return_time = 0;
        procTable[p].response_time = 0;
        procTable[p].completed = false;
    }

    Process* running_process = NULL;
    int current_time = 0;
    int completed_procs = 0;
    int next_proc_index = 0;
    int quantum_spent = 0;

    while(completed_procs < nprocs) {
        
        // Meter en la cola
        while (next_proc_index < nprocs && procTable[next_proc_index].arrive_time == current_time) {
            enqueue(&procTable[next_proc_index++]);
        }

        // Comprobar si hay procesos esperando con mayor prioridad
        if (running_process != NULL && modality == PREEMPTIVE && (algorithm == SJF || algorithm == PRIORITIES)) {
            int should_preempt = 0;
            for(int i = 0; i < nprocs; i++) {
                if (procTable[i].arrive_time <= current_time && !procTable[i].completed && procTable[i].id != running_process->id) {
                    
                    if (algorithm == PRIORITIES) {
                        if (comparePriority(&procTable[i], running_process) == -1) { 
                            should_preempt = 1; break; 
                        }
                    } else { // SJF
                         int current_worked = getCurrentBurst(running_process, current_time);
                         int remaining_current = running_process->burst - current_worked;
                         if (procTable[i].burst < remaining_current) { 
                             should_preempt = 1; break;
                         }
                    }
                }
            }
            if (should_preempt) { 
                enqueue(running_process);
                running_process = NULL; 
            }
        }
        
        // Pintar esperas
        for (int i = 0; i < nprocs; i++){
            if(procTable[i].arrive_time <= current_time && !procTable[i].completed) {
                if (running_process == NULL || procTable[i].id != running_process->id) {
                    procTable[i].lifecycle[current_time] = Bloqued;
                }
            }
        }

        // Planificador
        if (running_process == NULL && get_queue_size() > 0) {   

            if (algorithm == RR || algorithm == FCFS) {
                running_process = dequeue();
                if (algorithm == RR) { quantum_spent = 0; }
            }
            
            else { // SJF o PRIORITIES
                size_t count = get_queue_size();
                Process* temp[count];
                
                for(int i=0; i<count; i++){
                    temp[i] = dequeue();
                }

                int best_index = 0;
                for(int i=1; i < count; i++){
                
                    if (algorithm == SJF) {
                        if (compareBurst(temp[i], temp[best_index]) == -1) { best_index = i;} 
                    } else {
                        if (comparePriority(temp[i], temp[best_index]) == -1) {best_index = i;}
                    }
                }

                running_process = temp[best_index];

                for(int i=0; i < count; i++){
                    if (i != best_index) { enqueue(temp[i]); }
                }
            }
        }

        // Una vez la cpu tiene proceso, trabajar con el
        if (running_process != NULL) {
           
            running_process->lifecycle[current_time] = Running;
            int proc_worked_time = getCurrentBurst(running_process, current_time) + 1;
            if (algorithm == RR) { quantum_spent ++; }
            
            if (proc_worked_time == running_process->burst) {
                running_process->lifecycle[current_time + 1] = Finished;
                running_process->completed = true;
                running_process = NULL;
                completed_procs++;
            } 
            
            else if (algorithm == RR && quantum_spent == quantum) {
                enqueue(running_process);
                running_process = NULL;
            } 
        }
        
        current_time++;
    }

    printSimulation(nprocs,procTable,duration);

    for (int p=0; p<nprocs; p++ ){
        destroyProcess(procTable[p]);
    }

    cleanQueue();
    return EXIT_SUCCESS;
}


void printSimulation(size_t nprocs, Process *procTable, size_t duration){

    printf("%14s","== SIMULATION ");
    for (int t=0; t<duration; t++ ){
        printf("%5s","=====");
    }
    printf("\n");

    printf ("|%4s", "name");
    for(int t=0; t<duration; t++){
        printf ("|%2d", t);
    }
    printf ("|\n");

    for (int p=0; p<nprocs; p++ ){
        Process current = procTable[p];
            printf ("|%4s", current.name);
            for(int t=0; t<duration; t++){
                printf("|%2s",  (current.lifecycle[t]==Running ? "E" : 
                        current.lifecycle[t]==Bloqued ? "B" :   
                        current.lifecycle[t]==Finished ? "F" : " "));
            }
            printf ("|\n");
        
    }


}

void printMetrics(size_t simulationCPUTime, size_t nprocs, Process *procTable ){

    printf("%-14s","== METRICS ");
    for (int t=0; t<simulationCPUTime+1; t++ ){
        printf("%5s","=====");
    }
    printf("\n");

    printf("= Duration: %ld\n", simulationCPUTime );
    printf("= Processes: %ld\n", nprocs );

    size_t baselineCPUTime = getTotalCPU(procTable, nprocs);
    double throughput = (double) nprocs / (double) simulationCPUTime;
    double cpu_usage = (double) simulationCPUTime / (double) baselineCPUTime;

    printf("= CPU (Usage): %lf\n", cpu_usage*100 );
    printf("= Throughput: %lf\n", throughput*100 );

    double averageWaitingTime = 0;
    double averageResponseTime = 0;
    double averageReturnTime = 0;
    double averageReturnTimeN = 0;

    for (int p=0; p<nprocs; p++ ){
            averageWaitingTime += procTable[p].waiting_time;
            averageResponseTime += procTable[p].response_time;
            averageReturnTime += procTable[p].return_time;
            averageReturnTimeN += procTable[p].return_time / (double) procTable[p].burst;
    }


    printf("= averageWaitingTime: %lf\n", (averageWaitingTime/(double) nprocs) );
    printf("= averageResponseTime: %lf\n", (averageResponseTime/(double) nprocs) );
    printf("= averageReturnTimeN: %lf\n", (averageReturnTimeN/(double) nprocs) );
    printf("= averageReturnTime: %lf\n", (averageReturnTime/(double) nprocs) );

}