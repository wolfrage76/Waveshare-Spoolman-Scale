#pragma once

#include "services/ble_service.h"
#include <string.h>

constexpr int BLE_DEVICE_PAGE_SIZE = 8;
struct BleCandidatePage {
  int indices[BLE_DEVICE_PAGE_SIZE]{};
  int count = 0, number = 0, pages = 1;
};

inline BleCandidatePage bleCandidatePage(const BleDevice* devices, int count,
                                         bool show_all, int number, const char* preferred) {
  BleCandidatePage page;
  auto visible = [&](int i) {
    return show_all || devices[i].name[0] || strcmp(devices[i].address, preferred) == 0;
  };
  int total = 0;
  for (int i = 0; i < count; ++i) if (visible(i)) ++total;
  page.pages = total ? (total + BLE_DEVICE_PAGE_SIZE - 1) / BLE_DEVICE_PAGE_SIZE : 1;
  page.number = number < 0 ? 0 : number >= page.pages ? page.pages - 1 : number;
  int skip = page.number * BLE_DEVICE_PAGE_SIZE;
  for (int i = 0; i < count && page.count < BLE_DEVICE_PAGE_SIZE; ++i) {
    if (!visible(i)) continue;
    if (skip) { --skip; continue; }
    page.indices[page.count++] = i;
  }
  return page;
}

// Compact scan records, independent of the Bluetooth stack's advertisement cache.
class BleCandidates {
 public:
  BleCandidates(BleDevice* devices, int capacity, const char* preferred, bool target_only)
      : devices_(devices), capacity_(capacity), preferred_(preferred), target_only_(target_only) {}

  void observe(const BleDevice& device) {
    if (capacity_ <= 0) return;
    if (target_only_ && strcmp(device.address, preferred_) != 0) return;
    int slot = -1;
    for (int i = 0; i < count_; ++i) {
      if (strcmp(devices_[i].address, device.address) == 0) {
        slot = i;
        break;
      }
    }
    BleDevice updated = device;
    if (slot >= 0 && !updated.name[0])
      memcpy(updated.name, devices_[slot].name, sizeof(updated.name));
    if (slot < 0) {
      if (count_ < capacity_) slot = count_++;
      else if (before(updated, devices_[count_ - 1])) slot = count_ - 1;
      else return;
    }
    devices_[slot] = updated;
    for (int i = slot; i > 0 && before(devices_[i], devices_[i - 1]); --i) {
      const BleDevice swap = devices_[i - 1];
      devices_[i - 1] = devices_[i]; devices_[i] = swap;
      slot = i - 1;
    }
    for (int i = slot; i + 1 < count_ && before(devices_[i + 1], devices_[i]); ++i) {
      const BleDevice swap = devices_[i + 1];
      devices_[i + 1] = devices_[i]; devices_[i] = swap;
    }
  }
  int count() const { return count_; }

 private:
  bool before(const BleDevice& a, const BleDevice& b) const {
    const bool ap = strcmp(a.address, preferred_) == 0;
    const bool bp = strcmp(b.address, preferred_) == 0;
    if (ap != bp) return ap;
    if (!!a.name[0] != !!b.name[0]) return !!a.name[0];
    if (a.rssi != b.rssi) return a.rssi > b.rssi;
    return strcmp(a.address, b.address) < 0;
  }
  BleDevice* devices_;
  int capacity_, count_ = 0;
  const char* preferred_;
  bool target_only_;
};
