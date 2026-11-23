#ifndef AFFINITY_H
#define AFFINITY_H

void affinity_set_mode(int mode);
void affinity_bind_current(void);
#ifdef PTEXT_HOST_TEST
int affinity_test_mode(void);
int affinity_test_ready(void);
#endif

#endif
