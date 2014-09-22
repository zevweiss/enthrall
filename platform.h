/*
 * Interfaces that must be implemented to support a platform.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include "types.h"

struct xypoint get_mousepos(void);
void set_mousepos(struct xypoint pos);
void move_mousepos(int32_t dx, int32_t dy);

void do_clickevent(mousebutton_t button, pressrel_t pressrel);

int grab_inputs(void);
void ungrab_inputs(void);

int remote_mode(void);

int platform_init(void);
void platform_exit(void);

#endif /* PLATFORM_H */
