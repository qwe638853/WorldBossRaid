#ifndef DICE_H
#define DICE_H

// Initialize the random number generator for dice rolls
void dice_init();

// Calculate attack damage with randomness
// Returns a damage value between min_damage and max_damage
int dice_roll_damage(int min_damage, int max_damage);

// Calculate attack damage with critical hit chance
// base_damage: base damage value
// crit_chance: critical hit chance (0.0 to 1.0, e.g., 0.1 = 10%)
// crit_multiplier: damage multiplier on critical hit (e.g., 2.0 = double damage)
// Returns the final damage (may be critical)
int dice_roll_damage_with_crit(int base_damage, float crit_chance, float crit_multiplier);

// Roll a random number between min and max (inclusive)
int dice_roll_range(int min, int max);

// Roll a random float between 0.0 and 1.0
float dice_roll_float();

#endif

