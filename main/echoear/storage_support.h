#pragma once
#include <driver/sdmmc_host.h>
#include <esp_partition.h>
#include <esp_vfs_fat.h>
#include <dirent.h>
#include <sdmmc_cmd.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include "echoear_target.h"
// Explicit, on-demand mount. Never format or modify an inserted card automatically.
class BoardStorage {
    sdmmc_card_t* card_ = nullptr;

public:
    static BoardStorage& Get() {
        static BoardStorage s;
        return s;
    }
    std::string Inspect(bool backup) {
        if (!card_) {
            sdmmc_host_t host = SDMMC_HOST_DEFAULT();
            host.flags = SDMMC_HOST_FLAG_1BIT;
            host.max_freq_khz = SDMMC_FREQ_DEFAULT;
            sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
            slot.width = 1;
            slot.clk = EchoEarTarget::kSdClock;
            slot.cmd = EchoEarTarget::kSdCommand;
            slot.d0 = EchoEarTarget::kSdData;
            slot.d1 = GPIO_NUM_NC;
            slot.d2 = GPIO_NUM_NC;
            slot.d3 = GPIO_NUM_NC;
            slot.d4 = GPIO_NUM_NC;
            slot.d5 = GPIO_NUM_NC;
            slot.d6 = GPIO_NUM_NC;
            slot.d7 = GPIO_NUM_NC;
            slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
            esp_vfs_fat_sdmmc_mount_config_t cfg{};
            cfg.format_if_mount_failed = false;
            cfg.max_files = 4;
            cfg.allocation_unit_size = 16 * 1024;
            if (esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &cfg, &card_) != ESP_OK) {
                card_ = nullptr;
                return "SD unavailable\n未插卡或格式不支持\n请插入 FAT32 卡";
            }
        }
        if (backup) {
            auto part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY,
                                                 "assets");
            if (!part)
                return "No resource partition";
            char name[100];
            snprintf(name, sizeof(name), "/sdcard/echoear-assets-%lld.bin",
                     (long long)time(nullptr));
            FILE* file = fopen(name, "wx");
            if (!file)
                return "Backup file unavailable\n文件已存在或卡不可写";
            unsigned char buf[1024];
            bool ok = true;
            for (size_t offset = 0; offset < part->size; offset += sizeof(buf)) {
                size_t n = std::min(sizeof(buf), size_t(part->size) - offset);
                if (esp_partition_read(part, offset, buf, n) != ESP_OK ||
                    fwrite(buf, 1, n, file) != n) {
                    ok = false;
                    break;
                }
            }
            if (fclose(file) != 0)
                ok = false;
            if (!ok) {
                remove(name);
                return "Backup failed\n备份失败，未修改设备资源";
            }
            return std::string("Backup saved\n备份成功\n") + (name + 9);
        }
        DIR* dir = opendir("/sdcard");
        if (!dir)
            return "Read failed\n请关机后重新插卡";
        char info[80];
        snprintf(info, sizeof(info), "SD %llu MB\n",
                 (unsigned long long)card_->csd.capacity * card_->csd.sector_size / 1048576);
        std::string result = info;
        dirent* entry;
        int shown = 0;
        while ((entry = readdir(dir)) && shown < 4) {
            if (entry->d_name[0] == '.')
                continue;
            result += std::string(entry->d_name).substr(0, 24) + "\n";
            shown++;
        }
        closedir(dir);
        if (!shown)
            result += "Empty / 空卡";
        return result;
    }
};
