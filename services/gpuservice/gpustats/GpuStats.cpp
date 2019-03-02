/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#undef LOG_TAG
#define LOG_TAG "GpuStats"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "GpuStats.h"

#include <unordered_set>

#include <log/log.h>
#include <utils/Trace.h>

namespace android {

static bool addLoadingCount(GraphicsEnv::Driver driver, bool isDriverLoaded,
                            GpuStatsGlobalInfo* const outGlobalInfo) {
    switch (driver) {
        case GraphicsEnv::Driver::GL:
        case GraphicsEnv::Driver::GL_UPDATED:
            outGlobalInfo->glLoadingCount++;
            if (!isDriverLoaded) outGlobalInfo->glLoadingFailureCount++;
            break;
        case GraphicsEnv::Driver::VULKAN:
        case GraphicsEnv::Driver::VULKAN_UPDATED:
            outGlobalInfo->vkLoadingCount++;
            if (!isDriverLoaded) outGlobalInfo->vkLoadingFailureCount++;
            break;
        default:
            // Currently we don't support GraphicsEnv::Driver::ANGLE because the
            // basic driver package info only belongs to system or updated driver.
            return false;
    }

    return true;
}

static void addLoadingTime(GraphicsEnv::Driver driver, int64_t driverLoadingTime,
                           GpuStatsAppInfo* const outAppInfo) {
    switch (driver) {
        case GraphicsEnv::Driver::GL:
        case GraphicsEnv::Driver::GL_UPDATED:
            outAppInfo->glDriverLoadingTime.emplace_back(driverLoadingTime);
            break;
        case GraphicsEnv::Driver::VULKAN:
        case GraphicsEnv::Driver::VULKAN_UPDATED:
            outAppInfo->vkDriverLoadingTime.emplace_back(driverLoadingTime);
            break;
        default:
            break;
    }
}

void GpuStats::insert(const std::string& driverPackageName, const std::string& driverVersionName,
                      uint64_t driverVersionCode, int64_t driverBuildTime,
                      const std::string& appPackageName, GraphicsEnv::Driver driver,
                      bool isDriverLoaded, int64_t driverLoadingTime) {
    ATRACE_CALL();

    std::lock_guard<std::mutex> lock(mLock);
    ALOGV("Received:\n"
          "\tdriverPackageName[%s]\n"
          "\tdriverVersionName[%s]\n"
          "\tdriverVersionCode[%" PRIu64 "]\n"
          "\tdriverBuildTime[%" PRId64 "]\n"
          "\tappPackageName[%s]\n"
          "\tdriver[%d]\n"
          "\tisDriverLoaded[%d]\n"
          "\tdriverLoadingTime[%" PRId64 "]",
          driverPackageName.c_str(), driverVersionName.c_str(), driverVersionCode, driverBuildTime,
          appPackageName.c_str(), static_cast<int32_t>(driver), isDriverLoaded, driverLoadingTime);

    if (!mGlobalStats.count(driverVersionCode)) {
        GpuStatsGlobalInfo globalInfo;
        if (!addLoadingCount(driver, isDriverLoaded, &globalInfo)) {
            return;
        }
        globalInfo.driverPackageName = driverPackageName;
        globalInfo.driverVersionName = driverVersionName;
        globalInfo.driverVersionCode = driverVersionCode;
        globalInfo.driverBuildTime = driverBuildTime;
        mGlobalStats.insert({driverVersionCode, globalInfo});
    } else if (!addLoadingCount(driver, isDriverLoaded, &mGlobalStats[driverVersionCode])) {
        return;
    }

    if (mAppStats.size() >= MAX_NUM_APP_RECORDS) {
        ALOGV("GpuStatsAppInfo has reached maximum size. Ignore new stats.");
        return;
    }

    const std::string appStatsKey = appPackageName + std::to_string(driverVersionCode);
    if (!mAppStats.count(appStatsKey)) {
        GpuStatsAppInfo appInfo;
        addLoadingTime(driver, driverLoadingTime, &appInfo);
        appInfo.appPackageName = appPackageName;
        appInfo.driverVersionCode = driverVersionCode;
        mAppStats.insert({appStatsKey, appInfo});
        return;
    }

    addLoadingTime(driver, driverLoadingTime, &mAppStats[appStatsKey]);
}

void GpuStats::dump(const Vector<String16>& args, std::string* result) {
    ATRACE_CALL();

    if (!result) {
        ALOGE("Dump result shouldn't be nullptr.");
        return;
    }

    std::lock_guard<std::mutex> lock(mLock);
    bool dumpAll = true;

    std::unordered_set<std::string> argsSet;
    for (size_t i = 0; i < args.size(); i++) {
        argsSet.insert(String8(args[i]).c_str());
    }

    const bool dumpGlobal = argsSet.count("--global") != 0;
    if (dumpGlobal) {
        dumpGlobalLocked(result);
        dumpAll = false;
    }

    const bool dumpApp = argsSet.count("--app") != 0;
    if (dumpApp) {
        dumpAppLocked(result);
        dumpAll = false;
    }

    if (argsSet.count("--clear")) {
        bool clearAll = true;

        if (dumpGlobal) {
            mGlobalStats.clear();
            clearAll = false;
        }

        if (dumpApp) {
            mAppStats.clear();
            clearAll = false;
        }

        if (clearAll) {
            mGlobalStats.clear();
            mAppStats.clear();
        }

        dumpAll = false;
    }

    if (dumpAll) {
        dumpGlobalLocked(result);
        dumpAppLocked(result);
    }
}

void GpuStats::dumpGlobalLocked(std::string* result) {
    for (const auto& ele : mGlobalStats) {
        result->append(ele.second.toString());
        result->append("\n");
    }
}

void GpuStats::dumpAppLocked(std::string* result) {
    for (const auto& ele : mAppStats) {
        result->append(ele.second.toString());
        result->append("\n");
    }
}

void GpuStats::pullGlobalStats(std::vector<GpuStatsGlobalInfo>* outStats) {
    ATRACE_CALL();

    std::lock_guard<std::mutex> lock(mLock);
    outStats->clear();
    outStats->reserve(mGlobalStats.size());

    for (const auto& ele : mGlobalStats) {
        outStats->emplace_back(ele.second);
    }

    mGlobalStats.clear();
}

} // namespace android