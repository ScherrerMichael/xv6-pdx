#ifdef CS333_P3

#include "user.h"

int main()
{
    int pid = fork();
    if (pid < 0)
    {
        printf(1, "Fork Error\n");
        exit();
    }
    if (pid == 0)
    {
        printf(1, "child exits\n");
        exit();
    }
    printf(1, "Parent Sleeping for 10 seconds\n");
    sleep(10000);
    printf(1, "Parent calling wait\n");
    wait();
    printf(1, "Parent sleeping for 10 seconds\n");
    sleep(10000);

    printf(1, "Parent exiting\n");
    exit();
}

#endif