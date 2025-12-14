#include <Server/Process.hh>

#include <Server/EntityFunctions.hh>

#include <Shared/Simulation.hh>
#include <Shared/Entity.hh>

void tick_drop_behavior(Simulation *sim, Entity &ent) {
    if (ent.immunity_ticks > 0) return;
    EntityID target_id = find_nearest_magnet(sim, ent, MAX_PICKUP_RANGE);
    if (target_id == NULL_ENTITY) return;
    Entity &parent = sim->get_ent(sim->get_ent(target_id).get_parent());
    pickup_drop(sim, parent, ent);
}
