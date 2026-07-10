#ifndef SENSEL_FRAME_COUNTER_H
#define SENSEL_FRAME_COUNTER_H

void senselFrameCounterReset(unsigned char *previous, unsigned char *initialized);
unsigned int senselFrameCounterAdvance(unsigned char *previous,
                                       unsigned char *initialized,
                                       unsigned char current);

#endif
