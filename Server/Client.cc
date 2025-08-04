#include <Server/Client.hh>

#include <Server/PetalTracker.hh>
#include <Server/Server.hh>
#include <Server/Spawn.hh>

#include <Shared/Binary.hh>
#include <Shared/Config.hh>
#include <Shared/Simulation.hh>

#include <iostream>

Client::Client() : simulation(nullptr) {}

void Client::init() {
    DEBUG_ONLY(assert(simulation == nullptr);)
    simulation = &Server::simulation;
    Entity &ent = simulation->alloc_ent();
    ent.add_component(kCamera);
    ent.add_component(kRelations);
    ent.set_team(ent.id);
    ent.set_fov(BASE_FOV);
    ent.set_respawn_level(1); 
    for (uint32_t i = 0; i < loadout_slots_at_level(ent.respawn_level); ++i)
        ent.set_inventory(i, PetalID::kBasic);
    if (frand() < 0.001 && PetalTracker::get_count(PetalID::kUniqueBasic) == 0)
        ent.set_inventory(0, PetalID::kUniqueBasic);

    for (uint32_t i = 0; i < loadout_slots_at_level(ent.respawn_level); ++i)
        PetalTracker::add_petal(ent.inventory[i]);
    camera = ent.id;
    seen_arena = 0;
}

void Client::remove() {
    if (simulation == nullptr) return;
    if (simulation->ent_exists(camera)) {
        Entity &c = simulation->get_ent(camera);
        if (simulation->ent_exists(c.player))
            simulation->request_delete(c.player);
        for (uint32_t i = 0; i < 2 * MAX_SLOT_COUNT; ++i)
            PetalTracker::remove_petal(c.inventory[i]);
        simulation->request_delete(camera);
    }
}

void Client::disconnect() {
    if (ws == nullptr) return;
    remove();
    ws->end();
}

uint8_t Client::alive() {
    if (simulation == nullptr) return false;
    return simulation->ent_exists(camera) 
    && simulation->ent_exists(simulation->get_ent(camera).player);
}

#define VALIDATE(expr) if (!expr) { client->disconnect(); }

void Client::on_message(WebSocket *ws, std::string_view message, uint64_t code) {
    if (ws == nullptr) return;
    uint8_t const *data = reinterpret_cast<uint8_t const *>(message.data());
    Reader reader(data);
    Validator validator(data, data + message.size());
    Client *client = ws->getUserData();
    if (client == nullptr) {
        ws->end();
        return;
    }
    if (!client->verified) {
        VALIDATE(validator.validate_uint8());
        Server::clients.insert(client);
        if (reader.read<uint8_t>() != Serverbound::kVerify) {
            //disconnect
            client->disconnect();
            return;
        }
        VALIDATE(validator.validate_uint64());
        if (reader.read<uint64_t>() != VERSION_HASH) {
            Writer writer(Server::OUTGOING_PACKET);
            writer.write<uint8_t>(Clientbound::kOutdated);
            client->send_packet(writer.packet, writer.at - writer.packet);
            client->disconnect();
            return;
        }
        client->verified = 1;
        client->init();
        return;
    }
    if (client->simulation == nullptr) {
        client->disconnect();
        return;
    }
    VALIDATE(validator.validate_uint8());
    switch (reader.read<uint8_t>()) {
        case Serverbound::kVerify:
            client->disconnect();
            return;
        case Serverbound::kClientInput: {
            if (!client->alive()) break;
            Entity &camera = client->simulation->get_ent(client->camera);
            Entity &player = client->simulation->get_ent(camera.player);
            VALIDATE(validator.validate_float());
            VALIDATE(validator.validate_float());
            float x = reader.read<float>();
            float y = reader.read<float>();
            if (x == 0 && y == 0) camera.acceleration.set(0,0);
            else {
                if (std::abs(x) > 5e3 || std::abs(y) > 5e3) break;
                Vector accel(x,y);
                float m = accel.magnitude();
                if (m > 200) accel.set_magnitude(PLAYER_ACCELERATION);
                else accel.set_magnitude(m / 200 * PLAYER_ACCELERATION);
                camera.acceleration = accel;
            }
            VALIDATE(validator.validate_uint8());
            camera.input = reader.read<uint8_t>();
            //store player's acceleration and input in camera (do not reset ever)
            break;
        }
        case Serverbound::kClientSpawn: {
            if (client->alive()) break;
            Entity &camera = client->simulation->get_ent(client->camera);
            Entity &player = alloc_player(client->simulation, camera.id);
            player_spawn(client->simulation, camera, player);
            std::string name;
            //check string length;
            VALIDATE(validator.validate_string(MAX_NAME_LENGTH));
            reader.read<std::string>(name);
            name = UTF8Parser::trunc_string(name, MAX_NAME_LENGTH);
            player.set_name(name);
            break;
        }
        case Serverbound::kPetalDelete: {
            if (!client->alive()) break;
            Entity &camera = client->simulation->get_ent(client->camera);
            Entity &player = client->simulation->get_ent(camera.player);
            VALIDATE(validator.validate_uint8());
            uint8_t pos = reader.read<uint8_t>();
            if (pos >= MAX_SLOT_COUNT + player.loadout_count) break;
            PetalID::T old_id = player.loadout_ids[pos];
            if (old_id != PetalID::kNone && old_id != PetalID::kBasic) {
                uint8_t rarity = PETAL_DATA[old_id].rarity;
                uint32_t const rarity_to_xp[RarityID::kNumRarities] = { 2, 10, 50, 200, 1000, 5000, 0 };
                player.set_score(player.score + rarity_to_xp[rarity]);
                //need to delete if over cap
                if (player.deleted_petals.size() == MAX_SLOT_COUNT)
                    //removes old trashed old petal
                    PetalTracker::remove_petal(player.deleted_petals.curr());
                player.deleted_petals.push(old_id);
            }
            player.set_loadout_ids(pos, PetalID::kNone);
            break;
        }
        case Serverbound::kPetalSwap: {
            if (!client->alive()) break;
            Entity &camera = client->simulation->get_ent(client->camera);
            Entity &player = client->simulation->get_ent(camera.player);
            VALIDATE(validator.validate_uint8());
            uint8_t pos1 = reader.read<uint8_t>();
            if (pos1 >= MAX_SLOT_COUNT + player.loadout_count) break;
            VALIDATE(validator.validate_uint8());
            uint8_t pos2 = reader.read<uint8_t>();
            if (pos2 >= MAX_SLOT_COUNT + player.loadout_count) break;
            PetalID::T tmp = player.loadout_ids[pos1];
            player.set_loadout_ids(pos1, player.loadout_ids[pos2]);
            player.set_loadout_ids(pos2, tmp);
            break;
        }
    }
}

void Client::on_disconnect(WebSocket *ws, int code, std::string_view message) {
    std::cout << "client disconnection\n";
    Client *client = ws->getUserData();
    client->remove();
    Server::clients.erase(client);
    //delete player in systems
}