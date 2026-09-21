#ifndef DERATE_H
#define DERATE_H

#include <stddef.h>

/* Charge-current derating: a piecewise-linear map from cell temperature to an
 * allowed fraction of the maximum current, evaluated on a hysteresis-filtered
 * temperature (play operator) so the request does not chatter near a breakpoint. */
typedef struct {
    float t_c;
    float factor;
} derate_point_t;

typedef struct {
    const derate_point_t *map;
    size_t n;
    float hyst_c;
    float t_used;
    int primed;
} derate_t;

void derate_init(derate_t *d, const derate_point_t *map, size_t n, float hyst_c);
/* Linear interpolation on the map, clamped at both ends. */
float derate_lookup(const derate_point_t *map, size_t n, float t_c);
/* Returns the derate factor in [0, 1] after applying hysteresis. */
float derate_step(derate_t *d, float t_c);

extern const derate_point_t DERATE_DEFAULT_MAP[];
extern const size_t DERATE_DEFAULT_MAP_LEN;

#endif
