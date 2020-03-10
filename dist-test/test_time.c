#ifdef CS333_P2
#include "user.h"
#include "types.h"
int main(int argc, char *argv[])
{
    if (argc == 1)
    {
        printf(1, "%s command ran in 0.00\n", argv[0]);
    }
    else
    {
        int time_1 = uptime();
        int pid = fork();
        if (pid > 0)
        {
            pid = wait();
        }
        else if ((pid == 0))
        {
            exec(argv[1], argv + 1);
            printf(1, "Command doesn't exist.\n");
            kill(getppid());
            exit();
        }
        int time_2 = uptime();
        int time = time_2 - time_1;
        int m = time % 1000;
        char *zero = "";
        if (m < 100 && m >= 10)
        {
            zero = "0";
        }
        if (m < 10)
        {
            zero = "00";
        }
        printf(1, "%s run in %d.%s%d\n", argv[1], time / 1000, zero,
               m);
    }
    exit();
}
#endif CS333_P2
