#include "derate.h"

/* Allowed fraction of BMS_I_CHARGE_MAX_A vs cell temperature. */
const derate_point_t DERATE_DEFAULT_MAP[] = {
    {0.0f, 0.15f}, {10.0f, 0.40f}, {20.0f, 1.00f}, {40.0f, 1.00f},
    {45.0f, 0.60f}, {50.0f, 0.30f}, {55.0f, 0.00f},
};
const size_t DERATE_DEFAULT_MAP_LEN = sizeof(DERATE_DEFAULT_MAP) / sizeof(DERATE_DEFAULT_MAP[0]);

void derate_init(derate_t *d, const derate_point_t *map, size_t n, float hyst_c)
{
    d->map = map;
    d->n = n;
    d->hyst_c = hyst_c;
    d->t_used = 0.0f;
    d->primed = 0;
}

float derate_lookup(const derate_point_t *map, size_t n, float t_c)
{
    if (n == 0u) {
        return 0.0f;
    }
    if (t_c <= map[0].t_c) {
        return map[0].factor;
    }
    for (size_t i = 1u; i < n; i++) {
        if (t_c <= map[i].t_c) {
            const float span = map[i].t_c - map[i - 1u].t_c;
            const float w = (t_c - map[i - 1u].t_c) / span;
            return map[i - 1u].factor + w * (map[i].factor - map[i - 1u].factor);
        }
    }
    return map[n - 1u].factor;
}

float derate_step(derate_t *d, float t_c)
{
    /* Play operator: t_used follows t_c only once it moves more than the
     * hysteresis band away; rising temperatures are tracked immediately on the
     * hot side (the map is evaluated at the band's upper edge). */
    if (!d->primed) {
        d->t_used = t_c;
        d->primed = 1;
    } else if (t_c > d->t_used) {
        d->t_used = t_c;
    } else if (t_c < d->t_used - d->hyst_c) {
        d->t_used = t_c + d->hyst_c;
    } else {
        /* inside the band: hold */
    }
    return derate_lookup(d->map, d->n, d->t_used);
}
