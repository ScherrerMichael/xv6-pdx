#ifdef CS333_P3

#include "user.h"

void loop(int duration)
{

    int elapsed_time = 0;
    int tic = uptime();
    int toc = 0;
    while (elapsed_time < duration)
    {
        toc = uptime();
        elapsed_time = toc - tic;
    }
}

int main(int argc, char *argv[])
{
    int procs;
    if (argc == 1)
    {
        procs = 1;
    }
    else
    {
        procs = atoi(argv[1]);
    }
    int pid = 0;
    int i;
    for (i = 0; i < procs; i++)
    {
        pid = fork();
        if (pid < 0)
        {
            printf(2, "Fork Error! Abort Abort\n");
        }
        if (pid == 0)
        {
            break;
        }
    }
    if (pid == 0)
    {
        printf(1, "Running for 5 seconds.\n");
        loop(5000);
        printf(1, "Sleeping for 5 seconds.\n");
        sleep(5000);
        printf(1, "Waking up, and loop running for 5 more seconds.\n");
        loop(5000);
        printf(1, "Sleeping 5 more seconds.\n");
        sleep(5000);
        printf(1, "All done.\n");
        exit();
    }
    wait();
    exit();
}

#endif
