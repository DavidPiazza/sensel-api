#include "sensel_frame_counter.h"

#include <stdio.h>

static int failures;

#define CHECK(expression)                                                        \
  do {                                                                           \
    if (!(expression))                                                           \
    {                                                                            \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n",                             \
              __FILE__, __LINE__, #expression);                                  \
      ++failures;                                                                \
    }                                                                            \
  } while (0)

int main(void)
{
  unsigned char previous;
  unsigned char initialized;

  senselFrameCounterReset(&previous, &initialized);
  CHECK(senselFrameCounterAdvance(&previous, &initialized, 44) == 0);
  CHECK(senselFrameCounterAdvance(&previous, &initialized, 45) == 0);
  CHECK(senselFrameCounterAdvance(&previous, &initialized, 47) == 1);

  senselFrameCounterReset(&previous, &initialized);
  CHECK(senselFrameCounterAdvance(&previous, &initialized, 255) == 0);
  CHECK(senselFrameCounterAdvance(&previous, &initialized, 0) == 0);
  CHECK(senselFrameCounterAdvance(&previous, &initialized, 2) == 1);

  senselFrameCounterReset(&previous, &initialized);
  CHECK(senselFrameCounterAdvance(&previous, &initialized, 200) == 0);

  if (failures)
  {
    fprintf(stderr, "%d frame counter test(s) failed\n", failures);
    return 1;
  }
  puts("Sensel frame counter tests passed");
  return 0;
}
