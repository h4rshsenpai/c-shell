#ifndef HOP_H
#define HOP_H

void hop_init(void);
void hop_shutdown(void);
void run_hop(int argc, char **argv);
const char *hop_prev_dir(void);

#endif // HOP_H
