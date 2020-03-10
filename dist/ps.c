#ifdef CS333_P2
#include "user.h"
#include "types.h"
#include "uproc.h"
#define MAX 6
#define MAXNAME 12
#endif
int main(void)
{
#if defined(CS333_P4)
    struct uproc *proc = malloc(sizeof(struct uproc) * MAX);
    int num_procs = getprocs(16, proc);
    printf(1, "PID\tName         UID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\n");
    for (int i = 0; i < num_procs; i++)
    {
        uint zero;
        char *eslaped = "", *cpu = "";
        zero = proc[i].elapsed_ticks % 1000;
        if (zero < 100 && zero >= 10)
            eslaped = "0";
        else if (zero < 10)
            eslaped = "00";
        zero = proc[i].CPU_total_ticks % 1000;
        if (zero < 100 && zero >= 10)
            cpu = "0";
        else if (zero < 10)
            cpu = "00";
        printf(1, "%d\t%s", proc[i].pid, proc[i].name);
        int len = strlen(proc[i].name);
        for (int j = len; j < MAXNAME; j++)
            printf(1, " ");
        printf(1, " %d\t        %d \t%d\t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\n", proc[i].uid, proc[i].gid, proc[i].ppid, proc[i].priority, proc[i].elapsed_ticks / 1000, eslaped, proc[i].elapsed_ticks % 1000, proc[i].CPU_total_ticks / 1000, cpu, proc[i].CPU_total_ticks % 1000, proc[i].state, proc[i].size);
    }
    free(proc);
#elif defined(CS333_P2)
    struct uproc *proc = malloc(sizeof(struct uproc) * MAX);
    int num_procs = getprocs(16, proc);
    printf(1, "PID\tName         UID\tGID\tPPID\tElapsed\tCPU\tState\tSize\n");
    for (int i = 0; i < num_procs; i++)
    {
        uint zero;
        char *eslaped = "", *cpu = "";
        zero = proc[i].elapsed_ticks % 1000;
        if (zero < 100 && zero >= 10)
            eslaped = "0";
        else if (zero < 10)
            eslaped = "00";
        zero = proc[i].CPU_total_ticks % 1000;
        if (zero < 100 && zero >= 10)
            cpu = "0";
        else if (zero < 10)
            cpu = "00";
        printf(1, "%d\t%s", proc[i].pid, proc[i].name);
        int len = strlen(proc[i].name);
        for (int j = len; j < MAXNAME; j++)
            printf(1, " ");
        printf(1, " %d\t        %d \t%d\t%d.%s%d\t%d.%s%d\t%s\t%d\n", proc[i].uid, proc[i].gid, proc[i].ppid, proc[i].elapsed_ticks / 1000, eslaped, proc[i].elapsed_ticks % 1000, proc[i].CPU_total_ticks / 1000, cpu, proc[i].CPU_total_ticks % 1000, proc[i].state, proc[i].size);
    }
    free(proc);
#endif
    exit();
}
