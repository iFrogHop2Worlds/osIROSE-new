#include "items/inventory.h"
#include "entity_system.h"
#include "dataconsts.h"
#include "random.h"

#include "components/basic_info.h"
#include "components/inventory.h"
#include "components/item.h"
#include "components/lua.h"
#include "components/position.h"
#include "components/owner.h"
#include "itemdb.h"

#include "srv_equip_item.h"
#include "srv_set_item.h"
#include "srv_set_money.h"

#include <limits>

using namespace RoseCommon;
using namespace Items;

namespace {
// only for items, not cart/castle gear
inline bool is_spot_correct(const EntitySystem& entitySystem, RoseCommon::Entity entity, size_t spot) {
    const auto& item = entitySystem.get_component<ItemDef>(entity);
    const EquippedPosition pos = static_cast<EquippedPosition>(spot);
    if (pos >= EquippedPosition::MAX_EQUIP_ITEMS) {
        return true; // we don't care of the spot if we are not equipping anything
    }
    switch (item.type) {
        case ItemType::ITEM_GOGGLES:
            return pos == EquippedPosition::GOGGLES;
        case ItemType::ITEM_HELMET:
            return pos == EquippedPosition::HELMET;
        case ItemType::ITEM_ARMOR:
            return pos == EquippedPosition::ARMOR;
        case ItemType::ITEM_GAUNTLET:
            return pos == EquippedPosition::GAUNTLET;
        case ItemType::ITEM_BOOTS:
            return pos == EquippedPosition::BOOTS;
        case ItemType::ITEM_BACKPACK:
            return pos == EquippedPosition::BACKPACK;
        case ItemType::ITEM_RING:
            return pos == EquippedPosition::RING;
        case ItemType::ITEM_WEAPON_R:
            return pos == EquippedPosition::WEAPON_R;
        case ItemType::ITEM_WEAPON_L:
            return pos == EquippedPosition::WEAPON_L;
        default:
            return false;
    }
}

inline bool is_spot_equipped(size_t spot) {
    return spot < EquippedPosition::MAX_EQUIP_ITEMS;
}
}

size_t Items::get_first_available_spot(const EntitySystem& entitySystem, RoseCommon::Entity entity, RoseCommon::Entity item) {
    const auto& inv = entitySystem.get_component<Component::Inventory>(entity);
    size_t res = decltype(inv.getInventory())::offset();
    bool stackable = false;
    ItemType type = ItemType::NONE;
    uint16_t id = 0;
    if (item != entt::null) {
        const auto& i = entitySystem.get_component<RoseCommon::ItemDef>(item);
        stackable = i.is_stackable;
        type = i.type;
        id = i.id;
    }

    for (const auto item : inv.getInventory()) {
        if (item == entt::null) {
            return res;
        } else if (stackable) {
            const auto& i = entitySystem.get_component<RoseCommon::ItemDef>(item);
            const auto& it = entitySystem.get_component<Component::Item>(item);
            if (i.type == type && i.id == id && it.count < RoseCommon::MAX_STACK) {
                return res;
            }
        }
        ++res;
    }
    return 0;
}

ReturnValue Items::add_item(EntitySystem& entitySystem, RoseCommon::Entity entity, RoseCommon::Entity item) {
    const size_t pos = get_first_available_spot(entitySystem, entity, item);
    if (pos == 0) {
        return ReturnValue::NO_SPACE;
    }
    auto& inv = entitySystem.get_component<Component::Inventory>(entity);
    if (inv.items[pos] == entt::null) {
        inv.items[pos] = item;
    } else {
        // add the stack
        auto& i = entitySystem.get_component<Component::Item>(inv.items[pos]);
        auto& it = entitySystem.get_component<Component::Item>(item);
        if (i.count + it.count < RoseCommon::MAX_STACK) {
            // below max stack
            i.count += it.count;
        } else {
            // split the stack in two or more
            const uint32_t stack_tmp1 = i.count;
            const uint32_t stack_tmp2 = it.count;
            it.count -= RoseCommon::MAX_STACK - i.count;
            i.count = RoseCommon::MAX_STACK;
            if (add_item(entitySystem, entity, item) == ReturnValue::NO_SPACE) {
                it.count = stack_tmp2;
                i.count = stack_tmp1;
                return ReturnValue::NO_SPACE;
            }
        }
    }
    RoseCommon::Packet::SrvSetItem::IndexAndItem index; index.set_index(pos);
    index.set_item(entitySystem.item_to_item<RoseCommon::Packet::SrvSetItem>(item));
    auto packet = RoseCommon::Packet::SrvSetItem::create();
    packet.add_items(index);
    entitySystem.send_to(entity, packet);
    return ReturnValue::OK;
}

