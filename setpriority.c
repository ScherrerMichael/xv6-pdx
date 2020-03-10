#ifdef CS333_P4
#include "types.h"
#include "user.h"

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 3)
    {
        printf(1, "Invalid arguments\n setpriority [pid] [priority]\n");
    }
    else
    {
        int pid = atoi(argv[1]);
        int priority = atoi(argv[2]);
        int rc = setpriority(pid, priority);
        if (rc)
        {
            printf(1, "setpriority have failed failed\n");
        }
    }
    exit();
}

#endif