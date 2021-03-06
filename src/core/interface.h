/* Copyright (c) 2013-2015 Jeffrey Pfau
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef CORE_INTERFACE_H
#define CORE_INTERFACE_H

#include "util/common.h"

struct mCore;

#ifdef COLOR_16_BIT
typedef uint16_t color_t;
#define BYTES_PER_PIXEL 2
#else
typedef uint32_t color_t;
#define BYTES_PER_PIXEL 4
#endif

struct blip_t;

struct mAVStream {
	void (*videoDimensionsChanged)(struct mAVStream*, unsigned width, unsigned height);
	void (*postVideoFrame)(struct mAVStream*, const color_t* buffer, size_t stride);
	void (*postAudioFrame)(struct mAVStream*, int16_t left, int16_t right);
	void (*postAudioBuffer)(struct mAVStream*, struct blip_t* left, struct blip_t* right);
};

struct mKeyCallback {
	uint16_t (*readKeys)(struct mKeyCallback*);
};

struct mStopCallback {
	void (*stop)(struct mStopCallback*);
};

struct mRotationSource {
	void (*sample)(struct mRotationSource*);

	int32_t (*readTiltX)(struct mRotationSource*);
	int32_t (*readTiltY)(struct mRotationSource*);

	int32_t (*readGyroZ)(struct mRotationSource*);
};

/* Have to include this here because of time_t returntype below. */
#include <time.h>

struct mRTCSource {
	void (*sample)(struct mRTCSource*);

	time_t (*unixTime)(struct mRTCSource*);
};

enum mRTCGenericType {
	RTC_NO_OVERRIDE,
	RTC_FIXED,
	RTC_FAKE_EPOCH
};

struct mRTCGenericSource {
	struct mRTCSource d;
	struct mCore* p;
	enum mRTCGenericType override;
	int64_t value;
};

void mRTCGenericSourceInit(struct mRTCGenericSource* rtc, struct mCore* core);

struct mRumble {
	void (*setRumble)(struct mRumble*, int enable);
};

#endif