RoseCommon::Entity Items::remove_item(EntitySystem& entitySystem, RoseCommon::Entity entity, size_t pos, uint32_t quantity) {
    auto& inv = entitySystem.get_component<Component::Inventory>(entity);
    RoseCommon::Entity item = inv.items[pos];
    auto& i = entitySystem.get_component<Component::Item>(item);
    const auto& it = entitySystem.get_component<ItemDef>(item);
    if (i.count < quantity) {
        return entt::null;
    }
    if (i.count > quantity) {
        const auto type = it.type;
        const auto id = it.id;
        i.count -= quantity;
        RoseCommon::Entity newItem = entitySystem.create_item(type, id, quantity);
        RoseCommon::Packet::SrvSetItem::IndexAndItem index;
        index.set_index(pos);
        index.set_item(entitySystem.item_to_item<RoseCommon::Packet::SrvSetItem>(item));
        auto packet = RoseCommon::Packet::SrvSetItem::create();
        packet.add_items(index);
        entitySystem.send_to(entity, packet);
        return newItem;
    }
    if (is_spot_equipped(pos)) {
        const auto& lua = entitySystem.get_component<Component::ItemLua>(item);
        if (const auto tmp = lua.api.lock(); tmp) {
            if (!tmp->on_unequip(item)) {
                return entt::null;
            }
        }
    }
    inv.items[pos] = entt::null;
    RoseCommon::Packet::SrvSetItem::IndexAndItem index;
    index.set_index(pos);
    auto packet = RoseCommon::Packet::SrvSetItem::create();
    packet.add_items(index);
    entitySystem.send_to(entity, packet);
    return item;
}

void Items::swap_item(EntitySystem& entitySystem, RoseCommon::Entity entity, size_t pos1, size_t pos2) {
    auto& inv = entitySystem.get_component<Component::Inventory>(entity);
    std::swap(inv.items[pos1], inv.items[pos2]);
}

ReturnValue Items::equip_item(EntitySystem& entitySystem, RoseCommon::Entity entity, size_t from, size_t to) {
    const auto& inv = entitySystem.get_component<Component::Inventory>(entity);

    if (from < decltype(inv.getInventory())::offset() || from >= decltype(inv.getInventory())::size()) {
        return ReturnValue::WRONG_INDEX;
    }
    if (to < decltype(inv.getEquipped())::offset() || to >= decltype(inv.getEquipped())::size()) {
        return ReturnValue::WRONG_INDEX;
    }

    const RoseCommon::Entity equipped = inv.items[to];
    const RoseCommon::Entity to_equip = inv.items[from];
    if (!is_spot_correct(entitySystem, to_equip, to)) {
        return ReturnValue::REQUIREMENTS_NOT_MET;
    }
    if (equipped != entt::null) {
        const auto& lua = entitySystem.get_component<Component::ItemLua>(equipped);
        if (const auto tmp = lua.api.lock(); tmp) {
            if (!tmp->on_unequip(entity)) {
                //return ReturnValue::REQUIREMENTS_NOT_MET;
            }
        }
    }
    if (from != entt::null) {
        const auto& lua = entitySystem.get_component<Component::ItemLua>(to_equip);
        if (const auto tmp = lua.api.lock(); tmp) {
            if (!tmp->on_equip(entity)) {
                //return ReturnValue::REQUIREMENTS_NOT_MET;
            }
        }
    }
    swap_item(entitySystem, entity, from, to);
    const auto& basicInfo = entitySystem.get_component<Component::BasicInfo>(entity);
    {
        const auto packet = RoseCommon::Packet::SrvEquipItem::create(basicInfo.id, to,
                entitySystem.item_to_equipped<RoseCommon::Packet::SrvEquipItem>(inv.items[to]));
        entitySystem.send_nearby(entity, packet);
    }

    RoseCommon::Packet::SrvSetItem::IndexAndItem index; index.set_index(to);
    index.set_item(entitySystem.item_to_item<RoseCommon::Packet::SrvSetItem>(to_equip));
    auto packet = RoseCommon::Packet::SrvSetItem::create();
    packet.add_items(index);
    index.set_index(from);
    index.set_item(entitySystem.item_to_item<RoseCommon::Packet::SrvSetItem>(equipped));
    packet.add_items(index);
    entitySystem.send_to(entity, packet);
    return ReturnValue::OK;
}

