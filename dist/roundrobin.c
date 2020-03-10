#ifdef CS333_P3
#include "types.h"
#include "user.h"
#define NUM_PROCS 5
int main()
{
    int pid;
    int i;
    printf(1, "Create 5 processes and let it run forever");
    for (i = 0; i < NUM_PROCS; i++)
    {
        pid = fork();
        if (pid < 0)
        {
            printf(2, "Fork Error! Abort Abort\n");
        }
        if (pid == 0)
        {
            while (1) // the process will run forever
                ;
        }
    }
    wait();
    exit();
}

#endif
