#ifndef CHILD_H
#define CHILD_H

struct dynscope;

int child_spawn(struct dynscope *ds, char **argv);
void child_terminate(struct dynscope *ds);

#endif