ReturnValue Items::unequip_item(EntitySystem& entitySystem, RoseCommon::Entity entity, size_t from) {
    const size_t to = get_first_available_spot(entitySystem, entity);
    if (to == 0) {
        return ReturnValue::NO_SPACE;
    }

    const auto& inv = entitySystem.get_component<Component::Inventory>(entity);
    if (from < decltype(inv.getEquipped())::offset() || from >= decltype(inv.getEquipped())::size()) {
        return ReturnValue::WRONG_INDEX;
    }

    const RoseCommon::Entity equipped = inv.items[from];
    if (equipped != entt::null) {
        const auto& lua = entitySystem.get_component<Component::ItemLua>(equipped);
        if (const auto tmp = lua.api.lock(); tmp) {
            if (!tmp->on_unequip(entity)) {
                //return ReturnValue::REQUIREMENTS_NOT_MET;
            }
        }
    }
    swap_item(entitySystem, entity, from, to);
    const auto& basicInfo = entitySystem.get_component<Component::BasicInfo>(entity);
    {
        const auto packet = RoseCommon::Packet::SrvEquipItem::create(basicInfo.id, from, {});
        entitySystem.send_nearby(entity, packet);
    }

    RoseCommon::Packet::SrvSetItem::IndexAndItem index;
    index.set_index(to);
    index.set_item(entitySystem.item_to_item<RoseCommon::Packet::SrvSetItem>(equipped));
    auto packet = RoseCommon::Packet::SrvSetItem::create();
    packet.add_items(index);
    index.set_index(from);
    index.set_item({});
    packet.add_items(index);
    entitySystem.send_to(entity, packet);
    return ReturnValue::OK;
}

void Items::drop_item(EntitySystem& entitySystem, RoseCommon::Entity item, float x, float y, RoseCommon::Entity owner) {
    Component::BasicInfo bi;
    bi.id = entitySystem.get_free_id();
    if (owner != entt::null) {
        const auto& basicInfo = entitySystem.get_component<Component::BasicInfo>(owner);
        bi.teamId = basicInfo.teamId;
        auto& Cowner = entitySystem.add_component<Component::Owner>(item);
        Cowner.owner = owner;
    } else {
        bi.teamId = bi.id;
    }
    entitySystem.add_component(item, std::move(bi));

    entitySystem.update_position(item, x, y);

    entitySystem.add_timer(2min, [item](EntitySystem& entitySystem) {
        if (!entitySystem.is_valid(item)) {
            return;
        }
        if (entitySystem.has_component<Component::Owner>(item)) {
            entitySystem.remove_component<Component::Owner>(item);
            auto& basic = entitySystem.get_component<Component::BasicInfo>(item);
            basic.teamId = basic.id;
        }
    });

    entitySystem.add_timer(5min, [item](EntitySystem& entitySystem) {
        if (!entitySystem.is_valid(item)) {
            return;
        }
        if (entitySystem.has_component<Component::Position>(item)) {
            entitySystem.delete_entity(item);
        }
    });
}

