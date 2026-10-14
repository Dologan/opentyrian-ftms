/*
 * bike_rules.h - mapping C (bike effort -> generator income) and the link-loss
 * blend back to the stock refill.  See doc/ftms/design.md section 2.
 */
#ifndef BIKE_RULES_H
#define BIKE_RULES_H

#include "bike_effort.h"

#include <stdbool.h>
#include <stdint.h>

#define BIKE_BLEND_DELAY_US 1500000u /* link down this long before the blend starts */

void bike_rules_level_start(void); /* remainder accumulator = 0 */
/* Once per frame tick, after the effort step.  `active` is whether the generator
 * is driving the refill (a closed->open edge restarts the blend at stock). */
void bike_rules_frame(const BikeEffortSignals *sig, bool link_up, bool active, uint32_t dt_us);
/* Replaces powerAdd at the refill: `stock_add` is the equipped generator's value. */
unsigned bike_rules_power_add(unsigned stock_add);

/* clamp(G*(bias + gain*n), F, G*cap), Q16 power units per frame.  Pure. */
int32_t bike_income_q16(unsigned G, bq16 effort_norm, uint8_t floor, uint16_t bias_pct,
                        uint16_t gain_pct, uint16_t cap_pct);

bq16 bike_rules_blend(void);       /* w (Q16): 1 = stock, 0 = bike */
int32_t bike_rules_last_income(void);
unsigned bike_rules_last_add(void);
uint64_t bike_rules_digest(void);

#endif
