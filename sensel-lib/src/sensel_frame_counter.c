#include "sensel_frame_counter.h"

void senselFrameCounterReset(unsigned char *previous, unsigned char *initialized)
{
  *previous = 0;
  *initialized = 0;
}

unsigned int senselFrameCounterAdvance(unsigned char *previous,
                                       unsigned char *initialized,
                                       unsigned char current)
{
  int elapsed_frames;

  if (!*initialized)
  {
    *previous = current;
    *initialized = 1;
    return 0;
  }

  elapsed_frames = (int)current - (int)*previous;
  if (elapsed_frames <= 0)
    elapsed_frames += 256;

  *previous = current;
  return (unsigned int)(elapsed_frames - 1);
}
