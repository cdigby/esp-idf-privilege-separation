// Copyright 2017-2020 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string.h>
#include <errno.h>

#include "mesh.h"
#include "mesh_common.h"
#include "access.h"
#include "beacon.h"
#include "foundation.h"
#include "proxy_client.h"
#include "provisioner_prov.h"
#include "provisioner_main.h"

#if CONFIG_BLE_MESH_FAST_PROV

#define ACTION_ENTER    0x01
#define ACTION_SUSPEND  0x02
#define ACTION_EXIT     0x03

const u8_t *bt_mesh_fast_prov_dev_key_get(u16_t dst)
{
    if (!BLE_MESH_ADDR_IS_UNICAST(dst)) {
        BT_ERR("%s, Not a unicast address 0x%04x", __func__, dst);
        return NULL;
    }

    if (dst == bt_mesh_primary_addr()) {
        return bt_mesh.dev_key;
    }

    return bt_mesh_provisioner_dev_key_get(dst);
}

struct bt_mesh_subnet *bt_mesh_fast_prov_subnet_get(u16_t net_idx)
{
    struct bt_mesh_subnet *sub = NULL;
    int i;

    for (i = 0; i < ARRAY_SIZE(bt_mesh.sub); i++) {
        sub = &bt_mesh.sub[i];
        if (sub->net_idx == net_idx) {
            return sub;
        }
    }

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_sub); i++) {
        sub = bt_mesh.p_sub[i];
        if (sub && sub->net_idx == net_idx) {
            return sub;
        }
    }

    return NULL;
}

struct bt_mesh_app_key *bt_mesh_fast_prov_app_key_find(u16_t app_idx)
{
    struct bt_mesh_app_key *key = NULL;
    int i;

    for (i = 0; i < ARRAY_SIZE(bt_mesh.app_keys); i++) {
        key = &bt_mesh.app_keys[i];
        if (key->net_idx != BLE_MESH_KEY_UNUSED &&
            key->app_idx == app_idx) {
            return key;
        }
    }

    for (i = 0; i < ARRAY_SIZE(bt_mesh.p_app_keys); i++) {
        key = bt_mesh.p_app_keys[i];
        if (key && key->net_idx != BLE_MESH_KEY_UNUSED &&
            key->app_idx == app_idx) {
            return key;
        }
    }

    return NULL;
}

u8_t bt_mesh_set_fast_prov_net_idx(u16_t net_idx)
{
    struct bt_mesh_subnet_keys *key = NULL;
    struct bt_mesh_subnet *sub = NULL;

    sub = bt_mesh_fast_prov_subnet_get(net_idx);
    if (sub) {
        key = BLE_MESH_KEY_REFRESH(sub->kr_flag) ? &sub->keys[1] : &sub->keys[0];
        return bt_mesh_provisioner_set_fast_prov_net_idx(key->net, net_idx);
    }

    /* If NetKey is not found, set net_idx for fast provisioning,
     * and wait for Primary Provisioner to add NetKey.
     */
    return bt_mesh_provisioner_set_fast_prov_net_idx(NULL, net_idx);
}

u8_t bt_mesh_add_fast_prov_net_key(const u8_t net_key[16])
{
    const u8_t *keys = NULL;
    u16_t net_idx = 0U;
    int err = 0;

    net_idx = bt_mesh_provisioner_get_fast_prov_net_idx();
    bt_mesh.p_net_idx_next = net_idx;

    err = bt_mesh_provisioner_local_net_key_add(net_key, &net_idx);
    if (err) {
        return 0x01; /* status: add net_key fail */
    };

    keys = bt_mesh_provisioner_local_net_key_get(net_idx);
    if (!keys) {
        return 0x01; /* status: add net_key fail */
    }

    return bt_mesh_provisioner_set_fast_prov_net_idx(keys, net_idx);
}

const u8_t *bt_mesh_get_fast_prov_net_key(u16_t net_idx)
{
    struct bt_mesh_subnet *sub = NULL;

    sub = bt_mesh_fast_prov_subnet_get(net_idx);
    if (!sub) {
        BT_ERR("%s, NetKey Index 0x%03x not exists", __func__, net_idx);
        return NULL;
    }

    return (sub->kr_flag ? sub->keys[1].net : sub->keys[0].net);
}

const u8_t *bt_mesh_get_fast_prov_app_key(u16_t net_idx, u16_t app_idx)
{
    struct bt_mesh_app_key *key = NULL;

    key = bt_mesh_fast_prov_app_key_find(app_idx);
    if (!key) {
        BT_ERR("%s, AppKey Index 0x%03x not exists", __func__, app_idx);
        return NULL;
    }

    return (key->updated ? key->keys[1].val : key->keys[0].val);
}

u8_t bt_mesh_set_fast_prov_action(u8_t action)
{
    if (!action || action > ACTION_EXIT) {
        return 0x01;
    }

    if ((!bt_mesh_is_provisioner_en() && (action == ACTION_SUSPEND || action == ACTION_EXIT)) ||
        (bt_mesh_is_provisioner_en() && (action == ACTION_ENTER))) {
        BT_WARN("%s, Already", __func__);
        return 0x0;
    }

    if (action == ACTION_ENTER) {
        if (bt_mesh_beacon_get() == BLE_MESH_BEACON_ENABLED) {
            bt_mesh_beacon_disable();
        }
        if (IS_ENABLED(CONFIG_BLE_MESH_PB_GATT)) {
            bt_mesh_provisioner_pb_gatt_enable();
        }
        bt_mesh_provisioner_set_primary_elem_addr(bt_mesh_primary_addr());
        bt_mesh_provisioner_set_prov_bearer(BLE_MESH_PROV_ADV, false);
        bt_mesh_provisioner_fast_prov_enable(true);
        bt_mesh_atomic_or(bt_mesh.flags, BIT(BLE_MESH_PROVISIONER) | BIT(BLE_MESH_VALID_PROV));
    } else {
        if (IS_ENABLED(CONFIG_BLE_MESH_PB_GATT)) {
            bt_mesh_provisioner_pb_gatt_disable();
        }
        if (bt_mesh_beacon_get() == BLE_MESH_BEACON_ENABLED) {
            bt_mesh_beacon_enable();
        }
        bt_mesh_atomic_and(bt_mesh.flags, ~(BIT(BLE_MESH_PROVISIONER) | BIT(BLE_MESH_VALID_PROV)));
        bt_mesh_provisioner_fast_prov_enable(false);
        if (action == ACTION_EXIT) {
            bt_mesh_provisioner_remove_node(NULL);
        }
    }

    return 0x0;
}
#endif /* CONFIG_BLE_MESH_FAST_PROV */
