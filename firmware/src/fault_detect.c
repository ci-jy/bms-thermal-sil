#include "fault_detect.h"
#include "bms_config.h"

void debounce_init(debounce_t *d, uint16_t inc, uint16_t dec, uint16_t limit)
{
    d->count = 0u;
    d->inc = inc;
    d->dec = dec;
    d->limit = limit;
    d->active = false;
}

bool debounce_step(debounce_t *d, bool failing)
{
    if (failing) {
        const uint32_t next = (uint32_t)d->count + d->inc;
        d->count = (uint16_t)((next > d->limit) ? d->limit : next);
    } else {
        d->count = (d->count > d->dec) ? (uint16_t)(d->count - d->dec) : 0u;
    }
    if (d->count >= d->limit) {
        d->active = true;
    } else if (d->count == 0u) {
        d->active = false;
    } else {
        /* hold previous qualification between the thresholds */
    }
    return d->active;
}

void sensor_monitor_init(sensor_monitor_t *m)
{
    debounce_init(&m->range, 1u, 1u, BMS_SENS_RANGE_DEBOUNCE);
    debounce_init(&m->roc, 1u, 1u, BMS_SENS_ROC_DEBOUNCE);
    m->raw_prev = 0u;
    m->same_count = 0u;
    m->stuck_samples = BMS_SENS_STUCK_SAMPLES;
    m->last_c = 0.0f;
    m->have_prev = false;
    m->stuck = false;
}

sensor_status_t sensor_monitor_step(sensor_monitor_t *m, uint16_t raw, float value_c)
{
    sensor_status_t s;
    const bool out_of_range = (value_c < BMS_SENS_MIN_C) || (value_c > BMS_SENS_MAX_C);
    s.range = debounce_step(&m->range, out_of_range);

    bool roc_fail = false;
    if (m->have_prev) {
        const float delta = value_c - m->last_c;
        roc_fail = (delta > BMS_SENS_ROC_MAX_C) || (delta < -BMS_SENS_ROC_MAX_C);
        if (raw == m->raw_prev) {
            if (m->same_count < 0xFFFFu) {
                m->same_count++;
            }
        } else {
            m->same_count = 0u;
            m->stuck = false;
        }
    }
    /* same_count counts repeats, so N identical samples give N-1 repeats. */
    if ((m->same_count + 1u) >= m->stuck_samples) {
        m->stuck = true;
    }
    s.roc = debounce_step(&m->roc, roc_fail);
    s.stuck = m->stuck;

    m->raw_prev = raw;
    m->last_c = value_c;
    m->have_prev = true;
    return s;
}

bool sensor_status_faulty(sensor_status_t s)
{
    return s.range || s.stuck || s.roc;
}

void plausibility_init(plausibility_t *p, float max_diff, uint16_t debounce)
{
    debounce_init(&p->deb, 1u, 1u, debounce);
    p->max_diff = max_diff;
}

bool plausibility_step(plausibility_t *p, float a, float b)
{
    const float diff = a - b;
    return debounce_step(&p->deb, (diff > p->max_diff) || (diff < -p->max_diff));
}
