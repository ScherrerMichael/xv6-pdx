#ifdef CS333_P2
#include "types.h"
#include "user.h"
#include "uproc.h"
static int
getcputime(char *name, struct uproc *table)
{
    struct uproc *p = 0;
    int size;

    size = getprocs(64, table);
    for (int i = 0; i < size; ++i)
    {
        if (strcmp(table[i].name, name) == 0)
        {
            p = table + i;
            break;
        }
    }
    if (p == 0)
    {
        printf(2, "Failed: Could not found \"%s\"\n", name);
        return -1;
    }
    else
        return p->CPU_total_ticks;
}

static void
testcputime(char *name)
{
    struct uproc *table;
    uint time1, time2;
    int success = 0;
    printf(1, "\nRunning CPU Time Test\n");
    table = malloc(sizeof(struct uproc) * 64);
    time1 = getcputime(name, table);
    printf(1, "Time 1: %d\n", time1);
    printf(1, "Test on %s process\n", name);
    printf(1, "Start sleeping for: %d s\n", 10);
    sleep(10 * TPS);
    time2 = getcputime(name, table);
    printf(1, "Time 2: %d\n", time2);
    if ((time2 - time1) < 0)
    {
        printf(2, "FAILED: CPU travel back in time.  T2 - T1 = %d\n", (time2 - time1));
        success = -1;
    }
    if ((time2 - time1) > 400)
    {
        printf(2, "Warning: T2 - T1 = %d milliseconds. There is dust in the CPU \n", (time2 - time1));
        success = -1;
    }
    printf(1, "T2 - T1 = %d milliseconds\n", (time2 - time1));
    free(table);

    if (success == 0)
        printf(1, "CPU tests Passed!\n");
}
int main(int argc, char *argv[])
{
    testcputime(argv[0]);
    printf(1, "Done!\n");
    exit();
}
#endif