void Items::pickup_item(EntitySystem& entitySystem, RoseCommon::Entity entity, RoseCommon::Entity item) {
  const float x = entitySystem.get_component<Component::Position>(item).x;
  const float y = entitySystem.get_component<Component::Position>(item).y;
  const auto* owner = entitySystem.try_get_component<Component::Owner>(item);
  if (owner && owner->owner != entity) {
    return;
  }
  entitySystem.remove_component<Component::Position>(item);
  entitySystem.remove_component<Component::BasicInfo>(item);
  entitySystem.remove_component<Component::Owner>(item);
  if(entitySystem.has_component<Component::Item>(item)) {
    const auto& i = entitySystem.get_component<Component::Item>(item);
    if(i.is_zuly == true) {
      Items::add_zuly(entitySystem, entity, i.count);
      entitySystem.delete_entity(item);
    }
    else if (Items::add_item(entitySystem, entity, item) != ReturnValue::OK) {
      const RoseCommon::Entity o = owner ? owner->owner : entt::null;
      Items::drop_item(entitySystem, item, x, y, o);
    }
  }
}

bool Items::add_zuly(EntitySystem& entitySystem, RoseCommon::Entity entity, int64_t zuly) {
    auto& inv = entitySystem.get_component<Component::Inventory>(entity);
    if (zuly < 0 && inv.zuly + zuly < 0) {
        return false;
    } else if (zuly < 0) {
        inv.zuly += zuly;
    } else {
        inv.zuly = static_cast<int64_t>(std::min(static_cast<uint64_t>(inv.zuly) + static_cast<uint64_t>(zuly),
                                        static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
    }
    entitySystem.send_to(entity,
                         RoseCommon::Packet::SrvSetMoney::create(inv.zuly));
    return true;
}

void Items::equip_item_packet(EntitySystem& entitySystem, RoseCommon::Entity entity, const RoseCommon::Packet::CliEquipItem& packet) {
    auto logger = Core::CLog::GetLogger(Core::log_type::GENERAL).lock();
    logger->trace("equip_item_packet()");
    logger->trace("from {} to {}", packet.get_slotFrom(), packet.get_slotTo());
    const auto from = packet.get_slotFrom();
    const auto to = packet.get_slotTo();
    const auto res = from == 0 ? // we want to unequip something, 0 being a "fake" no-item flag
        unequip_item(entitySystem, entity, to):
        equip_item(entitySystem, entity, from, to);
    (void) res;
}

void Items::drop_item_packet(EntitySystem& entitySystem, RoseCommon::Entity entity, const RoseCommon::Packet::CliDropItem& packet) {
    auto logger = Core::CLog::GetLogger(Core::log_type::GENERAL).lock();
    logger->trace("equip_item_packet()");
    logger->trace("drop {}x{}", packet.get_index(), packet.get_quantity());
    const auto index = packet.get_index();
    const auto quantity = packet.get_quantity();
    const auto& inv = entitySystem.get_component<Component::Inventory>(entity);

    if (index > inv.items.size()) {
        logger->warn("wrong index {} for item drop, client {}", index, entity);
        return;
    }
    RoseCommon::Entity item = entt::null;
    if (index == 0) { // we drop zulies
        if (add_zuly(entitySystem, entity, -static_cast<int64_t>(packet.get_quantity()))) {
            item = entitySystem.create_zuly(packet.get_quantity());
        } else {
            return; // we don't have enough zuly to remove
        }
    } else {
        item = remove_item(entitySystem, entity, index, quantity);
    }
    const auto& pos = entitySystem.get_component<Component::Position>(entity);
    const auto [x, y] = Core::Random::getInstance().random_in_circle(pos.x, pos.y, DROP_RANGE);
    drop_item(entitySystem, item, x, y, entity);
}
