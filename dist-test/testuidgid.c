#ifdef CS333_P2
#include "types.h"
#include "user.h"
#include "uproc.h"
static int
uidtest(uint nval, int expected_val)
{
  uint uid = getuid();
  printf(1, "Current UID: %d\n", uid);
  printf(1, "Setting UID to %d\n", nval);
  int result = setuid(nval);
  printf(1, "Expected return value: %d\tvs\tActual return value: %d\n", expected_val, result);
  uid = getuid();
  printf(1, "Current UID: %d\n\n", uid);
  //sleep(5 * TPS); // now type control-p
  if (result == expected_val)
    return 0;
  else
    return -1;
}

static int
gidtest(uint nval, int expected_val)
{
  uint gid = getgid();
  printf(1, "Current GID: %d\n", gid);
  printf(1, "Setting GID to %d\n", nval);
  int result = setgid(nval);
  printf(1, "Expected return value: %d\tvs\tActual return value: %d\n", expected_val, result);
  gid = getgid();
  printf(1, "Current GID: %d\n\n", gid);
  //sleep(5 * TPS); // now type control-p
  if (result == expected_val)
    return 0;
  else
    return -1;
}
static void
uidgidtest(void)
{
  printf(1, "Starting UID and GID Test\n");
  int success = 0;
  if (uidtest(2, 0))
    success = -1;
  if (uidtest(32800, -1))
    success = -1;
  if (uidtest(-1, -1))
    success = -1;
  if (uidtest(5, 0))
    success = -1;
  if (gidtest(2, 0))
    success = -1;
  if (gidtest(32800, -1))
    success = -1;
  if (gidtest(-1, -1))
    success = -1;
  if (gidtest(5, 0))
    success = -1;
  if (success == 0)
    printf(1, "UIG GID tests passed!\n");
  else
    printf(2, "UID GID tests failed!\n");
}
static void
forkTest(uint nval)
{
  uint uid, gid;
  int pid;
  printf(1, "\nInherited test!\n ");
  printf(1, "Setting UID to %d and GID to %d before fork(). Value"
            " should be inherited\n",
         nval, nval);
  if (setuid(nval))
    printf(1, "Setting UID to: %d...\n", nval);
  if (setgid(nval))
    printf(1, "Setting GID to: %d...\n", nval);
  printf(1, "Before fork(), UID = %d, GID = %d\n", getuid(), getgid());
  pid = fork();
  if (pid == 0)
  { // child
    uid = getuid();
    gid = getgid();
    if (uid != nval)
    {
      printf(2, "FAILED: Parent UID is %d, child UID is %d\n", nval, uid);
    }
    else if (gid != nval)
    {
      printf(2, "FAILED: Parent GID is %d, child GID is %d\n", nval, gid);
    }
    else
    {
      printf(1, "After fork(), UID = %d, GID = %d\n", uid, gid);
      printf(1, "Inherited tests passed!\n");
    }
    sleep(5 * TPS);
    exit();
  }
  else
    sleep(10 * TPS);
  pid = wait();
}
static void
testppid(void)
{
  int ret, pid, ppid;

  printf(1, "\nRunning PPID Test\n");
  pid = getpid();
  ret = fork();
  if (ret == 0)
  {
    ppid = getppid();
    if (ppid != pid)
      printf(2, "FAILED: Parent PID is %d, Child's PPID is %d\n", pid, ppid);
    else
    {
      printf(1, "Parent PID is %d, Child's PPID is %d\n", pid, ppid);
      printf(1, "PPID tests passed!\n");
    }
    exit();
  }
  else
    wait();
}
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
  printf(1, "Test on %s process\n", name);
  printf(1, "Time 1: %d\n", time1);
  printf(1, "Test on %s process\n", name);
  printf(1, "Start sleeping for: %d s\n", 10);
  sleep(5 * TPS);
  time2 = getcputime(name, table);
  printf(1, "Time 2: %d\n", time2);
  sleep(5 * TPS);
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
    printf(1, "** CPU Tests Passed! **\n");
}
int main(int argc, char *argv[])
{
  uidgidtest();
  sleep(10000);
  forkTest(30);
  sleep(10000);
  testppid();
  sleep(10000);
  testcputime(argv[0]);
  printf(1, "Done!\n");
  exit();
}
#endif
